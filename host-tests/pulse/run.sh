#!/bin/bash
# pulse.sh against a server built for the purpose.
#
# The pulse is what says a service is down when it posts nothing, so what is
# asserted is the shape of what it posts: an info event for a host that
# answers as expected (an expected 401 included), an error event with a fixed
# fingerprint for one that answers wrong or not at all, and the upstream-sync
# rule on three states of a git history. Nothing here reaches the network.
#
#   host-tests/pulse/run.sh
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
TOOL="$HERE/../../server/pulse/pulse.sh"
WORK="$(mktemp -d)"
SRV=""
trap '[ -n "$SRV" ] && { kill "$SRV" 2>/dev/null; wait "$SRV" 2>/dev/null; }; rm -rf "$WORK"' EXIT
[ -f "$TOOL" ] || { echo "FAIL cannot find $TOOL"; exit 1; }

PASS=0; FAIL=0
ok()  { PASS=$((PASS+1)); echo "  ok   $1"; }
bad() { FAIL=$((FAIL+1)); echo "  FAIL $1"; }

cat > "$WORK/srv.py" <<'PY'
import sys, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
log = sys.argv[2]
class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def _answer(self, code):
        self.send_response(code); self.send_header("Content-Length", "0"); self.end_headers()
    def do_GET(self):
        if self.path == "/slow": time.sleep(3)
        self._answer({"/ok": 200, "/auth": 401, "/bad": 500}.get(self.path, 404))
    def do_POST(self):
        n = int(self.headers.get("Content-Length") or 0); body = self.rfile.read(n)
        if self.path == "/rest/v1/events":
            with open(log, "a") as f: f.write(body.decode() + "\n")
            return self._answer(201)
        self._answer(401 if self.path == "/login" else 404)
ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), H).serve_forever()
PY
PORT=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()')
python3 "$WORK/srv.py" "$PORT" "$WORK/events.log" & SRV=$!
for _ in $(seq 50); do curl -s -o /dev/null "http://127.0.0.1:$PORT/ok" && break; sleep 0.1; done

export SUPABASE_URL="http://127.0.0.1:$PORT" SUPABASE_ANON_KEY=test PULSE_TIMEOUT=1
cat > "$WORK/hosts.txt" <<H
# name method url alive
site   GET  http://127.0.0.1:$PORT/ok     200
inbox  POST http://127.0.0.1:$PORT/login  401
books  GET  http://127.0.0.1:$PORT/auth   2xx,401
bad    GET  http://127.0.0.1:$PORT/bad    200
gone   GET  http://127.0.0.1:$PORT/slow   200
H
: > "$WORK/events.log"
bash "$TOOL" "$WORK/hosts.txt" > "$WORK/out" 2>&1; rc=$?
[ "$rc" = 2 ] && ok "exit status is the number of hosts down" || { bad "exit status $rc, wanted 2"; cat "$WORK/out"; }
grep -q "ok   site 200" "$WORK/out" && ok "a 200 where 200 is expected is up" || bad "site not up"
grep -q "ok   inbox 401" "$WORK/out" && ok "a POST answered 401 where 401 is expected is up" || bad "inbox not up"
grep -q "ok   books 401" "$WORK/out" && ok "a class list (2xx,401) accepts 401" || bad "books not up"
grep -q "DOWN bad answered 500" "$WORK/out" && ok "a 500 is down, and says the status" || bad "bad not down"
grep -q "DOWN gone no answer in 1s" "$WORK/out" && ok "a timeout is down, and says so" || bad "gone not down"
python3 - "$WORK/events.log" <<'PY' && ok "posted three info and two error events with fixed fingerprints" || bad "the posted events are not what the board expects"
import json, sys
ev = [json.loads(l) for l in open(sys.argv[1]) if l.strip()]
info = [e for e in ev if e.get("level", "info") == "info"]
err = [e for e in ev if e.get("level") == "error"]
assert len(ev) == 5, ev
assert all(e["service"] == "pulse" and e["event"] == "probe" for e in ev)
assert sorted(e["props"]["host"] for e in info) == ["books", "inbox", "site"], info
assert all(isinstance(e["props"]["ms"], int) and e["props"]["status"] for e in info)
assert sorted(e["fingerprint"] for e in err) == ["pulse|bad", "pulse|gone"], err
msgs = {e["props"]["host"]: e["props"]["message"] for e in err}
assert "answered 500" in msgs["bad"] and "/bad" in msgs["bad"], msgs
assert "no answer in 1s" in msgs["gone"], msgs
PY

# Without a board address it still probes, and posts nothing.
: > "$WORK/events.log"
SUPABASE_URL= bash "$TOOL" "$WORK/hosts.txt" > "$WORK/out2" 2>&1
[ ! -s "$WORK/events.log" ] && grep -q "DOWN bad" "$WORK/out2" && ok "without a board it prints and posts nothing" || bad "posted without a board, or stopped probing"

# The upstream-sync rule, on a history built here.
U="$WORK/up"; M="$WORK/mine"
G="git -c user.name=t -c user.email=t@t -c commit.gpgsign=false"
$G init -q -b develop "$U" && $G -C "$U" commit -q --allow-empty -m base
$G clone -q "$U" "$M" && $G -C "$M" checkout -q -b xteink
echo "[]" > "$WORK/noprs.json"
echo '[{"headRefName":"sync/upstream-20260903"},{"headRefName":"app/x"}]' > "$WORK/syncpr.json"
: > "$WORK/empty.txt"
export PULSE_UPSTREAM=1 PULSE_REPO="$M" UPSTREAM_URL="$U" PULSE_PRS_JSON="$WORK/noprs.json"

: > "$WORK/events.log"
bash "$TOOL" "$WORK/empty.txt" > "$WORK/out3" 2>&1; rc=$?
[ "$rc" = 0 ] && grep -q "ok   upstream in sync" "$WORK/out3" && ok "upstream fully merged is in sync" || { bad "in-sync case (rc $rc)"; cat "$WORK/out3"; }

old=$(( $(date +%s) - 3 * 86400 ))
GIT_COMMITTER_DATE="$old +0000" GIT_AUTHOR_DATE="$old +0000" $G -C "$U" commit -q --allow-empty -m "upstream change three days ago"
: > "$WORK/events.log"
bash "$TOOL" "$WORK/empty.txt" > "$WORK/out4" 2>&1; rc=$?
[ "$rc" = 1 ] && grep -q "DOWN upstream sync late: 1 commits behind, oldest 72h" "$WORK/out4" && ok "an unmerged upstream commit older than 30h with no sync PR is late" || { bad "late case (rc $rc)"; cat "$WORK/out4"; }
grep -q '"fingerprint":"pulse|upstream-sync"' "$WORK/events.log" && ok "and posts an error with the upstream-sync fingerprint" || bad "no upstream-sync error event"

: > "$WORK/events.log"
PULSE_PRS_JSON="$WORK/syncpr.json" bash "$TOOL" "$WORK/empty.txt" > "$WORK/out5" 2>&1; rc=$?
[ "$rc" = 0 ] && grep -q "a sync pull request is open" "$WORK/out5" && ok "the same state with a sync pull request open is fine" || { bad "open-PR case (rc $rc)"; cat "$WORK/out5"; }

$G -C "$M" fetch -q "$U" develop && $G -C "$M" merge -q FETCH_HEAD
$G -C "$U" commit -q --allow-empty -m "upstream change just now"
bash "$TOOL" "$WORK/empty.txt" > "$WORK/out6" 2>&1; rc=$?
[ "$rc" = 0 ] && grep -q "sync due" "$WORK/out6" && ok "a fresh upstream commit is due, not late" || { bad "fresh case (rc $rc)"; cat "$WORK/out6"; }

echo "$((PASS + FAIL)) checks, $FAIL failed"
[ "$FAIL" -eq 0 ]

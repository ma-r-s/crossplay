#!/usr/bin/env bash
# Does verify_open.sh actually work? Watch every verdict it can reach.
#
#   server/verify_open_selftest.sh
#
# Same precedent as verify_attacks.sh: a check nobody has seen fail is not a
# check. This one matters more than most, because the thing verify_open.sh
# distinguishes is TWO SIGN-IN FAILURES THAT LOOK ALIKE. A classifier that
# silently matched neither sentence, or matched both, would print a confident
# verdict either way and nobody would know.
#
# So this stands up a stub that answers with each canned body in turn and
# asserts the exit code: 0 open, 1 shut, 2 cannot tell. It needs no network,
# no pi, and no credentials.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PORT="${VERIFY_SELFTEST_PORT:-9310}"
PY="$(command -v python3)"
fail=0

if [ -z "$PY" ]; then
  echo "SKIP: no python3, so the stub cannot run."
  exit 0
fi

stub() {
  # $1 = scenario name the stub serves
  SCENARIO="$1" "$PY" - "$PORT" <<'PYEOF' &
import base64, os, sys
from http.server import BaseHTTPRequestHandler, HTTPServer

SC = os.environ["SCENARIO"]
SHUT = b"<p class=lede>This bridge is invitation-only for now.</p>"
ANKI = b"<p class=lede>AnkiWeb did not accept that email and password.</p>"
INSTA = b"<p class=lede>Instapaper did not accept that email and password.</p>"
SLOW = b"<h1>Slow down</h1><p class=lede>Too many attempts.</p>"
WEIRD = b"<p class=lede>Something nobody has written a branch for.</p>"
FEED = b'<?xml version="1.0"?><feed xmlns="http://www.w3.org/2005/Atom"></feed>'
# The pair the firmware ships; the stub accepts exactly it.
OK = b"Basic " + base64.b64encode(b"crossplay:r4ulp-zm4cg-awjtf-z5zfj")


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def send(self, code, body, hdrs=()):
        self.send_response(code)
        self.send_header("Content-Length", str(len(body)))
        for k, v in hdrs:
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path.startswith("/healthz"):
            return self.send(200, b"ok")
        if self.path.startswith("/opds"):
            auth = self.headers.get("authorization", "").encode()
            if SC == "books_shut" or auth != OK:
                return self.send(401, b"unauthorized",
                                 [("WWW-Authenticate", 'Basic realm="Get Books"')])
            return self.send(200, FEED)
        return self.send(404, b"no")

    def do_POST(self):
        n = int(self.headers.get("content-length") or 0)
        self.rfile.read(n)
        if SC == "shut":
            return self.send(200, SHUT)
        if SC == "slow":
            return self.send(200, SLOW)
        if SC == "weird":
            return self.send(200, WEIRD)
        # open, and books_shut: both bridges let the attempt through. Which
        # upstream sentence comes back does not depend on the port here, so
        # answer with both -- verify_open.sh greps for one each.
        return self.send(200, ANKI + INSTA)


HTTPServer(("127.0.0.1", int(sys.argv[1])), H).serve_forever()
PYEOF
  STUB_PID=$!
  for _ in $(seq 1 50); do
    curl -sS -o /dev/null --max-time 1 "http://127.0.0.1:$PORT/healthz" && return 0
    "$PY" -c 'import time;time.sleep(0.1)'
  done
  echo "  the stub never came up on $PORT"
  return 1
}

kill_stub() {
  [ -n "${STUB_PID:-}" ] && kill "$STUB_PID" 2>/dev/null
  wait "$STUB_PID" 2>/dev/null
  STUB_PID=""
}
trap kill_stub EXIT

run_case() {
  # $1 scenario  $2 expected exit  $3 what it means
  local sc="$1" want="$2" meaning="$3" got out
  printf '%-14s ' "$sc"
  if ! stub "$sc" > /dev/null 2>&1; then
    echo "STUB FAILED"
    fail=1
    return
  fi
  out="$(VERIFY_BASE_STUDY="http://127.0.0.1:$PORT" \
         VERIFY_BASE_READ="http://127.0.0.1:$PORT" \
         VERIFY_BASE_BOOKS="http://127.0.0.1:$PORT" \
         bash "$HERE/verify_open.sh" 2>&1)"
  got=$?
  kill_stub
  if [ "$got" = "$want" ]; then
    printf 'ok    exit %s  (%s)\n' "$got" "$meaning"
  else
    printf 'FAIL  exit %s, wanted %s  (%s)\n' "$got" "$want" "$meaning"
    printf '%s\n' "$out" | sed 's/^/      /'
    fail=1
  fi
}

# ---------------------------------------------------------------------------
# FIRST: are the sentences still the service's own words?
#
# Everything below this point tests the classifier against a stub, and a stub
# says whatever it was told to say. If somebody rewords a refusal in
# accounts.py, the stub keeps agreeing with the classifier and both keep
# agreeing with nothing real. So the literals are checked against the source
# that produces them.
#
# Verified on 2026-09-05 by running both real services in-process and reading
# what they actually returned, in three configurations each: variable unset
# (SHUT), variable set to * (OPEN), and the TWIN'S variable name set to *
# (byte-identical to unset, which is the trap this whole card is about).
# ---------------------------------------------------------------------------
echo "the sentences, against the source that produces them"
check_literal() {
  # $1 file  $2 literal
  if grep -qF "$2" "$1"; then
    printf '  ok    %s\n' "$2"
  else
    printf '  GONE  %s\n' "$2"
    printf '        not in %s any more. verify_open.sh cannot classify it.\n' "$1"
    fail=1
  fi
}
check_literal "$HERE/read-bridge/bridge/accounts.py"  "This bridge is invitation-only for now."
check_literal "$HERE/study-bridge/bridge/accounts.py" "This bridge is invitation-only for now."
check_literal "$HERE/read-bridge/bridge/instapaper.py" "Instapaper did not accept that email and password."
check_literal "$HERE/study-bridge/bridge/accounts.py" "AnkiWeb did not accept that email and password."
# And the variable names themselves, because they are the trap.
check_literal "$HERE/read-bridge/bridge/accounts.py"  'os.environ.get("READ_ALLOWLIST"'
check_literal "$HERE/study-bridge/bridge/accounts.py" 'os.environ.get("BRIDGE_ALLOWLIST"'
echo

echo "proving verify_open.sh can reach each verdict"
echo
run_case open       0 "all three open to a stranger"
run_case shut       1 "both bridges refuse before the credential leaves us"
run_case books_shut 1 "bridges open, Get Books refuses the shipped pair"
run_case slow       2 "rate-limited, so the gate is unknowable right now"
run_case weird      1 "a sentence with no branch is a failure, never a pass"

# The one that needs no stub: nothing listening at all must be exit 2, not a
# confident verdict. This is the state the pi was in on 2026-09-05.
printf '%-14s ' "box_down"
out="$(VERIFY_BASE_STUDY="http://127.0.0.1:$PORT" \
       VERIFY_BASE_READ="http://127.0.0.1:$PORT" \
       VERIFY_BASE_BOOKS="http://127.0.0.1:$PORT" \
       bash "$HERE/verify_open.sh" 2>&1)"
got=$?
if [ "$got" = 2 ]; then
  echo "ok    exit 2  (nothing serving: refuses to judge the gate)"
else
  echo "FAIL  exit $got, wanted 2  (nothing serving)"
  printf '%s\n' "$out" | sed 's/^/      /'
  fail=1
fi

echo
if [ "$fail" -eq 0 ]; then
  echo "every verdict verify_open.sh can print was reached on purpose."
  exit 0
fi
echo "verify_open.sh does NOT classify correctly. Do not trust its verdict."
exit 1

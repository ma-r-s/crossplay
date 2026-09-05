#!/bin/bash
# The bridge test harnesses must refuse to run against somebody else's server,
# and must not leak their own (card #286).
#
# The bug: every harness launches a server and then wait_port()s the port. That
# proves A process is listening, never that it is the one this suite started. An
# orphaned bridge from a deleted worktree held a port for four days and a suite
# spent an hour signing credentials into it, every failure reading exactly like
# the bridge refusing a password. And it orphaned because the launcher was
# killed while the socket lived on in a child.
#
# This exercises the REAL code, not a copy:
#   * server/{read,study}-bridge/tests/portguard.py -- the shared guard the four
#     CI-run python harnesses import -- loaded by path and driven directly;
#   * the require_free_port function LIFTED out of each sim_stack.sh by text, the
#     way host-tests/qastack lifts qa_stack.sh's down(), and run against a real
#     foreign listener.
#
# The python half needs nothing installed (portguard is stdlib only), so it runs
# even in a bare checkout. The shell half needs nc (to see the listener) and
# lsof (to name it), the same tools sim_stack.sh itself uses.
#
#   host-tests/portguard/run.sh
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$HERE/../.."
WORK="$(mktemp -d)"
trap 'for f in "$WORK"/*.pid; do [ -f "$f" ] && kill "$(cat "$f")" 2>/dev/null; done; rm -rf "$WORK"' EXIT

PASS=0; FAIL=0
ok()  { PASS=$((PASS+1)); echo "  ok   $1"; }
bad() { FAIL=$((FAIL+1)); echo "  FAIL $1"; }

READ_PG="$ROOT/server/read-bridge/tests/portguard.py"
STUDY_PG="$ROOT/server/study-bridge/tests/portguard.py"
for p in "$READ_PG" "$STUDY_PG"; do
  [ -f "$p" ] || { bad "missing $p"; echo "$((PASS+FAIL)) checks, $FAIL failed"; exit 1; }
done

# --- the shared python guard, driven directly -----------------------------
echo "portguard.py (the guard the four CI harnesses import)"
for PG in "$READ_PG" "$STUDY_PG"; do
  label="$(basename "$(dirname "$(dirname "$PG")")")"   # read-bridge / study-bridge

  # 1. a free port is allowed through.
  if python3 - "$PG" <<'PY'
import importlib.util, socket, sys
spec = importlib.util.spec_from_file_location("pg", sys.argv[1])
pg = importlib.util.module_from_spec(spec); spec.loader.exec_module(pg)
f = socket.socket(); f.bind(("127.0.0.1", 0)); port = f.getsockname()[1]; f.close()
pg.require_free_port(port, "the fake server")   # must NOT raise
PY
  then ok "$label: a free port is allowed"; else bad "$label: a free port was refused"; fi

  # 2. a HELD port is refused loudly (SystemExit 2), naming the port, the lsof
  #    command, AND the pid of whatever holds it. This is the whole card: a
  #    refusal that does not name the holder is the silence that cost the hour.
  if python3 - "$PG" <<'PY'
import contextlib, importlib.util, io, os, socket, sys
spec = importlib.util.spec_from_file_location("pg", sys.argv[1])
pg = importlib.util.module_from_spec(spec); spec.loader.exec_module(pg)
srv = socket.socket(); srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", 0)); srv.listen(); port = srv.getsockname()[1]
buf = io.StringIO(); raised = False
with contextlib.redirect_stdout(buf):
    try:
        pg.require_free_port(port, "the fake server")
    except SystemExit as e:
        raised = (e.code == 2)
out = buf.getvalue(); srv.close()
named_pid = str(os.getpid()) in out   # WE hold the listening socket
sys.exit(0 if (raised and str(port) in out and "lsof" in out and named_pid) else 1)
PY
  then ok "$label: a held port is refused, and names the holding pid"; else bad "$label: a held port was not refused loudly with the pid"; fi

  # 3. reap refuses to kill its OWN process group (the suicide guard).
  if python3 - "$PG" <<'PY'
import importlib.util, os, sys
spec = importlib.util.spec_from_file_location("pg", sys.argv[1])
pg = importlib.util.module_from_spec(spec); spec.loader.exec_module(pg)
class Fake:  # pgid(getpid()) == getpgrp(), so this looks like our own group
    pid = os.getpid()
try:
    pg.reap(Fake()); sys.exit(1)      # should have refused
except RuntimeError:
    sys.exit(0)
PY
  then ok "$label: reap refuses to kill its own group"; else bad "$label: reap would kill its own group"; fi

  # 4. popen_group puts the child in its OWN group, and reap takes it down.
  if python3 - "$PG" <<'PY'
import importlib.util, os, sys, time
spec = importlib.util.spec_from_file_location("pg", sys.argv[1])
pg = importlib.util.module_from_spec(spec); spec.loader.exec_module(pg)
proc = pg.popen_group([sys.executable, "-c", "import time; time.sleep(30)"])
child_pgid = os.getpgid(proc.pid)
if child_pgid == os.getpgrp() or child_pgid != proc.pid:
    sys.exit(2)                        # start_new_session did not take
pg.reap(proc); time.sleep(0.2)
sys.exit(0 if proc.poll() is not None else 1)   # child must be gone
PY
  then ok "$label: popen_group + reap kills the whole child group"; else bad "$label: the launched child survived reap"; fi
done

# --- the shell guard, lifted out of each sim_stack.sh ----------------------
echo "sim_stack.sh require_free_port (lifted, vs a real foreign listener)"
if ! command -v nc >/dev/null 2>&1; then
  # A skip on a laptop, a failure in CI, same rule as host-tests/qastack: the
  # guard is written with nc and cannot be exercised without it.
  if [ -n "${CI:-}" ]; then
    bad "no nc on PATH; the shell guard cannot be exercised (a skip is a failure in CI)"
  else
    echo "  SKIP  no nc on PATH; sim_stack.sh's nc-based guard cannot be exercised"
  fi
else
  for SS in "$ROOT/server/read-bridge/tests/sim_stack.sh" "$ROOT/server/study-bridge/tests/sim_stack.sh"; do
    label="$(basename "$(dirname "$(dirname "$SS")")")"
    [ -f "$SS" ] || { bad "$label: missing $SS"; continue; }
    # Lift require_free_port() by text: one brace-balanced block from the
    # `require_free_port() {` line to the `}` in column 0.
    python3 - "$SS" > "$WORK/guard.sh" <<'PY'
import sys
lines = open(sys.argv[1]).read().split("\n")
start = next(i for i, l in enumerate(lines) if l.startswith("require_free_port() {"))
end = next(i for i in range(start + 1, len(lines)) if lines[i] == "}")
print("\n".join(lines[start:end + 1]))
PY
    if [ ! -s "$WORK/guard.sh" ]; then bad "$label: could not lift require_free_port from sim_stack.sh"; continue; fi

    # A real listener on an ephemeral port, holding it while we probe.
    python3 - "$WORK/port" <<'PY' &
import socket, sys, time
s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("127.0.0.1", 0)); s.listen()
open(sys.argv[1], "w").write(str(s.getsockname()[1]))
time.sleep(20)
PY
    LPID=$!
    disown "$LPID" 2>/dev/null || true   # no "Terminated" job-control noise when we kill it
    echo "$LPID" > "$WORK/listener.pid"
    for _ in $(seq 1 50); do [ -s "$WORK/port" ] && break; sleep 0.1; done
    PORT="$(cat "$WORK/port" 2>/dev/null || true)"
    if [ -z "$PORT" ]; then bad "$label: test listener never reported its port"; kill "$LPID" 2>/dev/null; rm -f "$WORK/port" "$WORK/listener.pid"; continue; fi

    OUT="$( ( . "$WORK/guard.sh"; require_free_port "$PORT" "the fake server" ) 2>&1 )"; RC=$?
    kill "$LPID" 2>/dev/null || true
    rm -f "$WORK/port" "$WORK/listener.pid"

    if [ "$RC" -ne 0 ]; then ok "$label: the lifted guard exits nonzero on a held port"; else bad "$label: the lifted guard passed over a held port (RC=$RC)"; fi
    case "$OUT" in *"$PORT"*) ok "$label: and names the port";; *) bad "$label: the refusal does not name the port: $OUT";; esac
    if command -v lsof >/dev/null 2>&1; then
      case "$OUT" in *"$LPID"*) ok "$label: and names the holding pid";; *) bad "$label: the refusal does not name the holding pid ($LPID): $OUT";; esac
    fi
  done
fi

echo "$((PASS+FAIL)) checks, $FAIL failed"
[ "$FAIL" -eq 0 ]

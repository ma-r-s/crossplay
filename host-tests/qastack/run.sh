#!/usr/bin/env bash
# qa_stack.sh `down` must not return while its ports are still answering.
#
#   host-tests/qastack/run.sh
#
# `kill` returns the instant the signal is QUEUED. uvicorn keeps its listening
# socket for a moment after that, so a `down; up` -- typed in one breath, or
# scripted, which is how this was found -- hits up's port guard, aborts before
# pairing is attempted, and blames "another qa_stack" that does not exist.
#
# The function's TEXT is lifted out of qa_stack.sh and run here, the way
# host-tests/checksh lifts check.sh's build loop, so this asserts the shipped
# code rather than a copy of it. Lifting is also what makes STATE and the ports
# ours: qa_stack.sh hard-codes STATE to $REPO/qa-artifacts, and a suite that ran
# the real script would kill the pid files of a stack a person has up in this
# very tree.
#
# The stub holds its port for 1.2s after SIGTERM where uvicorn holds it for
# ~0.15s. That is deliberate: the property under test is "does down WAIT for
# the port, or does it return on the signal", and a 150ms window is a race this
# suite would lose often enough to be worthless. A down() that returns on the
# signal fails this every time; the shipped one passes with ~9s to spare.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SCRIPT="$HERE/../../server/read-bridge/tests/qa_stack.sh"
checks=0
failed=0

# Same rule as host-tests/release, relwatch and boardmigrate: a skip is
# information on a laptop, and a FAILURE in CI, where every input is meant to be
# present. Both paths below are things CI has -- the script is committed, and nc
# is on the runner image -- so a skip here means CI stopped being the machine
# this suite thinks it is, which is worth a red rather than a line nobody reads.
skip() {
  if [ -n "${CI:-}" ]; then
    echo "FAIL qastack  $1 (a skip is a failure in CI: the inputs should be present here)"
    echo "1 checks, 1 failed"
    exit 1
  fi
  echo "SKIP qastack  $1"
  echo "0 checks, 0 failed"
  exit 0
}

[ -r "$SCRIPT" ] || skip "$SCRIPT is not readable"

# nc is not optional here: down()'s wait is written with `nc -z`, and so is up()'s
# port guard. Without it the wait breaks out of its loop on the first iteration
# and this suite would pass while proving nothing, which is the failure mode this
# whole file exists to refuse.
command -v nc >/dev/null 2>&1 || skip "no nc on PATH; qa_stack.sh's port wait cannot be exercised"

WORK="$(mktemp -d)"
trap 'for p in "$WORK"/*.pid; do [ -f "$p" ] && kill -9 "$(cat "$p")" 2>/dev/null; done; rm -rf "$WORK"' EXIT

# Lift down() out by text. Its body is one brace-balanced block starting at the
# `down() {` line and closing on the `}` in column 0.
python3 - "$SCRIPT" >"$WORK/down.sh" <<'PY'
import sys
lines = open(sys.argv[1]).read().split("\n")
start = next(i for i, l in enumerate(lines) if l.startswith("down() {"))
end = next(j for j in range(start + 1, len(lines)) if lines[j] == "}")
print("\n".join(lines[start:end + 1]))
PY
grep -q '^down() {' "$WORK/down.sh" || { echo "FAIL qastack  down() could not be lifted out of qa_stack.sh"; echo "1 checks, 1 failed"; exit 1; }

# Two free ports, asked of the kernel rather than guessed.
read -r FAKE_PORT BRIDGE_PORT <<<"$(python3 -c '
import socket
def free():
    s = socket.socket(); s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p
print(free(), free())')"

cat >"$WORK/holder.py" <<'PY'
# A listener that survives SIGTERM for a while, the way uvicorn survives it for
# ~150ms. It keeps the socket BOUND the whole time: that, not the process being
# alive, is what up()'s port guard trips over.
import signal, socket, sys, time
# A hard lifetime, not just a SIGTERM handler. If down() ever stops killing --
# a regression this suite's second check would report -- an un-signalled holder
# would sit on a real port forever, on CI and on the machine this was run from.
# A leaked listener is a worse bug than the one being tested for.
DEADLINE = time.time() + 60
port = int(sys.argv[1])
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("127.0.0.1", port))
s.listen(8)
dying = []
signal.signal(signal.SIGTERM, lambda *_: dying.append(time.time()))
while True:
    if (dying and time.time() - dying[0] >= 1.2) or time.time() > DEADLINE:
        s.close()
        sys.exit(0)
    time.sleep(0.02)
PY

start_holders() {
  python3 "$WORK/holder.py" "$FAKE_PORT" & echo $! >"$WORK/fake.pid"
  python3 "$WORK/holder.py" "$BRIDGE_PORT" & echo $! >"$WORK/bridge.pid"
  for _ in $(seq 1 100); do
    nc -z 127.0.0.1 "$FAKE_PORT" 2>/dev/null && nc -z 127.0.0.1 "$BRIDGE_PORT" 2>/dev/null && return 0
    sleep 0.1
  done
  return 1
}

# --- the check -------------------------------------------------------------
# Guard first: if the holders never come up, a "port is free" verdict below
# would be true and meaningless.
checks=$((checks + 1))
if ! start_holders; then
  failed=$((failed + 1))
  echo "FAIL qastack  the stub listeners never bound; nothing was tested"
else
  ( set -uo pipefail
    STATE="$WORK"; export STATE FAKE_PORT BRIDGE_PORT
    # shellcheck disable=SC1090
    . "$WORK/down.sh"
    down
  ) >"$WORK/down.log" 2>&1

  still=""
  nc -z 127.0.0.1 "$FAKE_PORT"   2>/dev/null && still="$still $FAKE_PORT"
  nc -z 127.0.0.1 "$BRIDGE_PORT" 2>/dev/null && still="$still $BRIDGE_PORT"
  if [ -n "$still" ]; then
    failed=$((failed + 1))
    echo "FAIL qastack  down returned while$still still answered; the next 'up' aborts on its own port guard"
  fi
fi

# down must still remove the pid files it was given, wait or no wait.
checks=$((checks + 1))
if [ -f "$WORK/fake.pid" ] || [ -f "$WORK/bridge.pid" ]; then
  failed=$((failed + 1))
  echo "FAIL qastack  down left its pid files behind; 'status' will report a stack that is gone"
fi

echo "$checks checks, $failed failed"
[ "$failed" -eq 0 ]

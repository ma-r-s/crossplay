#!/usr/bin/env bash
# The whole stack, end to end: the simulator, this bridge, and a fake
# Instapaper, with a real pairing handshake in the middle.
#
#   server/read-bridge/tests/sim_stack.sh
#
# What this proves that tests/test_api.py cannot: that the DEVICE's half of the
# protocol works. The suites drive the bridge with curl and prove the server; a
# firmware that composed its `have` string wrongly, or wrote its index in a
# shape it cannot read back, would pass every one of them.
#
# The handshake is the interesting part, because it deliberately cannot be done
# by one side alone: the device shows a code, a signed-in human claims it, and
# the device asks for a button press before it stores anything. So this script
# is the human -- it reads the code out of the simulator's log and claims it
# over HTTP while the simulator is still running.
#
# Two traps, both paid for on 2026-08-30:
#
#   * sim-shot.sh TRUNCATES qa-artifacts/sim.log when it starts, and it builds
#     first, so a grep issued too early reads the PREVIOUS run's code. It looked
#     like it worked: the claim returned 200 and the pairing silently did not
#     happen. Hence the marker below -- the code is only read from a log written
#     after this run began.
#   * A FAILED claim also returns HTTP 200; the bridge answers a "Not found"
#     page rather than an error status, because the page is for a person. Check
#     the body, never the status.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVICE="$(dirname "$HERE")"
REPO="$(cd "$SERVICE/../.." && pwd)"
PY="$SERVICE/.venv/bin/python"
[ -x "$PY" ] || { echo "no venv: uv venv .venv && uv pip install -r requirements.txt"; exit 1; }

# One port slice per tree, same reason as LINKPLAY_BASE_PORT: two trees running
# this at once must not share a bridge.
BASE="${BRIDGE_TEST_PORT:-8996}"
FAKE_PORT=$((BASE + 4))
BRIDGE_PORT=$((BASE + 5))

for port in $FAKE_PORT $BRIDGE_PORT; do
  if nc -z 127.0.0.1 "$port" 2>/dev/null; then
    echo "port $port is already in use -- another run of this script, or a stray"
    echo "server from one. Testing against it would prove nothing about this tree."
    exit 1
  fi
done

WORK="$(mktemp -d)"
PIDS=()
cleanup() {
  for pid in "${PIDS[@]:-}"; do kill "$pid" 2>/dev/null || true; done
  rm -rf "$WORK"
}
trap cleanup EXIT

USER_EMAIL="mario@example.com"
PASSWORD="pw"

"$PY" - "$WORK/fake.json" <<'PYEOF'
import json, sys
prose = "<p>" + ("The panel refreshes in about three hundred milliseconds, which is long "
                 "enough to see and short enough not to mind. " * 8) + "</p>"
json.dump({
    "users": {"mario@example.com": {"password": "pw", "token": "tok-1", "secret": "sec-1"}},
    "bookmarks": [
        {"bookmark_id": 500 + i, "url": f"https://example.com/{i}", "title": f"Article {i}",
         "description": "", "time": 1756000000 + i, "progress": 0.0, "progress_timestamp": 0,
         "folder": "unread", "text": prose}
        for i in range(1, 4)
    ],
}, open(sys.argv[1], "w"))
PYEOF

export FAKE_INSTAPAPER_STATE="$WORK/fake.json" FAKE_CONSUMER_KEY=k FAKE_CONSUMER_SECRET=s
export PYTHONPATH="$SERVICE"
# No subshell: $! must be the python process itself. Killing a subshell orphans
# its child rather than stopping it, the orphan keeps the port, and the NEXT run
# then talks to a server whose state file has been deleted -- which presents as
# the bridge refusing a correct password. PYTHONPATH is set above, so uvicorn
# needs no working directory of its own.
"$PY" -m uvicorn tests.fake_instapaper:app --host 127.0.0.1 \
  --port "$FAKE_PORT" --log-level warning > "$WORK/fake.log" 2>&1 &
PIDS+=($!)

export READ_DATA="$WORK/data" READ_ALLOWLIST="$USER_EMAIL" \
  READ_CONSUMER_KEY=k READ_CONSUMER_SECRET=s \
  READ_INSTAPAPER_BASE="http://127.0.0.1:$FAKE_PORT"
READ_FERNET_KEY="$("$PY" -c 'from cryptography.fernet import Fernet;print(Fernet.generate_key().decode())')"
export READ_FERNET_KEY
"$PY" -m uvicorn bridge.app:app --host 127.0.0.1 \
  --port "$BRIDGE_PORT" --log-level warning > "$WORK/bridge.log" 2>&1 &
PIDS+=($!)

# BOTH, and the fake is not optional: signing in goes straight through it, so a
# bridge that is up against a fake that is not answers the login with a polite
# "could not be reached" page and HTTP 200. That looks exactly like a working
# stack refusing a password.
for _ in $(seq 1 60); do
  curl -sf --max-time 2 "http://127.0.0.1:$BRIDGE_PORT/healthz" > /dev/null \
    && curl -sf --max-time 2 "http://127.0.0.1:$FAKE_PORT/_test/state" > /dev/null && break
  sleep 0.5
done
curl -sf --max-time 2 "http://127.0.0.1:$BRIDGE_PORT/healthz" > /dev/null || { echo "the bridge never came up"; exit 1; }
curl -sf --max-time 2 "http://127.0.0.1:$FAKE_PORT/_test/state" > /dev/null || { echo "the fake Instapaper never came up"; exit 1; }

echo "signing the account in ..."
# The session cookie is read out of the response header and sent back by hand,
# rather than through a cookie jar. The bridge sets it Secure -- correct behind
# the tunnel, and it means curl will not return it over plaintext loopback, so
# a jar silently yields a signed-out /pair page and the claim fails with a 200.
# This script is the browser; sending its own cookie over 127.0.0.1 is its
# call to make, and it keeps the SERVER's flag honest in the test.
SESSION="$(curl -sS -D - -o /dev/null -X POST \
  -d "username=$USER_EMAIL&password=$PASSWORD" "http://127.0.0.1:$BRIDGE_PORT/login" \
  | grep -i '^set-cookie: read_session=' | sed 's/.*read_session=\([^;]*\).*/\1/' | tr -d '\r' || true)"
if [ -z "$SESSION" ]; then
  echo "FAIL signing in did not return a session. The bridge said:"
  curl -sS -X POST -d "username=$USER_EMAIL&password=$PASSWORD" \
    "http://127.0.0.1:$BRIDGE_PORT/login" | grep -o '<p>[^<]*</p>' | head -2
  exit 1
fi

# A clean card, or the first sync has nothing to deliver and proves nothing.
rm -rf "$REPO/fs_agent/.crosspoint/instapaper"
SIMLOG="$REPO/qa-artifacts/sim.log"
MARKER="stack-$$"
rm -f "$SIMLOG"

echo "driving the simulator ..."
(
  cd "$REPO"
  READ_BRIDGE_URL="http://127.0.0.1:$BRIDGE_PORT" CROSSPLAY_AUTOSTART=INSTAPAPER \
    ./scripts_local/sim-shot.sh \
      '1500:TAP:240,756;3200:ENTER;16000:TAP:240,700;34000:QUIT' \
      '14000:./qa-artifacts/stack-confirm.bmp;30000:./qa-artifacts/stack-verdict.bmp' \
      > "$WORK/simshot.log" 2>&1
) &
SIM_PID=$!
PIDS+=($SIM_PID)

# Wait for a code from THIS run: the log must exist and have been written after
# the marker file, which is created after the old log was removed.
touch "$WORK/$MARKER"
CODE=""
for _ in $(seq 1 120); do
  # `|| true` on both lines is load-bearing under `set -e`: grep exits 1 when
  # the code has not been logged yet, and so does the test when CODE is still
  # empty. Either one kills the script mid-wait, which presents as the whole
  # stack tearing itself down for no stated reason.
  if [ -f "$SIMLOG" ] && [ "$SIMLOG" -nt "$WORK/$MARKER" ]; then
    CODE="$(grep -o 'pairing code [A-Z0-9]*' "$SIMLOG" | tail -1 | awk '{print $3}' || true)"
    [ -n "$CODE" ] && break || true
  fi
  sleep 1
done
[ -n "$CODE" ] || { echo "FAIL the reader never showed a pairing code"; exit 1; }
echo "  the reader is showing $CODE"

PAIR_PAGE="$(curl -sS -H "Cookie: read_session=$SESSION" "http://127.0.0.1:$BRIDGE_PORT/pair")"
CSRF="$(printf '%s' "$PAIR_PAGE" | grep -o "name=csrf value='[^']*'" | sed "s/.*'\(.*\)'/\1/" || true)"
if [ -z "$CSRF" ]; then
  # Almost always the session cookie: the bridge sets it Secure, which is right
  # in production behind the tunnel and means curl will not send it back over
  # plaintext loopback unless it treats 127.0.0.1 as a secure context.
  echo "FAIL no CSRF token on /pair -- the session cookie did not come back."
  echo "     page said: $(printf '%s' "$PAIR_PAGE" | grep -o '<title>[^<]*</title>')"
  exit 1
fi
CLAIM="$(curl -sS -H "Cookie: read_session=$SESSION" -X POST -d "code=$CODE&csrf=$CSRF" \
  "http://127.0.0.1:$BRIDGE_PORT/api/pair/claim")"
case "$CLAIM" in
  *"confirm on the reader"*) echo "  claimed; waiting for the reader to confirm" ;;
  *) echo "FAIL the claim was refused: $(echo "$CLAIM" | grep -o '<title>[^<]*</title>')"; exit 1 ;;
esac

wait $SIM_PID || true

FAILED=0
say() { echo "  $1"; }
fail() { echo "  FAIL $1"; FAILED=1; }

CARD="$REPO/fs_agent/.crosspoint/instapaper"
[ -f "$CARD/.bridge" ] && say "the pairing token is on the card" || fail "no .bridge on the card"
[ -f "$CARD/index.tsv" ] && say "the index is on the card" || fail "no index.tsv on the card"
COUNT="$(grep -c '^[0-9]' "$CARD/index.tsv" 2>/dev/null || echo 0)"
COUNT="${COUNT//[^0-9]/}"
[ "$COUNT" -eq 3 ] && say "three articles in the index" || fail "index holds $COUNT rows, expected 3"
# find, not ls: under `set -o pipefail` an `ls` that matches nothing fails the
# whole substitution and kills the script -- which is how the "no .part files"
# check below, the one that PASSES by matching nothing, silently ended the run
# three assertions early and still looked like a clean finish.
FILES="$(find "$CARD" -maxdepth 1 -name 'a*.txt' | wc -l | tr -d ' ')"
[ "$FILES" -eq 3 ] && say "three article files downloaded" || fail "$FILES article files, expected 3"
# A .part left behind means a download was never committed, and the row it
# belongs to would open nothing.
PARTS="$(find "$CARD" -maxdepth 1 -name '*.part' | wc -l | tr -d ' ')"
[ "$PARTS" -eq 0 ] && say "no half-written downloads left behind" || fail "$PARTS .part files remain"
if grep -q "INSTA] paired" "$SIMLOG"; then say "the reader confirmed the pairing"; else fail "the reader never confirmed"; fi
# A glyph the cut does not carry draws as NOTHING, so this is the only way an
# overflowing line announces itself. See the font-cuts memory.
if grep -q "No glyph" "$SIMLOG"; then fail "something drew a glyph these cuts do not have"; else say "no missing glyphs in anything drawn"; fi

if [ "$FAILED" -eq 0 ]; then
  echo "sim_stack: PASS"
else
  echo "sim_stack: FAILED"
  exit 1
fi

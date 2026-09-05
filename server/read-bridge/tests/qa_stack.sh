#!/usr/bin/env bash
# The same stack sim_stack.sh builds, but LEFT RUNNING and already paired, so a
# person can walk up to a device that is signed in and just use it.
#
#   server/read-bridge/tests/qa_stack.sh up      # start, pair, print the wrapper
#   server/read-bridge/tests/qa_stack.sh down    # stop and clean up
#   server/read-bridge/tests/qa_stack.sh status
#
# sim_stack.sh tears its servers down on exit, which is right for a suite and
# wrong for a session with a human in it. Everything below is lifted from it,
# including the traps it documents; read that file's header before changing
# anything here.
#
# WHY THE WRAPPER MATTERS MORE THAN THE STACK. The device finds this bridge
# through READ_BRIDGE_URL, read per process from the environment. A single run
# without it goes to the LIVE host, is answered 401, and InstapaperActivity
# then does exactly what it should:
#
#   InstapaperSync.cpp:29        any 401 sets sync.unpaired
#   InstapaperActivity.cpp:366   bridge_ = BridgeState{}; library_.clearBridgeState();
#
# So one forgotten export does not fail a run, it DELETES the pairing off the
# card. A tester mid-task lands at a pairing screen, and the two obvious
# diagnoses -- "the tester broke it" and "pairing does not persist" -- are both
# wrong. Hence qa_shot.sh: hand that to the tester and never mention the
# variable, because a variable nobody types cannot be forgotten.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVICE="$(dirname "$HERE")"
REPO="$(cd "$SERVICE/../.." && pwd)"
PY="$SERVICE/.venv/bin/python"

BASE="${BRIDGE_TEST_PORT:-8996}"
FAKE_PORT=$((BASE + 6))
BRIDGE_PORT=$((BASE + 7))
STATE="$REPO/qa-artifacts/qa-instapaper"
USER_EMAIL="mario@example.com"
PASSWORD="pw"

url() { echo "http://127.0.0.1:$BRIDGE_PORT"; }

status() {
  if [ -f "$STATE/bridge.pid" ] && kill -0 "$(cat "$STATE/bridge.pid")" 2>/dev/null; then
    echo "stack UP   bridge=$(url)  fake=http://127.0.0.1:$FAKE_PORT"
    echo "paired card: $REPO/fs_agent/.crosspoint/instapaper"
    [ -f "$REPO/fs_agent/.crosspoint/instapaper/.bridge" ] \
      && echo "  .bridge present -- the device is signed in" \
      || echo "  NO .bridge -- the device is NOT paired; run 'up' again"
  else
    echo "stack DOWN"
  fi
}

down() {
  for f in bridge.pid fake.pid; do
    [ -f "$STATE/$f" ] && { kill "$(cat "$STATE/$f")" 2>/dev/null || true; rm -f "$STATE/$f"; }
  done
  # WAIT for the ports, do not just signal. kill returns the instant the signal
  # is queued; uvicorn is still listening for another ~150ms. A `down && up`
  # typed in one breath -- or scripted, which is how this was found -- then hits
  # up's port guard and aborts before pairing is even attempted, and the message
  # blames "another qa_stack" that does not exist.
  for _ in $(seq 1 100); do
    nc -z 127.0.0.1 "$FAKE_PORT" 2>/dev/null || nc -z 127.0.0.1 "$BRIDGE_PORT" 2>/dev/null || break
    sleep 0.1
  done
  if nc -z 127.0.0.1 "$FAKE_PORT" 2>/dev/null || nc -z 127.0.0.1 "$BRIDGE_PORT" 2>/dev/null; then
    echo "WARNING: $FAKE_PORT/$BRIDGE_PORT still answer after 10s. Something else"
    echo "is on them -- an orphan from a killed shell, or another tree. 'up' will refuse."
  fi
  echo "stopped. The card keeps its pairing, which is now useless -- the bridge"
  echo "that issued the token is gone, so 'up' pairs again from scratch."
}

up() {
  [ -x "$PY" ] || { echo "no venv: cd $SERVICE && uv venv .venv && uv pip install -r requirements.txt"; exit 1; }
  for port in $FAKE_PORT $BRIDGE_PORT; do
    if nc -z 127.0.0.1 "$port" 2>/dev/null; then
      echo "port $port is in use -- another qa_stack, or a stray server from one."
      echo "Run 'down' first. Testing against it would prove nothing about this tree."
      exit 1
    fi
  done
  mkdir -p "$STATE"

  "$PY" - "$STATE/fake.json" <<'PYEOF'
import json, sys
prose = "<p>" + ("The panel refreshes in about three hundred milliseconds, which is long "
                 "enough to see and short enough not to mind. " * 8) + "</p>"
json.dump({
    "users": {"mario@example.com": {"password": "pw", "token": "tok-1", "secret": "sec-1"}},
    "bookmarks": [
        {"bookmark_id": 500 + i, "url": f"https://example.com/{i}", "title": f"Article {i}",
         "description": "", "time": 1756000000 + i, "progress": 0.0, "progress_timestamp": 0,
         "folder": "unread", "text": prose}
        for i in range(1, 6)
    ],
}, open(sys.argv[1], "w"))
PYEOF

  # No subshell: $! must be the python process. Killing a subshell orphans its
  # child, the orphan keeps the port, and the next run talks to a server whose
  # state file has been deleted -- which presents as a refused password.
  export FAKE_INSTAPAPER_STATE="$STATE/fake.json" FAKE_CONSUMER_KEY=k FAKE_CONSUMER_SECRET=s
  export PYTHONPATH="$SERVICE"
  "$PY" -m uvicorn tests.fake_instapaper:app --host 127.0.0.1 \
    --port "$FAKE_PORT" --log-level warning > "$STATE/fake.log" 2>&1 &
  echo $! > "$STATE/fake.pid"

  export READ_DATA="$STATE/data" READ_ALLOWLIST="$USER_EMAIL" \
    READ_CONSUMER_KEY=k READ_CONSUMER_SECRET=s \
    READ_INSTAPAPER_BASE="http://127.0.0.1:$FAKE_PORT"
  if [ ! -f "$STATE/fernet.key" ]; then
    "$PY" -c 'from cryptography.fernet import Fernet;print(Fernet.generate_key().decode())' > "$STATE/fernet.key"
  fi
  READ_FERNET_KEY="$(cat "$STATE/fernet.key")"; export READ_FERNET_KEY
  "$PY" -m uvicorn bridge.app:app --host 127.0.0.1 \
    --port "$BRIDGE_PORT" --log-level warning > "$STATE/bridge.log" 2>&1 &
  echo $! > "$STATE/bridge.pid"

  # BOTH must answer. A bridge up against a fake that is not answers a login
  # with a polite "could not be reached" page and HTTP 200, which looks exactly
  # like a working stack refusing a correct password.
  for _ in $(seq 1 60); do
    curl -sf --max-time 2 "$(url)/healthz" > /dev/null \
      && curl -sf --max-time 2 "http://127.0.0.1:$FAKE_PORT/_test/state" > /dev/null && break
    sleep 0.5
  done
  curl -sf --max-time 2 "$(url)/healthz" > /dev/null || { echo "the bridge never came up"; exit 1; }
  curl -sf --max-time 2 "http://127.0.0.1:$FAKE_PORT/_test/state" > /dev/null || { echo "the fake never came up"; exit 1; }

  # The session cookie is read from the header and sent back by hand. The bridge
  # sets it Secure -- correct behind the tunnel -- so curl will not return it
  # over plaintext loopback, and a cookie jar silently yields a signed-OUT /pair
  # page whose claim then fails with a 200.
  SESSION="$(curl -sS -D - -o /dev/null -X POST \
    -d "username=$USER_EMAIL&password=$PASSWORD" "$(url)/login" \
    | grep -i '^set-cookie: read_session=' | sed 's/.*read_session=\([^;]*\).*/\1/' | tr -d '\r' || true)"
  [ -n "$SESSION" ] || { echo "FAIL: signing in returned no session"; exit 1; }

  rm -rf "$REPO/fs_agent/.crosspoint/instapaper"
  SIMLOG="$REPO/qa-artifacts/sim.log"
  MARKER="$STATE/marker"; touch "$MARKER"; rm -f "$SIMLOG"

  echo "pairing the device (about a minute) ..."
  # Only through the wrapper, so this script cannot be the one that forgets.
  # CROSSPLAY_AUTOSTART boots straight into the app. Without it the run lands
  # on Home and the taps below, which start at the app's own SYNC control, open
  # whatever the shelf happens to have under them.
  READ_BRIDGE_URL="$(url)" CROSSPLAY_AUTOSTART=INSTAPAPER "$REPO/scripts_local/sim-shot.sh" \
    '1500:TAP:240,756;3200:ENTER;16000:TAP:240,700;22000:TAP:240,700;28000:TAP:240,700;34000:TAP:240,700;40000:TAP:240,700;46000:TAP:240,700;52000:TAP:240,700;58000:TAP:240,700;90000:QUIT' > /dev/null 2>&1 &
  SIMPID=$!

  # Tap the gate on a CADENCE rather than at a guessed moment. The gate
  # appears only once the claim lands, and the claim waits on a poll for the
  # code, so its instant is not knowable when this string is written. Eight
  # taps six seconds apart cover the whole window: the ones before the gate
  # land on the QR screen and the ones after land on the verdict, both inert.
  #
  # The cadence is NOT what made this work. Five runs failed here and every
  # one was read as a tap-timing problem; the cause was the claim below going
  # to a GET-only route and being answered 405 while the check looked only at
  # the body. No tap schedule could have rescued a code nobody had claimed.
  # Keep the cadence anyway -- it costs nothing and the gate's moment really
  # is not fixed -- but suspect the claim, not the taps, when this fails.

  # sim-shot TRUNCATES sim.log when it starts and builds first, so a grep issued
  # too early reads the PREVIOUS run's code. The claim then returns 200 and
  # nothing pairs. Only read a log written after the marker.
  CODE=""
  for _ in $(seq 1 90); do
    if [ -f "$SIMLOG" ] && [ "$SIMLOG" -nt "$MARKER" ]; then
      CODE="$(grep -o 'pairing code [A-Z0-9]*' "$SIMLOG" | tail -1 | awk '{print $3}' || true)"
      [ -n "$CODE" ] && break || true
    fi
    sleep 1
  done
  [ -n "$CODE" ] || { kill $SIMPID 2>/dev/null || true; echo "FAIL: the reader never showed a code"; exit 1; }
  echo "  the reader is showing $CODE"

  PAIR_PAGE="$(curl -sS -H "Cookie: read_session=$SESSION" "$(url)/pair")"
  CSRF="$(printf '%s' "$PAIR_PAGE" | grep -o "name=csrf value='[^']*'" | sed "s/.*'\(.*\)'/\1/" || true)"
  [ -n "$CSRF" ] || { kill $SIMPID 2>/dev/null || true; echo "FAIL: /pair came back signed out"; exit 1; }

  # The claim goes to /api/pair/claim, which is what the form on /pair posts
  # to. /pair itself is a GET-only page: posting there answers 405, and 405 is
  # not "not found" or "expired", so a body-only check called it a success and
  # the device polled a code nobody had claimed until the run timed out. That
  # was read as a tap-timing problem for five runs.
  #
  # So BOTH are checked, and each catches what the other cannot. The status
  # catches a wrong route, a stale cookie, a rejected CSRF. The body catches a
  # refused claim, which really does answer 200 with a person-facing page.
  CLAIM_STATUS="$(curl -sS -o "$STATE/claim.html" -w '%{http_code}' \
    -H "Cookie: read_session=$SESSION" \
    -d "code=$CODE&csrf=$CSRF" "$(url)/api/pair/claim")"
  CLAIM="$(cat "$STATE/claim.html")"
  if [ "$CLAIM_STATUS" != "200" ]; then
    kill $SIMPID 2>/dev/null || true
    echo "FAIL: the claim was answered $CLAIM_STATUS, not 200."
    printf '%s' "$CLAIM" | head -c 400
    echo
    exit 1
  fi
  if printf '%s' "$CLAIM" | grep -qi 'not found\|expired'; then
    kill $SIMPID 2>/dev/null || true
    echo "FAIL: the claim was refused. The page said:"
    printf '%s' "$CLAIM" | grep -o '<p>[^<]*</p>' | head -2
    exit 1
  fi

  wait $SIMPID 2>/dev/null || true
  if [ ! -f "$REPO/fs_agent/.crosspoint/instapaper/.bridge" ]; then
    # The claim was accepted, so what did not happen is the on-device confirm
    # tap. Do NOT retry by hand with another qa_shot.sh run: a new simulator
    # shows a NEW pairing code, so the claim above is for a code the device is
    # no longer displaying and the gate would have nothing to confirm. Run
    # 'down' then 'up' again, which pairs from scratch.
    echo
    echo "SERVERS UP AND SIGNED IN, BUT THE DEVICE IS NOT PAIRED."
    echo
    echo "The claim was accepted, so this is the confirm tap and not the"
    echo "route. Look at qa-artifacts/sim.log: the last thing there should be"
    echo "the confirm screen being drawn. Then 'down' and 'up' again."
    echo
    echo "The servers are still up. qa_stack.sh down stops them."
    exit 2
  fi

  echo
  echo "READY. The device is signed in and the stack stays up."
  echo
  echo "Hand the tester THIS, and nothing else:"
  echo "  cd $REPO && ./server/read-bridge/tests/qa_shot.sh '<input>' '<shots>'"
  echo
  echo "It takes the same arguments as sim-shot.sh. Do not hand them sim-shot.sh"
  echo "directly: one run without READ_BRIDGE_URL goes to the live host, is"
  echo "answered 401, and the app deletes the pairing off the card."
}

case "${1:-status}" in
  up) up ;;
  down) down ;;
  status) status ;;
  *) echo "usage: qa_stack.sh up|down|status"; exit 1 ;;
esac

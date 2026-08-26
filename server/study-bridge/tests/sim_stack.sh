#!/usr/bin/env bash
# A local bridge + local "AnkiWeb" for simulator end-to-end runs. Never points
# anywhere near a real account: the allowlist is the throwaway user simtest
# and the AnkiWeb endpoint is the local sync server.
#
#   tests/sim_stack.sh start   # starts both, seeds the account, prints ports
#   tests/sim_stack.sh claim   # signs in and claims the newest pairing code
#   tests/sim_stack.sh stop
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$HERE")"
PY="$ROOT/.venv/bin/python"
BASE="${TMPDIR:-/tmp}/ankibridge-simstack"
SYNC_PORT=8995
BRIDGE_PORT=8087
USERNAME=simtest
PASSWORD=simtest-pw

start() {
  mkdir -p "$BASE"
  SYNC_USER1="$USERNAME:$PASSWORD" SYNC_BASE="$BASE/server" SYNC_HOST=127.0.0.1 SYNC_PORT=$SYNC_PORT \
    "$PY" -m anki.syncserver > "$BASE/syncserver.log" 2>&1 &
  echo $! > "$BASE/syncserver.pid"
  for i in $(seq 1 40); do nc -z 127.0.0.1 $SYNC_PORT 2>/dev/null && break; sleep 0.25; done

  "$PY" - <<PYEOF
import os
from anki.collection import Collection
col = Collection("$BASE/desktop.anki2")
auth = col.sync_login("$USERNAME", "$PASSWORD", "http://127.0.0.1:$SYNC_PORT/")
nt = col.models.by_name("Basic")
if not col.find_notes("simseed"):
    for front in ("simseed-uno", "simseed-dos"):
        n = col.new_note(nt); n["Front"], n["Back"] = front, front.upper()
        col.add_note(n, col.decks.get_current_id())
out = col.sync_collection(auth, sync_media=False)
if out.required:
    col.full_upload_or_download(auth=auth, server_usn=out.server_media_usn, upload=True)
col.close()
print("desktop seeded")
PYEOF

  "$PY" -c 'from cryptography.fernet import Fernet;print(Fernet.generate_key().decode())' > "$BASE/bridge.env.key"
  BRIDGE_DATA="$BASE/data" \
  BRIDGE_FERNET_KEY="$(cat "$BASE/bridge.env.key")" \
  BRIDGE_ALLOWLIST="$USERNAME" \
  BRIDGE_ANKIWEB_ENDPOINT="http://127.0.0.1:$SYNC_PORT/" \
  PYTHONPATH="$ROOT:$ROOT/../../tools_local/study" \
    "$PY" -m uvicorn bridge.app:app --host 127.0.0.1 --port $BRIDGE_PORT > "$BASE/bridge.log" 2>&1 &
  echo $! > "$BASE/bridge.pid"
  for i in $(seq 1 40); do nc -z 127.0.0.1 $BRIDGE_PORT 2>/dev/null && break; sleep 0.25; done
  curl -s "http://127.0.0.1:$BRIDGE_PORT/healthz"
  echo " bridge up on $BRIDGE_PORT, ankiweb on $SYNC_PORT"
}

# Claim the pairing code given as $2 (read it from the simulator's log).
claim() {
  local code="${1:?usage: sim_stack.sh claim <CODE>}"
  local jar="$BASE/cookies.txt"
  # Reuse the session when the jar already holds one: the login endpoint is
  # rate-limited on purpose, and a test harness that logs in per claim locks
  # itself out mid-run.
  if ! grep -q bridge_session "$jar" 2>/dev/null; then
    curl -s -c "$jar" -X POST -d "username=$USERNAME&password=$PASSWORD" \
      "http://127.0.0.1:$BRIDGE_PORT/login" > /dev/null
  fi
  local csrf
  csrf=$("$PY" - "$jar" <<PYEOF
import sys, json
from cryptography.fernet import Fernet
import re, os
jar = open(sys.argv[1]).read()
cookie = re.search(r'bridge_session\s+(\S+)', jar).group(1)
key = open("$BASE/bridge.env.key").read().strip() if os.path.exists("$BASE/bridge.env.key") else None
print(json.loads(Fernet(key).decrypt(cookie.encode()))["csrf"])
PYEOF
  )
  curl -s -b "$jar" -X POST -d "code=$code&csrf=$csrf" \
    "http://127.0.0.1:$BRIDGE_PORT/api/pair/claim" | grep -o "confirm on the reader" || echo "CLAIM FAILED"
}

stop() {
  for pid in "$BASE"/*.pid; do
    [ -f "$pid" ] && kill "$(cat "$pid")" 2>/dev/null || true
    rm -f "$pid"
  done
}

"${1:-start}" "${2:-}"

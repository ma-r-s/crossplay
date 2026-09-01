#!/usr/bin/env bash
# sim-shot.sh, pointed at the local QA stack. Use this and never sim-shot.sh
# directly while testing Instapaper.
#
#   ./server/read-bridge/tests/qa_shot.sh '<input>' '<shots>' [out-dir]
#
# Same arguments as sim-shot.sh, same behaviour, one difference: it exports
# READ_BRIDGE_URL for you.
#
# That is the whole point. The device finds its service through that variable,
# read per process. A single run without it goes to the LIVE host, is answered
# 401, and the app clears its stored pairing -- correctly, because a dead token
# that repeats the same refusal forever is worse. But it means one forgotten
# export does not fail a run, it signs the device out mid-session, and the
# obvious diagnoses ("the tester broke it", "pairing does not persist") are both
# wrong. A variable nobody types cannot be forgotten.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
BASE="${BRIDGE_TEST_PORT:-8996}"
BRIDGE_PORT=$((BASE + 7))

if ! curl -sf --max-time 2 "http://127.0.0.1:$BRIDGE_PORT/healthz" > /dev/null; then
  echo "The QA bridge is not answering on port $BRIDGE_PORT." >&2
  echo "Start it:  ./server/read-bridge/tests/qa_stack.sh up" >&2
  echo >&2
  echo "Refusing to run: without it the device would reach the live service," >&2
  echo "be refused, and delete its pairing -- which looks like an app defect" >&2
  echo "and is not one." >&2
  exit 1
fi

export READ_BRIDGE_URL="http://127.0.0.1:$BRIDGE_PORT"
# Boot straight into the app. A tester driving Instapaper should not have to
# find it on the shelf first, and every coordinate they are given is relative
# to the app's own screens rather than to Home.
export CROSSPLAY_AUTOSTART="${CROSSPLAY_AUTOSTART:-INSTAPAPER}"
exec "$REPO/scripts_local/sim-shot.sh" "$@"

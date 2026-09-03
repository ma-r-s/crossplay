#!/bin/bash
# Is site/emulator/ older than the sources it was built from?
#
# One answer for two callers: check.sh's staleness gate and the CI job that
# rebuilds the emulator after a merge (.github/workflows/crossplay-emulator.yml).
# Exit 0 and print "stale" when the newest commit touching a source path is
# newer than the newest commit touching the artefact; exit 1 and print "fresh"
# otherwise. The source list is the guard's, in one place.
#
#   scripts_local/emulator-stale.sh [repo-dir]
#   scripts_local/emulator-stale.sh --paths      # the source list itself
set -uo pipefail

SOURCES=(src lib assets_local tools_local/wasm platformio.sim.ini freeink-sdk)
ARTEFACT=site/emulator

# --paths: print the source list, one per line, for callers that want to name
# what moved (check.sh's message) without spelling the list a second time.
if [ "${1:-}" = "--paths" ]; then
  printf '%s\n' "${SOURCES[@]}"
  exit 0
fi
cd "${1:-$(dirname "$0")/..}" || exit 2

src_ts="$(git log -1 --format=%ct -- "${SOURCES[@]}" 2>/dev/null || echo 0)"
art_ts="$(git log -1 --format=%ct -- "$ARTEFACT" 2>/dev/null || echo 0)"
if [ "${src_ts:-0}" -gt "${art_ts:-0}" ]; then
  echo "stale (sources $(date -u -r "$src_ts" +%FT%TZ 2>/dev/null || echo "$src_ts"), emulator $(date -u -r "$art_ts" +%FT%TZ 2>/dev/null || echo "$art_ts"))"
  exit 0
fi
echo "fresh"
exit 1

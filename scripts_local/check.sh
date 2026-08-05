#!/bin/bash
# Everything that can be verified without a device. Run before every commit.
#
#   ./scripts/check.sh            # host tests, both builds
#   ./scripts/check.sh --tests    # host tests only (fast)
#
# Exits non-zero if anything fails. Prints every suite's own exit code rather
# than only its last line: a suite that fails to compile still prints "0 failed"
# for the sub-suites that ran before it, and reading only that is how a green
# report once covered a suite whose source was not even present.
set -uo pipefail

REPO="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
cd "$REPO"
LOGS="${TMPDIR:-/tmp}/xteink-check"
mkdir -p "$LOGS"
FAILED=0

echo "host tests"
for suite in host-tests/*/; do
  name=$(basename "$suite")
  [ -x "$suite/run.sh" ] || continue
  "$suite/run.sh" > "$LOGS/$name.log" 2>&1
  code=$?
  passed=$(grep -c "checks, 0 failed" "$LOGS/$name.log" || true)
  if [ "$code" -ne 0 ]; then
    printf "  %-12s FAILED (exit %d)\n" "$name" "$code"
    grep -E "FAIL|error:" "$LOGS/$name.log" | head -5 | sed 's/^/      /'
    FAILED=1
  else
    printf "  %-12s ok (%s sub-suite(s))\n" "$name" "$passed"
  fi
done

if [ "${1:-}" != "--tests" ]; then
  for env in simulator_x4_pro x4pro; do
    echo "build: $env"
    if pio run -e "$env" > "$LOGS/$env.log" 2>&1; then
      # The native build reports no RAM/Flash. Say "ok" rather than printing
      # nothing, or a clean build reads like a swallowed failure.
      grep -E "^(RAM|Flash):" "$LOGS/$env.log" | sed 's/^/  /' || echo "  ok" 
    else
      echo "  FAILED"
      grep -E "error:" "$LOGS/$env.log" | head -5 | sed 's/^/    /'
      FAILED=1
    fi
  done
fi

echo
if [ "$FAILED" -eq 0 ]; then
  echo "all green. logs in $LOGS"
else
  echo "SOMETHING FAILED. logs in $LOGS"
fi
exit "$FAILED"

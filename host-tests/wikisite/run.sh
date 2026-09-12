#!/bin/bash
# The Wikipedia install page's pure half, run under node with no browser.
# site/wikipedia/plan.js is DOM-free on purpose: the manifest check, the plan
# (copy, verify or skip each file), the ten-second rate window, the
# twenty-minute rule and the number formats are all decided there, so they
# are pinned here. site/wikipedia/sha256.js is the streaming hash every shard
# is checked with, pinned against the FIPS vectors and node's own crypto.
#
# The browser half (the picker, the streams, every screen) is driven by
# site/wikipedia/tests/flow.py in Chrome against the mock pack; that needs
# playwright and a running serve.py, so it is a laptop check, not this one.
#
# Node rather than bun to match the site suite's other JS checks and because
# GitHub's ubuntu-latest ships it. The same files run under `bun test`.
#
#   host-tests/wikisite/run.sh
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"

out="$(node --test "$ROOT"/site/wikipedia/tests/*.test.js 2>&1)"
status=$?
# node's spec reporter says "ℹ pass 27" on a terminal; its TAP reporter, which
# is what a CI runner's non-tty stdout gets, says "# pass 27". Both count.
pass="$(printf '%s\n' "$out" | sed -nE 's/^(ℹ|#) pass ([0-9]+)$/\2/p' | tail -1)"
fail="$(printf '%s\n' "$out" | sed -nE 's/^(ℹ|#) fail ([0-9]+)$/\2/p' | tail -1)"
# A run that reports nothing is not a pass: a missing node, a syntax error in
# a test file or an empty glob all print no tally, and status alone would
# read a crashed runner as green on some node versions.
if [ "$status" -ne 0 ] || [ -z "$pass" ] || [ "${fail:-1}" -ne 0 ] || [ "$pass" -lt 1 ]; then
  printf '%s\n' "$out" | grep -vE '^ℹ (duration|cancelled|skipped|todo|suites|tests)' | sed 's/^/  /'
  echo "FAIL wikisite  node --test reported pass=${pass:-none} fail=${fail:-none} status=$status"
  exit 1
fi
echo "wikisite: $pass tests, 0 failed"

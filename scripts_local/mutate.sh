#!/bin/bash
# Run one mutation test honestly.
#
#   ./scripts_local/mutate.sh <file> <old-string> <new-string> <test-command...>
#
# Exists because the same trap caught three mutants in one session: a mutation
# that does not COMPILE produces no failure lines, which looks exactly like a
# mutation the suite caught -- and a mutation whose search string does not match
# produces no change at all, which looks exactly like a mutation the suite
# missed. Both were read as results. Neither was one.
#
# So this distinguishes four outcomes and never lets two of them look alike:
#
#   CAUGHT      the suite failed. The assertion works.
#   SURVIVED    the suite passed. The assertion does NOT cover this. A finding.
#   BUILD-FAIL  the mutant did not compile. Proves nothing; rewrite it.
#   NO-MATCH    the search string was not found. Proves nothing; fix the string.
#
# The file is always restored, including on interrupt.
set -uo pipefail

FILE="${1:?usage: mutate.sh <file> <old> <new> <test-command...>}"
OLD="${2:?}"
NEW="${3:?}"
shift 3
[ $# -gt 0 ] || { echo "mutate.sh: no test command given" >&2; exit 2; }

[ -f "$FILE" ] || { echo "mutate.sh: no such file: $FILE" >&2; exit 2; }

BACKUP="$(mktemp)"
cp "$FILE" "$BACKUP"
restore() { cp "$BACKUP" "$FILE"; rm -f "$BACKUP"; }
trap restore EXIT INT TERM

if ! OLD="$OLD" NEW="$NEW" FILE="$FILE" python3 - <<'PY'
import os, sys, pathlib
p = pathlib.Path(os.environ["FILE"])
s = p.read_text()
old = os.environ["OLD"]
n = s.count(old)
if n != 1:
    print(f"NO-MATCH   the search string appears {n} times, expected exactly 1")
    sys.exit(3)
p.write_text(s.replace(old, os.environ["NEW"]))
PY
then
  echo "           nothing was mutated, so this run proves nothing."
  exit 3
fi

OUTPUT="$("$@" 2>&1)"
STATUS=$?

# Checked BEFORE the exit status, because a compiler error also exits non-zero
# and would otherwise be reported as the suite catching the mutant. That is the
# precise confusion this script exists to end, and the first version of this
# very check had it: the pattern was anchored and did not match the compiler's
# `path:line:col: error:` format.
if printf '%s\n' "$OUTPUT" | grep -qE '(^|[: ])(error|fatal error):'; then
  echo "BUILD-FAIL the mutant did not compile, so this run proves nothing."
  printf '%s\n' "$OUTPUT" | grep -E '(^|[: ])(error|fatal error):' | sed -n '1,3p'
  exit 4
fi

if [ "$STATUS" -ne 0 ]; then
  echo "CAUGHT     the suite failed, as it should."
  printf '%s\n' "$OUTPUT" | grep -E '^ *FAIL' | sed -n '1,4p'
  exit 0
fi

echo "SURVIVED   the suite passed. Nothing asserts this. That is a finding."
exit 1

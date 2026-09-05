#!/usr/bin/env bash
# Does the attack suite actually work? Watch every check go red.
#
#   server/verify_attacks.sh [read-bridge|study-bridge]
#
# The suite in attacks.py is only worth running if its checks CAN fail. This
# script proves that they can, one weakening at a time: for each entry in
# server/weaken.py it breaks the service on purpose, runs the suite, and
# asserts that exactly the checks that weakening claims to cover went red.
# Then it runs the suite unweakened and requires it to be green.
#
# Two directions, and both matter. A check that stays green under its own
# weakening does not work. A check that goes red under a weakening it does not
# claim is either mislabelled or coupled to something it should not be, and
# either way the map from "this went red" to "this is broken" is wrong -- which
# is the only thing an attack suite is for.
#
# This is not part of deploying (deploy runs attack_test.py itself). It is what
# you run when you ADD or CHANGE a check, and it is the reason the suite is
# allowed to gate a deploy at all.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVICES=("${1:-read-bridge study-bridge}")
read -r -a SERVICES <<< "${SERVICES[*]}"

# One port slice per tree, and away from the default: another worktree's stray
# server on the default slice is how this suite once spent a whole run
# attacking somebody else's service. attack_test.py refuses rather than allows
# that now, and this picks a slice unlikely to collide in the first place.
export BRIDGE_TEST_PORT="${BRIDGE_TEST_PORT:-9200}"

fail=0

for service in "${SERVICES[@]}"; do
  PY="$HERE/$service/.venv/bin/python"
  if [ ! -x "$PY" ]; then
    echo "SKIP $service: no venv (uv venv .venv && uv pip install -r requirements.txt)"
    continue
  fi
  echo "==== $service ===================================================="

  weakenings="$(cd "$HERE" && "$PY" -c 'import sys; sys.path.insert(0, "."); import weaken; print(" ".join(sorted(weaken.WEAKENINGS)))')"

  for w in $weakenings; do
    out="$(cd "$HERE/$service" && "$PY" tests/attack_test.py --weaken "$w" 2>&1)"
    rc=$?
    expected="$(printf '%s\n' "$out" | sed -n 's/^  EXPECT-RED: //p')"
    actual="$(printf '%s\n' "$out" | sed -n 's/^RED: //p')"

    if printf '%s\n' "$out" | grep -qx "NOTHING-TO-WEAKEN"; then
      # This service has nothing of that shape to break, and said so in a line
      # meant to be read by this script rather than inferred from prose. Not a
      # failure, but it IS a check that has not been seen red here, so it is
      # named rather than passed over in silence.
      echo "  n/a  $w -- nothing of that shape on this service; see weaken.py"
      continue
    fi
    if [ -z "$expected" ]; then
      echo "  FAIL $w -- the weakening applied but named no checks and did not"
      echo "       print NOTHING-TO-WEAKEN, so it may have done nothing at all"
      fail=1
      continue
    fi

    missing=""
    while IFS= read -r name; do
      [ -z "$name" ] && continue
      printf '%s\n' "$actual" | grep -qxF "$name" || missing="$missing\n    $name"
    done <<< "$expected"

    extra=""
    while IFS= read -r name; do
      [ -z "$name" ] && continue
      printf '%s\n' "$expected" | grep -qxF "$name" || extra="$extra\n    $name"
    done <<< "$actual"

    if [ -n "$missing" ]; then
      echo "  FAIL $w -- these checks stayed GREEN under a weakening that should redden them:"
      printf "$missing\n"
      fail=1
    elif [ -n "$extra" ]; then
      echo "  FAIL $w -- these checks went red but the weakening does not claim them:"
      printf "$extra\n"
      echo "    (either the label is wrong or the check is coupled to something else)"
      fail=1
    else
      echo "  ok   $w -- $(printf '%s\n' "$expected" | grep -c .) check(s) went red, and only those"
    fi
    [ "$rc" -eq 0 ] && { echo "  FAIL $w -- the suite exited 0 while red"; fail=1; }
  done

  out="$(cd "$HERE/$service" && "$PY" tests/attack_test.py 2>&1)"
  if [ $? -eq 0 ]; then
    echo "  ok   unweakened -- $(printf '%s' "$out" | tail -1)"
  else
    echo "  FAIL unweakened -- the suite is red against the real service:"
    printf '%s\n' "$out" | grep -E "^  FAIL|^RED:" | sed 's/^/    /'
    fail=1
  fi
done

echo
if [ "$fail" -eq 0 ]; then
  echo "verify_attacks: PASS -- every check was seen failing, and passing"
else
  echo "verify_attacks: FAILED"
fi
exit $fail

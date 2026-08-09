#!/bin/bash
# The CI workflow's own test.
#
# The "Host tests" step in .github/workflows/crossplay-ci.yml has to forgive
# exactly one thing -- ui:paperOnTheBand -- and nothing else. It used to say
# that as `bash "$suite" || echo "::warning::"`, which forgives the whole ui
# suite for failing instead. The two differ precisely when it matters: a
# test_ui.cpp that does not compile exits non-zero with no FAIL lines, and the
# old form waved it through as the expected baseline. So did a real regression
# sitting next to the baseline.
#
# Shell in a yaml file is the one part of this repo nothing else executes, so
# it is the one part that can quietly stop meaning what it says. This runs the
# step's actual text -- extracted from the yaml, not copied -- against the four
# shapes a ui run can have, plus a non-ui suite failing.
#
#   host-tests/ci/run.sh
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
YML="$HERE/../../.github/workflows/crossplay-ci.yml"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

[ -f "$YML" ] || { echo "FAIL cannot find $YML"; exit 1; }

# Lift the step body out of the yaml and dedent it, so this test cannot drift
# from the text CI actually runs.
python3 - "$YML" >"$WORK/step.sh" <<'PY'
import sys
lines = open(sys.argv[1]).read().splitlines()
i = next(i for i, l in enumerate(lines) if l.strip() == '- name: Host tests')
j = next(j for j in range(i, len(lines)) if lines[j].strip() == 'run: |')
body, indent = [], None
for l in lines[j + 1:]:
    if not l.strip():
        body.append('')
        continue
    cur = len(l) - len(l.lstrip())
    if indent is None:
        indent = cur
    if cur < indent:
        break
    body.append(l[indent:])
print('\n'.join(body))
PY

[ -s "$WORK/step.sh" ] || { echo "FAIL could not extract the Host tests step"; exit 1; }

fake() {  # name, exit code, stdout
  mkdir -p "$WORK/host-tests/$1"
  { echo '#!/bin/sh'
    printf 'cat <<%s\n%s\n%s\n' "'EOF'" "$3" EOF
    echo "exit $2"
  } >"$WORK/host-tests/$1/run.sh"
}

checks=0
failed=0
expect() {  # label, pass|fail
  local label="$1" want="$2" code got
  # bash -eo pipefail is what GitHub Actions gives a `run:` block.
  ( cd "$WORK" && bash -eo pipefail step.sh >"$WORK/out" 2>&1 )
  code=$?
  got=pass
  [ $code -ne 0 ] && got=fail
  checks=$((checks + 1))
  if [ "$got" != "$want" ]; then
    failed=$((failed + 1))
    echo "FAIL ci-workflow  $label: got $got, wanted $want"
    sed 's/^/       /' "$WORK/out"
  fi
}

rm -rf "$WORK/host-tests"; fake ui 0 '2284 checks, 0 failed'
expect "a green ui suite passes" pass

rm -rf "$WORK/host-tests"; fake ui 1 'FAIL test_ui.cpp:1411  paperOnTheBand
2284 checks, 1 failed'
expect "the paperOnTheBand baseline alone is forgiven" pass

rm -rf "$WORK/host-tests"; fake ui 1 'test_ui.cpp:1993:19: error: incompatible pointer types'
expect "a ui suite that never compiled fails the build" fail

rm -rf "$WORK/host-tests"; fake ui 1 'FAIL test_ui.cpp:1411  paperOnTheBand
FAIL test_ui.cpp:2100  someRealRegression
2284 checks, 2 failed'
expect "a regression beside the baseline fails the build" fail

rm -rf "$WORK/host-tests"; fake ui 0 '2284 checks, 0 failed'; fake chess 1 'FAIL boom'
expect "a non-ui suite failing still fails the build" fail

echo "$checks checks, $failed failed"
[ "$failed" -eq 0 ]

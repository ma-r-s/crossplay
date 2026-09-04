#!/bin/bash
# The CI workflow's own test.
#
# The "Host tests" step in .github/workflows/crossplay-ci.yml forgives nothing:
# every suite must exit zero. It used to forgive exactly one thing --
# ui:paperOnTheBand, a real baseline failure until 2026-08-10 -- and the
# exemption outlived the failure by four days. In that window a regression in
# exactly that test passed CI with a warning while a local check.sh went red.
#
# Shell in a yaml file is the one part of this repo nothing else executes, so
# it is the one part that can quietly stop meaning what it says. This runs the
# step's actual text -- extracted from the yaml, not copied -- against the
# shapes that once got special treatment, and asserts none of them do now.
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

rm -rf "$WORK/host-tests"; fake ui 0 '2284 checks, 0 failed'; fake chess 0 'ok'
expect "green suites pass" pass

# The shape that used to be forgiven. It must not be anymore.
rm -rf "$WORK/host-tests"; fake ui 1 'FAIL test_ui.cpp:1411  paperOnTheBand
2284 checks, 1 failed'
expect "a paperOnTheBand failure fails the build" fail

rm -rf "$WORK/host-tests"; fake ui 1 'test_ui.cpp:1993:19: error: incompatible pointer types'
expect "a ui suite that never compiled fails the build" fail

rm -rf "$WORK/host-tests"; fake ui 0 '2284 checks, 0 failed'; fake chess 1 'FAIL boom'
expect "a non-ui suite failing still fails the build" fail

# Every git dependency must be pinned by its FULL commit id.
#
# A short SHA resolves against a warm cache, because the object is already on
# disk, and fails on a cold clone with `fatal: couldn't find remote ref
# 8323320` -- a fetch takes a ref or a full commit id, never an abbreviation.
# So it passes every local build and breaks CI alone, which is the one failure
# mode nothing else here can see. It cost a red xteink that was blamed on
# upstream for hours.
for ini in "$HERE/../.."/platformio*.ini; do
  [ -f "$ini" ] || continue
  while IFS= read -r pin; do
    checks=$((checks + 1))
    if [ "${#pin}" -eq 40 ]; then
      :
    else
      failed=$((failed + 1))
      echo "FAIL ci  $(basename "$ini"): git pin '#$pin' is ${#pin} chars, not a full 40-character commit id; it will fail on a cold clone"
    fi
  done < <(grep -oE '^[^;#]*=(https?|git)://[^ ]*#[0-9a-f]+' "$ini" | sed 's/.*#//')
done

# The release trigger, which is a concurrency setting three files away.
#
# crossplay-autorelease.yml fires on `workflow_run` with conclusion success.
# A run cancelled by the next merge has no conclusion, so it releases nothing.
# With cancel-in-progress true on xteink, a stream of merges arriving faster
# than this build takes cancels every run in turn and NOTHING ever releases,
# while each individual cancellation looks like the concurrency rule working.
# That ran for six merges on 2026-09-03 with RELEASE_HOLD at 0.
#
# Superseding is still right on a pull request, so this asserts the shape
# rather than the absence: cancellation must be conditional on the ref.
checks=$((checks + 1))
cip=$(grep -A 2 '^concurrency:' "$YML" | grep 'cancel-in-progress:' | cut -d: -f2- | tr -d ' ')
case "$cip" in
  true|"")
    failed=$((failed + 1))
    echo "FAIL ci  crossplay-ci.yml cancels in progress unconditionally: a merge on xteink kills the previous merge's run, and the autorelease only fires on a run that reached success"
    ;;
  *xteink*) ;;
  *)
    failed=$((failed + 1))
    echo "FAIL ci  crossplay-ci.yml's cancel-in-progress ('$cip') does not mention xteink, so it cannot be exempting the branch the autorelease listens to"
    ;;
esac

echo "$checks checks, $failed failed"
[ "$failed" -eq 0 ]

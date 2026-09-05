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

# Lift a named step's shell body out of a workflow and dedent it, so these
# tests cannot drift from the text the runner actually executes. ONE extractor
# for every caller below: this python was copied three times in this file, and a
# step whose name stops matching is a "could not extract" rather than a silent
# pass in each copy separately.
lift_step() {  # <workflow.yml> <step name>  -> that step's run: block, dedented
  python3 - "$1" "$2" <<'PY'
import sys
lines = open(sys.argv[1]).read().splitlines()
i = next(i for i, l in enumerate(lines) if l.strip() == '- name: ' + sys.argv[2])
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
}

lift_step "$YML" 'Host tests' >"$WORK/step.sh"
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

# The release is built once per tag. host-tests/release asserts the SHAPE
# (the dispatch is conditional on RELEASE_TOKEN, and crossplay-release.yml
# keeps its tag trigger); this runs the step's own text, lifted from the
# yaml, with a fake gh that records every call, so a condition that is
# present but inverted fails here and nowhere else. v1.12.16 was built and
# published twice on 2026-09-04, one run per path, before either existed.
AYML="$HERE/../../.github/workflows/crossplay-autorelease.yml"
lift_step "$AYML" 'Build and publish the release' >"$WORK/publish.sh"
[ -s "$WORK/publish.sh" ] || { echo "FAIL could not extract the publish step from crossplay-autorelease.yml"; exit 1; }
mkdir -p "$WORK/bin"
printf '#!/bin/sh\necho "gh $*" >> "%s/gh.calls"\n' "$WORK" >"$WORK/bin/gh"; chmod +x "$WORK/bin/gh"
publish() {  # label, PUSH_STARTS_IT value, wanted number of dispatches
  rm -f "$WORK/gh.calls"
  ( cd "$WORK" && PATH="$WORK/bin:$PATH" PUSH_STARTS_IT="$2" NEXT=1.2.3 bash -eo pipefail publish.sh >"$WORK/out" 2>&1 )
  local n; n=$(grep -c 'workflow run crossplay-release.yml --ref v1.2.3' "$WORK/gh.calls" 2>/dev/null || true); [ -n "$n" ] || n=0
  checks=$((checks + 1))
  if [ "$n" -ne "$3" ]; then
    failed=$((failed + 1))
    echo "FAIL ci-autorelease  $1: crossplay-release.yml dispatched $n times, wanted $3"
    sed 's/^/       /' "$WORK/out"
  fi
}
publish "a tag pushed with RELEASE_TOKEN is not dispatched again"      true  0
publish "a tag pushed with the workflow token is dispatched once"      false 1
publish "no flag at all (no secret, older text) still dispatches once" ""    1

# The two stack-checked device envs build in ONE pio run invocation.
#
# pio run wipes the whole .pio/build root on every invocation
# (clean_build_dir, whose checksum changes because the build generates
# gitignored headers). Split across two invocations, the x4pro stack check
# passes only because it is sequenced before the sticky build that deletes its
# directory -- correct today, guaranteed by nothing, and one moved step from
# reading a directory that no longer exists.
#
# Asserting the grouping rather than the spacing, because the spacing is
# satisfied by the broken arrangement too: each check already follows its own
# build. What is missing there is the guarantee, not the order.
#
# stack_budget.py refuses rather than passing empty (it exits on no frames and
# fails an unchecked task), so the split would have gone red rather than
# silent. That is why this is fragility and not a defect -- and why it is
# asserted here instead of waiting for someone to trip it.
checks=$((checks + 1))
stack_builds=$(grep -c 'fstack-usage.*pio run' "$YML")
if [ "$stack_builds" -ne 1 ]; then
  failed=$((failed + 1))
  echo "FAIL ci  expected ONE stack-flagged pio run covering both device envs, found $stack_builds; each extra invocation wipes .pio/build and leaves the stack checks depending on step order"
else
  # Which envs must that invocation cover? Ask the stack checks, do not name
  # them. They were x4pro and sticky, they are gh_release_x4pro and
  # gh_release_sticky now that CI builds only what ships, and a hardcoded pair
  # here would have gone quietly wrong at exactly that rename -- asserting a
  # grouping over envs the workflow no longer builds.
  stack_line=$(grep 'fstack-usage.*pio run' "$YML")
  for env in $(grep -o -- '--build-dir \.pio/build/[A-Za-z0-9_]*' "$YML" | sed 's#.*/##'); do
    checks=$((checks + 1))
    case "$stack_line" in
      *"-e $env"*) ;;
      *)
        failed=$((failed + 1))
        echo "FAIL ci  $env has a stack check but is not built by the stack-flagged invocation"
        ;;
    esac
  done
  if [ -z "$(grep -o -- '--build-dir \.pio/build/[A-Za-z0-9_]*' "$YML")" ]; then
    failed=$((failed + 1))
    echo "FAIL ci  no stack checks found at all; this assertion just checked nothing"
  fi
fi

# -- the packaging change must say what is new -------------------------------
#
# scripts_local/device-build-needed.sh calls
# .github/workflows/crossplay-release.yml `quiet`: it cuts a release, and it
# cannot describe itself in a player's words, so release_notes.py gives it a
# bullet only when the pull request wrote one. Without this step the page goes
# silent about a packaging fix -- and PR #42's packaging fix is the reason
# somebody's install works at all.
#
# EXECUTED, not grepped, for the reason at the top of this file: four greps for
# a step's ingredients once passed a version of it ending in `&& false`.
STEP="$(python3 - "$YML" <<'LIFT'
import sys
lines = open(sys.argv[1]).read().splitlines()
try:
    i = next(i for i, l in enumerate(lines)
             if l.strip() == "- name: A change to what the release publishes must say what is new")
except StopIteration:
    sys.exit(0)
j = next(k for k in range(i, len(lines)) if lines[k].strip() == "run: |")
out = []
for l in lines[j + 1:]:
    if l.strip() and not l.startswith(" " * 10):
        break
    out.append(l[10:] if l.startswith(" " * 10) else "")
print("\n".join(out))
LIFT
)"
if [ -z "$STEP" ]; then
  checks=$((checks + 1)); failed=$((failed + 1))
  echo "FAIL ci  crossplay-ci.yml has no step asking a packaging change to say what is new; a release-workflow fix would reach the page as its own title or not at all"
else
  # A repository where the change DOES touch the publishing workflow, so the
  # step gets past its own early exit and actually judges the body.
  PKG="$WORK/pkg"; mkdir -p "$PKG/.github/workflows"
  ( cd "$PKG" \
    && git init -q -b xteink && git config user.email t@t && git config user.name t \
    && echo x > seed.txt && git add -A && git commit -qm base \
    && git checkout -qb pr && echo y >> .github/workflows/crossplay-release.yml \
    && git add -A && git commit -qm "ci: publish the merged image" ) >/dev/null 2>&1
  ( cd "$PKG" && git remote add origin "$PKG" && git fetch -q origin 2>/dev/null ) >/dev/null 2>&1

  step_says() {  # <body>  -> 0 when the step is satisfied
    ( cd "$PKG" && PR_BODY="$1" BASE=xteink bash -c "$STEP" ) >/dev/null 2>&1
  }
  if step_says "What is new: installs that failed part-way now work."; then
    checks=$((checks + 1))
  else
    checks=$((checks + 1)); failed=$((failed + 1))
    echo "FAIL ci  the step rejects a pull request that DID write a What is new line"
  fi
  # The half a grep cannot see.
  if step_says "Just a refactor, nothing to say."; then
    checks=$((checks + 1)); failed=$((failed + 1))
    echo "FAIL ci  the step ACCEPTS a packaging change with no What is new line; the release page would be silent about it"
  else
    checks=$((checks + 1))
  fi
  # And it must not fire on a change that publishes nothing, or every pull
  # request in the repository needs release prose.
  OTHER="$WORK/other"; mkdir -p "$OTHER"
  ( cd "$OTHER" \
    && git init -q -b xteink && git config user.email t@t && git config user.name t \
    && echo x > seed.txt && git add -A && git commit -qm base \
    && git checkout -qb pr && mkdir -p src && echo 'int x;' > src/x.cpp \
    && git add -A && git commit -qm "fix: a game" \
    && git remote add origin "$OTHER" && git fetch -q origin ) >/dev/null 2>&1
  if ( cd "$OTHER" && PR_BODY="nothing here" BASE=xteink bash -c "$STEP" ) >/dev/null 2>&1; then
    checks=$((checks + 1))
  else
    checks=$((checks + 1)); failed=$((failed + 1))
    echo "FAIL ci  the step demands release prose from a pull request that publishes nothing"
  fi
fi

# -- the emulator rebuild must be able to FAIL -------------------------------
#
# scripts_local/emulator-stale.sh answers three ways, and says so in its own
# header: 0 stale, 1 fresh, 2 it could not look. crossplay-emulator.yml asked it
# inside `if bash ...; then stale=true; else stale=false; fi`, which keeps only
# "was that a zero" -- so the crash was recorded as fresh. Every later step in
# that job is gated on `steps.stale.outputs.stale == 'true'`, so all of them
# were skipped, and the job ended green having rebuilt nothing. That is
# byte-for-byte the outcome of a genuinely fresh emulator, and site/emulator/ is
# what the web page runs.
#
# EXECUTED against a fake script rather than grepped, for the reason at the top
# of this file: the broken shape and the fixed one both contain the script's
# name, both mention GITHUB_OUTPUT, and a grep for either matches both.
EYML="$HERE/../../.github/workflows/crossplay-emulator.yml"
lift_step "$EYML" 'Is the emulator behind its sources?' >"$WORK/stale.sh"
[ -s "$WORK/stale.sh" ] || { echo "FAIL could not extract the staleness step from crossplay-emulator.yml"; exit 1; }

stale_says() {  # label, the script's exit code, wanted step outcome, wanted output line
  local label="$1" rc="$2" want="$3" line="$4" code got
  rm -rf "$WORK/emu"; mkdir -p "$WORK/emu/scripts_local"
  # "gone" is the script deleted or renamed rather than any exit code, which is
  # the same family of non-answer and used to be filed as 'fresh' too.
  if [ "$rc" != gone ]; then
    printf '#!/bin/sh\necho "pretending"\nexit %s\n' "$rc" >"$WORK/emu/scripts_local/emulator-stale.sh"
  fi
  : >"$WORK/emu/gh_output"
  # bash -eo pipefail is what GitHub Actions gives a `run:` block, and it is
  # half of what made this subtle: a bare `bash script` returning 1 under -e
  # would abort the step, so the fix has to hold the code without tripping it.
  ( cd "$WORK/emu" && GITHUB_OUTPUT="$WORK/emu/gh_output" bash -eo pipefail "$WORK/stale.sh" ) >"$WORK/emu/log" 2>&1
  code=$?
  got=pass; [ $code -ne 0 ] && got=fail
  checks=$((checks + 1))
  if [ "$got" != "$want" ]; then
    failed=$((failed + 1))
    echo "FAIL ci-emulator  $label: emulator-stale.sh exited $rc and the step $got, wanted $want"
    sed 's/^/       /' "$WORK/emu/log"
    return
  fi
  checks=$((checks + 1))
  if [ -n "$line" ] && ! grep -qx "$line" "$WORK/emu/gh_output"; then
    failed=$((failed + 1))
    echo "FAIL ci-emulator  $label: exit $rc did not record '$line' ($(tr '\n' ' ' < "$WORK/emu/gh_output"))"
  elif [ -z "$line" ] && grep -q 'stale=' "$WORK/emu/gh_output"; then
    failed=$((failed + 1))
    echo "FAIL ci-emulator  $label: exit $rc still recorded '$(tr '\n' ' ' < "$WORK/emu/gh_output")'; a script that could not answer must not be filed as an answer, least of all as the answer that skips the rebuild"
  fi
}

stale_says "0 means stale, and the rebuild runs"  0 pass "stale=true"
stale_says "1 means fresh, and the job does nothing" 1 pass "stale=false"
stale_says "2 is the script telling us it could not look" 2 fail ""
# The script deleted, renamed or not executable: 127 is not one of the two real
# answers either, and it used to be filed as 'fresh' by the same else branch.
stale_says "a missing script is not an answer" gone fail ""

# -- the long jobs have a cap on being stuck ---------------------------------
#
# No job in any workflow here had timeout-minutes, so every one of them
# inherited GitHub's SIX HOUR default. A hung step -- a fetch retrying forever,
# a build waiting on a lock -- therefore holds a runner for a quarter of a day
# while the thing that started it reads as still running, which is the same
# display as a build that is merely slow.
#
# Only the two that cross-compile are asserted: they are the ones long enough
# that "slow" and "stuck" look alike, and a blanket rule over every job would be
# a number to maintain in six places for jobs that finish in one minute.
job_timeout() {  # <workflow.yml> <job name>
  python3 - "$1" "$2" <<'TMO'
import re, sys
lines = open(sys.argv[1]).read().splitlines()
try:
    i = next(i for i, l in enumerate(lines) if l.rstrip() == '  ' + sys.argv[2] + ':')
except StopIteration:
    print('NOJOB'); raise SystemExit(0)
for l in lines[i + 1:]:
    if l.strip() and not l.startswith('    '):
        break
    m = re.match(r'\s*timeout-minutes:\s*(\d+)', l)
    if m:
        print(m.group(1)); raise SystemExit(0)
print('NONE')
TMO
}
for pair in "$YML:build" "$HERE/../../.github/workflows/crossplay-release.yml:release"; do
  f="${pair%:*}"; j="${pair##*:}"
  checks=$((checks + 1))
  t="$(job_timeout "$f" "$j")"
  case "$t" in
    NOJOB)
      failed=$((failed + 1))
      echo "FAIL ci  $(basename "$f") has no job called '$j'; this assertion is looking at nothing" ;;
    NONE)
      failed=$((failed + 1))
      echo "FAIL ci  $(basename "$f")'s '$j' job has no timeout-minutes, so a hung step holds a runner for GitHub's default six hours and reads as a slow build the whole time" ;;
    "")
      failed=$((failed + 1))
      echo "FAIL ci  could not read a timeout out of $(basename "$f") at all; this check answered nothing rather than answering no" ;;
    *) : ;;
  esac
done

# -- an inherited workflow must say what it does HERE -------------------------
#
# ci.yml and pr-formatting-check.yml are disabled_manually on GitHub and said
# nothing about it in the file: ci.yml still reads `on: pull_request`, which is
# as live-looking a trigger as exists. release_candidate.yml is worse -- it is
# dispatch-only AND gated on a `release/` ref that this fork's `app/*` branches
# can never match, so dispatching it produces a green run with zero jobs, which
# is indistinguishable from a release candidate that built.
#
# None of them is deleted, because deleting them conflicts on every sync from
# upstream. So the rule is that they carry the reason instead, in release.yml's
# shape.
#
# Written over "every workflow that is not this fork's own" rather than over a
# list of the four, so the NEXT upstream workflow a sync brings in arrives red
# until somebody says whether it runs here.
for wf in "$HERE/../.."/.github/workflows/*.yml; do
  case "$(basename "$wf")" in crossplay-*) continue ;; esac
  checks=$((checks + 1))
  if grep -q "FORK CHANGE" "$wf"; then
    :
  else
    failed=$((failed + 1))
    echo "FAIL ci  $(basename "$wf") is inherited from upstream and carries no 'FORK CHANGE:' note saying whether it runs in this fork; a disabled or unreachable workflow reads exactly like a live one"
  fi
done

echo "$checks checks, $failed failed"
[ "$failed" -eq 0 ]

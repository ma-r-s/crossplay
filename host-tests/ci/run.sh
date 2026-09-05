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

# The release is built once per tag. host-tests/release asserts the SHAPE
# (the dispatch is conditional on RELEASE_TOKEN, and crossplay-release.yml
# keeps its tag trigger); this runs the step's own text, lifted from the
# yaml, with a fake gh that records every call, so a condition that is
# present but inverted fails here and nowhere else. v1.12.16 was built and
# published twice on 2026-09-04, one run per path, before either existed.
AYML="$HERE/../../.github/workflows/crossplay-autorelease.yml"
python3 - "$AYML" 'Build and publish the release' >"$WORK/publish.sh" <<'PY'
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

# -- the tip check must ask what MOVED, not what the commit called itself -----
#
# The gate refuses to release when xteink has moved past the commit CI
# verified, and it has to: releasing a tip nothing verified is exactly the
# thing it exists to stop. But this workflow carries
# `paths-ignore: site/emulator/**` (above), so the emulator rebuild that
# crossplay-emulator.yml commits after every merge gets NO CI run and never
# will. If the gate refuses on that commit, the tip is stuck behind a run that
# cannot exist, and the only thing that ever releases anything again is an
# unrelated push -- which is why the stall heals itself often enough to read as
# weather rather than as a deadlock.
#
# It was excused by `git log --format=%s | grep -vq '^chore: emulator rebuilt'`:
# a copy of a string that lives in another workflow file, deciding a question
# about content by reading a subject line. Both halves are asserted here, and
# each one is a different way for that to be wrong:
#
#   a gap that reaches nothing releases WHATEVER its commits are titled, so
#   renaming the emulator commit cannot silently stop every release;
#   a gap that reaches something refuses WHATEVER it titles itself, so a commit
#   claiming to be an emulator rebuild cannot carry src/ past the gate.
#
# EXECUTED, against real repositories and the real classification table, for
# the reason at the top of this file.
#
# THE EXTRACTOR BELOW IS A COPY, and it should not survive contact with the
# branch that collapses the other three into a `lift_step` helper (board #235,
# wt/cigaps). The two changes do not overlap textually, so git will merge them
# without a word and leave this here as a fourth copy. Whoever lands second:
# delete the heredoc and write
#   lift_step "$AYML" 'Decide whether to release' >"$WORK/gate.sh"
python3 - "$AYML" 'Decide whether to release' >"$WORK/gate.sh" <<'PY'
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
[ -s "$WORK/gate.sh" ] || { echo "FAIL could not extract the gate step from crossplay-autorelease.yml"; exit 1; }

# One fixture repository, rebuilt per case. The classification table is the
# REAL one -- the whole point is that the gate reads it rather than restating
# it -- and release-needed.sh is a stub that always says yes, so what this
# measures is the tip check alone. Whether a range warrants a release at all is
# host-tests/autorelease's subject and is answered by the same table.
gate_repo() {  # <path>  -> a repo whose HEAD is the base, with the table in it
  rm -rf "$1"; mkdir -p "$1/scripts_local"
  cp "$HERE/../../scripts_local/device-build-needed.sh" "$1/scripts_local/"
  printf '#!/bin/sh\necho "reaches a user: yes (stubbed)"\nexit 0\n' >"$1/scripts_local/release-needed.sh"
  mkdir -p "$1/src"; echo 'int base;' >"$1/src/base.cpp"
  ( cd "$1" && git init -q -b xteink && git config user.email t@t && git config user.name t \
    && git add -A && git commit -qm "base" ) >/dev/null 2>&1
}
gate_commit() {  # <repo> <path> <subject>
  mkdir -p "$(dirname "$1/$2")"; echo "$(date +%s%N)" >>"$1/$2"
  ( cd "$1" && git add -A && git commit -qm "$3" ) >/dev/null 2>&1
}
gate_expect() {  # <label> <repo> <VERIFIED> <true|false wanted go> <substring wanted in the log>
  local label="$1" repo="$2" verified="$3" want="$4" msg="$5" got
  : >"$WORK/gh_output"
  ( cd "$repo" && GITHUB_OUTPUT="$WORK/gh_output" VERIFIED="$verified" HOLD="" \
      bash -eo pipefail "$WORK/gate.sh" ) >"$WORK/out" 2>&1
  got="$(grep -o 'go=[a-z]*' "$WORK/gh_output" | tail -1 | cut -d= -f2)"
  checks=$((checks + 1))
  if [ "$got" != "$want" ]; then
    failed=$((failed + 1))
    echo "FAIL ci-autorelease  $label: gate said go=${got:-<nothing>}, wanted go=$want"
    sed 's/^/       /' "$WORK/out"
    return
  fi
  checks=$((checks + 1))
  if ! grep -q "$msg" "$WORK/out"; then
    failed=$((failed + 1))
    echo "FAIL ci-autorelease  $label: right answer, wrong reason -- the log never says '$msg'"
    sed 's/^/       /' "$WORK/out"
  fi
}

G="$WORK/gate-repo"

# (1) The tip has not moved. Nothing to excuse; the gate never reaches the
#     comparison at all.
gate_repo "$G"
gate_expect "an unmoved tip releases" "$G" "$(git -C "$G" rev-parse HEAD)" true "reaches a user"

# (2) The tip moved by an emulator rebuild and nothing else. This is the case
#     no CI run can ever arrive for.
gate_repo "$G"; V="$(git -C "$G" rev-parse HEAD)"
gate_commit "$G" site/emulator/crossplay.wasm "chore: emulator rebuilt for $V"
gate_expect "a tip that moved only by an emulator rebuild releases" "$G" "$V" true \
  "only by commits no device build and no release can see"

# (3) The tip moved by real firmware. The protection, unchanged: nothing
#     verified src/ at this tip, so nothing releases it.
gate_repo "$G"; V="$(git -C "$G" rev-parse HEAD)"
gate_commit "$G" src/apps_local/Sudoku.cpp "fix(sudoku): the givens survive a rotate"
gate_expect "a tip that moved by firmware refuses" "$G" "$V" false \
  "moved past the commit CI verified"

# (4) The same firmware change, TITLED as an emulator rebuild. The subject grep
#     released this; the table cannot be talked to.
gate_repo "$G"; V="$(git -C "$G" rev-parse HEAD)"
gate_commit "$G" src/apps_local/Sudoku.cpp "chore: emulator rebuilt for $V"
gate_expect "a firmware commit calling itself an emulator rebuild still refuses" "$G" "$V" false \
  "moved past the commit CI verified"

# (5) The same emulator rebuild under any other name. The subject grep refused
#     this forever, silently, from the first time somebody reworded the commit
#     crossplay-emulator.yml writes.
gate_repo "$G"; V="$(git -C "$G" rev-parse HEAD)"
gate_commit "$G" site/emulator/crossplay.wasm "build(site): wasm refreshed"
gate_expect "an emulator rebuild under another name still releases" "$G" "$V" true \
  "only by commits no device build and no release can see"

# (6) The gap builds but does not ship, and the gap ships but does not build.
#     One column would answer no to each of these; the gate asks both.
gate_repo "$G"; V="$(git -C "$G" rev-parse HEAD)"
gate_commit "$G" scripts_local/check.sh "gate: trim the cache harder"
gate_expect "a gap that breaks a build but ships nothing refuses" "$G" "$V" false \
  "moved past the commit CI verified"
gate_repo "$G"; V="$(git -C "$G" rev-parse HEAD)"
gate_commit "$G" .github/workflows/crossplay-release.yml "ci: publish the merged image"
gate_expect "a gap that changes what the release publishes refuses" "$G" "$V" false \
  "moved past the commit CI verified"

# (7) A path in no row of the table. The tool refuses to classify it and the
#     gate must inherit that refusal rather than reading it as "inert".
gate_repo "$G"; V="$(git -C "$G" rev-parse HEAD)"
gate_commit "$G" brandnew/thing.c "feat: a directory nobody has classified"
gate_expect "an unclassified path in the gap refuses" "$G" "$V" false \
  "moved past the commit CI verified"

# (8) A verified commit that is not in the tip's history at all. --range diffs
#     from the merge base, so without the ancestry check this could answer
#     "nothing changed" about a history CI never saw.
gate_repo "$G"; V="$(git -C "$G" rev-parse HEAD)"
( cd "$G" && git checkout -q -b sideline && mkdir -p src && echo 'int side;' >src/side.cpp \
  && git add -A && git commit -qm "feat: on a branch of its own" ) >/dev/null 2>&1
SIDE="$(git -C "$G" rev-parse HEAD)"
( cd "$G" && git checkout -q xteink ) >/dev/null 2>&1
gate_commit "$G" site/emulator/crossplay.wasm "chore: emulator rebuilt for $V"
gate_expect "a verified commit outside the tip's history refuses" "$G" "$SIDE" false \
  "is not in the history of"

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

echo "$checks checks, $failed failed"
[ "$failed" -eq 0 ]

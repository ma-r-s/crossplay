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
# tests cannot drift from the text the runner actually executes.
#
# This python was copied three times in this file. Two of the three were
# identical and now call this; the third (the packaging-step check further down,
# heredoc LIFT) is deliberately left alone -- it dedents by a hardcoded ten
# spaces and exits 0 rather than raising when its step is absent, so folding it
# in here would change its behaviour rather than tidy it.
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
# Same floor as everywhere else in this file: no .ini files found means this
# loop asserted nothing, and said so by staying silent.
inis=0
for ini in "$HERE/../.."/platformio*.ini; do
  [ -f "$ini" ] || continue
  inis=$((inis + 1))
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
checks=$((checks + 1))
if [ "$inis" -eq 0 ]; then
  failed=$((failed + 1))
  echo "FAIL ci  no platformio*.ini found, so the git-pin check examined nothing"
fi

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

# -- and the OTHER half of the concurrency rule, the one nothing governs -----
#
# `cancel-in-progress: false` reads like "no run of this group is ever
# cancelled". It is not what it does. A group holds at most ONE run in progress
# and ONE run pending; cancel-in-progress decides the fate of the first. The
# second is cancelled whenever a third run arrives, either way. So the check
# above -- cancellation must be conditional on the ref -- was satisfied by a
# workflow that still lost a run per merge.
#
# MEASURED before this was written: in the 36 hours after 52d0907f made
# cancellation conditional, 41 runs of this workflow on xteink were cancelled
# and `actions/runs/<id>/jobs` returned total_count 0 for every one of them.
# None had started a job. Each was a merge whose CI reached no conclusion, and
# crossplay-autorelease.yml only fires on a run that reached success.
#
# The property that makes that impossible is not a setting, it is the GROUP:
# two commits on xteink must land in groups that cannot contend. So this
# EVALUATES the expression -- the operators GitHub's own expression syntax uses,
# over a context this supplies -- rather than reading it. A group asserted by
# grep would pass on `crossplay-ci-${{ github.ref }}`, which is the bug.
#
# And it asserts the pull-request half in the same breath, because the cheap way
# to make merges unique is to make EVERY run unique, and that silently ends
# superseding on branches -- five runs of one branch sharing the runners, which
# is what the group was added for in the first place.
conc_eval() {  # <workflow.yml> <group|cancel-in-progress> <github.ref> <github.sha>
  python3 - "$1" "$2" "$3" "$4" <<'PY'
import re, sys

yml, key, ref, sha = sys.argv[1:5]
raw, inblock = None, False
for line in open(yml).read().splitlines():
    if line.rstrip() == 'concurrency:':
        inblock = True
        continue
    if inblock:
        if line.strip() and line[:1] not in (' ', '\t'):
            break
        m = re.match(r'\s+' + re.escape(key) + r':\s*(.*)$', line)
        if m:
            raw = m.group(1).strip()
            break
if raw is None:
    print('NO-' + key.upper())
    raise SystemExit(0)
if len(raw) > 1 and raw[0] == raw[-1] and raw[0] in '"\'':
    raw = raw[1:-1]

ctx = {'github.ref': ref, 'github.sha': sha}


def operand(tok):
    tok = tok.strip()
    if len(tok) > 1 and tok[0] == tok[-1] and tok[0] in '"\'':
        return tok[1:-1]
    if tok in ('true', 'false'):
        return tok == 'true'
    if tok in ctx:
        return ctx[tok]
    print('UNEVALUABLE ' + tok)
    raise SystemExit(0)


def truthy(v):
    return v not in ('', False, 0, None)


def compare(part):
    for op in ('==', '!='):
        if op in part:
            a, b = part.split(op, 1)
            same = operand(a) == operand(b)
            return same if op == '==' else not same
    return operand(part)


def evaluate(expr):
    # GitHub's precedence: && binds tighter than ||, and both return the
    # OPERAND rather than a boolean, which is the whole trick behind
    # `cond && a || b`.
    last = None
    for alt in expr.split('||'):
        val = None
        for conj in alt.split('&&'):
            v = compare(conj)
            val = v if val is None else (v if truthy(val) else val)
        if truthy(val):
            return val
        last = val
    return last


def render(v):
    return 'true' if v is True else ('false' if v is False else str(v))


print(re.sub(r'\$\{\{(.*?)\}\}', lambda m: render(evaluate(m.group(1))), raw))
PY
}

XTEINK=refs/heads/xteink
PR=refs/pull/7/merge

# Two merges. Different commits, and they must not be able to queue behind one
# another, because a queue of two is a queue GitHub trims from the middle.
merge_a="$(conc_eval "$YML" group "$XTEINK" 1111111111111111111111111111111111111111)"
merge_b="$(conc_eval "$YML" group "$XTEINK" 2222222222222222222222222222222222222222)"
checks=$((checks + 1))
if [ "$merge_a" = "$merge_b" ]; then
  failed=$((failed + 1))
  echo "FAIL ci  two different commits on xteink evaluate to ONE concurrency group ('$merge_a'), so the second pends behind the first and the third cancels it -- 41 runs died that way in the 36 hours before this test existed, none of which ever started a job"
fi
# One branch, two pushes. Superseding is still right here and must survive.
pr_a="$(conc_eval "$YML" group "$PR" 1111111111111111111111111111111111111111)"
pr_b="$(conc_eval "$YML" group "$PR" 2222222222222222222222222222222222222222)"

# The guard has to cover EVERY value this compares, not just the first one.
# Written over $merge_a alone it left the pull-request assertion below
# satisfied by empty == empty: an expression the reader cannot evaluate (a
# `format()` call, a context it does not know) produces nothing on both sides,
# they match, and "a push still supersedes" passes having measured nothing.
readable() {  # <label> <value>
  checks=$((checks + 1))
  case "$2" in
    NO-GROUP|NO-CANCEL-IN-PROGRESS|UNEVALUABLE*|'')
      failed=$((failed + 1))
      echo "FAIL ci  crossplay-ci.yml's concurrency $1 could not be evaluated ('$2'), so every check that compares it asserted nothing"
      ;;
  esac
}
readable "group on xteink"        "$merge_a"
readable "group on a pull request" "$pr_a"
checks=$((checks + 1))
if [ "$pr_a" != "$pr_b" ]; then
  failed=$((failed + 1))
  echo "FAIL ci  two pushes to one pull request evaluate to different concurrency groups ('$pr_a' vs '$pr_b'), so a push no longer supersedes the run it replaced and every commit of a branch holds a runner"
fi

# The same two refs, through the cancellation setting, so the pair is asserted
# as a pair: unique group + no cancellation on xteink, shared group +
# cancellation on a branch. Either half alone is satisfied by a broken
# arrangement.
cancel_xteink="$(conc_eval "$YML" cancel-in-progress "$XTEINK" 1111111111111111111111111111111111111111)"
cancel_pr="$(conc_eval "$YML" cancel-in-progress "$PR" 1111111111111111111111111111111111111111)"
checks=$((checks + 1))
if [ "$cancel_xteink" != "false" ]; then
  failed=$((failed + 1))
  echo "FAIL ci  crossplay-ci.yml evaluates cancel-in-progress to '$cancel_xteink' on xteink; a merge would kill the previous merge's build outright"
fi
checks=$((checks + 1))
if [ "$cancel_pr" != "true" ]; then
  failed=$((failed + 1))
  echo "FAIL ci  crossplay-ci.yml evaluates cancel-in-progress to '$cancel_pr' on a pull request ref; superseding is off and five runs of one branch share the runners"
fi

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
# The gate asks GitHub one question -- does the TIP have a successful run of
# the CI workflow -- so every case here answers it with a fake `gh` on PATH,
# which also records the call. The DEFAULT answer is 0, meaning nothing green
# has seen the tip, so the eight cases below keep measuring the tip check alone.
# FAKE_GREEN=error makes the query fail, which is the fail-closed path.
mkdir -p "$WORK/gatebin"
cat >"$WORK/gatebin/gh" <<'FAKEGH'
#!/bin/sh
echo "gh $*" >>"$FAKE_GH_CALLS"
if [ "$FAKE_GREEN" = "error" ]; then
  echo "HTTP 403: Resource not accessible by integration"
  exit 1
fi
echo "${FAKE_GREEN:-0}"
FAKEGH
chmod +x "$WORK/gatebin/gh"

gate_expect() {  # <label> <repo> <VERIFIED> <true|false wanted go> <substring wanted in the log> [green]
  local label="$1" repo="$2" verified="$3" want="$4" msg="$5" green="${6:-0}" got
  : >"$WORK/gh_output"; : >"$WORK/gate.gh.calls"
  ( cd "$repo" && GITHUB_OUTPUT="$WORK/gh_output" VERIFIED="$verified" HOLD="" \
      PATH="$WORK/gatebin:$PATH" FAKE_GREEN="$green" FAKE_GH_CALLS="$WORK/gate.gh.calls" \
      GITHUB_REPOSITORY="ma-r-s/crossplay" CI_WORKFLOW_ID="${GATE_WORKFLOW_ID-4242}" CI_BRANCH=xteink \
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

# -- and the case the eight above cannot reach: the run for the new tip -------
#
# Cases 3, 4, 6 and 7 all refuse for the same stated reason: "the run for the
# new tip releases it". That was a claim about GitHub's concurrency, and it was
# false. A concurrency group holds one run in progress and one run pending, and
# a third arrival cancels the pending one -- `cancel-in-progress: false` governs
# the first half only. crossplay-ci.yml lost 41 runs to exactly that in 36
# hours, and this workflow's own `crossplay-release` group can lose an
# autorelease the same way. That group is KEPT, deliberately: two publishers at
# once built v1.12.16 twice. What changes is that the survivor no longer needs
# to be the one for the tip.
#
# So the gate stopped asking "am I the run for the tip" and started asking "has
# anything green verified the tip". Four cases, and the last two matter most:
# the failure mode of a check like this is silence, and silence must refuse.
#
# (9) The tip moved by firmware, and the tip has its own successful CI run.
#     This is the release that used to be lost, every time the autorelease that
#     would have cut it was the one GitHub trimmed out of the queue.
gate_repo "$G"; V="$(git -C "$G" rev-parse HEAD)"
gate_commit "$G" src/apps_local/Sudoku.cpp "fix(sudoku): the givens survive a rotate"
gate_expect "a moved tip that has its OWN green run releases" "$G" "$V" true \
  "successful run(s) of its own" 1

# And it must not ALSO claim the gap was inert. The reason lives one line below
# the branch it belongs to, and written as a trailing echo it ran on both paths:
# the log said the tip had a green run of its own and then, in the next
# sentence, that nothing in the gap could reach a device. Both cannot be true,
# the second is the one that reads like the explanation, and `gate_expect` alone
# never sees it because it only asks whether the sentence it wants is present.
checks=$((checks + 1))
if grep -q "only by commits no device build" "$WORK/out"; then
  failed=$((failed + 1))
  echo "FAIL ci-autorelease  the gate released a tip on its own green run and then told the log the gap was inert; the two sentences contradict each other and the wrong one is the one that looks like the reason"
  sed 's/^/       /' "$WORK/out"
fi

# The query has to be about the tip. A copy of this line asking about $VERIFIED
# would answer yes on every run that triggered it, which is the same as deleting
# the gate -- and every case above would still pass.
TIP="$(git -C "$G" rev-parse HEAD)"
checks=$((checks + 1))
if ! grep -q "head_sha=$TIP" "$WORK/gate.gh.calls"; then
  failed=$((failed + 1))
  echo "FAIL ci-autorelease  the gate asked GitHub about something other than the tip ($TIP); it must ask whether the TIP is verified, not whether the commit that triggered it is"
  sed 's/^/       /' "$WORK/gate.gh.calls"
fi

# And about the right RUNS. The fake gh answers whatever it is told to answer
# and ignores the URL, so every case above passes on a query missing any filter
# -- which is not a hypothetical: against the real repository, sha f5115b01
# (a run cancelled while pending) answers total_count 0 with `status=success`
# and total_count 1 without it. Dropping that one parameter releases from a
# commit whose CI never ran, which is the failure this whole change exists to
# stop. `branch` is asserted for the same reason one step weaker: a run on
# another ref is not a verdict on this one.
for want in "status=success" "branch=xteink"; do
  checks=$((checks + 1))
  if ! grep -q "$want" "$WORK/gate.gh.calls"; then
    failed=$((failed + 1))
    echo "FAIL ci-autorelease  the gate's query does not carry '$want', so it counts runs that are not a green verdict on this branch"
    sed 's/^/       /' "$WORK/gate.gh.calls"
  fi
done

# (10) The same tip, with no green run of its own. The protection, unchanged.
gate_repo "$G"; V="$(git -C "$G" rev-parse HEAD)"
gate_commit "$G" src/apps_local/Sudoku.cpp "fix(sudoku): the givens survive a rotate"
gate_expect "a moved tip nothing has verified still refuses" "$G" "$V" false \
  "nothing green has verified" 0

# (11) The query itself fails -- no actions:read, a rate limit, GitHub down.
#      An answer that is not a number is not a yes.
gate_repo "$G"; V="$(git -C "$G" rev-parse HEAD)"
gate_commit "$G" src/apps_local/Sudoku.cpp "fix(sudoku): the givens survive a rotate"
gate_expect "a query that fails refuses rather than assuming" "$G" "$V" false \
  "could not be asked" error

# (12) No workflow id in the event at all. The gate must not ask GitHub about
#      `workflows//runs`, and must not read whatever that returns as a yes.
gate_repo "$G"; V="$(git -C "$G" rev-parse HEAD)"
gate_commit "$G" src/apps_local/Sudoku.cpp "fix(sudoku): the givens survive a rotate"
GATE_WORKFLOW_ID="" gate_expect "an empty workflow id refuses" "$G" "$V" false \
  "could not be asked" 1
checks=$((checks + 1))
if [ -s "$WORK/gate.gh.calls" ]; then
  failed=$((failed + 1))
  echo "FAIL ci-autorelease  the gate called gh with no workflow id in the event; the URL names no workflow, GitHub answers 404, and the only thing standing between that and a released tip is that a 404 is not a number"
  sed 's/^/       /' "$WORK/gate.gh.calls"
fi

# (13) The case above that this DOES change, said out loud. Case 7 refuses an
#      unclassified path in the gap, and reads as an unconditional refusal.
#      It is not one any more, and the reason is that the two questions are
#      different: the table answers "can CI's verdict on an OLDER commit be
#      trusted for this tip", and a tip with a green run of its own has a
#      verdict of its own, so nothing is being inherited. The unclassified path
#      is still caught -- by release-needed.sh, which sees the whole range since
#      the newest tag and fails the job loudly on exit 2 -- and is stubbed out
#      here, which is why this case measures the tip check and not that one.
gate_repo "$G"; V="$(git -C "$G" rev-parse HEAD)"
gate_commit "$G" brandnew/thing.c "feat: a directory nobody has classified"
gate_expect "an unclassified gap under a tip with its own green run releases" "$G" "$V" true \
  "successful run(s) of its own" 1

# (14) THE SHAPE THIS BRANCH IS ACTUALLY FOR, and the one the first draft could
#      not answer. crossplay-ci.yml carries `paths-ignore: site/emulator/**`
#      and `site/emulator-manifest.json`, so the rebuild crossplay-emulator.yml
#      pushes after every merge has no CI run and never will -- and after most
#      merges that rebuild IS the tip. A gate that asks GitHub about the tip
#      gets 0 for it forever, so the rescue would have fired for every shape
#      except the commonest one on this branch.
#
#      Here: a firmware merge the run did not cover, then an emulator rebuild on
#      top. The gap is not inert (it contains the firmware), the tip has no run
#      and cannot have one, and the merge underneath it does. The gate must walk
#      the invisible commit off the tip and ask about the merge.
gate_repo "$G"; V="$(git -C "$G" rev-parse HEAD)"
gate_commit "$G" src/apps_local/Sudoku.cpp "fix(sudoku): the givens survive a rotate"
MERGE="$(git -C "$G" rev-parse HEAD)"
gate_commit "$G" site/emulator-manifest.json "chore: emulator rebuilt for $MERGE"
gate_expect "a tip that is an emulator rebuild asks about the merge under it" "$G" "$V" true \
  "successful run(s) of its own" 1
checks=$((checks + 1))
if ! grep -q "head_sha=$MERGE" "$WORK/gate.gh.calls"; then
  failed=$((failed + 1))
  echo "FAIL ci-autorelease  the tip was an emulator rebuild, which CI is configured never to run for, and the gate asked GitHub about it anyway; it must ask about $MERGE, the commit under the invisible ones"
  sed 's/^/       /' "$WORK/gate.gh.calls"
fi

# The walk must stop at something visible. A firmware commit ABOVE the merge is
# not invisible, so the tip is the thing to ask about and the answer is no.
gate_repo "$G"; V="$(git -C "$G" rev-parse HEAD)"
gate_commit "$G" src/apps_local/Sudoku.cpp "fix(sudoku): the givens survive a rotate"
gate_commit "$G" src/apps_local/Chess.cpp "fix(chess): the clock survives a sleep"
TIP2="$(git -C "$G" rev-parse HEAD)"
gate_expect "the walk does not step over a visible commit" "$G" "$V" false \
  "nothing green has verified" 0
checks=$((checks + 1))
if ! grep -q "head_sha=$TIP2" "$WORK/gate.gh.calls"; then
  failed=$((failed + 1))
  echo "FAIL ci-autorelease  the walk stepped past a commit a device build can see; with firmware on top of firmware the only commit worth asking about is the tip ($TIP2)"
  sed 's/^/       /' "$WORK/gate.gh.calls"
fi

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

# -- and the gate the staleness answer feeds --------------------------------
#
# Everything above proves the step ANSWERS correctly. It says nothing about
# whether the answer is read correctly, and the rebuild is skipped by an `if:`
# on every later step. Change one of those to `== 'True'` and the job goes back
# to doing nothing on every push, silently, with all four assertions above
# still green -- the same failure one step downstream.
#
# So: derive the values the step can WRITE, derive the values the later steps
# COMPARE against, and insist the second set is contained in the first. Neither
# side is named here, because naming either is how this check would come to
# agree with a typo.
python3 - "$EYML" <<'GATE' >"$WORK/gate.out"
import re, sys
src = open(sys.argv[1]).read()
i = src.index("- name: Is the emulator behind its sources?")
j = src.index("- uses:", i)
written = set(re.findall(r'stale=([A-Za-z]+)', src[i:j]))
compared = set(re.findall(r"steps\.stale\.outputs\.stale\s*==\s*'([^']*)'", src))
print("WRITTEN " + " ".join(sorted(written)))
print("COMPARED " + " ".join(sorted(compared)))
print("NGATED %d" % len(re.findall(r"steps\.stale\.outputs\.stale", src[j:])))
GATE
w=$(sed -n 's/^WRITTEN //p' "$WORK/gate.out")
c=$(sed -n 's/^COMPARED //p' "$WORK/gate.out")
n=$(sed -n 's/^NGATED //p' "$WORK/gate.out")

checks=$((checks + 1))
if [ -z "$w" ]; then
  failed=$((failed + 1))
  echo "FAIL ci-emulator  the staleness step writes no stale=<value> at all; this check has nothing to compare against"
fi
checks=$((checks + 1))
if [ "${n:-0}" -lt 1 ]; then
  failed=$((failed + 1))
  echo "FAIL ci-emulator  no later step in crossplay-emulator.yml is gated on steps.stale.outputs.stale; either the gating was removed (every push now rebuilds) or it was renamed and the rebuild never runs"
fi
for v in $c; do
  checks=$((checks + 1))
  case " $w " in
    *" $v "*) : ;;
    *)
      failed=$((failed + 1))
      echo "FAIL ci-emulator  crossplay-emulator.yml gates a step on stale == '$v', but the staleness step only ever writes: $w. That comparison is never true, so the step it guards never runs and the job still reports success"
      ;;
  esac
done

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
  # Read the JOB's own key, at the job's own indent. Walking the whole block
  # and matching any timeout-minutes at any depth was wrong in the one
  # direction that matters: every step is indented deeper, so a step-level
  # `timeout-minutes: 5` satisfied a check about the job and the job still
  # inherited the six-hour default. Anchored to the exact indent, so a key
  # under `steps:` cannot answer for the job above it.
  python3 - "$1" "$2" <<'TMO'
import re, sys
lines = open(sys.argv[1]).read().splitlines()
try:
    i = next(i for i, l in enumerate(lines) if l.rstrip() == '  ' + sys.argv[2] + ':')
except StopIteration:
    print('NOJOB'); raise SystemExit(0)
for l in lines[i + 1:]:
    if not l.strip():
        continue
    indent = len(l) - len(l.lstrip())
    if indent <= 2:          # the next job, or a top-level key: block over
        break
    if indent != 4:          # deeper than the job's own keys -- a step, not the job
        continue
    m = re.match(r'timeout-minutes:\s*(\d+)\s*$', l.strip())
    if m:
        print(m.group(1)); raise SystemExit(0)
print('NONE')
TMO
}
# crossplay-emulator.yml's rebuild job belongs here for exactly the reason the
# other two do, and more so: it shallow-clones emsdk from GitHub, installs a
# toolchain, runs pio and builds wasm. It was the job that best fit the argument
# and the one job the first version of this left out.
for pair in "$YML:build" \
            "$HERE/../../.github/workflows/crossplay-release.yml:release" \
            "$HERE/../../.github/workflows/crossplay-emulator.yml:rebuild"; do
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
    *)
      # A number, but it also has to be a STUCK cap rather than a budget. Both
      # of these jobs are measured at 19-20 minutes; a cap at or below that
      # kills honest builds, and the comments beside them argue for headroom
      # that nothing was holding them to.
      if [ "$t" -lt 30 ]; then
        failed=$((failed + 1))
        echo "FAIL ci  $(basename "$f")'s '$j' job caps at ${t}m, and that job is measured at 19-20 minutes; a cap this tight fails honest builds instead of catching stuck ones"
      fi
      ;;
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
# BOTH extensions. GitHub runs .yaml as readily as .yml, so a rule written over
# *.yml alone lets in half of what a sync could bring. And the marker asked for
# is the one the message names, colon included: `grep -q "FORK CHANGE"` was
# satisfied by prose merely mentioning the convention.
seen=0
for wf in "$HERE/../.."/.github/workflows/*.yml "$HERE/../.."/.github/workflows/*.yaml; do
  [ -f "$wf" ] || continue
  seen=$((seen + 1))
  case "$(basename "$wf")" in crossplay-*) continue ;; esac
  checks=$((checks + 1))
  if grep -q "FORK CHANGE:" "$wf"; then
    :
  else
    failed=$((failed + 1))
    echo "FAIL ci  $(basename "$wf") is inherited from upstream and carries no 'FORK CHANGE:' note saying whether it runs in this fork; a disabled or unreachable workflow reads exactly like a live one"
  fi
done
checks=$((checks + 1))
if [ "$seen" -eq 0 ]; then
  failed=$((failed + 1))
  echo "FAIL ci  found no workflow files at all; the fork-marker rule just checked nothing"
fi

echo "$checks checks, $failed failed"
[ "$failed" -eq 0 ]

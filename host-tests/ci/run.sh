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

# ---------------------------------------------------------------------------
# THE TRIGGER INVARIANT, and it is why much of the rest of this file is now
# conditional.
#
# Until 2026-09-21 this workflow ran on every pull request and every push to
# xteink, crossplay-autorelease.yml fired on its completion, tagged, and
# pushed a version-bump commit that started it AGAIN. One change compiled four
# times from cold: ~4,955 runner-minutes over 13 days, 92 per merged pull
# request, forty minutes between a merge and the assets existing. The local
# gate already built the same two release envs, in 113 seconds against 867
# here, so all four were re-runs of a build that had already passed.
#
# The workflow is now a nightly audit and blocks nothing. ADDING A push OR
# pull_request TRIGGER BACK RESTORES THE WHOLE PIPELINE, so that is the thing
# asserted here, and it is strictly stronger than every paths-ignore check
# further down: a trigger that does not exist needs no paths to ignore.
#
# The blocks reasoning about concurrency, superseding and paths-ignore are
# kept rather than deleted and re-arm themselves the moment a blocking trigger
# reappears. Each encodes a failure that cost a night and none is
# re-derivable from the yaml.
# ---------------------------------------------------------------------------
TRIGGERS="$(sed -n '/^on:/,/^[a-z]/p' "$YML")"
BLOCKING_TRIGGER=no
case "$TRIGGERS" in
  *"  push:"*|*"  pull_request:"*) BLOCKING_TRIGGER=yes ;;
esac

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

checks=$((checks + 1))
if [ "$BLOCKING_TRIGGER" = yes ]; then
  failed=$((failed + 1))
  echo "FAIL ci  crossplay-ci.yml has a push or pull_request trigger again. It is a nightly audit: a blocking trigger puts a 20-minute cross-compile back in front of every merge, and with crossplay-autorelease.yml gone nothing downstream waits for its verdict anyway. Landing and publishing are scripts_local/ship.sh."
fi

checks=$((checks + 1))
case "$TRIGGERS" in
  *schedule:*) ;;
  *)
    failed=$((failed + 1))
    echo "FAIL ci  crossplay-ci.yml has no schedule trigger, so the one thing it still exists for -- proving xteink builds on a machine that is not Mario's, from a clean checkout, with nothing of his installed -- never runs"
    ;;
esac
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

# DORMANT WHILE THIS IS A NIGHTLY AUDIT, live again the moment a push or
# pull_request trigger returns. Every check in this block reasons about a
# trigger the workflow no longer has, and each encodes a night lost to it, so
# they are re-armed rather than deleted. Not re-indented: the bodies contain
# heredocs whose terminators must stay at column 0.
if [ "$BLOCKING_TRIGGER" = yes ]; then
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

fi  # BLOCKING_TRIGGER

# -- the autorelease gate: deleted with the workflow it tested ---------------
#
# About 390 lines and ~30 cases lived here, exercising
# crossplay-autorelease.yml's own text with a fake gh: whether a tip that had
# moved still released, whether an emulator rebuild counted as a move,
# whether an unclassified path in the gap refused rather than guessed, and
# whether a query that failed refused rather than assuming. Every one of them
# was earned. The workflow is gone (2026-09-21) and so is the whole
# release-on-a-green-CI-run mechanism they described.
#
# WHERE EACH PART WENT, because "the tests were deleted" is not the same
# claim as "the behaviour is still checked":
#
#   "does anything since the tag reach a user"  -> scripts_local/release-needed.sh,
#       which ship.sh calls and which already answers three ways (release,
#       nothing to release, REFUSED because a changed path is in no row of
#       the table). Its table is asserted by host-tests/gatepath.
#   "has the tip moved since what was verified"  -> ship.sh's fast-forward
#       guard, asserted by host-tests/ship. This is BLUNTER than what was
#       here, deliberately: the old gate reasoned about whether a move was
#       harmless (an emulator rebuild was), and ship.sh refuses any branch
#       that cannot fast-forward and asks for a rebase. That costs a rebase
#       in the case the old gate waved through, and it removes the whole
#       class of "we decided the move was harmless and were wrong". A rebase
#       is cheap now the gate is 113 seconds.
#   "one dispatch per tag, never two"  -> gone with the two paths that could
#       race. ship.sh publishes once, in one process, on this Mac.
#   RELEASE_HOLD  -> ship.sh reads it and treats ANY non-empty value as held
#       rather than the literal "1" (card #572, where the documented format
#       sailed straight through the brake). host-tests/ship asserts that.

# DORMANT WHILE THIS IS A NIGHTLY AUDIT, live again the moment a push or
# pull_request trigger returns. Every check in this block reasons about a
# trigger the workflow no longer has, and each encodes a night lost to it, so
# they are re-armed rather than deleted. Not re-indented: the bodies contain
# heredocs whose terminators must stay at column 0.
if [ "$BLOCKING_TRIGGER" = yes ]; then
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

fi  # BLOCKING_TRIGGER

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
# crossplay-release.yml was the third entry here and is gone with the
# workflow. The cap it asserted has no equivalent to keep: ship.sh runs in a
# terminal on Mario's Mac, where a hung build is a cursor that stopped moving
# rather than a runner quietly held for six hours with nobody looking.
for pair in "$YML:build" \
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

# -- what the pull_request filter may and may not silence (card #547) ---------
#
# Mario, 2026-09-20: a documentation change should not run a pipeline. The
# filter that does that is four lines of glob in another file, and the way it
# fails is SILENT IN THE RIGHT DIRECTION: a pattern one character too wide
# (`'**/*.*'`, `'src/**'` pasted in by mistake) does not break a build, it
# stops one from ever running, and a pull request that changed firmware then
# merges with no check having looked at it. Nothing else in this repository
# would notice. So the property is constructed here from sample paths rather
# than read off the file: the paths that MUST still run CI, and the ones that
# must not.
#
# The matcher below is GitHub's rule as this filter uses it: later pattern
# wins, `!` un-ignores. It is deliberately a few lines, because a test that
# reimplements a glob engine tests the reimplementation.
ci_ignored() {  # path -- prints yes/no against the pull_request block
  local path="$1" verdict=no p core
  # set -f, because the patterns are globs and an unquoted expansion makes the
  # SHELL expand them against this working directory first: `docs/**` became a
  # list of real files and every sample then read as "not covered". It runs in
  # a subshell (the caller uses $( )), so nothing outside sees the flag.
  set -f
  for p in $PR_IGNORE; do
    case "$p" in
      '!'*) core="${p#!}"; case "$path" in $core) verdict=no ;; esac ;;
      *)    case "$path" in $p) verdict=yes ;; esac ;;
    esac
  done
  printf '%s' "$verdict"
}

# DORMANT WHILE THIS IS A NIGHTLY AUDIT, live again the moment a push or
# pull_request trigger returns. Every check in this block reasons about a
# trigger the workflow no longer has, and each encodes a night lost to it, so
# they are re-armed rather than deleted. Not re-indented: the bodies contain
# heredocs whose terminators must stay at column 0.
if [ "$BLOCKING_TRIGGER" = yes ]; then
CI_WF="$HERE/../../.github/workflows/crossplay-ci.yml"
PR_IGNORE="$(sed -n '/^  pull_request:/,/^  [a-z_]*:/p' "$CI_WF" | grep -oE "'[^']+'" | tr -d "'")"

checks=$((checks + 1))
if [ -z "$PR_IGNORE" ]; then
  failed=$((failed + 1))
  echo "FAIL ci  crossplay-ci.yml's pull_request trigger has no paths-ignore, so every documentation change runs four cross-compiles and every host suite again (card #547)"
fi

for path in \
  src/main.cpp \
  src/apps_local/wikipedia/WikipediaCore.cpp \
  lib/hal/HalDisplay.h \
  platformio.ini \
  scripts_local/check.sh \
  host-tests/ci/run.sh \
  site/wikipedia/plan.js \
  .github/workflows/crossplay-ci.yml \
  .github/workflows/crossplay-release.yml \
  docs/release-notes.md \
  docs/release-body.md
do
  checks=$((checks + 1))
  if [ "$(ci_ignored "$path")" = yes ]; then
    failed=$((failed + 1))
    echo "FAIL ci  the pull_request paths-ignore silences $path, so a pull request changing it would merge with no run at all"
  fi
done

for path in \
  docs/apps/wikipedia-plan.md \
  docs/workflow/worker-contract.md \
  README.md
do
  checks=$((checks + 1))
  if [ "$(ci_ignored "$path")" != yes ]; then
    failed=$((failed + 1))
    echo "FAIL ci  the pull_request paths-ignore does not cover $path, which is the documentation case card #547 exists to stop building"
  fi
done

fi  # BLOCKING_TRIGGER

echo "$checks checks, $failed failed"
[ "$failed" -eq 0 ]

#!/bin/bash
# check.sh's browser-artifact gate, tested against every shape it can meet.
#
# The gate is the one branch-conditional check in check.sh, and it is reached
# only on the deploy branch. That made it the rare piece of shell that runs for
# nobody: it sat switched off for its entire life under `--committed`, because a
# detached worktree has no current branch and the gate compared against one. The
# mode you reach for precisely when you are about to trust a result was the mode
# with the check missing, and nothing said so -- a gate that never fires and a
# gate that always passes read identically.
#
# So the gate gets its own test, and the test runs the gate's actual text lifted
# out of check.sh rather than a copy of it, for the same reason host-tests/ci
# lifts the CI step out of the yaml: a copy drifts, and drifts silently.
#
#   host-tests/checksh/run.sh
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
CHECK="$HERE/../../scripts_local/check.sh"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

[ -f "$CHECK" ] || { echo "FAIL cannot find $CHECK"; exit 1; }

# Lift the gate out of check.sh. Anchored on the message it prints and then
# walked back to the top-level `if` above it, rather than matched against the
# condition's text: the condition is the part under test, and an extractor that
# recognises only today's spelling of it turns tomorrow's regression into a
# "could not extract" instead of a failing assertion.
python3 - "$CHECK" >"$WORK/gate.sh" <<'PY'
import sys
lines = open(sys.argv[1]).read().splitlines()
msg = next(i for i, l in enumerate(lines) if 'browser artifact is STALE' in l)
start = next(i for i in range(msg, -1, -1)
             if lines[i].startswith('if ') and lines[i].rstrip().endswith('then'))
depth = 0
end = None
for i in range(start, len(lines)):
    head = lines[i].strip()
    if head.startswith('if ') or head.startswith('if['):
        depth += 1
    if head == 'fi':
        depth -= 1
        if depth == 0:
            end = i
            break
if end is None:
    raise SystemExit('unbalanced gate block')
print('\n'.join(lines[start:end + 1]))
PY

[ -s "$WORK/gate.sh" ] || { echo "FAIL could not extract the gate from check.sh"; exit 1; }

# A repo whose artifact and source commits have times we choose. The gate reads
# commit timestamps, not file mtimes, so this is the whole of what it sees.
build_repo() {  # artifact_epoch, source_epoch
  rm -rf "$WORK/repo"
  mkdir -p "$WORK/repo"
  (
    cd "$WORK/repo"
    git init --quiet -b xteink
    git config user.email t@t; git config user.name t
    mkdir -p site/emulator src
    stamp() {  # epoch, path, message
      echo x >"$2"
      git add -A
      GIT_AUTHOR_DATE="$1 +0000" GIT_COMMITTER_DATE="$1 +0000" \
        git commit --quiet -m "$3"
    }
    # Committed oldest-first so the two orders differ only in the arguments.
    if [ "$1" -le "$2" ]; then
      stamp "$1" site/emulator/crossplay.wasm "artifact"
      stamp "$2" src/thing.cpp "source"
    else
      stamp "$2" src/thing.cpp "source"
      stamp "$1" site/emulator/crossplay.wasm "artifact"
    fi
  )
}

checks=0
failed=0

# $CARRY is what check.sh would have handed across the worktree boundary; empty
# means it handed nothing. It is a variable of this harness rather than an
# ambient CHECK_OUTER_BRANCH because check.sh exports that one to every suite it
# runs, this suite included -- so a run under check.sh inherited "xteink" and
# the three cases that must stay silent could not. Neither the carried branch
# nor the deploy branch is read from the environment here for the same reason.
run_gate() {  # runs the gate in $WORK/repo, echoes "fired" or "silent"
  (
    cd "$WORK/repo"
    FAILED=0
    DEPLOY_BRANCH=xteink
    if [ -n "${CARRY:-}" ]; then
      export CHECK_OUTER_BRANCH="$CARRY"
    else
      unset CHECK_OUTER_BRANCH
    fi
    . "$WORK/gate.sh" >"$WORK/out" 2>&1
    [ "$FAILED" -eq 0 ] && echo silent || echo fired
  )
}

expect() {  # label, fired|silent
  local label="$1" want="$2" got
  got="$(run_gate)"
  checks=$((checks + 1))
  if [ "$got" != "$want" ]; then
    failed=$((failed + 1))
    echo "FAIL check-gate  $label: got $got, wanted $want"
    sed 's/^/       /' "$WORK/out"
  fi
}

OLD=1700000000
NEW=1700009999

# On the deploy branch, in a normal worktree: the ordinary case, both ways.
build_repo "$OLD" "$NEW"
expect "an artifact older than its source is caught" fired

build_repo "$NEW" "$OLD"
expect "an artifact rebuilt after its source passes" silent

# Off the deploy branch the gate stays quiet, because an app worktree's artifact
# is stale by construction and a check that is always red is one people learn to
# scroll past.
build_repo "$OLD" "$NEW"
(cd "$WORK/repo" && git checkout --quiet -b app/something)
expect "a feature branch is not asked to rebuild the wasm" silent

# The regression this test exists for. --committed runs inside a detached
# worktree, where `git branch --show-current` is empty, so the gate can only
# know its branch if check.sh hands it over.
build_repo "$OLD" "$NEW"
(cd "$WORK/repo" && git checkout --quiet --detach HEAD)
expect "a detached HEAD alone cannot see the branch" silent

CARRY=xteink expect "the branch carried in reaches the gate" fired

# And the carried branch is compared, not merely present: a feature branch
# handed across stays quiet.
CARRY=app/something expect "a carried feature branch stays quiet" silent

echo "$checks checks, $failed failed"
[ "$failed" -eq 0 ]

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

# ---------------------------------------------------------------------------
# The firmware build lock, tested for its SPAN rather than its existence.
#
# The lock serialises device builds across worktrees because they share
# ~/.platformio, and a collision surfaces as a missing framework header naming
# no file of ours. It was acquired on x4pro and released on a hardcoded
# "sticky" -- correct until gh_release_x4pro and gh_release_sticky were appended
# to BUILD_ENVS and the release was not moved with them, leaving both release
# builds unlocked for a week. A release gate then collided with any other tree's
# device build by design, which is how it was found: WiFi.h, on 2026-08-29.
#
# So this asserts the lock is HELD DURING every env that touches ~/.platformio.
# Asserting it is merely taken and released would pass the broken code -- the
# bug was never a missing lock, it was a lock whose span stopped early.
# ---------------------------------------------------------------------------

# Lift the real loop, from the env list down to the `done` that closes it.
python3 - "$CHECK" >"$WORK/loop.sh" <<'PY'
import sys
lines = open(sys.argv[1]).read().split("\n")
start = next(i for i, l in enumerate(lines) if l.strip().startswith('BUILD_ENVS="simulator'))
head = next(j for j in range(start, len(lines)) if lines[j].strip().startswith("for env in"))
# Close on the `done` at the FOR's own indentation. The first `done` after it
# belongs to the inner `while ! mkdir` spin that waits for the lock, and
# stopping there lifts half a loop that will not parse.
col = len(lines[head]) - len(lines[head].lstrip())
end = next(j for j in range(head + 1, len(lines))
           if lines[j].strip() == "done" and len(lines[j]) - len(lines[j].lstrip()) == col)
body = lines[start:end + 1]
indent = min(len(l) - len(l.lstrip()) for l in body if l.strip())
print("\n".join(l[indent:] if l.strip() else "" for l in body))
PY

[ -s "$WORK/loop.sh" ] || { echo "FAIL checksh  could not lift the build loop out of check.sh"; failed=$((failed + 1)); }

# A pio that builds nothing and records whether the lock was held while it ran.
mkdir -p "$WORK/bin"
cat >"$WORK/bin/pio" <<'STUB'
#!/bin/bash
env_name="$3"
if [ -d "$FW_LOCK" ]; then echo "$env_name held" >>"$LOCK_TRACE"
else echo "$env_name free" >>"$LOCK_TRACE"; fi
exit 0
STUB
chmod +x "$WORK/bin/pio"

run_loop() {  # $1 = value for CHECK_BUILD_RELEASE_ENVS ("" or "1")
  rm -f "$WORK/trace"; rm -rf "$WORK/lockdir"; mkdir -p "$WORK/lockdir" "$WORK/logs"
  (
    export PATH="$WORK/bin:$PATH"
    export FW_LOCK="$WORK/lockdir/x4pro.lock"
    export LOCK_TRACE="$WORK/trace"
    export LOGS="$WORK/logs"
    export CHECK_BUILD_RELEASE_ENVS="$1"
    FAILED=0
    # shellcheck disable=SC1090
    . "$WORK/loop.sh"
  ) >/dev/null 2>&1
  cat "$WORK/trace" 2>/dev/null
}

lock_spans() {  # every env but the simulator must report "held"
  local trace="$1" bad=""
  while read -r name state; do
    case "$name" in simulator*) continue ;; esac
    [ "$state" = "held" ] || bad="$bad $name"
  done <<<"$trace"
  printf '%s' "$bad"
}

for mode in "" "1"; do
  label="plain"; [ -n "$mode" ] && label="--committed"
  trace="$(run_loop "$mode")"
  checks=$((checks + 1))
  if [ -z "$trace" ]; then
    failed=$((failed + 1))
    echo "FAIL checksh  the lifted build loop ran no envs in $label mode"
    continue
  fi
  unlocked="$(lock_spans "$trace")"
  if [ -n "$unlocked" ]; then
    failed=$((failed + 1))
    echo "FAIL checksh  in $label mode these device builds ran with no lock held:$unlocked"
    echo "$trace" | sed 's/^/       /'
  fi
done

# And the test must be able to see the bug it was written for: put the hardcoded
# release back and the span assertion has to go red. A check that cannot fail
# against the code it replaced is not evidence of anything.
checks=$((checks + 1))
sed 's/if \[ "\$env" = "\$LAST_FW_ENV" \]; then/if [ "$env" = "sticky" ]; then/' \
  "$WORK/loop.sh" >"$WORK/loop-broken.sh"
if cmp -s "$WORK/loop.sh" "$WORK/loop-broken.sh"; then
  failed=$((failed + 1))
  echo "FAIL checksh  could not reintroduce the early release; the span test proves nothing"
else
  cp "$WORK/loop.sh" "$WORK/loop-good.sh"; cp "$WORK/loop-broken.sh" "$WORK/loop.sh"
  broken_unlocked="$(lock_spans "$(run_loop 1)")"
  cp "$WORK/loop-good.sh" "$WORK/loop.sh"
  if [ -z "$broken_unlocked" ]; then
    failed=$((failed + 1))
    echo "FAIL checksh  releasing the lock on 'sticky' left the release builds unlocked and this test did not notice"
  fi
fi

# --- a lock is stale when its HOLDER is gone, never when it is merely old -----
#
# The 900s timeout this replaced could not tell a working release gate from a
# corpse: a cold --committed run's four device builds outlast it, so a queued
# tree reclaimed the lock from a live holder BY DESIGN and both then raced
# ~/.platformio. On 2026-08-31 the thief died on
# `ComponentManager/.../index.lock: File exists`, naming no file of ours.
#
# The three cases below are the whole decision. pgrep is STUBBED rather than
# consulted, because the real one answers about this machine: a sibling
# session's build would otherwise decide whether this suite passes.
cat >"$WORK/bin/pgrep" <<'STUB'
#!/bin/bash
[ -n "${FAKE_DEVICE_BUILD:-}" ] || exit 1
echo "4242 /somewhere/bin/pio run -e x4pro"
STUB
chmod +x "$WORK/bin/pgrep"

# Runs the lifted loop against a lock that ALREADY exists, and reports whether
# it got past it. The simulator env builds before the lock is taken, so its
# trace line proves nothing: only a device env in the trace means "proceeded".
seeded_loop() {  # $1 = owner file content ("" = none), $2 = FAKE_DEVICE_BUILD
  rm -f "$WORK/trace"; rm -rf "$WORK/lockdir"; mkdir -p "$WORK/lockdir" "$WORK/logs"
  mkdir -p "$WORK/lockdir/x4pro.lock"
  [ -n "$1" ] && printf '%s\n' "$1" >"$WORK/lockdir/x4pro.lock/owner"
  (
    export PATH="$WORK/bin:$PATH"
    export FW_LOCK="$WORK/lockdir/x4pro.lock"
    export LOCK_TRACE="$WORK/trace"
    export LOGS="$WORK/logs"
    export CHECK_BUILD_RELEASE_ENVS=""
    export FAKE_DEVICE_BUILD="$2"
    FAILED=0
    # shellcheck disable=SC1090
    . "$WORK/loop.sh"
  ) >/dev/null 2>&1 &
  local pid=$! waited=0
  while [ "$waited" -lt 10 ]; do
    grep -qvE '^simulator' "$WORK/trace" 2>/dev/null && break
    sleep 1; waited=$((waited + 1))
  done
  kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
  if grep -qvE '^simulator' "$WORK/trace" 2>/dev/null; then echo proceeded; else echo waited; fi
}

( : ) & dead_pid=$!; wait "$dead_pid" 2>/dev/null   # a pid that is certainly gone
sleep 45 & live_pid=$!

checks=$((checks + 1))
if [ "$(seeded_loop "$dead_pid tree" "")" != "proceeded" ]; then
  failed=$((failed + 1))
  echo "FAIL checksh  a lock whose holder is dead, with no build running, was not reclaimed"
fi

checks=$((checks + 1))
if [ "$(seeded_loop "$live_pid tree" "")" != "waited" ]; then
  failed=$((failed + 1))
  echo "FAIL checksh  the lock was taken from a LIVE holder -- this is the concurrent-build corruption"
fi

# Killing a shell orphans its pio child while the EXIT trap that frees the lock
# never runs, so "holder dead" alone must not authorise a break.
checks=$((checks + 1))
if [ "$(seeded_loop "$dead_pid tree" "1")" != "waited" ]; then
  failed=$((failed + 1))
  echo "FAIL checksh  the lock was reclaimed from a dead holder while a device build was still running"
fi

kill "$live_pid" 2>/dev/null; wait "$live_pid" 2>/dev/null

echo "$checks checks, $failed failed"
[ "$failed" -eq 0 ]

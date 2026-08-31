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
# "held" is not enough. A lock can exist and be UNOWNED -- acquired by a run
# whose owner line died -- and an unowned lock is one no waiter can judge and
# no run can release. Record the stronger fact.
if [ -s "$FW_LOCK/owner" ]; then echo "$env_name owned" >>"$LOCK_TRACE"
elif [ -d "$FW_LOCK" ]; then echo "$env_name held" >>"$LOCK_TRACE"
else echo "$env_name free" >>"$LOCK_TRACE"; fi
# Simulates the lock being reclaimed out from under this run: whoever holds it
# now, it is not us, and neither the release path nor the trap may delete it.
[ -n "${STEAL_LOCK:-}" ] && [ -d "$FW_LOCK" ] && printf '999999 thief\n' >"$FW_LOCK/owner"
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
    [ "$state" = "owned" ] || bad="$bad $name"
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

# A lock with NO owner file is held, never abandoned. It belongs to a tree whose
# check.sh predates the owner file and took it with a plain mkdir; such a holder
# cannot say whether it is alive, so it must be assumed to be. Deciding by
# builds_alive instead is not safe, and this is the exact gap: a --committed run
# builds four device envs in sequence and between them NO pio exists, so a
# waiter landing in that window would take a live holder's lock. Observed on
# 2026-08-31 -- an empty x4pro.lock held by a live pre-owner-file check.sh while
# another tree waited on it. FAKE_DEVICE_BUILD is "" here precisely because that
# is the dangerous case: nothing is building at this instant, and the lock must
# STILL be respected.
checks=$((checks + 1))
if [ "$(seeded_loop "" "")" != "waited" ]; then
  failed=$((failed + 1))
  echo "FAIL checksh  an ownerless lock was reclaimed -- an older check.sh holding it would have been robbed"
fi

# And with a build running, for the same reason and by a different route.
checks=$((checks + 1))
if [ "$(seeded_loop "" "1")" != "waited" ]; then
  failed=$((failed + 1))
  echo "FAIL checksh  an ownerless lock was reclaimed while a device build was running"
fi

kill "$live_pid" 2>/dev/null; wait "$live_pid" 2>/dev/null

# --- the probe must actually RUN, not merely be present --------------------
#
# Both of tonight's lock incidents were the same shape and neither was a wrong
# answer: `pgrep -c -f X 2>/dev/null || echo 0` exits 2 on macOS (no -c flag),
# so the fallback printed "no builds" forever and a constant was read as a
# measurement. A test that asserts the check SAID something cannot catch that;
# this one gives the real probe a real process and demands it be found.
mkdir -p "$WORK/realbin/bin"
cat >"$WORK/realbin/bin/pio" <<'STUB'
#!/bin/bash
sleep 30
STUB
chmod +x "$WORK/realbin/bin/pio"

probe() {  # the production form, lifted from check.sh so it cannot drift
  grep -o 'pgrep -fl "\[b\]in/pio run"[^;]*grep -q \.' "$CHECK" | head -1
}

checks=$((checks + 1))
probe_expr="$(probe)"
if [ -z "$probe_expr" ]; then
  failed=$((failed + 1))
  echo "FAIL checksh  cannot find the liveness probe in check.sh; it may have been rewritten into an untested form"
else
  "$WORK/realbin/bin/pio" run -e x4pro & probe_target=$!
  sleep 1
  checks=$((checks + 1))
  if eval "$probe_expr" >/dev/null 2>&1; then
    :
  else
    failed=$((failed + 1))
    echo "FAIL checksh  the liveness probe did not see a running device build -- it is reporting a constant, not measuring"
  fi
  kill "$probe_target" 2>/dev/null; wait "$probe_target" 2>/dev/null
fi

# The counting form that started all this must never come back: on macOS it
# exits 2, and any `|| echo 0` around it answers "nothing is running" forever.
checks=$((checks + 1))
if grep -vE '^\s*#' "$CHECK" | grep -q 'pgrep -c'; then
  failed=$((failed + 1))
  echo "FAIL checksh  check.sh uses 'pgrep -c', which macOS pgrep does not support: it exits 2 and reports a constant"
fi

# A lock is removed by its owner, or by whoever proved that owner dead -- never
# unconditionally. Judged by BEHAVIOUR, because a guarded delete and an
# unguarded one are the same line of shell: the stub pio rewrites the owner
# file mid-build, so the run finishes holding a lock that is no longer its own
# and must leave it alone. Without the guard, a waiter descheduled between
# deciding "abandoned" and deleting removes a lock another waiter has taken.
checks=$((checks + 1))
steal_trace="$(STEAL_LOCK=1 run_loop "")"
if [ -z "$steal_trace" ]; then
  failed=$((failed + 1))
  echo "FAIL checksh  the lock-theft scenario ran no envs, so it proves nothing"
elif [ ! -d "$WORK/lockdir/x4pro.lock" ]; then
  failed=$((failed + 1))
  echo "FAIL checksh  the run deleted a lock it no longer owned; a second tree's build is now unprotected"
fi

# ...and the ordinary case must still release, or every other tree waits for
# nothing. This is the rmdir-on-a-non-empty-directory bug, asserted directly.
checks=$((checks + 1))
if run_loop "" >/dev/null && [ -d "$WORK/lockdir/x4pro.lock" ]; then
  failed=$((failed + 1))
  echo "FAIL checksh  the lock outlived its own run; the early release silently did nothing"
fi

# The lock must carry a usable owner LABEL, not just a pid. This is the CI gap
# that shipped: the line wrote "${REPO##*/}", REPO does not exist when the loop
# is lifted here, and bash 4.4+ aborts on it under `set -u` while macOS bash
# 3.2 substitutes empty and says nothing. Asserting the label is non-empty
# fails on both, which is the only kind of assertion worth having for a
# difference that only one platform reports.
checks=$((checks + 1))
rm -f "$WORK/trace"; rm -rf "$WORK/lockdir"; mkdir -p "$WORK/lockdir" "$WORK/logs"
cat >"$WORK/bin/pio" <<'STUB'
#!/bin/bash
[ -s "$FW_LOCK/owner" ] && cat "$FW_LOCK/owner" >>"$LOCK_TRACE.owner"
exit 0
STUB
chmod +x "$WORK/bin/pio"
(
  export PATH="$WORK/bin:$PATH" FW_LOCK="$WORK/lockdir/x4pro.lock" LOCK_TRACE="$WORK/trace"
  export LOGS="$WORK/logs" CHECK_BUILD_RELEASE_ENVS=""
  unset REPO
  FAILED=0
  # shellcheck disable=SC1090
  . "$WORK/loop.sh"
) >/dev/null 2>&1
owner_line="$(head -1 "$WORK/trace.owner" 2>/dev/null || true)"
label="${owner_line#* }"
if [ -z "$owner_line" ]; then
  failed=$((failed + 1))
  echo "FAIL checksh  the lock was never owned during a build; the owner line did not run"
elif [ -z "$label" ] || [ "$label" = "$owner_line" ]; then
  failed=$((failed + 1))
  echo "FAIL checksh  the lock owner carries no tree label ('$owner_line'); a waiter cannot say who holds it"
fi

# ---------------------------------------------------------------------------
# The submodule-state wiring (2026-08-31).
#
# The probe itself is covered by host-tests/submodules. What is covered HERE is
# the part that decided nothing tonight: check.sh's reaction to it. A gate ran
# fully green on a tree whose freeink-sdk was checked out at an unlanded
# upstream merge, and the existing "uncommitted file(s)" note was read straight
# past. So uninitialised must STOP the run, and drift must reach the verdict
# line -- a warning printed 400 lines above "all green" is a warning nobody
# reads.
lift() {  # substring that appears on the opening `if` line
  python3 - "$CHECK" "$1" <<'PY'
import re
import sys
lines = open(sys.argv[1]).read().splitlines()
needle = sys.argv[2]
start = next(i for i, l in enumerate(lines)
             if l.strip().startswith('if ') and needle in l)
# Carry down the assignments the block reads but does not make: lifting
# `if [ -x "$SUB_STATE" ]` without the SUB_STATE= line above it produces an
# unbound-variable failure that looks like the block refusing, not like a
# broken extraction.
top = start
while top > 0 and re.match(r"^\s*[A-Za-z_][A-Za-z0-9_]*=", lines[top - 1]):
    top -= 1
depth, end = 0, None
for i in range(start, len(lines)):
    head = lines[i].strip()
    if head.startswith('if '):
        depth += 1
    if head == 'fi':
        depth -= 1
        if depth == 0:
            end = i
            break
if end is None:
    raise SystemExit('unbalanced block')
print("\n".join(lines[top:end + 1]))
PY
}

lift '-x "$MERGE_STATE"' >"$WORK/mergewire.sh" 2>/dev/null
lift '-x "$SUB_STATE"' >"$WORK/subwire.sh" 2>/dev/null
lift 'SUBMODULE_DRIFT' >"$WORK/verdict.sh" 2>/dev/null
if [ -s "$WORK/subwire.sh" ] && [ -s "$WORK/verdict.sh" ]; then
  # A repo whose probe we control, so each verdict can be driven on demand.
  stub_repo() {  # exit_code, message
    rm -rf "$WORK/subrepo"
    mkdir -p "$WORK/subrepo/scripts_local"
    { echo '#!/bin/bash'
      [ -n "$2" ] && echo "echo '$2'"
      echo "exit $1"
    } > "$WORK/subrepo/scripts_local/submodule_state.sh"
    chmod +x "$WORK/subrepo/scripts_local/submodule_state.sh"
  }

  # ---- merge-state wiring: a conflicted tree must not be gated at all ----
  #
  # This one STOPS the run. A tree with markers in it cannot report anything
  # meaningful, and the suites will not notice, because they do not read every
  # file: markers in platformio.ini gated all green on 2026-08-31 since --tests
  # never parses it. Both refusal codes must stop it.
  if [ -s "$WORK/mergewire.sh" ]; then
    merge_stub() {  # exit_code
      rm -rf "$WORK/mrepo"
      mkdir -p "$WORK/mrepo/scripts_local"
      { echo '#!/bin/bash'
        [ "$1" -ne 0 ] && echo "echo 'conflicted: platformio.ini'"
        echo "exit $1"
      } > "$WORK/mrepo/scripts_local/merge_state.sh"
      chmod +x "$WORK/mrepo/scripts_local/merge_state.sh"
    }
    for code in 2 3; do
      checks=$((checks + 1))
      merge_stub "$code"
      if ( set +e; REPO="$WORK/mrepo"; . "$WORK/mergewire.sh"; exit 0 ) >/dev/null 2>&1; then
        failed=$((failed + 1))
        echo "FAIL checksh  merge_state exit $code did not stop the gate; a conflicted tree would be reported on"
      fi
    done
    checks=$((checks + 1))
    merge_stub 0
    clean_merge="$( set +e; REPO="$WORK/mrepo"; . "$WORK/mergewire.sh"; echo "CONTINUED" )"
    case "$clean_merge" in
      "CONTINUED") : ;;
      *) failed=$((failed + 1)); echo "FAIL checksh  a clean tree was stopped or spoke: $clean_merge" ;;
    esac
  else
    checks=$((checks + 1))
    failed=$((failed + 1))
    echo "FAIL checksh  could not lift the merge-state wiring out of check.sh"
  fi

  # Uninitialised (2) must stop the gate. Twenty minutes later, inside a
  # compiler error naming no file of ours, is the alternative.
  checks=$((checks + 1))
  stub_repo 2 "submodule sdk is NOT INITIALISED."
  if ( set +e; REPO="$WORK/subrepo"; SUBMODULE_DRIFT=""; . "$WORK/subwire.sh"; exit 0 ) >/dev/null 2>&1; then
    failed=$((failed + 1))
    echo "FAIL checksh  an uninitialised submodule did not stop the gate"
  fi

  # Drift (3) must NOT stop the gate -- an SDK bump in progress is real work --
  # but must set the flag that qualifies the verdict.
  checks=$((checks + 1))
  stub_repo 3 "submodule sdk is CHECKED OUT AT A DIFFERENT COMMIT"
  drift_out="$( set +e; REPO="$WORK/subrepo"; SUBMODULE_DRIFT=""; . "$WORK/subwire.sh"; echo "FLAG=[$SUBMODULE_DRIFT]" 2>/dev/null )"
  case "$drift_out" in
    *"FLAG=[]"*) failed=$((failed + 1)); echo "FAIL checksh  drift ran but set no flag; the verdict would read as unqualified green" ;;
    *"FLAG=["*)  : ;;
    *)           failed=$((failed + 1)); echo "FAIL checksh  drift stopped the gate; an SDK bump in progress cannot be gated at all" ;;
  esac

  # A healthy tree stays silent and unflagged.
  checks=$((checks + 1))
  stub_repo 0 ""
  clean_out="$( set +e; REPO="$WORK/subrepo"; SUBMODULE_DRIFT=""; . "$WORK/subwire.sh"; echo "FLAG=[$SUBMODULE_DRIFT]" )"
  case "$clean_out" in
    "FLAG=[]") : ;;
    *) failed=$((failed + 1)); echo "FAIL checksh  a healthy tree was not silent+unflagged: $clean_out" ;;
  esac

  # The verdict line itself: the flag has to change what the last line says.
  checks=$((checks + 1))
  qualified="$( set +e; SUBMODULE_DRIFT=1; LOGS=/tmp/x; . "$WORK/verdict.sh" )"
  case "$qualified" in
    *DRIFTED*) : ;;
    *) failed=$((failed + 1)); echo "FAIL checksh  the verdict does not mention drift when drifted: $qualified" ;;
  esac

  checks=$((checks + 1))
  plain="$( set +e; SUBMODULE_DRIFT=""; LOGS=/tmp/x; . "$WORK/verdict.sh" )"
  case "$plain" in
    *DRIFTED*) failed=$((failed + 1)); echo "FAIL checksh  a clean tree's verdict claims drift: $plain" ;;
    *"all green"*) : ;;
    *) failed=$((failed + 1)); echo "FAIL checksh  a clean tree lost its 'all green' verdict: $plain" ;;
  esac
else
  checks=$((checks + 1))
  failed=$((failed + 1))
  echo "FAIL checksh  could not lift the submodule wiring or the verdict out of check.sh"
fi

echo "$checks checks, $failed failed"
[ "$failed" -eq 0 ]

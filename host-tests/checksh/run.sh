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
    # The gate asks scripts_local/emulator-stale.sh, so the fixture carries it.
    mkdir -p scripts_local && cp "$HERE/../../scripts_local/emulator-stale.sh" scripts_local/
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
    REPO="$WORK/repo"
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
head = next(j for j in range(start, len(lines)) if lines[j].strip().startswith("for unit in"))
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
# A function, because two later sections replace this stub with one of their own
# and must be able to put it back. A stub written once and silently overwritten
# is how a section ends up testing the previous section's pio.
span_stub_pio() {
cat >"$WORK/bin/pio" <<'STUB'
#!/bin/bash
# One `pio run` can carry several `-e`, so read them all rather than taking $3.
# Every line is tagged with the INVOCATION number as well as the lock state,
# because the trace has to answer two questions: was the lock owned while this
# env compiled, and were these envs in the same `pio run`.
n=$(( $(cat "$LOCK_TRACE.n" 2>/dev/null || echo 0) + 1 ))
echo "$n" >"$LOCK_TRACE.n"
# "held" is not enough. A lock can exist and be UNOWNED -- acquired by a run
# whose owner line died -- and an unowned lock is one no waiter can judge and
# no run can release. Record the stronger fact.
if [ -s "$FW_LOCK/owner" ]; then state=owned
elif [ -d "$FW_LOCK" ]; then state=held
else state=free; fi
while [ $# -gt 0 ]; do
  [ "$1" = "-e" ] && { echo "$2 $state $n" >>"$LOCK_TRACE"; shift; }
  shift
done
# Simulates the lock being reclaimed out from under this run: whoever holds it
# now, it is not us, and neither the release path nor the trap may delete it.
[ -n "${STEAL_LOCK:-}" ] && [ -d "$FW_LOCK" ] && printf '999999 thief\n' >"$FW_LOCK/owner"
exit 0
STUB
chmod +x "$WORK/bin/pio"
}
span_stub_pio

run_loop() {  # $1 = value for CHECK_BUILD_RELEASE_ENVS ("" or "1")
  rm -f "$WORK/trace" "$WORK/trace.n"; rm -rf "$WORK/lockdir"; mkdir -p "$WORK/lockdir" "$WORK/logs"
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
  local trace="$1" bad="" name state n
  while read -r name state n; do
    # An EMPTY trace is one empty line through a herestring, and without this
    # the empty name falls straight through to the state test and is reported
    # as an unlocked build. That turns "the loop built nothing" into "the
    # assertion caught something", which is the one answer that must never be
    # manufactured -- `mutate` below scores exactly this string.
    [ -n "$name" ] || continue
    case "$name" in simulator*) continue ;; esac
    [ "$state" = "owned" ] || bad="$bad $name"
  done <<<"$trace"
  printf '%s' "$bad"
}

# Every firmware env must have compiled in the SAME `pio run`.
#
# PlatformIO calls clean_build_dir() once per INVOCATION, against the whole
# .pio/build root, and rmtree's it whenever compute_project_checksum() differs
# from the checksum stored there. The checksum is over the file list under src/,
# include/ and lib/, and this project's pre-scripts write generated sources into
# those directories AS the build runs -- so on a fresh checkout the checksum
# moves during invocation #1 and invocation #2 opens by deleting invocation #1's
# output. That is what broke v1.12.14 and v1.12.15.
#
# Returns the distinct invocation numbers the firmware envs were spread over,
# and nothing when they shared one.
fw_invocations() {
  local trace="$1" seen="" name state n
  while read -r name state n; do
    [ -n "$name" ] || continue   # see lock_spans: an empty trace is one blank line
    case "$name" in simulator*) continue ;; esac
    case " $seen " in *" $n "*) ;; *) seen="$seen $n" ;; esac
  done <<<"$trace"
  # shellcheck disable=SC2086
  set -- $seen
  [ "$#" -le 1 ] || printf '%s' "$seen"
}

# What each mode is expected to BUILD. Named here rather than inferred from the
# trace, because a loop that silently built nothing would satisfy every
# assertion below that reads the trace and only this one can see it.
mode_envs() {  # $1 = CHECK_BUILD_RELEASE_ENVS
  if [ -n "$1" ]; then printf 'gh_release_x4pro gh_release_sticky'
  else printf 'x4pro sticky'; fi
}

# Both readers must answer "nothing" for an EMPTY trace rather than
# manufacture a finding out of the single blank line a herestring makes. This
# is not hygiene: `mutate` below scores exactly these strings, so a fabricated
# non-empty answer turns "the mutation made the loop build nothing" into "the
# assertion caught the mutation" -- a green that means the opposite of what it
# says, on the only evidence some of these assertions can fail at all.
checks=$((checks + 1))
if [ -n "$(lock_spans "")" ] || [ -n "$(fw_invocations "")" ]; then
  failed=$((failed + 1))
  echo "FAIL checksh  an empty trace produced a finding (lock_spans [$(lock_spans "")], fw_invocations [$(fw_invocations "")]); mutate would score a build-nothing mutation as caught"
fi

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

  # The envs themselves, asserted rather than inferred. A gate that quietly
  # stopped building anything passes very fast and is worth nothing, and
  # swapping the dev pair for the release pair is exactly the kind of edit that
  # can do it by one typo in a name nothing else reads.
  checks=$((checks + 1))
  built="$(printf '%s\n' "$trace" | awk '{print $1}' | sort | tr '\n' ' ' | sed 's/ *$//')"
  want="$(printf '%s\n' simulator_x4_pro $(mode_envs "$mode") | sort | tr '\n' ' ' | sed 's/ *$//')"
  if [ "$built" != "$want" ]; then
    failed=$((failed + 1))
    echo "FAIL checksh  in $label mode the loop built [$built], expected [$want]"
  fi

  # --committed must SWAP the dev pair for the release pair, not add to it.
  # Four device images where two would do, on every landing.
  if [ -n "$mode" ]; then
    checks=$((checks + 1))
    if printf '%s\n' "$trace" | awk '{print $1}' | grep -qxE 'x4pro|sticky'; then
      failed=$((failed + 1))
      echo "FAIL checksh  --committed built the dev pair as well as the release pair; the swap became an append again"
      echo "$trace" | sed 's/^/       /'
    fi
  fi

  checks=$((checks + 1))
  spread="$(fw_invocations "$trace")"
  if [ -n "$spread" ]; then
    failed=$((failed + 1))
    echo "FAIL checksh  in $label mode the firmware envs were built in separate pio invocations ($spread);"
    echo "              invocation #2 opens by deleting invocation #1's .pio/build output"
    echo "$trace" | sed 's/^/       /'
  fi
done

# And the tests must be able to see the bugs they were written for. Each
# mutation below is applied to the LIFTED loop, run, and the assertion it
# targets has to go red. A check that cannot fail against the code it replaced
# is not evidence of anything.
#
# mutate <label> <sed expression> <a function that must report something>
mutate() {
  local label="$1" expr="$2" probe="$3" out
  checks=$((checks + 1))
  sed "$expr" "$WORK/loop.sh" >"$WORK/loop-broken.sh"
  if cmp -s "$WORK/loop.sh" "$WORK/loop-broken.sh"; then
    failed=$((failed + 1))
    echo "FAIL checksh  mutation '$label' changed nothing; the assertion it targets proves nothing"
    return
  fi
  cp "$WORK/loop.sh" "$WORK/loop-good.sh"; cp "$WORK/loop-broken.sh" "$WORK/loop.sh"
  local trace; trace="$(run_loop 1)"
  cp "$WORK/loop-good.sh" "$WORK/loop.sh"
  # The mutated loop must still BUILD something, or the probe is answering
  # about nothing. A mutation that breaks the loop badly enough to run no envs
  # at all would otherwise score as "the assertion caught it", and this helper
  # is now the only evidence some of those assertions can fail at all. The
  # per-mode assertions guard this with their own empty-trace check; without
  # this line `mutate` had no equivalent.
  if [ -z "$trace" ]; then
    failed=$((failed + 1))
    echo "FAIL checksh  mutation '$label' made the loop build NOTHING, so the probe's answer is vacuous"
    return
  fi
  out="$($probe "$trace")"
  if [ -z "$out" ]; then
    failed=$((failed + 1))
    echo "FAIL checksh  mutation '$label' went unnoticed; the assertion is not testing anything"
  fi
}

# The 2f860bee shape, in the direction that still exists: the lock taken on a
# hardcoded env name that stops matching the moment the env list changes. Under
# --committed the firmware unit is gh_release_x4pro,gh_release_sticky, so the
# lock is never taken at all and both release builds run unprotected.
mutate "the lock acquired on a hardcoded name" \
  's/\[ "\$unit" = "\$FIRST_FW_UNIT" \]/[ "$unit" = "x4pro" ]/' lock_spans

# And the one this change is for: un-group the firmware envs, so each takes its
# own `pio run` again and invocation #2 deletes invocation #1's output.
mutate "one pio invocation per firmware env" \
  "s/tr ' ' ','/tr ' ' ' '/" fw_invocations

# --- a failure in EITHER env of a grouped unit must fail the gate -----------
#
# The grouping's own risk, and the half of it with no other coverage. Two envs
# now share one `pio run`, and a grouped build that swallowed the SECOND env's
# failure would be strictly worse than the per-env loop it replaced: the gate
# would go green on a release image that does not compile, which is the exact
# thing --committed exists to prevent.
#
# platformio/run/cli.py does the right thing -- its env loop has no `break` and
# it ends `command_failed = any(r.get("succeeded") is False)` -- but every stub
# in this file exits 0, so nothing here had ever run the loop against a failing
# build at all. Asserted through the loop's own FAILED flag rather than through
# printed text, because the flag is what the exit code is built from.
fail_on_env() {  # $1 = env whose build fails, $2 = CHECK_BUILD_RELEASE_ENVS
  rm -f "$WORK/trace" "$WORK/trace.n"; rm -rf "$WORK/lockdir"; mkdir -p "$WORK/lockdir" "$WORK/logs"
  # Fails only when asked for THIS env, and still records every env it was
  # asked for, so the assertion below can prove the env was really built.
  cat >"$WORK/bin/pio" <<STUB
#!/bin/bash
rc=0
while [ \$# -gt 0 ]; do
  if [ "\$1" = "-e" ]; then
    echo "\$2" >>"\$LOCK_TRACE"
    [ "\$2" = "$1" ] && { rc=1; echo "src/thing.cpp:1:1: error: deliberate failure in \$2"; }
    shift
  fi
  shift
done
exit \$rc
STUB
  chmod +x "$WORK/bin/pio"
  (
    export PATH="$WORK/bin:$PATH"
    export FW_LOCK="$WORK/lockdir/x4pro.lock"
    export LOCK_TRACE="$WORK/trace"
    export LOGS="$WORK/logs"
    export CHECK_BUILD_RELEASE_ENVS="$2"
    FAILED=0
    # shellcheck disable=SC1090
    . "$WORK/loop.sh"
    exit "$FAILED"
  ) >/dev/null 2>&1
  echo $?
}

# Both halves of the pair, and the SECOND is the one that matters: a loop that
# read only the first env's result would pass the first case and fail this one.
for fail_env in gh_release_x4pro gh_release_sticky; do
  rc="$(fail_on_env "$fail_env" 1)"
  checks=$((checks + 1))
  if ! grep -qx "$fail_env" "$WORK/trace" 2>/dev/null; then
    failed=$((failed + 1))
    echo "FAIL checksh  $fail_env was never built, so the failure assertion below proves nothing"
  fi
  checks=$((checks + 1))
  if [ "$rc" != "1" ]; then
    failed=$((failed + 1))
    echo "FAIL checksh  a build failure in $fail_env left the loop's FAILED flag at $rc; grouped envs swallow one another's failures"
  fi
done

# And the control: no failing env, so the flag must stay clean. Without it the
# two assertions above pass equally well against a loop that reports failure
# unconditionally.
checks=$((checks + 1))
rc="$(fail_on_env "no-such-env" 1)"
if [ "$rc" != "0" ]; then
  failed=$((failed + 1))
  echo "FAIL checksh  a run in which nothing failed still set FAILED=$rc"
fi

span_stub_pio   # put the shared stub back for the sections below

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
  rm -f "$WORK/trace" "$WORK/trace.n"; rm -rf "$WORK/lockdir"; mkdir -p "$WORK/lockdir" "$WORK/logs"
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

# --- the lock is granted in queue order, not to whoever wins the mkdir ------
#
# A polite waiter cannot out-wait an automated one. A session with an armed
# watcher fires the instant the machine goes quiet -- in the gap between one run
# releasing and a human-paced waiter noticing -- and wins every round. That
# starved one tree for fifty minutes on 2026-08-31 while it read the loss as bad
# luck among human-paced sessions.
#
# So these assert ORDER, against a FREE lock. That is the whole point: winning
# the mkdir is no longer sufficient, and a test that seeds a held lock would
# pass for the old reason.
queued_loop() {  # $1 = foreign ticket pid ("" = none)
  rm -f "$WORK/trace" "$WORK/trace.n"; rm -rf "$WORK/lockdir"; mkdir -p "$WORK/lockdir" "$WORK/logs"
  mkdir -p "$WORK/lockdir/x4pro.lock.queue"
  # Created BEFORE ours and given an older mtime, so it is unambiguously ahead
  # in the queue rather than relying on sub-second timing.
  if [ -n "$1" ]; then
    : > "$WORK/lockdir/x4pro.lock.queue/$1.ticket"
    touch -t 202001010000 "$WORK/lockdir/x4pro.lock.queue/$1.ticket"
  fi
  (
    export PATH="$WORK/bin:$PATH"
    export FW_LOCK="$WORK/lockdir/x4pro.lock"
    export LOCK_TRACE="$WORK/trace"
    export LOGS="$WORK/logs"
    export CHECK_BUILD_RELEASE_ENVS=""
    export FAKE_DEVICE_BUILD=""
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

sleep 45 & queue_holder=$!

# A live waiter ahead of us owns the lock's turn even though the lock is FREE.
# Without the queue this proceeds, because mkdir succeeds.
checks=$((checks + 1))
if [ "$(queued_loop "$queue_holder")" != "waited" ]; then
  failed=$((failed + 1))
  echo "FAIL checksh  the lock was taken out of queue order -- an armed watcher would starve everyone again"
fi

kill "$queue_holder" 2>/dev/null; wait "$queue_holder" 2>/dev/null

# A ticket whose owner is gone must not hold the queue. Same liveness rule as
# the lock itself: a waiter that dies cannot block the ones behind it.
( : ) & dead_ticket=$!; wait "$dead_ticket" 2>/dev/null
checks=$((checks + 1))
if [ "$(queued_loop "$dead_ticket")" != "proceeded" ]; then
  failed=$((failed + 1))
  echo "FAIL checksh  a dead waiter's ticket blocked the queue"
fi

# An empty queue must not stop anybody: with no ticket ahead, proceed.
checks=$((checks + 1))
if [ "$(queued_loop "")" != "proceeded" ]; then
  failed=$((failed + 1))
  echo "FAIL checksh  a waiter with an empty queue ahead of it did not proceed"
fi

# The ticket must be surrendered AT ACQUISITION, not at exit. A holder that
# keeps its ticket stays head of the queue while it builds and wins the next
# round too, so a session running back-to-back gates holds the head position
# for ever -- this queue's own starvation mode, introduced by the queue.
checks=$((checks + 1))
if ls "$WORK/lockdir/x4pro.lock.queue"/*.ticket >/dev/null 2>&1; then
  failed=$((failed + 1))
  echo "FAIL checksh  a ticket outlived the acquisition that consumed it -- the holder stays queue head"
fi

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
rm -f "$WORK/trace" "$WORK/trace.n"; rm -rf "$WORK/lockdir"; mkdir -p "$WORK/lockdir" "$WORK/logs"
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
# The build-SCOPING wiring.
#
# check.sh ran four cross-compiled builds for every change, including changes to
# site/index.html that cannot alter one byte of the result -- ten minutes,
# behind a workspace-wide lock, four times in one day.
#
# scripts_local/device-build-needed.sh owns the RULE and host-tests/gatepath
# owns its tests. What is tested HERE is the wiring: that check.sh acts on the
# answer, that it acts on it in only one direction, and that it says so.
#
# The dangerous direction is not "it built when it needn't have". It is "it
# skipped when it must not", which is silent by construction: a skipped build
# and a build that was never asked for look identical from outside. So every
# case below is written from the skip's point of view, and the fail-safe cases
# (a tool that is missing, that crashes, that is not executable) matter more
# than the happy ones -- each is a way the wiring could quietly stop asking.
# ---------------------------------------------------------------------------

SCOPE_TOOL="$HERE/../../scripts_local/device-build-needed.sh"

# A pio that builds nothing and records which env it was asked for.
scope_stub_pio() {
  cat >"$WORK/bin/pio" <<'STUB'
#!/bin/bash
# One invocation can carry several `-e`; name every env it was asked for.
while [ $# -gt 0 ]; do
  [ "$1" = "-e" ] && { echo "$2" >>"$LOCK_TRACE"; shift; }
  shift
done
exit 0
STUB
  chmod +x "$WORK/bin/pio"
}

# A throwaway repo with an origin/xteink to diff against, carrying the REAL
# rule rather than a stub of it: a stub would test that check.sh can read an
# exit code, which is not the thing that can hurt anybody.
#
# Built ONCE and reset per case. Building it per case cost 70s of every gate
# run in the workspace, which is a strange price to pay for a change whose
# entire purpose is making the gate cheaper.
scope_fixture_once() {
  rm -rf "$WORK/scoperepo" "$WORK/scopeorigin.git"
  (
    git init -q --bare -b xteink "$WORK/scopeorigin.git"
    git clone -q "$WORK/scopeorigin.git" "$WORK/scoperepo"
    cd "$WORK/scoperepo" || exit 1
    git config user.email t@t; git config user.name t
    mkdir -p src lib site docs scripts_local
    echo x > src/main.cpp; echo x > lib/thing.h
    echo x > site/index.html; echo x > site/styles.css; echo x > docs/a.md
    echo x > scripts_local/check.sh
    cp "$SCOPE_TOOL" scripts_local/device-build-needed.sh
    chmod +x scripts_local/device-build-needed.sh
    git add -A; git commit -qm base; git push -q -u origin xteink
  ) >/dev/null 2>&1
}

# `reset --hard` also restores the rule itself, which the fail-safe cases below
# deliberately break in the working tree.
scope_fixture() {  # $@ = files to change on top of the base commit
  (
    cd "$WORK/scoperepo" || exit 1
    git checkout -q xteink
    git reset -q --hard origin/xteink
    git clean -fdq
    chmod +x scripts_local/device-build-needed.sh
    for f in "$@"; do mkdir -p "$(dirname "$f")"; echo change >> "$f"; done
    [ $# -gt 0 ] && { git add -A; git commit -qm change; }
  ) >/dev/null 2>&1
}

# Runs the lifted loop against that fixture and returns the envs pio was asked
# to build. Its stdout is kept, because "did it say so" is half of what is
# being tested.
scope_loop() {  # $1 = CHECK_BUILD_RELEASE_ENVS, $2 = CHECK_FORCE_DEVICE_BUILDS
  rm -f "$WORK/trace" "$WORK/trace.n"; rm -rf "$WORK/lockdir"; mkdir -p "$WORK/lockdir" "$WORK/logs"
  (
    export PATH="$WORK/bin:$PATH"
    export FW_LOCK="$WORK/lockdir/x4pro.lock"
    export LOCK_TRACE="$WORK/trace"
    export LOGS="$WORK/logs"
    export CHECK_BUILD_RELEASE_ENVS="$1"
    export CHECK_FORCE_DEVICE_BUILDS="$2"
    export REPO="$WORK/scoperepo"
    cd "$REPO" || exit 1
    FAILED=0
    # shellcheck disable=SC1090
    . "$WORK/loop.sh"
  ) >"$WORK/scopeout" 2>&1
  grep -vE '^simulator' "$WORK/trace" 2>/dev/null | tr '\n' ' ' | sed 's/ *$//'
}

scope_expected() {  # $1 = CHECK_BUILD_RELEASE_ENVS
  mode_envs "$1"
}

# label, skip|build, CHECK_BUILD_RELEASE_ENVS, files...
scope_case() {
  local label="$1" expect="$2" rel="$3"; shift 3
  scope_fixture "$@"
  local devs; devs="$(scope_loop "$rel" "")"
  checks=$((checks + 1))
  if [ "$expect" = "skip" ]; then
    if [ -n "$devs" ]; then
      failed=$((failed + 1))
      echo "FAIL checksh  scope/$label: device builds RAN for a change that cannot reach an image ($devs)"
    elif ! grep -q "DEVICE BUILDS SKIPPED" "$WORK/scopeout"; then
      failed=$((failed + 1))
      echo "FAIL checksh  scope/$label: four builds were skipped SILENTLY -- nothing in the output says so"
    else
      # Naming them is the point. "some builds were skipped" is the shape of
      # message this project has been bitten by twice.
      #
      # The banner's OWN list, compared as an exact set. Two reasons it cannot
      # be `grep -q "$e"` over the whole output any more. First, that is a
      # SUBSTRING test, and x4pro and sticky are substrings of gh_release_x4pro
      # and gh_release_sticky -- so a plain run whose banner named the release
      # pair, or a committed run whose banner named the dev pair, passed. That
      # was harmless while both modes named a superset of each other; since
      # --committed SWAPS the pairs the two modes finally name DISJOINT sets,
      # and this is the assertion that most directly guards the swap. Second,
      # the old form could only see an env MISSING from the banner, never an
      # extra one, so a banner naming all four still passed in both modes.
      local want got
      want="$(printf '%s\n' $(scope_expected "$rel") | sort | tr '\n' ' ')"
      got="$(grep 'did not run:' "$WORK/scopeout" | sed 's/.*did not run: *//' \
             | tr ' ' '\n' | grep -v '^$' | sort | tr '\n' ' ')"
      if [ "$want" != "$got" ]; then
        failed=$((failed + 1))
        echo "FAIL checksh  scope/$label: the skip banner names [$got], expected exactly [$want]"
      fi
    fi
  else
    if [ "$devs" != "$(scope_expected "$rel")" ]; then
      failed=$((failed + 1))
      echo "FAIL checksh  scope/$label: expected [$(scope_expected "$rel")], ran [$devs]"
    fi
  fi
}

if [ ! -x "$SCOPE_TOOL" ] || [ ! -s "$WORK/loop.sh" ]; then
  checks=$((checks + 1)); failed=$((failed + 1))
  echo "FAIL checksh  cannot reach $SCOPE_TOOL or the lifted loop; the scoping wiring is UNTESTED"
else
  scope_stub_pio
  scope_fixture_once

  # 1. The case this exists for. Both modes, because --committed is the mode
  #    that hurts and it is the mode with an extra two builds to skip.
  scope_case "site-only diff"             skip  ""  site/index.html site/styles.css
  scope_case "site-only, --committed"     skip  "1" site/index.html site/styles.css

  # 2. Firmware still builds everything.
  scope_case "a src/ diff"                build ""  src/main.cpp
  scope_case "a src/ diff, --committed"   build "1" src/main.cpp

  # 3. The mixed diff. One firmware path among many inert ones, and the case
  #    most likely to be got wrong, because the inert paths are the majority
  #    and an implementation that asks "are any of these inert" passes 1 and 2
  #    and ships this broken.
  scope_case "mixed: site + docs + src"   build "1" site/index.html docs/a.md src/main.cpp
  scope_case "mixed the other order"      build "1" src/main.cpp site/styles.css

  # 4. A path the rule has never heard of. This is what stops the allowlist
  #    going stale the day somebody adds a source root.
  scope_case "an unclassified new path"   build "1" brandnew/thing.c

  # 5. The gate's own machinery. A change to check.sh that broke the build loop
  #    must not be verified by a run that skipped the build loop.
  scope_case "scripts_local/check.sh"     build "1" scripts_local/check.sh

  # ---- the run must say what it DECIDED, in all three directions ----------
  #
  # Not symmetry. "The gate did not skip", "the gate asked and was told to
  # build" and "the gate never asked at all" are three different states with
  # one appearance, and the third is the dangerous one. Expecting a skip and
  # not getting one had no diagnostic whatsoever until this: the first live
  # test of this feature lost time to a base ref that was not what the tester
  # assumed, and the log said nothing either way.

  scope_says() {  # $1 = label, $2 = substring the output must contain
    checks=$((checks + 1))
    grep -q "$2" "$WORK/scopeout" || { failed=$((failed + 1))
      echo "FAIL checksh  scope/$1: the run never said '$2'"
      sed 's/^/       /' "$WORK/scopeout" | head -6; }
  }

  scope_fixture src/main.cpp
  scope_loop "1" "" >/dev/null
  scope_says "a firmware diff names its reason" "device builds: needed"
  scope_says "and names the path that forced it" "src/main.cpp"

  scope_fixture site/index.html
  rm -f "$WORK/scoperepo/scripts_local/device-build-needed.sh"
  scope_loop "1" "" >/dev/null
  scope_says "no rule present says so out loud" "no usable rule"

  scope_fixture site/index.html
  scope_loop "1" "1" >/dev/null
  scope_says "the override says the diff was not consulted" "forced"

  # ---- fail-safe: only exit code 1 may skip -------------------------------
  #
  # The tool documents 0 as "needed, and ALSO the answer whenever anything is
  # uncertain". Written as `if ! tool; then skip`, every one of these becomes a
  # silent skip of four builds, and nothing downstream would ever notice.
  scope_broken() {  # $1 = label, $2 = body of the replacement tool
    scope_fixture site/index.html
    printf '%s\n' '#!/bin/bash' "$2" > "$WORK/scoperepo/scripts_local/device-build-needed.sh"
    chmod +x "$WORK/scoperepo/scripts_local/device-build-needed.sh"
    local devs; devs="$(scope_loop "1" "")"
    checks=$((checks + 1))
    if [ "$devs" != "$(scope_expected 1)" ]; then
      failed=$((failed + 1))
      echo "FAIL checksh  scope/$1: an inert diff SKIPPED the builds on a tool that did not say 1 (ran [$devs])"
    fi
  }
  scope_broken "a tool that crashes (exit 2)"   'echo boom >&2; exit 2'
  scope_broken "a tool that exits 127"          'exit 127'
  scope_broken "a tool with a syntax error"     'if then fi'
  scope_broken "a tool that says nothing (0)"   'exit 0'

  # A tool that is not there at all, and one that is not executable.
  scope_fixture site/index.html
  rm -f "$WORK/scoperepo/scripts_local/device-build-needed.sh"
  devs="$(scope_loop "1" "")"
  checks=$((checks + 1))
  [ "$devs" = "$(scope_expected 1)" ] || { failed=$((failed + 1))
    echo "FAIL checksh  scope/missing tool: an inert diff skipped the builds with no rule present (ran [$devs])"; }

  scope_fixture site/index.html
  chmod -x "$WORK/scoperepo/scripts_local/device-build-needed.sh"
  devs="$(scope_loop "1" "")"
  checks=$((checks + 1))
  [ "$devs" = "$(scope_expected 1)" ] || { failed=$((failed + 1))
    echo "FAIL checksh  scope/non-executable tool: an inert diff skipped the builds (ran [$devs])"; }

  # The escape hatch, so a skip is never something a human cannot overrule.
  scope_fixture site/index.html
  devs="$(scope_loop "1" "1")"
  checks=$((checks + 1))
  [ "$devs" = "$(scope_expected 1)" ] || { failed=$((failed + 1))
    echo "FAIL checksh  scope/CHECK_FORCE_DEVICE_BUILDS: the override did not force the builds (ran [$devs])"; }

  # ---- and the test must be able to SEE the bug it was written for ---------
  #
  # Turn `-eq 1` into `-ne 0` -- the single plausible way to write this wrong,
  # and the way that reads more naturally in shell -- and the fail-safe cases
  # above have to go red. A check that cannot fail against the code it replaced
  # is not evidence of anything.
  checks=$((checks + 1))
  sed 's/\[ "\$_scope_rc" -eq 1 \]/[ "$_scope_rc" -ne 0 ]/' "$WORK/loop.sh" >"$WORK/loop-unsafe.sh"
  if cmp -s "$WORK/loop.sh" "$WORK/loop-unsafe.sh"; then
    failed=$((failed + 1))
    echo "FAIL checksh  could not reintroduce the fail-open exit test; the fail-safe cases prove nothing"
  else
    cp "$WORK/loop.sh" "$WORK/loop-scopegood.sh"; cp "$WORK/loop-unsafe.sh" "$WORK/loop.sh"
    scope_fixture site/index.html
    printf '%s\n' '#!/bin/bash' 'exit 2' > "$WORK/scoperepo/scripts_local/device-build-needed.sh"
    chmod +x "$WORK/scoperepo/scripts_local/device-build-needed.sh"
    unsafe_devs="$(scope_loop "1" "")"
    cp "$WORK/loop-scopegood.sh" "$WORK/loop.sh"
    if [ -n "$unsafe_devs" ]; then
      failed=$((failed + 1))
      echo "FAIL checksh  a crashing rule still built with -ne 0, so the fail-safe assertions are not testing anything"
    fi
  fi

  # Leave the shared stub as the other sections expect to find it.
  scope_stub_pio
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

# A shell function, start line to its closing brace at column 0.
lift_fn() {  # function name
  python3 - "$CHECK" "$1" <<'PY'
import sys
lines = open(sys.argv[1]).read().splitlines()
name = sys.argv[2]
start = next(i for i, l in enumerate(lines) if l.startswith(name + '() {'))
end = next(i for i in range(start + 1, len(lines)) if lines[i] == '}')
print('\n'.join(lines[start:end + 1]))
PY
}

# check.sh's refusals go through die(), which prints the verdict token and
# exits. Lifted rather than stubbed: a harness that defined its own `die` would
# be supplying the behaviour under test, and the blocks below would pass even if
# the real one had stopped exiting. It is also what lets the token assertion
# below be about check.sh rather than about this file.
lift_fn die >"$WORK/die.sh" 2>/dev/null
lift '-x "$MERGE_STATE"' >"$WORK/mergewire.sh" 2>/dev/null
lift '-x "$SUB_STATE"' >"$WORK/subwire.sh" 2>/dev/null
lift '-x "$FRESH"'     >"$WORK/freshwire.sh" 2>/dev/null
lift 'QUALIFIER'       >"$WORK/verdict.sh" 2>/dev/null
lift_fn qualifier_text >"$WORK/qualifier.sh" 2>/dev/null
if [ -s "$WORK/subwire.sh" ] && [ -s "$WORK/verdict.sh" ] &&
   [ -s "$WORK/freshwire.sh" ] && [ -s "$WORK/qualifier.sh" ]; then
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
  # A REFUSAL IS A VERDICT. The docs card #317 produced say an absent token means
  # "the run never reached its verdict: killed, crashed, or still going" -- and a
  # deliberate refusal is none of those, so it must not look like one.
  checks=$((checks + 1))
  if [ ! -s "$WORK/die.sh" ]; then
    failed=$((failed + 1))
    echo "FAIL checksh  check.sh has no die(); its early refusals exit with no verdict token,"
    echo "              and a reader grepping for one finds nothing and reads it as 'still running'"
  else
    refusal="$( set +e; . "$WORK/die.sh"; die "refusing to gate something" )"
    checks=$((checks + 1))
    case "$refusal" in
      *"CHECKSH-VERDICT: failed"*) : ;;
      *) failed=$((failed + 1))
         echo "FAIL checksh  a refusal printed no verdict token: $refusal" ;;
    esac
    checks=$((checks + 1))
    case "$refusal" in
      *"refusing to gate something"*) : ;;
      *) failed=$((failed + 1))
         echo "FAIL checksh  a refusal swallowed its own reason: $refusal" ;;
    esac
  fi

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
      if ( set +e; REPO="$WORK/mrepo"; . "$WORK/die.sh"; . "$WORK/mergewire.sh"; exit 0 ) >/dev/null 2>&1; then
        failed=$((failed + 1))
        echo "FAIL checksh  merge_state exit $code did not stop the gate; a conflicted tree would be reported on"
      fi
    done
    checks=$((checks + 1))
    merge_stub 0
    clean_merge="$( set +e; REPO="$WORK/mrepo"; . "$WORK/die.sh"; . "$WORK/mergewire.sh"; echo "CONTINUED" )"
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
  if ( set +e; REPO="$WORK/subrepo"; SUBMODULE_DRIFT=""; . "$WORK/die.sh"; . "$WORK/subwire.sh"; exit 0 ) >/dev/null 2>&1; then
    failed=$((failed + 1))
    echo "FAIL checksh  an uninitialised submodule did not stop the gate"
  fi

  # Drift (3) must NOT stop the gate -- an SDK bump in progress is real work --
  # but must set the flag that qualifies the verdict.
  checks=$((checks + 1))
  stub_repo 3 "submodule sdk is CHECKED OUT AT A DIFFERENT COMMIT"
  drift_out="$( set +e; REPO="$WORK/subrepo"; SUBMODULE_DRIFT=""; . "$WORK/die.sh"; . "$WORK/subwire.sh"; echo "FLAG=[$SUBMODULE_DRIFT]" 2>/dev/null )"
  case "$drift_out" in
    *"FLAG=[]"*) failed=$((failed + 1)); echo "FAIL checksh  drift ran but set no flag; the verdict would read as unqualified green" ;;
    *"FLAG=["*)  : ;;
    *)           failed=$((failed + 1)); echo "FAIL checksh  drift stopped the gate; an SDK bump in progress cannot be gated at all" ;;
  esac

  # A healthy tree stays silent and unflagged.
  checks=$((checks + 1))
  stub_repo 0 ""
  clean_out="$( set +e; REPO="$WORK/subrepo"; SUBMODULE_DRIFT=""; . "$WORK/die.sh"; . "$WORK/subwire.sh"; echo "FLAG=[$SUBMODULE_DRIFT]" )"
  case "$clean_out" in
    "FLAG=[]") : ;;
    *) failed=$((failed + 1)); echo "FAIL checksh  a healthy tree was not silent+unflagged: $clean_out" ;;
  esac

  # ---- freshness wiring: behind origin must reach the verdict ----
  #
  # An armed watcher fired --committed on 26-commit-stale code because it was
  # written before the merge-first rule existed. Being behind never STOPS the
  # gate (unlike an uninitialised submodule, working behind origin is ordinary
  # mid-work) but it must change what the last line claims.
  fresh_stub() {  # exit_code, message
    rm -rf "$WORK/subrepo"
    mkdir -p "$WORK/subrepo/scripts_local"
    { echo '#!/bin/bash'
      [ -n "$2" ] && echo "echo '$2'"
      echo "exit $1"
    } > "$WORK/subrepo/scripts_local/tree_freshness.sh"
    chmod +x "$WORK/subrepo/scripts_local/tree_freshness.sh"
  }
  run_fresh() {  # exit_code -> prints FLAG=[...]
    # A healthy probe says nothing at all, so the stub must not either: a
    # message on the silent path would make this assert the wrong thing.
    if [ "$1" -eq 0 ]; then fresh_stub 0 ""; else fresh_stub "$1" "behind by something"; fi
    ( set +e; REPO="$WORK/subrepo"; TREE_STALE=""; . "$WORK/freshwire.sh"; echo "FLAG=[$TREE_STALE]" ) 2>/dev/null
  }

  checks=$((checks + 1))
  case "$(run_fresh 3)" in
    *"FLAG=[]"*) failed=$((failed + 1)); echo "FAIL checksh  behind-by-inert set no flag, so the verdict reads as unqualified green" ;;
    *"FLAG=["*)  : ;;
    *)           failed=$((failed + 1)); echo "FAIL checksh  behind-by-inert stopped the gate; being behind is ordinary mid-work" ;;
  esac

  checks=$((checks + 1))
  case "$(run_fresh 4)" in
    *FIRMWARE*) : ;;
    *) failed=$((failed + 1)); echo "FAIL checksh  behind-on-firmware is not distinguished from behind-by-anything: $(run_fresh 4)" ;;
  esac

  checks=$((checks + 1))
  case "$(run_fresh 0)" in
    "FLAG=[]") : ;;
    *) failed=$((failed + 1)); echo "FAIL checksh  an up-to-date tree was flagged stale: $(run_fresh 0)" ;;
  esac

  # ---- the verdict line, driven through the real composition function ----
  verdict() {  # SUBMODULE_DRIFT, TREE_STALE, DEVICE_BUILDS_SKIPPED
    ( set +e; SUBMODULE_DRIFT="$1"; TREE_STALE="$2"; DEVICE_BUILDS_SKIPPED="${3:-}"; LOGS=/tmp/x
      . "$WORK/qualifier.sh"; . "$WORK/verdict.sh" )
  }

  checks=$((checks + 1))
  case "$(verdict 1 '')" in
    *DRIFTED*) : ;;
    *) failed=$((failed + 1)); echo "FAIL checksh  the verdict does not mention drift when drifted: $(verdict 1 '')" ;;
  esac

  checks=$((checks + 1))
  case "$(verdict '' 'behind origin')" in
    *"behind origin"*) : ;;
    *) failed=$((failed + 1)); echo "FAIL checksh  the verdict does not mention staleness when stale: $(verdict '' 'behind origin')" ;;
  esac

  # Both at once. Either one silently swallowing the other is the bug here:
  # a stale tree ON drifted submodules must not report only half of why its
  # green means nothing.
  checks=$((checks + 1))
  both="$(verdict 1 'behind origin')"
  case "$both" in
    *DRIFTED*"behind origin"*) : ;;
    *) failed=$((failed + 1)); echo "FAIL checksh  the verdict drops one of two reasons: $both" ;;
  esac

  # The qualifier must LEAD. Trailing it after "all green" put it on the right
  # line and still lost to a real reader on 2026-08-31: they grep
  # `all green|SOMETHING FAILED`, the line matched, and they acted on its first
  # three words. A qualified verdict must not open with the words a skimmer is
  # looking for.
  checks=$((checks + 1))
  lead="$(verdict 1 'behind origin')"
  case "$lead" in
    "all green"*) failed=$((failed + 1)); echo "FAIL checksh  a qualified verdict still OPENS with 'all green': $lead" ;;
    *) : ;;
  esac

  # ---- and the THIRD verdict: green, but over less ground -------------------
  #
  # A run that skipped four device builds is not withheld -- it is honestly
  # green for what it covered -- but it did not cover what a full run covers,
  # and a reader must be able to tell the two apart AT A GLANCE without knowing
  # the diff. So it gets its own line rather than a footnote.
  scoped="$(verdict '' '' 'x4pro sticky gh_release_x4pro gh_release_sticky')"

  checks=$((checks + 1))
  case "$scoped" in
    *"all green"*) failed=$((failed + 1)); echo "FAIL checksh  a run that skipped four builds still says 'all green', so every grep written before this matches it and fails OPEN: $scoped" ;;
    *) : ;;
  esac

  checks=$((checks + 1))
  case "$scoped" in
    *SKIPPED*) : ;;
    *) failed=$((failed + 1)); echo "FAIL checksh  the verdict of a scoped run does not say anything was skipped: $scoped" ;;
  esac

  # Naming them, not just admitting to them.
  checks=$((checks + 1))
  scoped_missing=""
  for e in x4pro sticky gh_release_x4pro gh_release_sticky; do
    case "$scoped" in *"$e"*) : ;; *) scoped_missing="$scoped_missing $e" ;; esac
  done
  [ -z "$scoped_missing" ] || { failed=$((failed + 1))
    echo "FAIL checksh  the scoped verdict does not name what it skipped:$scoped_missing"; }

  # A full clean run must still be distinguishable from it, which is the whole
  # point: the two lines cannot both be greppable as the same thing.
  checks=$((checks + 1))
  clean="$(verdict '' '' '')"
  if [ "$clean" = "$scoped" ]; then
    failed=$((failed + 1))
    echo "FAIL checksh  a scoped run and a full run print the SAME verdict; nobody can tell them apart"
  fi
  checks=$((checks + 1))
  case "$clean" in
    "all green"*) : ;;
    *) failed=$((failed + 1)); echo "FAIL checksh  a full clean run lost its unqualified verdict: $clean" ;;
  esac

  # Two reasons are not one reason. Drift is the stronger statement and wins
  # the line, but the skip must still be ON it -- otherwise whoever fixes the
  # drift gets a rerun that looks clean and is still missing four builds.
  checks=$((checks + 1))
  bothways="$(verdict 1 'behind origin' 'x4pro sticky')"
  case "$bothways" in
    *DRIFTED*) : ;;
    *) failed=$((failed + 1)); echo "FAIL checksh  a scoped run swallowed the drift qualifier: $bothways" ;;
  esac
  checks=$((checks + 1))
  case "$bothways" in
    *SKIPPED*) : ;;
    *) failed=$((failed + 1)); echo "FAIL checksh  a withheld verdict hid the fact that four builds also did not run: $bothways" ;;
  esac

  # And it must not CONTAIN the clean phrase either. Front-loading fixed the
  # human skimmer and left every machine reader exactly where it was: a
  # qualified verdict carrying its own unqualified form as a substring is
  # matched by every grep written before the qualifier existed, and each one
  # fails in the direction of "it passed". Old patterns must find nothing here.
  # This assertion is what keeps that true when someone rewords the line later.
  checks=$((checks + 1))
  case "$lead" in
    *"all green"*) failed=$((failed + 1)); echo "FAIL checksh  a qualified verdict still CONTAINS 'all green', so every pre-existing grep matches it and fails open: $lead" ;;
    *) : ;;
  esac

  checks=$((checks + 1))
  plain="$(verdict '' '')"
  case "$plain" in
    *DRIFTED*|*behind*) failed=$((failed + 1)); echo "FAIL checksh  a clean tree's verdict claims a problem: $plain" ;;
    *"all green"*) : ;;
    *) failed=$((failed + 1)); echo "FAIL checksh  a clean tree lost its 'all green' verdict: $plain" ;;
  esac
else
  checks=$((checks + 1))
  failed=$((failed + 1))
  echo "FAIL checksh  could not lift the submodule wiring, freshness wiring, verdict or qualifier out of check.sh"
fi

# ---------------------------------------------------------------------------
# The SKIP surfacing, against every SKIP this tree can actually print.
#
# check.sh's host-tests loop ends with a grep whose comment reads "A check that
# did not run must not scroll past looking like one that passed." That grep was
# anchored at column zero for its whole life, and three of the six emitters
# indent theirs by two spaces -- host-tests/study twice and host-tests/
# autorelease once -- so the mechanism built to stop a skip hiding could not
# see half of the skips. Both study ones were checks that had never run once.
#
# So the test does not assert a pattern. It DISCOVERS the skip lines the tree
# can print, by reading the string literals out of every suite, and asserts
# check.sh's own grep -- lifted from the file, not copied -- matches each one.
# A future SKIP printed with any indentation adds itself to this list.
# ...LOG, not ...LOGS: card #320 moved the suite loop's log from "$LOGS/$name.log"
# to a per-run "$SUITE_LOG", and an anchor that only knew the old spelling would
# turn that into "could not lift the SKIP surfacing" -- a broken extraction
# wearing the costume of a broken gate, which is the failure this file's own
# comments keep warning about.
SKIPGREP="$(grep -E '^[[:space:]]*grep -E .*SKIP.*LOG' "$CHECK" | head -1)"
checks=$((checks + 1))
if [ -z "$SKIPGREP" ]; then
  failed=$((failed + 1))
  echo "FAIL checksh  could not lift the SKIP surfacing out of check.sh"
else
  # Just the pattern, as a literal: the line is
  #   grep -E "<pattern>" "$SUITE_LOG" | head -5 | sed ...
  SKIPPAT="$(printf '%s\n' "$SKIPGREP" | sed -E 's/^[[:space:]]*grep -E "([^"]*)".*$/\1/')"
  python3 - "$HERE/../.." "$SKIPPAT" <<'PY_SKIP'
import pathlib, re, subprocess, sys

root = pathlib.Path(sys.argv[1]).resolve()
pattern = sys.argv[2]

# Every string literal in a host suite that a run would print starting with
# SKIP, with the indentation it would print. Comments are excluded: a line
# whose first non-space characters are a comment marker prints nothing.
emit = re.compile(r'(printf|print|echo|puts|cout)', re.I)
literal = re.compile(r'"((?:[ \t]*)SKIP[^"\\]*)')
lines = []
for path in sorted((root / "host-tests").rglob("*")):
    if not path.is_file() or path.suffix not in (".sh", ".py", ".cpp", ".c", ".h"):
        continue
    for raw in path.read_text(errors="replace").splitlines():
        head = raw.lstrip()
        if head.startswith(("//", "#", "*")):
            continue
        if not emit.search(raw):
            continue
        for m in literal.finditer(raw):
            lines.append((path.relative_to(root), m.group(1)))

if not lines:
    print("FAIL checksh  found no SKIP emitters at all -- the discovery is broken,")
    print("              which would make this assertion pass by finding nothing")
    raise SystemExit(1)

bad = []
for where, text in lines:
    # grep -E, exactly as check.sh runs it, against the one line.
    hit = subprocess.run(["grep", "-E", pattern], input=text + "\n",
                         text=True, capture_output=True).returncode == 0
    if not hit:
        bad.append((where, text))

for where, text in bad:
    print("FAIL checksh  check.sh's SKIP surfacing cannot see %r from %s" % (text, where))
print("      (checked %d SKIP literals across the tree)" % len(lines))
raise SystemExit(1 if bad else 0)
PY_SKIP
  if [ $? -ne 0 ]; then
    failed=$((failed + 1))
  fi
fi

# The SUB-SUITE COUNT, against every summary line this tree can actually print.
#
# check.sh's host-tests loop prints "ok (N sub-suite(s))" where N comes from
#   passed=$(grep -c "checks, 0 failed" "$SUITE_LOG" || true)
# -- a free-text grep for a phrase each test binary happens to print. Nothing
# enforced that phrase, so a suite whose summary says anything else is INVISIBLE
# to the count: it runs, it passes, and the tally silently omits it.
#
# That is not hypothetical. host-tests/trivia/test_report.cpp printed
# "274 checks, 0 failures" while its two siblings printed "0 failed", so a suite
# of three reported "ok (2 sub-suite(s))" for a run in which all three passed.
# The green was real and the number was wrong, which is the worse half: the
# count is exactly what a person reads to answer "did everything I added run?".
#
# Same shape as the SKIP guard above, and for the same reason: this does not
# assert a pattern, it DISCOVERS the summary lines the tree can print and
# asserts check.sh's own counting grep -- lifted from the file, not copied --
# matches each one. A new suite worded differently adds itself to this list.
COUNTGREP="$(grep -E '^[[:space:]]*passed=\$\(grep -c ' "$CHECK" | head -1)"
checks=$((checks + 1))
if [ -z "$COUNTGREP" ]; then
  failed=$((failed + 1))
  echo "FAIL checksh  could not lift the sub-suite count out of check.sh"
else
  COUNTPAT="$(printf '%s\n' "$COUNTGREP" | sed -E 's/^[^"]*"([^"]*)".*$/\1/')"
  python3 - "$HERE/../.." "$COUNTPAT" <<'PY_COUNT'
import pathlib, re, subprocess, sys

root = pathlib.Path(sys.argv[1]).resolve()
pattern = sys.argv[2]

# Every string literal a host suite would PRINT that carries a "N checks, M
# <failword>" summary. Comments are excluded: a line whose first non-space
# characters are a comment marker prints nothing.
emit = re.compile(r"(printf|print|echo|puts|cout)", re.I)
# Escapes are allowed INSIDE the literal, not treated as its end. A first cut
# used [^"\\]* and so could not see a single C++ summary, because every one of
# them ends in \n -- the guard would have reported a clean tree while checking
# only the shell suites, which is this file's own recurring nightmare.
summary = re.compile(r'"((?:[^"\\]|\\.)*\bchecks,(?:[^"\\]|\\.)*fail(?:[^"\\]|\\.)*)"')
# The count is only ever read on a CLEAN run, so every placeholder becomes 0 --
# that is the line check.sh's grep will actually meet.
placeholder = re.compile(r"%[-0-9.]*[a-zA-Z]|\$\{?[A-Za-z_][A-Za-z_0-9]*\}?|\{[^}]*\}")
escape = re.compile(r"\\.")

found = []
for path in sorted((root / "host-tests").rglob("*")):
    if not path.is_file() or path.suffix not in (".sh", ".py", ".cpp", ".c", ".h", ".js"):
        continue
    for raw in path.read_text(errors="replace").splitlines():
        head = raw.lstrip()
        if head.startswith(("//", "#", "*")):
            continue
        if not emit.search(raw):
            continue
        for m in summary.finditer(raw):
            text = m.group(1)
            # A summary with no placeholder is a fixed sentence, not a per-run
            # tally -- "1 checks, 1 failed" is what a suite prints when it has
            # ALREADY failed, and check.sh is right not to count it.
            if not placeholder.search(text):
                continue
            found.append((path.relative_to(root), text))

if not found:
    print("FAIL checksh  found no sub-suite summary lines at all -- the discovery is")
    print("              broken, which would make this assertion pass by finding nothing")
    raise SystemExit(1)

bad = []
for where, text in found:
    rendered = escape.sub("", placeholder.sub("0", text))
    hit = subprocess.run(["grep", "-E", pattern], input=rendered + "\n",
                         text=True, capture_output=True).returncode == 0
    if not hit:
        bad.append((where, text, rendered))

for where, text, rendered in bad:
    print(f"FAIL checksh  {where} prints a summary check.sh cannot count:")
    print(f'                  literal  {text!r}')
    print(f'                  on a clean run  "{rendered}"')
    print(f'                  counted by  grep -c "{pattern}"')
if bad:
    print("              That suite RUNS and PASSES and is simply left out of the")
    print("              'ok (N sub-suite(s))' tally, so a suite nobody notices is")
    print("              missing looks exactly like one that was never added.")
print("      (checked %d summary literals across the tree)" % len(found))
raise SystemExit(1 if bad else 0)
PY_COUNT
  if [ $? -ne 0 ]; then
    failed=$((failed + 1))
  fi
fi

# -- and a skip must be a FAILURE in CI --------------------------------------
#
# The block above proves a skip is SEEN. This one asks the other half: on the
# runner, where every input a suite needs is supposed to be present, a check
# that did not run is a failure, not a line in a log. host-tests/release,
# relwatch and boardmigrate all turn a skip into a failure under $CI;
# host-tests/autorelease and host-tests/qastack did not, and CI runs every
# suite but relwatch, so all three of their skips were live and silent there.
#
# Detected loosely on purpose -- a line that names SKIP and an emitter, with
# comments stripped -- because the strict literal match above is about the
# text that gets printed, and this is about whether the branch printing it can
# be reached in CI at all. Quoting style is irrelevant to that question.
python3 - "$HERE/.." <<'CIGUARD' >"$WORK/ciguard"
import os, re, sys

suites = sys.argv[1]

def can_skip(src):
    for line in src.splitlines():
        bare = line.split('#', 1)[0]
        if re.search(r'\bSKIP\b', bare) and re.search(r'\b(?:echo|printf)\b', bare):
            return True
    return False

found = 0
for name in sorted(os.listdir(suites)):
    run = os.path.join(suites, name, "run.sh")
    if not os.path.isfile(run):
        continue
    src = open(run).read()
    if not can_skip(src):
        continue
    found += 1
    # Crude by design: one mention of ${CI:-} anywhere in the file, rather than
    # an attempt to decide statically which branch it guards. A suite that can
    # skip and never mentions CI cannot be turning a skip into a failure there.
    if "${CI:-}" in src:
        print("ok    %s: a skip is a failure in CI" % name)
    else:
        print("FAIL checksh  host-tests/%s can print a SKIP and never mentions ${CI:-}: "
              "on the runner, where every input is meant to be present, that check does "
              "not run and the suite still exits 0" % name)
if not found:
    print("FAIL checksh  no suite was found that can print a SKIP; this just checked nothing")
CIGUARD

checks=$((checks + 1))
if [ ! -s "$WORK/ciguard" ]; then
  failed=$((failed + 1))
  echo "FAIL checksh  the CI-guard scan produced no output at all, so it asserted nothing"
fi
while IFS= read -r line; do
  checks=$((checks + 1))
  case "$line" in FAIL*) failed=$((failed + 1)); echo "$line" ;; esac
done < "$WORK/ciguard"

# ---------------------------------------------------------------------------
# THE VERDICT MUST SURVIVE A WRAPPER, AND THE EXIT CODE MUST MEAN SOMETHING.
#
# Card #317. Three ways of reading this gate were wrong at once on 2026-09-05
# and two agents nearly shipped on a false green:
#
#   $?         A run that printed SOMETHING FAILED was observed exiting 0, and
#              `check.sh | tee out` exits with tee's status anyway.
#   tail -1    A background-task wrapper prints "[exited with code 0]" AFTER
#              this script's last line. The zero a reader acted on was the
#              WRAPPER's status. Every document in this repo said to read the
#              last line; all of them were wrong in the most common way the
#              gate is actually run.
#   tail -45   The cause had scrolled off the top while a screenful of `ok`
#              lines remained, so a red run LOOKED healthy.
#
# So the foot of check.sh is driven here exactly as an agent drives it: run it,
# capture the stream, append a wrapper line after the process is gone, and then
# ask both readings what happened. The token must survive; `tail -1` must not.
# The assertion that tail -1 fails is not decoration -- it is the proof that
# this suite is testing the thing that actually broke, and it goes red the day
# somebody "simplifies" the token away and relies on position again.
python3 - "$CHECK" >"$WORK/foot.sh" <<'PY_FOOT'
import sys
lines = open(sys.argv[1]).read().splitlines()
start = next(i for i, l in enumerate(lines) if l.startswith('if [ "$FAILED" -eq 0 ]'))
end = next(i for i in range(start, len(lines)) if lines[i].startswith('exit "$STATUS"'))
print('\n'.join(lines[start:end + 1]))
PY_FOOT
lift_fn qualifier_text >"$WORK/qual.sh" 2>/dev/null

checks=$((checks + 1))
if [ ! -s "$WORK/foot.sh" ] || [ ! -s "$WORK/qual.sh" ]; then
  failed=$((failed + 1))
  echo "FAIL checksh  could not lift check.sh's verdict foot. It must end in a machine-readable"
  echo "              token line and an exit whose code is chosen per verdict; if either the"
  echo "              'if [ \"\$FAILED\" -eq 0 ]' anchor or 'exit \"\$STATUS\"' is gone, so is card #317's fix"
else
  GATE_EXIT=0
  mkgate() {  # FAILED, SUBMODULE_DRIFT, TREE_STALE, DEVICE_BUILDS_SKIPPED
    rm -rf "$WORK/gatelogs"; mkdir -p "$WORK/gatelogs"
    { echo '#!/bin/bash'
      echo 'set -uo pipefail'
      cat "$WORK/qual.sh"
      echo "LOGS=\"$WORK/gatelogs\""
      echo "CHECK_RUNLOG=\"$WORK/gatelogs/run.tok\""
      # The transcript and a suite log, so the green branch's cleanup is
      # exercised on a directory that actually has something in it.
      echo 'echo transcript > "$CHECK_RUNLOG"'
      echo 'echo suite > "$LOGS/ui.log"'
      echo "FAILED=$1"
      echo "SUBMODULE_DRIFT='$2'"
      echo "TREE_STALE='$3'"
      echo "DEVICE_BUILDS_SKIPPED='$4'"
      cat "$WORK/foot.sh"
    } > "$WORK/gate-run.sh"
  }
  # Exactly how an agent runs it: the stream is captured, and the wrapper's own
  # line lands after the gate's last one.
  run_gate() {  # same four arguments
    mkgate "$1" "$2" "$3" "$4"
    bash "$WORK/gate-run.sh" > "$WORK/gate-out" 2>&1
    GATE_EXIT=$?
    echo "[exited with code 0]" >> "$WORK/gate-out"
  }
  token() { grep -o 'CHECKSH-VERDICT: [a-z-]*' "$WORK/gate-out" | head -1 | sed 's/^CHECKSH-VERDICT: //'; }

  # want_verdict <label> <FAILED> <drift> <stale> <skipped> <token> <exit>
  want_verdict() {
    run_gate "$2" "$3" "$4" "$5"
    checks=$((checks + 1))
    got="$(token)"
    if [ "$got" != "$6" ]; then
      failed=$((failed + 1))
      echo "FAIL checksh  $1: the verdict token is '$got', wanted '$6'. An agent greps"
      echo "              CHECKSH-VERDICT because no other reading of this output survives a"
      echo "              wrapper; a missing or wrong token is a run nobody can classify."
    fi
    checks=$((checks + 1))
    if [ "$GATE_EXIT" != "$7" ]; then
      failed=$((failed + 1))
      echo "FAIL checksh  $1: exited $GATE_EXIT, wanted $7"
    fi
  }

  want_verdict "a clean run"        0 ''  ''              ''              green                     0
  want_verdict "a red run"          1 ''  ''              ''              failed                    1
  want_verdict "a withheld run"     0 '1' 'behind origin' ''              withheld                  3
  want_verdict "a scoped run"       0 ''  ''              'x4pro sticky'  host-green-device-skipped 0
  # Withheld beats scoped, and both are qualified: the token must say withheld,
  # because that is the one that means "not on the code that ships".
  want_verdict "withheld and scoped" 0 '1' ''             'x4pro sticky'  withheld                  3

  # A red run exits non-zero. This is the assertion that was believed and never
  # tested: SOMETHING FAILED with a zero status is how the whole card started.
  run_gate 1 '' '' ''
  checks=$((checks + 1))
  if [ "$GATE_EXIT" = "0" ]; then
    failed=$((failed + 1))
    echo "FAIL checksh  a run that printed SOMETHING FAILED exited 0"
  fi

  # A withheld run must NOT exit 0. It is not a pass, and a status of 0 is the
  # reason four documents had to carry a warning instead of a rule.
  run_gate 0 '' 'behind origin' ''
  checks=$((checks + 1))
  if [ "$GATE_EXIT" = "0" ]; then
    failed=$((failed + 1))
    echo "FAIL checksh  a withheld verdict exited 0, so '&& git push' would push unverified code"
  fi

  # THE WRAPPER. tail -1 must be the wrapper's line, not ours -- that is the
  # world as it is -- and the token must still be recoverable from the same
  # stream. Both halves, or this proves nothing.
  run_gate 1 '' '' ''
  checks=$((checks + 1))
  case "$(tail -1 "$WORK/gate-out")" in
    *"exited with code"*) : ;;
    *) failed=$((failed + 1))
       echo "FAIL checksh  the wrapper fixture did not append its line, so the test that a"
       echo "              wrapper defeats tail -1 asserted nothing" ;;
  esac
  checks=$((checks + 1))
  case "$(tail -1 "$WORK/gate-out")" in
    *CHECKSH-VERDICT*) failed=$((failed + 1))
       echo "FAIL checksh  the fixture put the verdict on the last line, so this never" \
            "reproduced the wrapper case at all" ;;
    *) : ;;
  esac
  checks=$((checks + 1))
  if [ "$(token)" != "failed" ]; then
    failed=$((failed + 1))
    echo "FAIL checksh  a red gate under a wrapper cannot be recovered by grepping the token:"
    echo "              tail -1 gives the wrapper's zero and the token gives '$(token)'."
    echo "              This is the exact shape that nearly shipped twice on 2026-09-05."
  fi

  # Exactly ONE token line per run. Two would let a reader take the first and be
  # wrong, which is the failure mode this replaces rather than a fix for it.
  checks=$((checks + 1))
  ntok=$(grep -c 'CHECKSH-VERDICT:' "$WORK/gate-out")
  if [ "$ntok" != "1" ]; then
    failed=$((failed + 1))
    echo "FAIL checksh  the run printed $ntok verdict tokens; a reader grepping for one must find one"
  fi

  # No two verdicts may share a token, or the grep classifies nothing.
  checks=$((checks + 1))
  seen=""
  for spec in "0:::" "1:::" "0:1:behind origin:" "0:::x4pro"; do
    f="${spec%%:*}"; rest="${spec#*:}"
    d="${rest%%:*}"; rest="${rest#*:}"
    s="${rest%%:*}"; k="${rest#*:}"
    run_gate "$f" "$d" "$s" "$k"
    seen="$seen $(token)"
  done
  uniq_n=$(printf '%s\n' $seen | sort -u | wc -l | tr -d ' ')
  if [ "$uniq_n" != "4" ]; then
    failed=$((failed + 1))
    echo "FAIL checksh  the four verdicts produced $uniq_n distinct tokens:$seen"
  fi

  # THE TRANSCRIPT SURVIVES A GREEN RUN (card #314). check.sh prints its path on
  # the first line so a backgrounded run can be followed there instead of into a
  # name somebody invented in the shared scratchpad. Deleting the file you told a
  # reader to poll is the same silent lie as another session overwriting it.
  run_gate 0 '' '' ''
  checks=$((checks + 1))
  if [ ! -f "$WORK/gatelogs/run.tok" ]; then
    failed=$((failed + 1))
    echo "FAIL checksh  a green run deleted its own transcript, the one path it published"
  fi
  # ...and it still collects everything else, which is what card #144 bought.
  checks=$((checks + 1))
  if [ -f "$WORK/gatelogs/ui.log" ]; then
    failed=$((failed + 1))
    echo "FAIL checksh  a green run kept its suite logs; card #144's cleanup is gone"
  fi
fi

# ---------------------------------------------------------------------------
# THE TRANSCRIPT'S NAME IS UNREPEATABLE (card #314).
#
# The whole fix rests on one mktemp template. On BSD the X's must be the LAST
# characters: `mktemp "$LOGS/run-XXXXXXXX.log"` creates a file called literally
# "run-XXXXXXXX.log" -- a SHARED name wearing a unique one's costume, which is
# precisely the bug, silently reintroduced and impossible to see by reading. So
# the template is lifted out of check.sh and actually run, twice.
checks=$((checks + 1))
TMPL="$(grep -o 'mktemp "\$LOGS/[^"]*"' "$CHECK" | head -1 | sed 's/.*\$LOGS\///; s/"$//')"
if [ -z "$TMPL" ]; then
  failed=$((failed + 1))
  echo "FAIL checksh  check.sh no longer names its transcript with mktemp, so two runs of one"
  echo "              tree can choose the same path again (card #314)"
else
  mkdir -p "$WORK/tmpl"
  t1="$(mktemp "$WORK/tmpl/$TMPL" 2>/dev/null)"
  t2="$(mktemp "$WORK/tmpl/$TMPL" 2>/dev/null)"
  checks=$((checks + 1))
  # t2 must be non-empty too. A template with no trailing X's makes the FIRST
  # mktemp create the literal name and the SECOND fail, so "$t1" != "$t2" is
  # satisfied by a failure and the assertion passes on the broken template.
  if [ -z "$t1" ] || [ -z "$t2" ] || [ "$t1" = "$t2" ]; then
    failed=$((failed + 1))
    echo "FAIL checksh  the transcript template '$TMPL' does not produce distinct names on this"
    echo "              platform (got '$t1' and '$t2'). On BSD mktemp the X's must be the LAST"
    echo "              characters of the template, or it creates the literal name."
  fi
  checks=$((checks + 1))
  case "$t1" in
    *XXXX*) failed=$((failed + 1))
      echo "FAIL checksh  mktemp returned '$t1' with the X's unsubstituted: every run of every" \
           "tree would share that one path" ;;
    *) : ;;
  esac
fi

checks=$((checks + 1))
if ! grep -q '^  echo "transcript: \$CHECK_RUNLOG"' "$CHECK"; then
  failed=$((failed + 1))
  echo "FAIL checksh  check.sh does not print its transcript path. An unprinted unique path is"
  echo "              no better than a shared one: the reader invents a name instead."
fi

# ---------------------------------------------------------------------------
# THE CMAKE BUILD DIRECTORY IS PER RUN, AND ITS SWEEP CANNOT TAKE A LIVE ONE.
#
# Card #320. $LOGS became one directory per TREE, reused (PR #116, deliberately
# -- it is why a failed run's logs sit at a predictable path). cmake-build lived
# inside it, so two runs of one tree shared the BUILD directory, and killing a
# gate then starting another printed `ctest FAILED (0s)` with no error lines,
# because the grep found nothing in a log the other run had truncated. Running
# ctest by hand in the same tree passed all 197 tests. Silent, and misattributed
# to the agent's own diff.
#
# Isolation rather than a lock, same shape as the --committed trial worktree
# (app/checkrace): a per-pid path plus a startup sweep of orphans whose owner is
# gone. The dangerous half is the sweep, so that is what is driven here.
checks=$((checks + 1))
if ! grep -q 'CMB="\$LOGS/cmake-build\.\$\$"' "$CHECK"; then
  failed=$((failed + 1))
  echo "FAIL checksh  the cmake build directory is not per-run. Two runs of one tree share it"
  echo "              again, and the second dies as 'ctest FAILED (0s)' blaming your diff (#320)."
fi

python3 - "$CHECK" >"$WORK/cmsweep.sh" <<'PY_SWEEP'
import sys
lines = open(sys.argv[1]).read().splitlines()
start = next(i for i, l in enumerate(lines) if l.strip().startswith('for stale in "$LOGS"/cmake-build.'))
end = next(i for i in range(start, len(lines)) if lines[i].strip() == 'done')
print('\n'.join(l[2:] if l.startswith('  ') else l for l in lines[start:end + 1]))
PY_SWEEP

checks=$((checks + 1))
if [ ! -s "$WORK/cmsweep.sh" ]; then
  failed=$((failed + 1))
  echo "FAIL checksh  could not lift the cmake-build sweep out of check.sh"
else
  # A live owner, a dead owner, and this run's own. Only the dead one may go.
  sleep 30 &
  LIVE=$!
  DEAD=$(bash -c 'echo $$')          # a pid that has certainly exited
  rm -rf "$WORK/sweeplogs"; mkdir -p "$WORK/sweeplogs"
  mkdir -p "$WORK/sweeplogs/cmake-build.$LIVE" "$WORK/sweeplogs/cmake-build.$DEAD" \
           "$WORK/sweeplogs/cmake-build.$$"
  : > "$WORK/sweeplogs/cmake.$DEAD.log"
  ( LOGS="$WORK/sweeplogs"; CMB="$WORK/sweeplogs/cmake-build.$$"
    . "$WORK/cmsweep.sh" ) >/dev/null 2>&1

  checks=$((checks + 1))
  if [ -d "$WORK/sweeplogs/cmake-build.$DEAD" ]; then
    failed=$((failed + 1))
    echo "FAIL checksh  the sweep left a killed run's build directory behind; the next run of"
    echo "              this tree inherits a cache pointing at a deleted worktree"
  fi
  checks=$((checks + 1))
  if [ -f "$WORK/sweeplogs/cmake.$DEAD.log" ]; then
    failed=$((failed + 1))
    echo "FAIL checksh  the sweep took the orphan's build dir but left its log"
  fi
  checks=$((checks + 1))
  if [ ! -d "$WORK/sweeplogs/cmake-build.$LIVE" ]; then
    failed=$((failed + 1))
    echo "FAIL checksh  the sweep deleted a LIVE run's build directory. That is the bug it was"
    echo "              written to prevent, pointed the other way: a healthy gate dies mid-build"
    echo "              with an error naming no file of anyone's."
  fi
  checks=$((checks + 1))
  if [ ! -d "$WORK/sweeplogs/cmake-build.$$" ]; then
    failed=$((failed + 1))
    echo "FAIL checksh  the sweep deleted THIS run's own build directory"
  fi
  kill "$LIVE" 2>/dev/null
  wait "$LIVE" 2>/dev/null
fi

# ---------------------------------------------------------------------------
# A STEP THAT FAILS IN ZERO SECONDS DID NOT RUN, AND MUST SAY SO.
#
# The `(0s)` tell (card #320). `ctest FAILED (0s)` with no error lines under it
# was read twice in one evening as "my diff broke the unit tests"; a real ctest
# failure spends seconds configuring and compiling first. The note has to be
# conservative in one direction only: silent when it is not sure, never blaming
# the machine for a real break.
lift_fn infra_fault_note >"$WORK/infra.sh" 2>/dev/null
checks=$((checks + 1))
if [ ! -s "$WORK/infra.sh" ]; then
  failed=$((failed + 1))
  echo "FAIL checksh  check.sh has no infra_fault_note; a 0s failure with an empty log goes back"
  echo "              to reading as the agent's own diff (card #320)"
else
  infra() {  # logfile-contents, age-seconds -> the note, if any
    printf '%s' "$1" > "$WORK/infra.log"
    ( . "$WORK/infra.sh"; infra_fault_note ctest "$(( $(date +%s) - $2 ))" "$WORK/infra.log" )
  }
  checks=$((checks + 1))
  case "$(infra '' 0)" in
    *"INFRASTRUCTURE FAULT"*) : ;;
    *) failed=$((failed + 1))
       echo "FAIL checksh  an instant failure with an empty log said nothing, which is exactly" \
            "how it reads as your own diff" ;;
  esac
  checks=$((checks + 1))
  case "$(infra 'test_foo ... error: expected ;' 0)" in
    *"INFRASTRUCTURE FAULT"*) failed=$((failed + 1))
       echo "FAIL checksh  a real, fast failure was blamed on the machine; the note must stay" \
            "silent whenever the log has something to say" ;;
    *) : ;;
  esac
  # THE PROBE MUST BE ABLE TO FAIL. The reported ctest failure did NOT have an
  # empty log -- it had a full one that check.sh printed nothing from, because
  # cmake's own wording matches none of the patterns check.sh greps for. A note
  # that suppresses itself on any occurrence of the word "error" is silent on
  # exactly the case it was written for, which is how the first draft of this
  # was wrong.
  checks=$((checks + 1))
  case "$(infra 'CMake Error: The current CMakeCache.txt directory /tmp/xteink-committed-ab-123/test is different than the directory /tmp/xteink-committed-ab-456/test where CMakeCache.txt was created.' 0)" in
    *"INFRASTRUCTURE FAULT"*) : ;;
    *) failed=$((failed + 1))
       echo "FAIL checksh  the (0s) note stayed silent on the stale-cmake-cache text that IS the" \
            "reported failure: check.sh prints nothing from that log, so the reader sees a bare" \
            "'ctest FAILED (0s)' and reads it as their own diff" ;;
  esac

  checks=$((checks + 1))
  case "$(infra '' 30)" in
    *"INFRASTRUCTURE FAULT"*) failed=$((failed + 1))
       echo "FAIL checksh  a 30s failure was called instant; the duration is the whole signal" ;;
    *) : ;;
  esac
fi

# ---------------------------------------------------------------------------
# WHOSE GATE IS THAT (card #314).
#
# "Kill by PID, never by pattern" was not enough on 2026-09-05: an agent ran
# `pgrep -f "check.sh --committed"`, got four pids across three worktrees --
# every session runs an identically named script from an identically named
# relative path -- and nearly killed two siblings' builds. Only each pid's
# WORKING DIRECTORY distinguished them, so that is what whose-gate.sh resolves.
WG="$HERE/../../scripts_local/whose-gate.sh"
checks=$((checks + 1))
if [ ! -x "$WG" ]; then
  failed=$((failed + 1))
  echo "FAIL checksh  scripts_local/whose-gate.sh is missing or not executable"
else
  mkdir -p "$WORK/faketree/scripts_local" "$WORK/othertree/scripts_local"
  ( cd "$WORK/faketree" && exec sleep 25 ) &
  WGPID=$!
  # Wait for the EXEC, not for the fork. `$!` is the subshell, which exists the
  # instant it is forked, so `ps -p $!` succeeds immediately and a loop written
  # around it runs zero times -- while `pgrep -f 'sleep 25'` cannot match until
  # the exec has actually happened. On a loaded runner that is a flake, and a
  # readiness check that is never false is not a readiness check.
  n=0
  while [ $n -lt 100 ] && [ -z "$(pgrep -f '[s]leep 25' 2>/dev/null)" ]; do
    sleep 0.05; n=$((n + 1))
  done
  checks=$((checks + 1))
  if [ -z "$(pgrep -f '[s]leep 25' 2>/dev/null)" ]; then
    failed=$((failed + 1))
    echo "FAIL checksh  the whose-gate fixture process never appeared, so the two assertions"
    echo "              below tested nothing"
  fi
  out="$(cd "$WORK/faketree" && GATE_PATTERN='[s]leep 25' "$WG" 2>/dev/null)"
  checks=$((checks + 1))
  case "$out" in
    *"$WORK/faketree"*) : ;;
    *) failed=$((failed + 1))
       echo "FAIL checksh  whose-gate.sh did not resolve a running process to the tree it is"
       echo "              working in; that resolution is the entire point of the script: $out" ;;
  esac
  checks=$((checks + 1))
  mine="$(cd "$WORK/othertree" && GATE_PATTERN='[s]leep 25' "$WG" --mine 2>/dev/null)"
  case "$mine" in
    *"$WORK/faketree"*) failed=$((failed + 1))
       echo "FAIL checksh  --mine listed another tree's build. An agent acts on this list with" \
            "kill; naming a sibling's pid is the near-miss it exists to prevent." ;;
    *) : ;;
  esac
  # And the other direction, or a --mine that lists NOTHING, ever, passes the
  # assertion above and is useless.
  checks=$((checks + 1))
  ownmine="$(cd "$WORK/faketree" && GATE_PATTERN='[s]leep 25' "$WG" --mine 2>/dev/null)"
  case "$ownmine" in
    *"$WORK/faketree"*) : ;;
    *) failed=$((failed + 1))
       echo "FAIL checksh  --mine did not list this tree's OWN build, so it answers 'nothing" \
            "running' to every question and the negative assertion above proves nothing: $ownmine" ;;
  esac
  kill "$WGPID" 2>/dev/null
  wait "$WGPID" 2>/dev/null
fi

# ---------------------------------------------------------------------------
# A --tests RUN MUST NOT REPORT AN UNQUALIFIED GREEN.
#
# Card #317 made the verdict a token, and a token is read by somebody who never
# saw the command line. `--tests` runs the host suites and builds nothing, so a
# `green` token from it claims ground the run never covered -- which is the
# exact overstatement the third verdict (`host-green-device-skipped`) was
# invented to prevent when the scope gate skips the builds. The two cases are
# the same case and must produce the same token.
python3 - "$CHECK" >"$WORK/testsarm.sh" <<'PY_TESTS'
import sys
lines = open(sys.argv[1]).read().splitlines()
start = next(i for i, l in enumerate(lines) if l.startswith('if [ "${1:-}" = "--tests" ]'))
end = next(i for i in range(start, len(lines)) if lines[i] == 'fi')
print('\n'.join(lines[start:end + 1]))
PY_TESTS

checks=$((checks + 1))
if [ ! -s "$WORK/testsarm.sh" ]; then
  failed=$((failed + 1))
  echo "FAIL checksh  check.sh has no --tests arm setting the skip scope, so a run that built"
  echo "              nothing reports the same token as a full one (card #317)"
else
  arm() {  # $1 as check.sh saw it -> prints SKIPPED=[...] WHY=[...]
    ( set +e; set -- "$1"; DEVICE_BUILDS_SKIPPED=""; DEVICE_SKIP_WHY=""
      . "$WORK/testsarm.sh"; echo "SKIPPED=[$DEVICE_BUILDS_SKIPPED] WHY=[$DEVICE_SKIP_WHY]" )
  }
  checks=$((checks + 1))
  case "$(arm --tests)" in
    *"SKIPPED=[]"*) failed=$((failed + 1))
      echo "FAIL checksh  a --tests run leaves the skip scope empty, so its verdict is a bare" \
           "'green' for a run that compiled nothing" ;;
    *"WHY=[]"*) failed=$((failed + 1))
      echo "FAIL checksh  a --tests run names no reason, so the verdict line falls back to a" \
           "generic phrase instead of saying --tests" ;;
    *) : ;;
  esac
  checks=$((checks + 1))
  case "$(arm --committed)" in
    "SKIPPED=[] WHY=[]") : ;;
    *) failed=$((failed + 1))
       echo "FAIL checksh  a run that is NOT --tests was marked as having skipped the builds:" \
            "$(arm --committed)" ;;
  esac

  # The OTHER producer of the reason -- the scope gate's default, set where the
  # build block computes DEVICE_BUILDS_SKIPPED. Only the --tests arm was covered,
  # so "a reader can tell the two skips apart" was half-asserted.
  checks=$((checks + 1))
  if ! grep -q 'DEVICE_SKIP_WHY="nothing in this diff reaches a device image"' "$CHECK"; then
    failed=$((failed + 1))
    echo "FAIL checksh  the scope-skip reason is gone, so a device-build skip and a --tests run"
    echo "              print the same sentence and the reader cannot tell which happened"
  fi

  # And the verdict line must actually USE the reason, or the two skips read
  # identically and a reader cannot tell "your diff cannot reach a device" from
  # "you did not ask for a build".
  checks=$((checks + 1))
  if ! grep -q 'DEVICE_SKIP_WHY' "$WORK/verdict.sh" 2>/dev/null; then
    failed=$((failed + 1))
    echo "FAIL checksh  the HOST GREEN line does not carry the reason it was skipped, so a"
    echo "              --tests run and a scope-skipped run print the same sentence"
  fi
fi


# --- the undo guard: a branch that undoes what trunk recently landed ---------
#
# The block is lifted between its own markers and run in a fixture repository
# with a trunk of sixty-line commits and refs/remotes/origin/xteink pointing at
# its tip. Fires on a stale tree committed on top of the moved trunk; stays
# silent on an ordinary branch, on a branch deleting its own additions, and
# when CHECK_ALLOW_UNDO says the revert is meant.
python3 - "$CHECK" >"$WORK/undo.sh" <<'PY'
import sys
lines = open(sys.argv[1]).read().splitlines()
a = next(i for i, l in enumerate(lines) if 'undo guard begin' in l)
b = next(i for i, l in enumerate(lines) if 'undo guard end' in l)
print('\n'.join(lines[a + 1:b]))
PY
[ -s "$WORK/undo.sh" ] || { echo "FAIL checksh  could not lift the undo guard out of check.sh"; failed=$((failed + 1)); }
grep -q 'die$' "$WORK/undo.sh" || { echo "FAIL checksh  the undo guard no longer calls die, so it cannot refuse"; failed=$((failed + 1)); }

undo_repo() {  # builds $WORK/undo with trunk T (base + 3 commits x 30 lines) and origin/xteink = T
  rm -rf "$WORK/undo"; mkdir -p "$WORK/undo"
  ( cd "$WORK/undo" && git init -q -b xteink && git config user.email t@t && git config user.name t
    seq 1 5 | sed 's/^/base line number /' > a.txt && git add -A && git commit -qm base && git tag base
    for f in f1 f2 f3; do seq 1 30 | sed "s/^/trunk $f added this distinctive line /" > "$f.txt"; git add -A; git commit -qm "add $f"; done
    git update-ref refs/remotes/origin/xteink HEAD )
}
undo_run() {  # echoes fired|silent for HEAD of $WORK/undo; die leaves a marker, since the block's own output is captured
  rm -f "$WORK/undo.fired"
  ( cd "$WORK/undo" && die() { : >"$WORK/undo.fired"; exit 0; } && . "$WORK/undo.sh" >"$WORK/undo.out" 2>&1 )
  [ -e "$WORK/undo.fired" ] && echo fired || echo silent
}
undo_expect() {  # label, fired|silent
  local got; got="$(undo_run)"
  checks=$((checks + 1))
  if [ "$got" != "$2" ]; then failed=$((failed + 1)); echo "FAIL checksh  undo guard: $1: got $got, wanted $2"; sed 's/^/       /' "$WORK/undo.out"; fi
}
undo_repo; ( cd "$WORK/undo" && git checkout -q -b app/ok && echo "my own new file" > mine.txt && git add -A && git commit -qm mine )
undo_expect "an ordinary branch on top of trunk is silent" silent
undo_repo; ( cd "$WORK/undo" && git checkout -q -b app/stale && git read-tree base && git commit -qm "stale tree committed on the moved trunk" >/dev/null )
undo_expect "a stale tree committed on the moved trunk fires" fired
grep -q 'f1.txt (-30)' "$WORK/undo.out" && checks=$((checks + 1)) || { checks=$((checks + 1)); failed=$((failed + 1)); echo "FAIL checksh  undo guard: the refusal does not name the undone files"; }
rm -f "$WORK/undo.fired"; ( cd "$WORK/undo" && export CHECK_ALLOW_UNDO=1 && die() { : >"$WORK/undo.fired"; exit 0; } && . "$WORK/undo.sh" >"$WORK/undo.out" 2>&1 )
[ ! -e "$WORK/undo.fired" ] && checks=$((checks + 1)) || { checks=$((checks + 1)); failed=$((failed + 1)); echo "FAIL checksh  undo guard: CHECK_ALLOW_UNDO=1 did not let a meant revert through"; }
undo_repo; ( cd "$WORK/undo" && git checkout -q -b app/own && seq 1 80 | sed 's/^/my branch added this line and took it back /' > tmp.txt && git add -A && git commit -qm add && git rm -q tmp.txt && git commit -qm remove )
undo_expect "a branch deleting its own additions is silent" silent

# --- a test file its suite never runs -----------------------------------------
#
# The block that lists test files run.sh never names is lifted between its
# markers and run against fixture suite directories: one that names every file,
# one that forgot a file, one that globs test_* and therefore runs whatever is
# there.
python3 - "$CHECK" >"$WORK/unrun.sh" <<'PY'
import sys
lines = open(sys.argv[1]).read().splitlines()
a = next(i for i, l in enumerate(lines) if 'unrun test files begin' in l)
b = next(i for i, l in enumerate(lines) if 'unrun test files end' in l)
print('\n'.join(lines[a + 1:b]))
PY
[ -s "$WORK/unrun.sh" ] || { echo "FAIL checksh  could not lift the unrun-test-file block out of check.sh"; failed=$((failed + 1)); }
unrun_case() {  # label, run.sh body, wanted unrun value (space-led names, or empty)
  local d="$WORK/suite"; rm -rf "$d"; mkdir -p "$d"; printf '%s\n' "$2" >"$d/run.sh"; : >"$d/test_a.py"; : >"$d/test_b.cpp"
  local got; got="$(suite="$d" bash -c '. "'"$WORK"'/unrun.sh"; printf "%s" "$unrun"')"
  checks=$((checks + 1))
  if [ "$got" != "$3" ]; then failed=$((failed + 1)); echo "FAIL checksh  unrun test files: $1: got '$got', wanted '$3'"; fi
}
unrun_case "every test file named is clean"            'python3 test_a.py && g++ test_b.cpp -o t && ./t' ""
unrun_case "a file run.sh never names is reported"     'python3 test_a.py' " test_b.cpp"
unrun_case "a run.sh that globs test_* runs everything" 'for f in test_*.py; do python3 "$f"; done; for c in test_*.cpp; do g++ "$c"; done' ""

# --- the GCC suite list is derived, and a CXX-blind suite is a failure --------
#
# Lifted between its markers and run in a fixture tree of four suites: one that
# compiles src/ under -Werror through ${CXX:-c++} (in), one that does the same
# but hard-codes clang++ (blind), one without -Werror (out), one with -Werror on
# its own test sources only (out).
python3 - "$CHECK" >"$WORK/gccsuites.sh" <<'PY'
import sys
lines = open(sys.argv[1]).read().splitlines()
a = next(i for i, l in enumerate(lines) if 'gcc suites begin' in l)
b = next(i for i, l in enumerate(lines) if 'gcc suites end' in l)
print('\n'.join(lines[a + 1:b]))
PY
[ -s "$WORK/gccsuites.sh" ] || { echo "FAIL checksh  could not lift the gcc-suites block out of check.sh"; failed=$((failed + 1)); }
# The fixture's words are assembled from parts so that THIS file does not
# itself match the discovery (it would: the block reads run.sh files for the
# literal flag and path, and this suite's run.sh would then be listed as one
# that compiles app sources, which it does not).
GT="$WORK/gcctree"; rm -rf "$GT"; mkdir -p "$GT/host-tests"/{inlist,blind,nowerror,ownonly,viavar,talks}
werr="-W""error"; app="../../s""rc/apps_local/x/X.cpp"; inc="-I ../../s""rc"; cxx='${CXX:-c++}'
printf '#!/bin/bash\n%s -std=c++17 %s %s %s test.cpp -o t && ./t\n' "$cxx" "$werr" "$inc" "$app" >"$GT/host-tests/inlist/run.sh"
printf '#!/bin/bash\nclang++ -std=c++17 %s %s %s test.cpp -o t && ./t\n' "$werr" "$inc" "$app" >"$GT/host-tests/blind/run.sh"
printf '#!/bin/bash\n%s -std=c++17 %s %s test.cpp -o t && ./t\n' "$cxx" "$inc" "$app" >"$GT/host-tests/nowerror/run.sh"
printf '#!/bin/bash\n%s -std=c++17 %s test.cpp -o t && ./t\n' "$cxx" "$werr" >"$GT/host-tests/ownonly/run.sh"
# through the variables the real suites use for their sources (in), and a
# suite whose only mention of both is a comment (out)
printf '#!/bin/bash\nLIB=../../l""ib\n%s -std=c++17 %s -I"$LIB/GfxRenderer" "$LIB/GfxRenderer/Paint.cpp" test.cpp -o t && ./t\n' "$cxx" "$werr" >"$GT/host-tests/viavar/run.sh"
printf '#!/bin/bash\n# this suite once compiled %s under %s and no longer does\n%s -std=c++17 test.cpp -o t && ./t\n' "$app" "$werr" "$cxx" >"$GT/host-tests/talks/run.sh"
gcc_got="$(cd "$GT" && bash -c '. "'"$WORK"'/gccsuites.sh"; printf "in=%s|blind=%s" "$(echo $gcc_suites)" "$(echo $gcc_blind)"')"
checks=$((checks + 1))
[ "$gcc_got" = "in=inlist viavar|blind=blind" ] && : || { failed=$((failed + 1)); echo "FAIL checksh  gcc suites: got '$gcc_got', wanted 'in=inlist viavar|blind=blind'"; }
checks=$((checks + 1))
grep -q 'for gcc_suite in \$gcc_suites' "$CHECK" && : || { failed=$((failed + 1)); echo "FAIL checksh  the GCC loop no longer runs the derived list"; }

# --- one gate per tree -------------------------------------------------------
#
# Lifted between its markers and run with TAG and TMPDIR of this harness: no
# lock proceeds and leaves this pid; a lock held by a living check.sh refuses
# and names the pid; a lock left by a dead pid is taken over.
python3 - "$CHECK" >"$WORK/onegate.sh" <<'PY'
import sys
lines = open(sys.argv[1]).read().splitlines()
a = next(i for i, l in enumerate(lines) if 'one gate per tree begin' in l)
b = next(i for i, l in enumerate(lines) if 'one gate per tree end' in l)
print('\n'.join(lines[a + 1:b]))
PY
[ -s "$WORK/onegate.sh" ] || { echo "FAIL checksh  could not lift the one-gate block out of check.sh"; failed=$((failed + 1)); }
onegate_run() {  # echoes refused|proceeded and leaves $WORK/onegate.out
  rm -f "$WORK/onegate.refused"
  ( cd "$WORK" && unset CHECK_OUTER_LOGS && TAG=onegate TMPDIR="$WORK" && die() { echo "$*" >"$WORK/onegate.refused"; exit 0; } && . "$WORK/onegate.sh" >"$WORK/onegate.out" 2>&1; echo "$$" >"$WORK/onegate.pid" )
  [ -e "$WORK/onegate.refused" ] && echo refused || echo proceeded
}
rm -f "$WORK/xteink-check-onegate.running"
checks=$((checks + 1)); [ "$(onegate_run)" = proceeded ] || { failed=$((failed + 1)); echo "FAIL checksh  one gate: a free tree was refused: $(cat "$WORK/onegate.refused" 2>/dev/null)"; }
# the holder is a living check.sh: a sleeper wearing the name
bash -c 'exec -a check.sh sleep 30' & SLEEPER=$!
sleep 0.2
echo "$SLEEPER" >"$WORK/xteink-check-onegate.running"
checks=$((checks + 1)); [ "$(onegate_run)" = refused ] || { failed=$((failed + 1)); echo "FAIL checksh  one gate: a tree with a living gate was not refused"; }
checks=$((checks + 1)); grep -q "pid $SLEEPER" "$WORK/onegate.refused" 2>/dev/null || { failed=$((failed + 1)); echo "FAIL checksh  one gate: the refusal does not name the holder's pid"; }
kill "$SLEEPER" 2>/dev/null; wait "$SLEEPER" 2>/dev/null
# a dead holder: the sleeper's pid, now gone
echo "$SLEEPER" >"$WORK/xteink-check-onegate.running"
checks=$((checks + 1)); [ "$(onegate_run)" = proceeded ] || { failed=$((failed + 1)); echo "FAIL checksh  one gate: a dead holder's lock was not taken over"; }
# a living pid that is not a check.sh (reused pid) is taken over too
sleep 30 & OTHER=$!
echo "$OTHER" >"$WORK/xteink-check-onegate.running"
checks=$((checks + 1)); [ "$(onegate_run)" = proceeded ] || { failed=$((failed + 1)); echo "FAIL checksh  one gate: a living pid that is not a gate was refused"; }
kill "$OTHER" 2>/dev/null; wait "$OTHER" 2>/dev/null

echo "$checks checks, $failed failed"
[ "$failed" -eq 0 ]

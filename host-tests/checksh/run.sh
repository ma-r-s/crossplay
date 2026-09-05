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

# -- a skip must be VISIBLE, and it must be a FAILURE in CI -------------------
#
# check.sh surfaces a suite's skips with one grep (`grep -E "^SKIP"`), which is
# the entire mechanism by which a check that did not run is distinguished from
# one that passed. host-tests/autorelease printed its skip INDENTED, so that
# anchor never matched it: the suite could skip its only real-repository range
# and say nothing a human would see. It also had no CI guard, unlike release,
# relwatch and boardmigrate, so the same skip was silent on the runner too --
# unguarded and unseen, the two halves that make each other invisible.
#
# Written to DISCOVER rather than to assert: it walks every suite, finds every
# SKIP any of them can print, and holds each against check.sh's own pattern
# lifted out of check.sh. A copy of the pattern here would drift the moment
# somebody tightened the real one.
python3 - "$CHECK" "$HERE/.." <<'SKIPS' >"$WORK/skips"
import os, re, sys

check_src = open(sys.argv[1]).read()
suites = sys.argv[2]

# The detector itself, lifted. If it ever stops existing, everything below is
# checking a contract nothing enforces, so that is the first failure.
m = re.search(r'grep -E "([^"]*SKIP[^"]*)"', check_src)
if not m:
    print("FAIL checksh  check.sh no longer greps a suite's log for SKIP, so nothing "
          "surfaces a check that did not run -- and every assertion below would pass "
          "by having no contract to test")
    raise SystemExit(0)
anchor = m.group(1)
pattern = re.compile(anchor)
print("ok    check.sh surfaces skips with %s" % anchor)

# What a suite can print as a skip: the text of an echo whose message begins
# with SKIP. That deliberately does not match "FAIL ... SKIPPED ..." messages,
# which are failures already, or the word inside a grep.
echoed = re.compile(r'echo\s+"([^"]*)"')
is_skip = re.compile(r'\s*SKIP\b')

found = False
for name in sorted(os.listdir(suites)):
    run = os.path.join(suites, name, "run.sh")
    if not os.path.isfile(run):
        continue
    src = open(run).read()
    msgs = [t for t in echoed.findall(src) if is_skip.match(t)]
    if not msgs:
        continue
    found = True
    for t in msgs:
        if pattern.search(t):
            print("ok    %s: %r is visible to check.sh" % (name, t))
        else:
            print("FAIL checksh  host-tests/%s prints %r, which check.sh's own %s does "
                  "not match: that suite can skip and NOTHING says so" % (name, t, anchor))
    # Crude on purpose -- one mention of ${CI:-} anywhere in the file, rather
    # than an attempt to decide statically which branch it guards. A suite that
    # can skip and never mentions CI cannot be turning a skip into a failure
    # there, which is the shape this is looking for.
    if "${CI:-}" in src:
        print("ok    %s: it knows about CI" % name)
    else:
        print("FAIL checksh  host-tests/%s can print a SKIP and never mentions ${CI:-}: "
              "on the runner, where every input is meant to be present, the check does "
              "not run and the suite still exits 0" % name)

if not found:
    print("FAIL checksh  no suite prints a SKIP at all; this just checked nothing")
SKIPS

while IFS= read -r line; do
  checks=$((checks + 1))
  case "$line" in FAIL*) failed=$((failed + 1)); echo "$line" ;; esac
done < "$WORK/skips"

echo "$checks checks, $failed failed"
[ "$failed" -eq 0 ]

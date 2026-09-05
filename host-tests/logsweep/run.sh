#!/bin/bash
# check.sh's log directory ("$TMPDIR/xteink-check-$TAG") is one dir per tree
# path, reused forever, and until card #144 nothing removed it: 503 dirs / 5.1GB
# had accumulated in one TMPDIR, one per worktree that had ever run the gate,
# the oldest from a tree deleted a week earlier (#210 corrected the count; the
# defect is the same).
#
# Two mechanisms fix it, and both are exercised here against the REAL scripts:
#
#   1. scripts_local/log-sweep.sh removes SIBLING dirs no running gate could
#      still own. The safety property is the whole point -- several trees run
#      check.sh at once, and the sweep must NEVER delete a dir a live gate is
#      writing into. The freshness signal is the newest mtime in the SUBTREE,
#      not the directory's own mtime, because a directory's mtime stops
#      advancing the moment a run finishes adding NEW files even while it is
#      still appending to existing logs and writing objects deep under
#      cmake-build. The "deep-live" case below ages the whole tree and then
#      touches ONE deep file: a dir-mtime implementation deletes it, a
#      subtree-mtime implementation keeps it. That case is the reason the file
#      is written the way it is.
#
#   2. check.sh drops THIS run's own dir on a GREEN verdict, and keeps it on a
#      failure (a failed run's logs are what you debug with). That is lifted out
#      of check.sh and run, rather than asserted against its text, so a reword
#      that quietly moves the rm out of the green branch turns this red.
#
#   host-tests/logsweep/run.sh
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SWEEP="$HERE/../../scripts_local/log-sweep.sh"
CHECK="$HERE/../../scripts_local/check.sh"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

[ -f "$SWEEP" ] || { echo "FAIL cannot find $SWEEP"; echo "1 checks, 1 failed"; exit 1; }
[ -f "$CHECK" ] || { echo "FAIL cannot find $CHECK"; echo "1 checks, 1 failed"; exit 1; }

PASS=0; FAIL=0
ok()  { PASS=$((PASS+1)); echo "  ok   $1"; }
bad() { FAIL=$((FAIL+1)); echo "  FAIL $1"; }

# A base that is NOT the real TMPDIR: this test creates and deletes xteink-check-*
# dirs, and doing that in the real TMPDIR would race and delete live gates'
# directories -- the exact bug under test, committed by its own test.
BASE="$WORK/tmpdir"
mkdir -p "$BASE"

# age everything under $1 to Jan 2025, dir and contents both.
age() { find "$1" -exec touch -t 202501010000 {} + 2>/dev/null || true; }

# --- fixture -------------------------------------------------------------
# aged: a normal finished run, whole subtree old -> must be swept.
mkdir -p "$BASE/xteink-check-aged/cmake-build/sub"
echo log  > "$BASE/xteink-check-aged/ui.log"
echo obj  > "$BASE/xteink-check-aged/cmake-build/sub/a.o"
age "$BASE/xteink-check-aged"

# deep-live: a gate mid-build. Everything is old EXCEPT one file deep under
# cmake-build, and the directory's OWN mtime is old. Must be KEPT.
mkdir -p "$BASE/xteink-check-deeplive/cmake-build"
echo log > "$BASE/xteink-check-deeplive/ui.log"
echo obj > "$BASE/xteink-check-deeplive/cmake-build/live.o"
age "$BASE/xteink-check-deeplive"
touch "$BASE/xteink-check-deeplive/cmake-build/live.o"   # now: one fresh deep file

# fresh: a run that just wrote its logs -> must be KEPT.
mkdir -p "$BASE/xteink-check-fresh"
echo log > "$BASE/xteink-check-fresh/ui.log"

# self: this run's OWN dir, aged, passed as self -> must be KEPT despite age.
mkdir -p "$BASE/xteink-check-self"
echo log > "$BASE/xteink-check-self/ui.log"
age "$BASE/xteink-check-self"

# empty-aged: an interrupted run that made the dir and wrote nothing -> swept.
mkdir -p "$BASE/xteink-check-empty"
touch -t 202501010000 "$BASE/xteink-check-empty"

SELF="$BASE/xteink-check-self"

echo "log-sweep: stale sibling sweep"

# Drive the REAL function, sourced, exactly as check.sh sources it.
# shellcheck source=scripts_local/log-sweep.sh
. "$SWEEP"
OUT="$(log_sweep_prune_stale "$BASE" "$SELF" 24 2>&1)"

[ ! -d "$BASE/xteink-check-aged" ]  && ok "sweeps a finished run whose subtree is old" \
                                    || bad "kept an aged dir: it will accumulate forever"
[ -d "$BASE/xteink-check-deeplive" ] && ok "KEEPS a mid-build gate (fresh file deep under an old dir)" \
                                    || bad "deleted a live gate's log dir -- read subtree mtime, not the dir's own"
[ -d "$BASE/xteink-check-fresh" ]   && ok "keeps a just-written run" \
                                    || bad "deleted a fresh dir"
[ -d "$BASE/xteink-check-self" ]    && ok "keeps this run's own dir even when aged" \
                                    || bad "swept self: check.sh would lose its live log dir"
[ ! -d "$BASE/xteink-check-empty" ] && ok "sweeps an empty aged dir" \
                                    || bad "kept an empty aged dir"
case "$OUT" in
  *"swept 2 stale"*) ok "reports what it removed (2 dirs)" ;;
  *) bad "sweep summary wrong or missing: $OUT" ;;
esac

# A second sweep with nothing stale left must remove nothing and stay silent.
OUT2="$(log_sweep_prune_stale "$BASE" "$SELF" 24 2>&1)"
[ -z "$OUT2" ] && ok "a sweep that removes nothing says nothing" \
              || bad "sweep spoke with nothing to remove: $OUT2"

# The window boundary must be honoured, not just "old vs now". Stamp two
# siblings either side of a 1h window: one 3h idle (past it -> swept), one 10min
# idle (inside it -> kept). Timestamps are chosen well clear of touch -t's
# one-second granularity so the boundary, not rounding, is what is under test.
age_min() { # $1 dir, $2 minutes ago
  if date --version >/dev/null 2>&1; then _st="$(date -d "-$2 min" +%Y%m%d%H%M.%S)"
  else _st="$(date -v-"$2"M +%Y%m%d%H%M.%S)"; fi
  find "$1" -exec touch -t "$_st" {} + 2>/dev/null || true
}
mkdir -p "$BASE/xteink-check-oldish"; echo log > "$BASE/xteink-check-oldish/ui.log"; age_min "$BASE/xteink-check-oldish" 180
mkdir -p "$BASE/xteink-check-newish"; echo log > "$BASE/xteink-check-newish/ui.log"; age_min "$BASE/xteink-check-newish" 10
log_sweep_prune_stale "$BASE" "$SELF" 1 >/dev/null 2>&1
[ ! -d "$BASE/xteink-check-oldish" ] && ok "a 1h window sweeps a sibling idle 3h" \
                                     || bad "window not honoured: a 3h-idle sibling survived a 1h window"
[ -d "$BASE/xteink-check-newish" ]   && ok "a 1h window keeps a sibling idle 10min" \
                                     || bad "window too aggressive: a 10min-idle sibling was swept at 1h"
[ -d "$BASE/xteink-check-self" ]     && ok "and STILL spares self across windows" \
                                     || bad "self was swept once the window shrank"

echo "check.sh: green drops everything but its transcript, failure keeps it all"

# Lift the verdict tail (the outer FAILED==0 block, which contains the green
# rm) out of check.sh and run it, the way host-tests/checksh lifts the gate.
python3 - "$CHECK" > "$WORK/verdicttail.sh" 2>/dev/null <<'PY'
import sys
lines = open(sys.argv[1]).read().splitlines()
start = next(i for i, l in enumerate(lines)
             if l.strip() == 'if [ "$FAILED" -eq 0 ]; then')
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
    raise SystemExit('unbalanced')
print("\n".join(lines[start:end + 1]))
PY

if [ ! -s "$WORK/verdicttail.sh" ]; then
  bad "could not lift the verdict tail out of check.sh"
else
  # sanity: the lifted block must actually carry the cleanup it exists to test.
  # Named on $LOGS rather than on one spelling of it: card #314 turned the
  # single `rm -rf "$LOGS"` into a find that keeps one file back, and an
  # extractor that recognises only yesterday's spelling turns tomorrow's
  # regression into "could not extract" instead of a failing assertion.
  grep -q '"\$LOGS"' "$WORK/verdicttail.sh" \
    && ok "the lifted verdict tail still cleans up \$LOGS" \
    || bad "the lifted verdict tail no longer touches \$LOGS -- the green cleanup left the block"

  run_tail() { # $1 FAILED value, $2 LOGS dir, $3 transcript basename
    ( set +e
      qualifier_text() { echo ""; }   # stub: the real one reads git state
      DEVICE_BUILDS_SKIPPED=""
      FAILED="$1"; LOGS="$2"; CHECK_RUNLOG="$2/$3"
      . "$WORK/verdicttail.sh" ) >/dev/null 2>&1
  }

  # THE GREEN CLEANUP, card #144 as amended by card #314.
  #
  # #144: a passing run's logs record only passing suites, so a green verdict
  # drops them -- that is what stops 503 directories and 5.1GB piling up in
  # TMPDIR. #314 keeps exactly ONE file back: the transcript, whose path this
  # run PRINTED on its first line for somebody else to read. Deleting the file
  # you told a reader to poll is the same silent lie as another session
  # overwriting it, which is the bug #314 is about.
  #
  # So both halves are asserted, and the second one is the interesting one: the
  # bytes must still go.
  # A pid that has certainly exited, and one that certainly has not: the
  # cleanup must tell them apart, because $LOGS is shared per tree and a green
  # run finishing while a sibling compiles is a real sequence, not a contrived
  # one.
  DEADPID=$(bash -c 'echo $$')
  sleep 30 & LIVEPID=$!
  G="$BASE/greenlogs"; mkdir -p "$G/cmake-build.$DEADPID" "$G/cmake-build.$LIVEPID"
  echo x > "$G/ui.log"; echo x > "$G/cmake-build.$DEADPID/a.o"
  echo x > "$G/cmake-build.$LIVEPID/a.o"
  echo x > "$G/cmake.$DEADPID.log"; echo x > "$G/cmake.$LIVEPID.log"
  echo t > "$G/run.mine"; echo t > "$G/run.sibling"
  echo old > "$G/run.ancient"; touch -t 202501010000 "$G/run.ancient"
  run_tail 0 "$G" run.mine

  [ ! -f "$G/ui.log" ] && ok "a green verdict drops this run's suite logs" \
                      || bad "green run kept its suite logs (card #144 unfixed)"
  [ ! -d "$G/cmake-build.$DEADPID" ] && ok "and a finished run's build tree, which is where the bytes are" \
                                    || bad "green run kept a whole cmake build tree; this is the 5.1GB"
  [ ! -f "$G/cmake.$DEADPID.log" ] && ok "and that run's cmake log with it" \
                                  || bad "green run kept a dead run's cmake log"
  # The other direction, and it is the one that corrupts somebody: this run
  # going green while a SIBLING is still compiling must not delete the
  # sibling's build directory. That is card #320's failure caused by the
  # cleanup instead of by the build.
  [ -d "$G/cmake-build.$LIVEPID" ] && ok "but SPARES a live sibling's build tree" \
                                  || bad "the green cleanup deleted a concurrently running gate's build directory"
  [ -f "$G/cmake.$LIVEPID.log" ] && ok "and the log it is still writing" \
                                || bad "the green cleanup deleted a live sibling's cmake log"
  [ -f "$G/run.sibling" ] && ok "and another run's transcript, which may still be open" \
                         || bad "the green cleanup deleted a sibling run's transcript"
  [ -f "$G/run.mine" ] && ok "but KEEPS this run's transcript, the one path it published" \
                      || bad "green run deleted the transcript it printed on its own first line (card #314)"
  # Transcripts are text and tiny, but "tiny" is not "bounded": one per run,
  # forever, is still growth. They age out on the same window log-sweep.sh uses
  # for whole directories.
  [ ! -f "$G/run.ancient" ] && ok "and prunes transcripts older than the sweep window" \
                           || bad "old transcripts accumulate forever; the count grows per run again"
  kill "$LIVEPID" 2>/dev/null; wait "$LIVEPID" 2>/dev/null

  Fd="$BASE/faillogs"; mkdir -p "$Fd"; echo x > "$Fd/ui.log"; echo t > "$Fd/run.mine"
  run_tail 1 "$Fd" run.mine
  [ -d "$Fd" ] && ok "a failing run KEEPS its log dir for debugging" \
              || bad "a failure deleted its own logs -- nothing left to debug"
  [ -f "$Fd/ui.log" ] && ok "including the suite logs that say what broke" \
                     || bad "a failure kept the directory and dropped the logs in it"
fi

echo "check.sh: --committed reuses THIS tree's log dir instead of minting one per run"

# The leak the sweep was built to mop up had a source, and it was --committed.
#
# That mode builds a throwaway worktree at "xteink-committed-$TAG-$$" and
# re-invokes check.sh inside it. The inner run derives its own TAG from ITS
# $REPO -- the trial path -- which embeds the pid, so every --committed run
# named a log directory no later run would ever reuse. The outer's EXIT trap
# then removed the trial worktree, orphaning the directory by construction: the
# green rm could collect it only if that particular run went green, and nothing
# else ever would except the 24h sweep. Measured 2026-09-05, after the green rm
# and the sweep had both landed: 234 dirs / 4.4GB, essentially one day's runs.
#
# The fix carries the OUTER log path across the boundary, so the trial run
# writes into the directory keyed on the user's real tree. This asserts that
# property against the REAL assignment lifted out of check.sh, rather than the
# text of it: what matters is where a trial run's LOGS actually resolves to.

# Lift the TAG and LOGS assignments. Anchored on the assignment being tested,
# not on its spelling, so a reworded default still extracts.
python3 - "$CHECK" > "$WORK/logspath.sh" 2>/dev/null <<'PY'
import sys
lines = open(sys.argv[1]).read().splitlines()
keep = [l for l in lines
        if l.startswith('TAG=') or (l.startswith('LOGS=') and 'xteink-check-' in l)]
if len(keep) != 2:
    raise SystemExit('expected one TAG= and one LOGS= assignment, got %d' % len(keep))
print("\n".join(keep))
PY

if [ ! -s "$WORK/logspath.sh" ]; then
  bad "could not lift the TAG/LOGS assignments out of check.sh"
else
  # $1 = REPO. CHECK_OUTER_LOGS is inherited from the caller's environment,
  # exactly as the real inner run inherits it across the subshell boundary.
  logs_for() ( set +e; REPO="$1"; TMPDIR="$BASE"; . "$WORK/logspath.sh"; printf '%s\n' "$LOGS" )

  OUTER_TREE="/Users/someone/Xteink/wt/somework"
  # Two different trial paths: the SAME tree, checked twice, as two runs with
  # different pids. This is the pair that used to produce two directories.
  TRIAL_A="$BASE/xteink-committed-deadbeef-1111"
  TRIAL_B="$BASE/xteink-committed-deadbeef-2222"

  outer="$(unset CHECK_OUTER_LOGS; logs_for "$OUTER_TREE")"

  # Control: without the carry, distinct REPO paths MUST give distinct dirs.
  # Without this, an implementation that hardcoded one path would pass below.
  a_alone="$(unset CHECK_OUTER_LOGS; logs_for "$TRIAL_A")"
  b_alone="$(unset CHECK_OUTER_LOGS; logs_for "$TRIAL_B")"
  { [ "$a_alone" != "$b_alone" ] && [ "$a_alone" != "$outer" ]; } \
    && ok "control: two trial paths key two different dirs when nothing is carried" \
    || bad "control failed: the log path does not vary with \$REPO, so the test below proves nothing"

  # The property. Both runs must land in the OUTER tree's directory.
  a="$(CHECK_OUTER_LOGS="$outer" logs_for "$TRIAL_A")"
  b="$(CHECK_OUTER_LOGS="$outer" logs_for "$TRIAL_B")"
  [ "$a" = "$outer" ] \
    && ok "a trial run writes into the outer tree's log dir, not its own" \
    || bad "a trial run minted its own log dir ($a), so every --committed run leaks one"
  [ "$a" = "$b" ] \
    && ok "two --committed runs of one tree share ONE dir (count cannot grow per run)" \
    || bad "two --committed runs keyed two dirs -- the per-run leak is still open"

  # And the carry must actually happen: the export has to sit inside the
  # --committed block, BEFORE the line that invokes the trial run. Exported
  # after it, or dropped, the assignment above falls back to the per-pid path
  # and nothing else in this suite would notice.
  python3 - "$CHECK" > "$WORK/committed-block.sh" 2>/dev/null <<'PY'
import sys
lines = open(sys.argv[1]).read().splitlines()
start = next(i for i, l in enumerate(lines) if l.strip().startswith('TRIAL='))
end = next(i for i in range(start, len(lines))
           if 'cd "$TRIAL"' in lines[i] and 'check.sh' in lines[i])
print("\n".join(lines[start:end]))
PY
  if [ ! -s "$WORK/committed-block.sh" ]; then
    bad "could not lift the --committed block out of check.sh"
  else
    grep -q 'export CHECK_OUTER_LOGS=' "$WORK/committed-block.sh" \
      && ok "--committed exports the log dir before invoking the trial run" \
      || bad "--committed never exports CHECK_OUTER_LOGS, so the trial run keys its own dir"
  fi
fi

echo "$((PASS+FAIL)) checks, $FAIL failed"
[ "$FAIL" -eq 0 ]

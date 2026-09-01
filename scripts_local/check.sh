#!/bin/bash
# Everything that can be verified without a device. Run before every commit.
#
#   ./scripts_local/check.sh              # host tests, both builds
#   ./scripts_local/check.sh --tests      # host tests only (fast)
#   ./scripts_local/check.sh --committed  # verify HEAD, not your working tree
#
# Exits non-zero if anything fails. Prints every suite's own exit code rather
# than only its last line: a suite that fails to compile still prints "0 failed"
# for the sub-suites that ran before it, and reading only that is how a green
# report once covered a suite whose source was not even present.
set -uo pipefail

REPO="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
cd "$REPO"

# Per tree. Several trees run this at once now, and one shared log directory
# meant the failure you were reading could be another tree's.
# The branch the site deploys from. Named rather than spelled inline because
# the browser-artifact gate at the foot of this file is the only check that is
# branch-conditional, and a constant buried in one `if` is a constant nobody
# can test against.
DEPLOY_BRANCH="${DEPLOY_BRANCH:-xteink}"

TAG="$(printf '%s' "$REPO" | shasum | cut -c1-8)"
LOGS="${TMPDIR:-/tmp}/xteink-check-$TAG"
mkdir -p "$LOGS"
FAILED=0
SUBMODULE_DRIFT=""
TREE_STALE=""

# --ignore-submodules=untracked: the icon tools drop a __pycache__/ inside
# freeink-sdk, which is untracked content in a submodule and not your work.
dirty_count() {
  git status --porcelain --ignore-submodules=untracked | grep -vc '^??' || true
}

# check.sh verifies the WORKING DIRECTORY, and that is a real hazard, not a
# footnote. Uncommitted work masks a broken commit: Dungeon, Insider and Hacker
# News all shipped depending on Toybox symbols that were never committed, every
# check ran green because they sat unstaged, and xteink HEAD did not compile for
# three commits. --committed is the answer; this banner is so you know to reach
# for it.
if [ "${1:-}" = "--committed" ]; then
  # One trial directory PER RUN ($$), never shared. It used to be per tree,
  # which serialised nothing: sessions never close here, and two of them
  # verifying the same tree -- the integration tree, at release time, which is
  # exactly when the result matters -- each began by force-removing "the"
  # trial path, i.e. the other run's live worktree. On 2026-08-25 two
  # concurrent runs both died in the submodule step with the directory deleted
  # under them, and neither failure said anything about the code.
  TRIAL="${TMPDIR:-/tmp}/xteink-committed-$TAG-$$"
  echo "verifying HEAD ($(git rev-parse --short HEAD)) in a throwaway worktree"
  TRIAL_T0=$(date +%s)
  # Refresh the remote ref HERE, in the tree that owns it: the freshness check
  # runs inside the trial worktree, which shares these refs, and measuring
  # staleness against a stale ref is the joke telling itself. --committed is
  # the one mode worth a network round trip, because it is the mode run when
  # the result is about to be trusted. Never fatal: no network is a reason to
  # gate a little blind, not a reason not to gate.
  if ! git fetch --quiet origin 2>/dev/null; then
    echo "  could not fetch origin; staleness below is measured against the ref as it stands"
  fi
  # Unique paths mean a run killed hard (kill -9 skips the trap below) leaves
  # an orphan that no later run would inherit, so sweep siblings whose owning
  # process is gone: ~600MB each, and a half-populated worktree is a state
  # nobody can reason about. An old-format dir (no pid suffix) fails the
  # liveness probe and is swept with them.
  for stale in "${TMPDIR:-/tmp}/xteink-committed-$TAG"*; do
    [ -e "$stale" ] || continue
    [ "$stale" = "$TRIAL" ] && continue
    if ! kill -0 "${stale##*-}" 2>/dev/null; then
      git worktree remove --force "$stale" 2>/dev/null
      rm -rf "$stale"
    fi
  done
  git worktree prune 2>/dev/null
  # PID reuse can hand this run a dead predecessor's exact path. It cannot be
  # a live run's (the pid is ours), so clearing it is safe.
  if [ -e "$TRIAL" ]; then
    git worktree remove --force "$TRIAL" 2>/dev/null
    rm -rf "$TRIAL"
    git worktree prune 2>/dev/null
  fi
  echo "  your working tree is untouched, and its uncommitted work is not in this build"
  git worktree add --quiet --detach "$TRIAL" HEAD || exit 1
  # Clean up even when this run is interrupted. A killed --committed used to
  # leave ~600MB registered in TMPDIR until the same tree ran it again, and
  # long builds get killed on purpose here, so that is the normal case rather
  # than the rare one. One was found orphaned from a run that died mid-build.
  trap 'git worktree remove --force "$TRIAL" 2>/dev/null; git worktree prune 2>/dev/null' EXIT INT TERM
  # A fresh worktree does not populate submodules, and the host tests compile
  # FreeInkUI out of freeink-sdk/.
  #
  # alternateLocation=superproject makes this read the objects out of THIS
  # repository instead of cloning them again from GitHub. Without it every
  # --committed run re-downloaded freeink-sdk and, nested inside it, the whole
  # lucide icon repository. That is why this mode felt slow all along, and on
  # 2026-08-11 one of those fetches died mid-stream and git sat retrying for
  # twenty-five minutes with nothing compiling:
  #
  #   error: RPC failed; curl 92 HTTP/2 stream 5 was not closed cleanly
  #   Failed to clone 'libs/assets/Icons/lucide'. Retry scheduled
  #
  # The objects are already on this disk. Fetching them over the network to
  # verify a local commit was never necessary.
  #
  # NOT --recursive. The only nested submodule is the lucide icon repository,
  # thousands of SVGs that nothing here compiles: FreeInkUIIcon.h is generated
  # from them and committed, and the only mention of the path in the whole tree
  # is the comment saying how to regenerate. Pulling it cost 237s of the 244.
  # If a nested submodule ever does become load-bearing, this build breaks
  # loudly rather than silently, which is the right way round.
  if ! SUBMODULE_LOG=$(git -C "$TRIAL" \
        -c submodule.alternateLocation=superproject \
        -c submodule.alternateErrorStrategy=info \
        submodule update --init --quiet 2>&1); then
    echo "  submodules FAILED after $(( $(date +%s) - TRIAL_T0 ))s"
    echo "$SUBMODULE_LOG" | tail -5
    exit 1
  fi
  echo "  worktree and submodules ready ($(( $(date +%s) - TRIAL_T0 ))s)"
  # The trial worktree is detached, so `git branch --show-current` is empty
  # inside it, and the browser-artifact gate at the foot of this file -- the
  # only branch-conditional check here -- switched itself off in the one mode
  # you run precisely because you are about to rely on the result. Carry the
  # real branch across the boundary.
  export CHECK_OUTER_BRANCH="$(git branch --show-current 2>/dev/null)"
  # The trial worktree sits in TMPDIR, outside the workspace, so the installer
  # suite's venv lookup cannot climb to it. Resolve the venv HERE and carry it
  # across, or --committed skips those tests in the one mode you run because
  # you are about to rely on the result -- the same trap CHECK_OUTER_BRANCH
  # exists for.
  for candidate in "$REPO/.venv-study/bin/python" "$REPO/../../firmware-next/.venv-study/bin/python"; do
    if [ -x "$candidate" ]; then
      export CHECK_OUTER_STUDY_PY="$(cd "$(dirname "$candidate")" && pwd)/python"
      break
    fi
  done
  # Carry the shared object cache into the trial run.
  #
  # The trial worktree lives in /var/folders, and the cache is found by walking
  # UP from $REPO looking for the .xteink-workspace marker -- which from there
  # never reaches the workspace root. So --committed silently did a COLD ESP32
  # build every time, ignoring a 28GB cache that exists for exactly this, and
  # that is why verifying before a push cost minutes rather than seconds.
  #
  # Exported here, before the trial runs, because the inner check.sh only sets
  # this when it finds the marker itself: not finding it, it leaves whatever it
  # inherited, so this wins.
  WS_OUTER="$REPO"
  while [ "$WS_OUTER" != "/" ] && [ ! -e "$WS_OUTER/.xteink-workspace" ]; do
    WS_OUTER="$(dirname "$WS_OUTER")"
  done
  if [ -e "$WS_OUTER/.xteink-workspace" ]; then
    export PLATFORMIO_BUILD_CACHE_DIR="$WS_OUTER/.pio-cache"
    echo "  sharing the object cache at $WS_OUTER/.pio-cache"
  else
    echo "  no workspace marker found; this will be a cold build"
  fi
  # The envs that SHIP are not the envs anyone builds. x4pro and sticky define
  # CROSSPOINT_DEV_SERIAL_BRIDGE, so the release-only branches of main.cpp are
  # preprocessed away in every routine build -- and gh_release_* is compiled
  # for the first time by the release workflow, AFTER the tag exists. A typo
  # there costs a delete-and-retag. --committed is the mode you run because you
  # are about to rely on the result, so it builds them.
  export CHECK_BUILD_RELEASE_ENVS=1
  (cd "$TRIAL" && ./scripts_local/check.sh "${2:-}")
  exit $?
fi

# The link suite is real UDP on real loopback, and its port range IS its
# discovery mechanism: every radio in 45700..45707 finds every other one. Two
# runs checking at once therefore join each other's match, and the suite fails
# on whichever assertion the stray datagram happened to break -- three
# concurrent runs failed on three different lines and all three passed alone.
# One 16-port slice per tree for direct runs; committed runs get one per RUN,
# because the inner computes it from the trial path, which embeds the pid.
# This sits below the --committed block on purpose: a committed outer must not
# export its slice to the inner run, or two committed runs of the same tree
# would share one slice and their link suites would join each other's match.
if [ -z "${LINKPLAY_BASE_PORT:-}" ]; then
  SLICE=$(( 0x${TAG:0:4} % 900 ))
  export LINKPLAY_BASE_PORT=$(( 46000 + SLICE * 16 ))
fi

# The repo ships hooks in .githooks -- a pre-commit formatter and a pre-push
# guard against publishing another session's unpushed release. Git only runs
# them when core.hooksPath points there, and that is a per-clone step nobody
# on this machine had done, so both sat inert while everyone assumed they were
# covered. A guard nobody enabled is not a guard; say so where it will be read.
if [ -d "$REPO/.githooks" ] && [ -z "$(git -C "$REPO" config core.hooksPath || true)" ]; then
  echo "note: .githooks exists but core.hooksPath is unset, so NO repo hook runs here."
  echo "      enable per clone with: git config core.hooksPath .githooks"
  echo
fi

# THE ORDER OF THE NEXT THREE BLOCKS IS NOT ARBITRARY: each one decides whether
# the next is worth asking. A conflicted tree makes the submodule and freshness
# answers meaningless; a drifted submodule makes freshness meaningless, because
# a tree building an SDK its commit does not name is not describing any commit
# to be behind or current WITH. So: merge state, then submodules, then
# freshness. Adding a fourth means deciding what it invalidates and what
# invalidates it, not appending to the end.
#
# Before anything else: a tree mid-conflict cannot report anything meaningful,
# and the suites will not notice. On 2026-08-31 a merge conflicted, the failure
# was swallowed by `git merge ... | tail -2` (which reports tail's status), and
# the gate printed "all green" over conflict markers sitting in platformio.ini
# -- a file --tests never parses. The suites were honestly green. Nothing read
# the broken file. See scripts_local/merge_state.sh.
MERGE_STATE="$REPO/scripts_local/merge_state.sh"
if [ -x "$MERGE_STATE" ]; then
  MERGE_OUT="$("$MERGE_STATE" "$REPO" 2>&1)" && MERGE_RC=0 || MERGE_RC=$?
  if [ -n "$MERGE_OUT" ]; then
    echo "$MERGE_OUT" | sed 's/^/  /'
    echo
  fi
  if [ "$MERGE_RC" -ne 0 ]; then
    echo "refusing to gate a tree that is mid-conflict."
    exit 1
  fi
fi

# A tree can be checked out at submodules its own commit does not describe, and
# then every suite and every build passes while describing nothing. See
# scripts_local/submodule_state.sh. Uninitialised stops the gate here rather
# than twenty minutes later inside a compiler error naming no file of ours;
# drift is allowed to run (an SDK bump in progress is legitimate work) but
# QUALIFIES THE VERDICT, because a note above a green line is a note that gets
# read past -- which is how this arrived.
SUB_STATE="$REPO/scripts_local/submodule_state.sh"
if [ -x "$SUB_STATE" ]; then
  SUB_OUT="$("$SUB_STATE" "$REPO" 2>&1)" && SUB_RC=0 || SUB_RC=$?
  if [ -n "$SUB_OUT" ]; then
    echo "$SUB_OUT" | sed 's/^/  /'
    echo
  fi
  case "$SUB_RC" in
    2|4)
      echo "refusing to gate a tree whose submodules are not the ones it describes."
      exit 1
      ;;
    3) SUBMODULE_DRIFT=1 ;;
  esac
fi

# And a tree can be at the submodules its commit describes while the COMMIT is
# not what the branch describes. Same ending: a green that describes nothing.
# See scripts_local/tree_freshness.sh. Fetch only under --committed, the mode
# run precisely because the result is about to be trusted; --tests runs too
# often to put a network round trip in front of it.
FRESH="$REPO/scripts_local/tree_freshness.sh"
if [ -x "$FRESH" ]; then
  # No --fetch here: under --committed the OUTER run has already fetched (the
  # trial worktree shares this repository's refs), and every other mode
  # deliberately reads the ref as it stands rather than putting a network round
  # trip in front of a host suite.
  FRESH_OUT="$("$FRESH" "$REPO" 2>&1)" && FRESH_RC=0 || FRESH_RC=$?
  if [ -n "$FRESH_OUT" ]; then
    echo "$FRESH_OUT" | sed 's/^/  /'
    echo
  fi
  case "$FRESH_RC" in
    3) TREE_STALE="behind origin" ;;
    4) TREE_STALE="behind origin ON FIRMWARE" ;;
  esac
fi

DIRTY="$(dirty_count)"
if [ "$DIRTY" -ne 0 ]; then
  echo "note: verifying your working tree, which has $DIRTY uncommitted file(s)."
  echo "      a green result here does NOT mean HEAD compiles."
  echo "      use --committed before you rely on that."
  echo
fi

# Every stage says what it is STARTING and how long the last one took.
#
# This used to print only on completion, so a slow suite or a slow build was
# indistinguishable from a hang: on 2026-08-11 Mario twice had to ask why
# nothing was happening, once when a submodule clone really had died and once
# when the suites were running perfectly well. Neither of us could tell from
# the log, and "I had to ask what it was doing" is a logging bug, not a
# patience problem.
# What made this run not describe what ships. Both can be true at once, and
# each alone means the same thing, so they compose into one clause that lands
# ON the verdict line rather than in a note above it: the equivalent notes at
# the top of this script were read past three separate times in one night.
qualifier_text() {
  q=""
  [ -n "${SUBMODULE_DRIFT:-}" ] && q="ON DRIFTED SUBMODULES"
  if [ -n "${TREE_STALE:-}" ]; then
    [ -n "$q" ] && q="$q and "
    q="$q${TREE_STALE}"
  fi
  printf '%s' "$q"
}

say_stage() { printf "  %-12s %s ...\n" "$1" "$(date +%H:%M:%S)"; }
since() { echo "$(( $(date +%s) - $1 ))s"; }

echo "host tests"
for suite in host-tests/*/; do
  name=$(basename "$suite")
  [ -x "$suite/run.sh" ] || continue
  say_stage "$name"
  T0=$(date +%s)
  "$suite/run.sh" > "$LOGS/$name.log" 2>&1
  code=$?
  passed=$(grep -c "checks, 0 failed" "$LOGS/$name.log" || true)
  if [ "$code" -ne 0 ]; then
    printf "  %-12s FAILED (exit %d, %s)\n" "$name" "$code" "$(since $T0)"
    grep -E "FAIL|error:" "$LOGS/$name.log" | head -5 | sed 's/^/      /'
    FAILED=1
  else
    printf "  %-12s ok (%s sub-suite(s), %s)\n" "$name" "$passed" "$(since $T0)"
    # A check that did not run must not scroll past looking like one that
    # passed. Suites write SKIP to their own log, which nothing surfaced.
    grep -E "^SKIP" "$LOGS/$name.log" | head -5 | sed 's/^/      /'
  fi
done

# What CI's compiler says, which is not what this machine's compiler says.
#
# `c++` on macOS is Apple clang. CI builds the same suites with GCC and
# -Werror, and GCC rejects things clang accepts -- an enum beside a plain
# integer in a conditional, most often -- and on 2026-08-12 it also fell over
# outright, aborting in gimplify on a `d = Draft{}` inside a deep nesting. So a
# fully green check.sh sat next to a red CI for four pushes.
#
# The expensive part was not the errors, it was the loop: CI reports only the
# FIRST one and takes about fifteen minutes to do it, so two errors meant two
# rounds. One local GCC pass finds them all.
#
# The ui suite alone, because it compiles every app's Screens.cpp with -Werror
# and is where this class has bitten twice. Skipped LOUDLY: a check that did not
# run must not scroll past looking like one that passed.
echo "cross-compiler"
GCC=""
for candidate in g++-16 g++-15 g++-14 g++-13; do
  command -v "$candidate" >/dev/null 2>&1 && GCC="$candidate" && break
done
if [ -z "$GCC" ]; then
  printf "  %-12s SKIPPED -- no real GCC on PATH (/usr/bin/g++ is Apple clang).\n" "gcc"
  printf "  %-12s CI builds with GCC and will catch what this cannot: brew install gcc\n" ""
else
  T0=$(date +%s)
  say_stage "gcc"
  if CXX="$GCC" host-tests/ui/run.sh > "$LOGS/gcc-ui.log" 2>&1; then
    printf "  %-12s ok (%s, %s)\n" "gcc" "$GCC" "$(since $T0)"
  else
    printf "  %-12s FAILED under %s (%s)\n" "gcc" "$GCC" "$(since $T0)"
    grep -E "error:|internal compiler" "$LOGS/gcc-ui.log" | head -5 | sed 's/^/      /'
    FAILED=1
  fi
fi

# The installer page's Python boundary. Two runs of the same suite: beside
# the sources, and again from inside the committed tools.zip -- the second is
# the code the browser actually gets, and it is how a zip member that a tool
# imports but MEMBERS forgot fails a test instead of a user (which shipped
# once: deck_to_anki.py). Needs the study venv; skipped LOUDLY without one,
# because a skipped check that scrolls past as green is how vacuous passes
# happen.
WS_PY="$REPO"
while [ "$WS_PY" != "/" ] && [ ! -e "$WS_PY/.xteink-workspace" ]; do WS_PY="$(dirname "$WS_PY")"; done
STUDY_PY="${CHECK_OUTER_STUDY_PY:-}"
if [ ! -x "$STUDY_PY" ]; then
  STUDY_PY=""
  for candidate in "$REPO/.venv-study/bin/python" "$WS_PY/firmware-next/.venv-study/bin/python"; do
    [ -x "$candidate" ] && STUDY_PY="$candidate" && break
  done
fi
if [ -n "$STUDY_PY" ]; then
  for args in "tools_local/study/test_apkg.py" \
              "tools_local/study/test_fsrs.py" \
              "tools_local/study/test_web_glue.py" \
              "tools_local/study/test_web_glue.py --from-zip" \
              "tools_local/study/test_slug_parity.py" \
              "tools_local/study/test_font_parity.py"; do
    step="$(echo "$args" | sed 's|tools_local/study/||')"
    # One log per step: a failing step's output used to be overwritten by the
    # next step's, so the tail printed on failure showed a suite that passed.
    steplog="$LOGS/installer-$(printf '%s' "$step" | tr -c 'A-Za-z0-9._-' '_').log"
    if (cd "$REPO" && $STUDY_PY $args) > "$steplog" 2>&1; then
      printf "  %-12s ok (%s)\n" "installer" "$step"
    else
      printf "  %-12s FAILED (%s)\n" "installer" "$step"
      tail -5 "$steplog" | sed 's/^/      /'
      FAILED=1
    fi
  done
else
  echo "  installer    SKIPPED: no .venv-study -- the page's Python suite did NOT run"
fi

# The sync bridge server suites. Their venvs are not committed; uv rebuilds them
# in a --committed trial worktree (warm uv cache makes that cheap). A missing
# toolchain FAILS rather than skips: a bridge change riding a green gate whose
# bridge suite never ran is exactly the silence check.sh exists to prevent.
#
# Two services now, so this is a loop rather than a block. Each entry is
#   <directory> <label> <port offset within the tree's slice> <suites...>
# and the offsets are picked apart from each other AND from the link suite,
# which owns LINKPLAY_BASE_PORT+0..7 (LinkRadio.cpp, kSlots). readbridge takes
# 8..11 because its harness derives a fake-service port from the base; study
# takes 12. Sharing an offset would only bite when two trees gate at once,
# which is exactly when nobody is looking.
for entry in \
  "server/study-bridge:bridge:12:tests/test_engine.py tests/test_api.py tests/test_window.py" \
  "server/read-bridge:readbridge:8:tests/test_oauth.py tests/test_article.py tests/test_engine.py tests/test_api.py tests/test_window.py"
do
  BRIDGE_DIR="$REPO/${entry%%:*}"
  rest="${entry#*:}"
  BRIDGE_LABEL="${rest%%:*}"
  rest="${rest#*:}"
  BRIDGE_OFFSET="${rest%%:*}"
  BRIDGE_SUITES="${rest#*:}"
  [ -d "$BRIDGE_DIR" ] || continue

  BRIDGE_PY="$BRIDGE_DIR/.venv/bin/python"
  if [ ! -x "$BRIDGE_PY" ] && command -v uv > /dev/null 2>&1; then
    (cd "$BRIDGE_DIR" && uv venv .venv --quiet \
      && uv pip install --python .venv/bin/python --quiet -r requirements.txt) \
      > "$LOGS/$BRIDGE_LABEL-venv.log" 2>&1 || true
  fi
  if [ -x "$BRIDGE_PY" ]; then
    # Ports ride the tree's own slice, same reason as LINKPLAY_BASE_PORT:
    # two trees' gates must not share a test server's port.
    export BRIDGE_TEST_PORT=$(( LINKPLAY_BASE_PORT + BRIDGE_OFFSET ))
    for t in $BRIDGE_SUITES; do
      log="$LOGS/$BRIDGE_LABEL-$(basename "$t").log"
      if (cd "$BRIDGE_DIR" && "$BRIDGE_PY" "$t") > "$log" 2>&1; then
        printf "  %-12s ok (%s)\n" "$BRIDGE_LABEL" "$(basename "$t")"
      else
        printf "  %-12s FAILED (%s)\n" "$BRIDGE_LABEL" "$(basename "$t")"
        tail -5 "$log" | sed 's/^/      /'
        FAILED=1
      fi
    done
  else
    printf "  %-12s FAILED (no venv and uv could not build one)\n" "$BRIDGE_LABEL"
    FAILED=1
  fi
done

if [ -n "$STUDY_PY" ] && [ -f tools_local/site/precompress.py ]; then
  if (cd "$REPO" && $STUDY_PY tools_local/site/precompress.py --check) > "$LOGS/precompress.log" 2>&1; then
    printf "  %-12s ok\n" "encoding"
  else
    printf "  %-12s FAILED\n" "encoding"
    sed 's/^/      /' "$LOGS/precompress.log"
    FAILED=1
  fi
fi

if [ "${1:-}" != "--tests" ]; then
  # Shared, content-addressed object cache: a tree that has never built before
  # is mostly cache hits rather than a cold compile. Set here as well as in
  # lib-sim.sh because check.sh does not source it.
  WS="$REPO"
  while [ "$WS" != "/" ] && [ ! -e "$WS/.xteink-workspace" ]; do WS="$(dirname "$WS")"; done
  [ -e "$WS/.xteink-workspace" ] && export PLATFORMIO_BUILD_CACHE_DIR="$WS/.pio-cache"

  # x4pro is serialised across every tree; simulator_x4_pro is not.
  #
  # The ESP32 build reaches into ~/.platformio, which is shared by every tree
  # and cannot be sharded (it is 10GB). Three trees building x4pro at once for
  # the first time raced in the espressif32 builder and two of them died on
  # `TypeError: ... not 'NoneType'` out of arduino.py, with chip_variant unset
  # -- a platform-internal error that looks nothing like a race and points at
  # no file of ours. Alone, both rebuilt in 30s. The native simulator build
  # touches none of that and succeeded in all three concurrently, so it stays
  # parallel: that is the build the inner loop actually waits on.
  # Prefer the inherited cache dir: inside a --committed trial worktree the
  # marker walk dead-ends in TMPDIR, and deriving the lock from the walk there
  # would mean committed x4pro builds never took the lock at all.
  FW_LOCK="${PLATFORMIO_BUILD_CACHE_DIR:-$WS/.pio-cache}/x4pro.lock"

  BUILD_ENVS="simulator_x4_pro x4pro sticky"
  # See the note in the --committed block: these are the only builds that
  # compile the release-path serial code at all.
  [ -n "${CHECK_BUILD_RELEASE_ENVS:-}" ] && BUILD_ENVS="$BUILD_ENVS gh_release_x4pro gh_release_sticky"

  # Every env here except the native simulator reaches into the shared
  # ~/.platformio, so the lock must SPAN from the first of them to the last.
  # Both ends are derived from the list rather than named, because naming them
  # is how this broke: gh_release_x4pro and gh_release_sticky were appended in
  # 2f860bee and the hardcoded release on "sticky" was not moved with them, so
  # for a week both release builds ran with no lock at all. That makes a release
  # gate and any other tree's device build collide by design, and it surfaces as
  # a missing framework header naming no file of ours -- WiFi.h on 2026-08-29,
  # the same shape as the sdkconfig.h failure that has cost two sessions.
  FIRST_FW_ENV="$(printf '%s\n' $BUILD_ENVS | grep -v '^simulator' | head -1)"
  LAST_FW_ENV="$(printf '%s\n' $BUILD_ENVS | grep -v '^simulator' | tail -1)"
  for env in $BUILD_ENVS; do
    printf "build: %-18s %s ...\n" "$env" "$(date +%H:%M:%S)"
    BUILD_T0=$(date +%s)
    if [ "$env" = "$FIRST_FW_ENV" ] && [ -d "$(dirname "$FW_LOCK")" ]; then
      # A lock is stale when its HOLDER is gone, never merely when it is old.
      # This used to `rm -rf` the lock after 900s with no liveness test at all,
      # and 900s is shorter than a cold --committed run's four device builds --
      # so a queued tree stole the lock from a live release gate BY DESIGN and
      # both then raced ~/.platformio. Seen 2026-08-31: the thief died on
      # `ComponentManager/.../index.lock: File exists`, an error naming no file
      # of ours. The lock now records its holder's pid so a waiter can ask.
      waited=0
      while ! mkdir "$FW_LOCK" 2>/dev/null; do
        owner="$(cat "$FW_LOCK/owner" 2>/dev/null || true)"
        owner_pid="${owner%% *}"
        # Match the resolved pio path, not the bare words: a shell whose command
        # line merely MENTIONS "pio run" (a waiter, a probe, a heredoc) is not a
        # build, and on this workspace one usually does. Simulator builds are
        # excluded because they never take this lock -- they touch neither
        # ~/.platformio nor the ComponentManager cache -- so one running
        # elsewhere must not stop us reclaiming an abandoned lock.
        # And never `pgrep -c` here: macOS pgrep has no -c, so it exits 2 and
        # any `|| echo 0` fallback reports "no builds" forever.
        if pgrep -fl "[b]in/pio run" 2>/dev/null | grep -v -- "-e simulator" | grep -q .; then
          builds_alive=1
        else
          builds_alive=0
        fi
        if [ ! -e "$FW_LOCK/owner" ]; then
          # A lock with no owner file is HELD, never abandoned. It was taken by
          # a tree whose check.sh predates the owner file, with a plain mkdir,
          # and such a holder cannot tell us whether it is alive -- so assume it
          # is. Treating it as dead and falling through to the builds_alive test
          # is not safe: a --committed run builds four device envs in sequence,
          # and between them no `pio run` exists at all, so a waiter landing in
          # that gap would reclaim a live holder's lock and both would then race
          # ~/.platformio. Seen on 2026-08-31, an empty x4pro.lock held by a
          # live pre-owner-file check.sh while another tree waited on it.
          #
          # The cost is that a genuinely dead ownerless lock never self-clears.
          # That is the pre-owner-file status quo, and it is the right way round:
          # waiting too long is a delay, reclaiming too early is two concurrent
          # builds and an error naming no file of ours.
          [ $(( waited % 60 )) -eq 0 ] && {
            echo "  lock has no owner file: an older check.sh holds it, and this cannot self-clear." >&2
            echo "  waiting (${waited}s). If nothing is really building, remove it by hand:" >&2
            echo "    rm -rf $FW_LOCK" >&2
          }
        elif [ -n "$owner_pid" ] && kill -0 "$owner_pid" 2>/dev/null; then
          # Holder alive. Not stale at any age; a 40-minute release gate is
          # working, not hung.
          [ $(( waited % 30 )) -eq 0 ] &&
            echo "  waiting for another tree's firmware build (pid $owner_pid, ${waited}s) ..."
        elif [ "$builds_alive" -eq 1 ]; then
          # Holder gone but a build survives it: killing a shell orphans its pio
          # child, and the EXIT trap that frees the lock never runs. Breaking in
          # here is the corruption case, so say what is true and keep waiting.
          [ $(( waited % 60 )) -eq 0 ] &&
            echo "  lock holder is gone but a build is still running; waiting rather than racing it." >&2
        else
          # Holder dead (or a lock with no owner file, e.g. hand-made) and no
          # build anywhere: genuinely abandoned, and waiting out a clock buys
          # nothing.
          # Delete only the lock we judged. With three waiters, one can be
          # descheduled between deciding "abandoned" and deleting, and would
          # otherwise remove a lock a second waiter has since taken and is
          # building under -- the very race this loop exists to prevent. A lock
          # is removed by its owner, or by whoever proved that owner dead.
          owner_now="$(cat "$FW_LOCK/owner" 2>/dev/null || true)"
          if [ "$owner_now" = "$owner" ]; then
            echo "  firmware lock abandoned by ${owner_pid:-unknown holder}; reclaiming $FW_LOCK" >&2
            rm -rf "$FW_LOCK"
          fi
        fi
        sleep 2
        waited=$((waited + 2))
      done
      # ${REPO:-$PWD}, never a bare ${REPO}: this loop is lifted out and run by
      # host-tests/checksh, where REPO does not exist. Bash 4.4+ (every Linux
      # CI runner) aborts on the unset expansion under `set -u` and the lock is
      # then acquired but never owned or released; macOS bash 3.2 silently
      # substitutes empty and the tests pass. That gap is exactly how this
      # arrived red on CI and green here.
      owner_tree="${REPO:-$PWD}"
      printf '%s %s\n' "$$" "${owner_tree##*/}" > "$FW_LOCK/owner"
      # Tell the builds underneath us that this lock is ours. Without it,
      # scripts_local/require_build_lock.py -- which runs inside pio itself and
      # is the only place a raw `pio run` cannot skip -- would see a live
      # stranger holding the lock and refuse our own device builds.
      export XTEINK_FW_LOCK_OWNER="$$"
      # rm -rf, not rmdir: the owner file makes the directory non-empty, and an
      # rmdir that silently fails would leak the lock to every other tree.
      # Same rule on the way out: a run that died after its lock was reclaimed
      # must not delete the reclaimer's.
      trap 'o="$(cat "$FW_LOCK/owner" 2>/dev/null || true)"; [ "${o%% *}" = "$$" ] && rm -rf "$FW_LOCK"; true' EXIT INT TERM

      # The object cache is trimmed HERE and nowhere else: holding the firmware
      # lock is the only moment no other tree is reading those objects. Pruning
      # outside it deletes inputs from under somebody's running build, which
      # surfaces as a link error naming no file of ours -- the same shape as the
      # failure the guard exists to make legible.
      #
      # It also refuses to start when trimming cannot get the disk above the
      # floor, so a full disk arrives as a sentence about the disk rather than
      # as [Errno 28] from inside the espressif32 builder twenty minutes later.
      #
      # No manual lock removal on the failure path: the EXIT trap above already
      # removes it, and only if this run still owns it. Deleting it here as well
      # would take a lock a reclaimer had legitimately acquired in between.
      # Sourced only if it resolves, for the same reason the owner line uses
      # ${REPO:-$PWD}: host-tests/checksh lifts this loop out and runs its text
      # in a temp directory where scripts_local/ does not exist. An
      # unconditional source dies there under `set -e`, taking the owner line
      # with it -- so the lock gets acquired and never owned, and the tests
      # report exactly that. The guard is a safety check, not a build step;
      # skipping it in a harness costs nothing.
      # REPO, not ${REPO:-$PWD}: host-tests/checksh lifts this loop out and runs
      # it with REPO deliberately unset, and the guard must not fire there. It
      # would prune the REAL shared cache from inside a unit test -- 66GB of
      # another session's build inputs -- and an early exit from it takes the
      # rest of the loop with it, which is how this was found.
      _guard="${REPO:-}/scripts_local/cache-guard.sh"
      if [ -n "${REPO:-}" ] && [ -r "$_guard" ]; then
        # shellcheck source=scripts_local/cache-guard.sh
        . "$_guard"
        if ! cache_guard_check "$PLATFORMIO_BUILD_CACHE_DIR"; then
          exit 1
        fi
      fi
    fi
    if pio run -e "$env" > "$LOGS/$env.log" 2>&1; then
      # The native build reports no RAM/Flash. Say "ok" rather than printing
      # nothing, or a clean build reads like a swallowed failure.
      grep -E "^(RAM|Flash):" "$LOGS/$env.log" | sed 's/^/  /' || true
      echo "  ok ($(( $(date +%s) - BUILD_T0 ))s)"
    else
      echo "  FAILED ($(( $(date +%s) - BUILD_T0 ))s)"
      grep -E "error:" "$LOGS/$env.log" | head -5 | sed 's/^/    /'
      FAILED=1
    fi
    # Released as soon as the last firmware build is done rather than at exit,
    # so a tree that still has other work to print does not hold every other
    # tree up.
    if [ "$env" = "$LAST_FW_ENV" ]; then
      # rm -rf, matching the trap: the lock holds an owner file now, so the
      # rmdir this replaced could not empty it and failed into 2>/dev/null --
      # the early release silently stopped happening and every other tree kept
      # waiting until this run exited. host-tests/checksh caught exactly that.
      owner_now="$(cat "$FW_LOCK/owner" 2>/dev/null || true)"
      [ "${owner_now%% *}" = "$$" ] && rm -rf "$FW_LOCK"
      trap - EXIT INT TERM
    fi
  done
fi

# The published browser build is a committed artifact, and a committed artifact
# is a claim about the source next to it. Nothing else checks that claim, so it
# went stale twice in one afternoon: once when app/simheap changed what the
# simulator reports for heap, and again an hour later when app/xkcdclose fixed a
# panic. Both times site/emulator/ still described the code from before, and the
# live page would have shipped it.
#
# Only enforced on $DEPLOY_BRANCH, because that is where the site deploys from. In
# an app worktree the artifact is stale by construction -- nobody rebuilds a
# 6-minute wasm per feature commit -- and a check that is always red is a check
# people learn to scroll past.
#
# CHECK_OUTER_BRANCH is set by the --committed path above and is empty for a
# normal run; it exists because the branch is unknowable from inside a detached
# worktree. Do not set it by hand.
#
# 2026-08-10: the branch gate was itself the bug. On app/installer the guard
# never ran, so the site's Study page previewed a deck on twelve-hour-old
# firmware and told the user "this is the real firmware" while the real one
# had a fixed card layout. A user-test agent caught it; nothing here did.
# Any branch that changes firmware AND ships site/emulator now gets the
# warning; only $DEPLOY_BRANCH treats it as a failure, because a feature
# branch legitimately rebuilds the 6-minute artifact once, at the end.
if true; then
  ART=$(git log -1 --format=%ct -- site/emulator 2>/dev/null)
  SRC=$(git log -1 --format=%ct -- src lib assets_local tools_local/wasm 2>/dev/null)
  if [ -n "$ART" ] && [ -n "$SRC" ] && [ "$ART" -lt "$SRC" ]; then
    echo
    echo "browser artifact is STALE"
    echo "  site/emulator/ was built at $(git log -1 --format=%h\ %s -- site/emulator | cut -c1-58)"
    echo "  but $(git log -1 --format=%h\ %s -- src lib assets_local tools_local/wasm | cut -c1-58) came after it"
    echo "  the live page would ship code older than this branch. Rebuild:"
    echo "    pio run -e simulator_x4_pro -t compiledb"
    echo "    source ../.emsdk/emsdk_env.sh && python3 tools_local/wasm/build.py"
    echo "  anything the page previews is running that older firmware."
    if [ "${CHECK_OUTER_BRANCH:-$(git branch --show-current 2>/dev/null)}" = "$DEPLOY_BRANCH" ]; then
      FAILED=1
    else
      echo "  (warning only off $DEPLOY_BRANCH -- rebuild before you land)"
    fi
  fi
fi

# Files served with Content-Encoding: br must actually BE brotli. They are
# committed compressed, so anything that regenerates one (the wasm build, the
# pyodide fetcher) can leave raw bytes behind a header promising otherwise,
# and the site then serves a wasm the browser cannot decode. Cheap to check:
# try to decompress each one.
# The installer page runs the study tools out of a committed zip
# (site/study/tools.zip); a stale zip is a page converting with last week's
# converter while the CLI has this week's, the exact drift the Pyodide design
# exists to prevent. The check is byte-exact and instant, so unlike the wasm
# gate above it runs in every tree.
if [ -f site/study/tools.zip ]; then
  if ! python3 tools_local/study/sync_site.py --check > /dev/null 2>&1; then
    echo
    echo "site/study/tools.zip is STALE -- run: python3 tools_local/study/sync_site.py"
    FAILED=1
  fi
fi

# Same claim, same gate, for the wasm FreeType the font step runs on: edit
# ftshim.c or its build and forget the rebuild, and the page quietly builds
# fonts that are no longer byte-identical to the CLI's. Branch-gated like the
# emulator gate above, and for the same reason.
if [ "${CHECK_OUTER_BRANCH:-$(git branch --show-current 2>/dev/null)}" = "$DEPLOY_BRANCH" ]; then
  FT_ART=$(git log -1 --format=%ct -- site/study/ft.js site/study/ft.wasm 2>/dev/null)
  FT_SRC=$(git log -1 --format=%ct -- tools_local/wasm-ft 2>/dev/null)
  if [ -n "$FT_ART" ] && [ -n "$FT_SRC" ] && [ "$FT_ART" -lt "$FT_SRC" ]; then
    echo
    echo "site/study/ft.{js,wasm} is STALE against tools_local/wasm-ft/ -- rebuild:"
    echo "    python3 tools_local/wasm-ft/build.py"
    echo "  and re-run tools_local/study/test_font_parity.py for the byte-identical check"
    FAILED=1
  fi
fi

echo
if [ "$FAILED" -eq 0 ]; then
  QUALIFIER="$(qualifier_text)"
  if [ -n "$QUALIFIER" ]; then
    # The qualifier leads AND the clean phrase is absent. Both halves were paid
    # for: trailing it ("all green -- BUT ...") lost to a reader who greps
    # `all green|SOMETHING FAILED` and acted on the first three words, and
    # front-loading alone still left the line CONTAINING "all green", so every
    # grep written before the qualifier existed went on matching it and failing
    # OPEN. A qualified verdict that contains its own unqualified form is
    # matched by all of them. Old patterns must find nothing here and fail
    # closed, so this line says "suites passed" instead.
    echo "VERDICT WITHHELD ($QUALIFIER) -- suites passed, but not on the code that ships. logs in $LOGS"
  else
    echo "all green. logs in $LOGS"
  fi
else
  echo "SOMETHING FAILED. logs in $LOGS"
fi
exit "$FAILED"

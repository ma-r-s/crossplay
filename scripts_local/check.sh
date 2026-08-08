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
TAG="$(printf '%s' "$REPO" | shasum | cut -c1-8)"
LOGS="${TMPDIR:-/tmp}/xteink-check-$TAG"
mkdir -p "$LOGS"
FAILED=0

# The link suite is real UDP on real loopback, and its port range IS its
# discovery mechanism: every radio in 45700..45707 finds every other one. Two
# trees checking at once therefore join each other's match, and the suite fails
# on whichever assertion the stray datagram happened to break -- three
# concurrent runs failed on three different lines and all three passed alone.
# One 16-port slice per tree, derived from the same hash as everything else.
if [ -z "${LINKPLAY_BASE_PORT:-}" ]; then
  SLICE=$(( 0x${TAG:0:4} % 900 ))
  export LINKPLAY_BASE_PORT=$(( 46000 + SLICE * 16 ))
fi

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
  TRIAL="${TMPDIR:-/tmp}/xteink-committed-$TAG"
  git worktree remove --force "$TRIAL" 2>/dev/null || true
  echo "verifying HEAD ($(git rev-parse --short HEAD)) in a throwaway worktree"
  echo "  your working tree is untouched, and its uncommitted work is not in this build"
  git worktree add --quiet --detach "$TRIAL" HEAD || exit 1
  # Clean up even when this run is interrupted. A killed --committed used to
  # leave ~600MB registered in TMPDIR until the same tree ran it again, and
  # long builds get killed on purpose here, so that is the normal case rather
  # than the rare one. One was found orphaned from a run that died mid-build.
  trap 'git worktree remove --force "$TRIAL" 2>/dev/null; git worktree prune 2>/dev/null' EXIT INT TERM
  # A fresh worktree does not populate submodules, and the host tests compile
  # FreeInkUI out of freeink-sdk/.
  git -C "$TRIAL" submodule update --init --recursive --quiet
  (cd "$TRIAL" && ./scripts_local/check.sh "${2:-}")
  exit $?
fi

DIRTY="$(dirty_count)"
if [ "$DIRTY" -ne 0 ]; then
  echo "note: verifying your working tree, which has $DIRTY uncommitted file(s)."
  echo "      a green result here does NOT mean HEAD compiles."
  echo "      use --committed before you rely on that."
  echo
fi

echo "host tests"
for suite in host-tests/*/; do
  name=$(basename "$suite")
  [ -x "$suite/run.sh" ] || continue
  "$suite/run.sh" > "$LOGS/$name.log" 2>&1
  code=$?
  passed=$(grep -c "checks, 0 failed" "$LOGS/$name.log" || true)
  if [ "$code" -ne 0 ]; then
    printf "  %-12s FAILED (exit %d)\n" "$name" "$code"
    grep -E "FAIL|error:" "$LOGS/$name.log" | head -5 | sed 's/^/      /'
    FAILED=1
  else
    printf "  %-12s ok (%s sub-suite(s))\n" "$name" "$passed"
  fi
done

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
  FW_LOCK="$WS/.pio-cache/x4pro.lock"

  for env in simulator_x4_pro x4pro; do
    echo "build: $env"
    if [ "$env" = "x4pro" ] && [ -d "$(dirname "$FW_LOCK")" ]; then
      waited=0
      while ! mkdir "$FW_LOCK" 2>/dev/null; do
        [ "$waited" -eq 0 ] && echo "  waiting for another tree's firmware build ..."
        sleep 2
        waited=$((waited + 2))
        if [ "$waited" -gt 900 ]; then
          echo "  firmware lock held 15 minutes; removing stale $FW_LOCK" >&2
          rm -rf "$FW_LOCK"
        fi
      done
      trap 'rmdir "$FW_LOCK" 2>/dev/null' EXIT INT TERM
    fi
    if pio run -e "$env" > "$LOGS/$env.log" 2>&1; then
      # The native build reports no RAM/Flash. Say "ok" rather than printing
      # nothing, or a clean build reads like a swallowed failure.
      grep -E "^(RAM|Flash):" "$LOGS/$env.log" | sed 's/^/  /' || echo "  ok"
    else
      echo "  FAILED"
      grep -E "error:" "$LOGS/$env.log" | head -5 | sed 's/^/    /'
      FAILED=1
    fi
    # Released as soon as the firmware build is done rather than at exit, so a
    # tree that still has other work to print does not hold every other tree up.
    if [ "$env" = "x4pro" ]; then
      rmdir "$FW_LOCK" 2>/dev/null
      trap - EXIT INT TERM
    fi
  done
fi

echo
if [ "$FAILED" -eq 0 ]; then
  echo "all green. logs in $LOGS"
else
  echo "SOMETHING FAILED. logs in $LOGS"
fi
exit "$FAILED"

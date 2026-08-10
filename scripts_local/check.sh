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
              "tools_local/study/test_web_glue.py" \
              "tools_local/study/test_web_glue.py --from-zip" \
              "tools_local/study/test_font_parity.py"; do
    if (cd "$REPO" && $STUDY_PY $args) > "$LOGS/installer.log" 2>&1; then
      printf "  %-12s ok (%s)\n" "installer" "$(echo "$args" | sed 's|tools_local/study/||')"
    else
      printf "  %-12s FAILED (%s)\n" "installer" "$(echo "$args" | sed 's|tools_local/study/||')"
      tail -5 "$LOGS/installer.log" | sed 's/^/      /'
      FAILED=1
    fi
  done
else
  echo "  installer    SKIPPED: no .venv-study -- the page's Python suite did NOT run"
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
if [ "${CHECK_OUTER_BRANCH:-$(git branch --show-current 2>/dev/null)}" = "$DEPLOY_BRANCH" ]; then
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
    FAILED=1
  fi
fi

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
  echo "all green. logs in $LOGS"
else
  echo "SOMETHING FAILED. logs in $LOGS"
fi
exit "$FAILED"

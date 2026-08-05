#!/bin/bash
# Pull CrossPoint forward into this fork, preserving the mirror discipline.
#
#   base    is a pure mirror of the upstream branch. Never commit here.
#           Fast-forward only.
#   xteink  is the integration branch. base merges into it, never the reverse.
#
# Because base is a pure mirror, `git diff base..xteink` always shows exactly
# what this fork owns, and building base gives a clean upstream binary for
# answering "is this bug mine or theirs".
#
#   ./scripts/sync.sh            # report what changed upstream, change nothing
#   ./scripts/sync.sh --apply    # merge and verify in a trial worktree, then land
#
# The merge and its verification happen in a throwaway worktree checked out at
# the committed tip, never in your working directory. Two reasons, both learned
# the hard way on 2026-08-05:
#
#   1. Your tree is usually dirty. Refusing to sync until it is clean means
#      never syncing, or stashing a live session's work to run a merge that
#      cannot touch it anyway. A sync only conflicts with uncommitted work if
#      upstream changed the same files, which is checked below and is rare.
#   2. check.sh verifies the working directory, so uncommitted work masks a
#      broken commit. Three apps once shipped depending on a Toybox change that
#      was never committed; every check ran green because the change was sitting
#      unstaged. The trial worktree builds committed state only, so the sync is
#      verified against what is actually in the branch.
#
# See LOCAL_SCOPE.md for the seam this protects and docs/crosspoint-migration.md
# for why the base is what it is.
set -euo pipefail

REPO="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
cd "$REPO"

WORK_BRANCH="xteink"
MIRROR="base"
# The branch CrossPoint ships X4 Pro betas from. Not develop: X4 Pro support is
# not merged there. See the lifecycle warning below.
TRACKED="crosspoint/feat-touch-ui"
TRIAL="${TMPDIR:-/tmp}/xteink-sync-trial"

# The upstream-owned files this fork modifies. Kept in sync with LOCAL_SCOPE.md.
# Everything else we own is new files under src/apps_local/, host-tests/,
# scripts_local/ or docs/, which cannot conflict.
OWNED=(
  "src/activities/home/HomeActivity.cpp"
  "src/components/themes/BaseTheme.h"
  "src/components/themes/lyra/LyraTheme.cpp"
  ".skills/SKILL.md"
  ".gitignore"
  "platformio.ini"
  "SCOPE.md"
)

# --ignore-submodules=untracked: the icon tools drop a __pycache__/ inside
# freeink-sdk, which is untracked content in a submodule. Without this the tree
# reads as permanently dirty and the sync never runs. Submodule *pointer* moves
# are still reported, which is the part that matters.
dirty_files() {
  git status --porcelain --ignore-submodules=untracked | sed 's/^...//'
}

git fetch --quiet crosspoint

# --- the feature-branch lifecycle -------------------------------------------
# This fork is based on an unmerged upstream branch, which is a deliberate
# choice (it is the only place X4 Pro support lives, and it is where the public
# betas come from) with one failure mode: the branch can be squash-merged and
# deleted. Check for that before anything else, because the fix is to re-point
# the mirror at develop, not to debug a sync.
if ! git rev-parse --quiet --verify "$TRACKED" >/dev/null; then
  echo "!! $TRACKED is gone from the remote."
  echo "   It has most likely landed in develop. Re-point MIRROR at"
  echo "   crosspoint/develop here and in LOCAL_SCOPE.md, confirm develop still"
  echo "   builds -e x4pro, then sync."
  exit 1
fi
if git merge-base --is-ancestor "$TRACKED" crosspoint/develop 2>/dev/null; then
  echo "note: $TRACKED is now contained in crosspoint/develop."
  echo "      Consider re-pointing this script and LOCAL_SCOPE.md at develop;"
  echo "      tracking a merged branch means missing everything after it."
  echo
fi

BEHIND=$(git rev-list --count "$MIRROR..$TRACKED")
echo "$MIRROR is $BEHIND commit(s) behind $TRACKED"
# Measured against the upstream tip, not the mirror. Diffing a stale mirror
# counts upstream's own changes as ours, and the mirror is stale exactly when
# you are running this.
echo "this fork owns $(git diff --shortstat "$TRACKED...$WORK_BRANCH" | sed 's/^ *//')"

if [ "$BEHIND" -eq 0 ]; then
  echo "nothing to sync"
  exit 0
fi

echo
git log --oneline --no-decorate "$MIRROR..$TRACKED" | sed 's/^/  /'

echo
echo "upstream changes touching files this fork also modifies:"
HIT=0
for f in "${OWNED[@]}"; do
  n=$(git rev-list --count "$MIRROR..$TRACKED" -- "$f")
  if [ "$n" -gt 0 ]; then
    echo "  !! $f ($n commit(s)) -- expect a conflict"
    HIT=1
  fi
done
[ "$HIT" -eq 0 ] && echo "  none -- this sync should merge cleanly"

# A dirty tree is fine as long as upstream did not touch the same files. If it
# did, the landing fast-forward would fail (or worse, need a checkout that
# clobbers unstaged work), so stop while stopping is cheap.
echo
COLLIDE=$(comm -12 \
  <(git diff --name-only "$MIRROR..$TRACKED" | sort) \
  <(dirty_files | sort))
if [ -n "$COLLIDE" ]; then
  echo "!! upstream changed files you have uncommitted work in:"
  echo "$COLLIDE" | sed 's/^/     /'
  echo "   commit or stash those files, then sync."
  exit 1
fi
if [ -n "$(dirty_files)" ]; then
  echo "your tree is dirty, but upstream touched none of those files."
  echo "the sync will land as a fast-forward and leave your work alone."
else
  echo "working tree is clean."
fi

# The simulator shim exists only because the simulator lags this branch. When
# upstream fixes it there, the pre: hook says so on the next build; surface that
# here too, since a sync is when you would act on it.
echo
echo "reminder: if a build prints '[sim-catchup] ... no longer applies', the"
echo "          simulator has caught up. Delete that patch from"
echo "          scripts_local/sim_catchup.py rather than carrying it."

if [ "${1:-}" != "--apply" ]; then
  echo
  echo "re-run with --apply to sync"
  exit 0
fi

# --- apply -------------------------------------------------------------------
# Everything from here happens in a throwaway worktree. The only write to your
# working directory is the final fast-forward, and only if verification passed.

# Capture check.sh's per-suite verdicts so pre-existing failures can be told
# apart from ones this merge introduced. A fork that tracks an upstream branch
# always carries a known-failing suite or two; blocking every sync on a fully
# green tree means blocking every sync.
suite_status() {
  grep -E "^  [a-z]" | sed -E 's/^  ([a-z_]+) +(ok|FAILED).*/\1 \2/'
}

run_check_at() {
  local ref="$1" dest="$2" label="$3"
  rm -rf "$dest"
  git worktree add --quiet --detach "$dest" "$ref"
  # A fresh worktree does not populate submodules, and the host tests compile
  # FreeInkUI straight out of freeink-sdk/. Without this every UI-dependent
  # suite fails with "no such file or directory" and looks like a real break.
  git -C "$dest" submodule update --init --recursive --quiet
  echo "  verifying $label ..." >&2
  # || true: check.sh exits non-zero when any suite fails, which is the normal
  # case here (that is the whole point of taking a baseline). Under set -e an
  # unguarded failure would abort the sync instead of being compared.
  (cd "$dest" && ./scripts_local/check.sh --tests 2>&1) | suite_status || true
}

echo
echo "fast-forwarding $MIRROR (no checkout: it is a pure mirror)"
git merge-base --is-ancestor "$MIRROR" "$TRACKED" || {
  echo "!! $MIRROR is not an ancestor of $TRACKED -- someone committed to the"
  echo "   mirror. Reset it with: git branch -f $MIRROR $TRACKED"
  exit 1
}
git branch -f "$MIRROR" "$TRACKED"

echo
echo "trial merge in $TRIAL"
BEFORE=$(run_check_at "$WORK_BRANCH" "$TRIAL-before" "$WORK_BRANCH before the merge")

rm -rf "$TRIAL"
git worktree add --quiet --detach "$TRIAL" "$WORK_BRANCH"
if ! git -C "$TRIAL" merge --no-edit "$MIRROR" >/dev/null 2>&1; then
  echo "!! merge conflicts. Inspect and resolve in:"
  echo "   $TRIAL"
  echo "   then land with: git -C $REPO merge --ff-only \$(git -C $TRIAL rev-parse HEAD)"
  exit 1
fi
MERGED=$(git -C "$TRIAL" rev-parse HEAD)
git -C "$TRIAL" submodule update --init --recursive --quiet
echo "  verifying merged tree ..." >&2
AFTER=$( (cd "$TRIAL" && ./scripts_local/check.sh --tests 2>&1) | suite_status || true )

echo
NEW_FAILURES=$(comm -13 <(echo "$BEFORE" | sort) <(echo "$AFTER" | sort) | grep " FAILED" || true)
PRE_EXISTING=$(echo "$BEFORE" | grep " FAILED" || true)

if [ -n "$PRE_EXISTING" ]; then
  echo "already failing before this merge (not caused by it):"
  echo "$PRE_EXISTING" | sed 's/^/  /'
  echo
fi

if [ -n "$NEW_FAILURES" ]; then
  echo "!! this merge introduced new failures:"
  echo "$NEW_FAILURES" | sed 's/^/  /'
  echo "   the trial worktree is left at $TRIAL for inspection."
  echo "   nothing was landed; $WORK_BRANCH is untouched."
  exit 1
fi

echo "no new failures. landing."
git merge --ff-only "$MERGED"

git worktree remove --force "$TRIAL" 2>/dev/null || true
git worktree remove --force "$TRIAL-before" 2>/dev/null || true

echo
echo "synced. $WORK_BRANCH is now $(git rev-parse --short HEAD)"
echo "run ./scripts/check.sh before your next commit for the full builds."

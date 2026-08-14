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
# See LOCAL_SCOPE.md for the seam this protects and why the base is what it is.
set -euo pipefail

REPO="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
cd "$REPO"

WORK_BRANCH="xteink"
MIRROR="base"
# The durable upstream trunk. This fork was based on crosspoint/feat-touch-ui,
# the X4 Pro beta branch, until upstream deleted it unmerged (found
# 2026-08-14): xteink is that branch's only continuation, and our pushed copy
# survives as origin/feat-touch-ui. Upstream is reimplementing X4 Pro + touch
# support on crosspoint/feat-x4-papermono-support, based on develop, and it
# should land in develop for a future release. So track develop -- feature
# branches get deleted, trunks do not -- and treat papermono as the thing to
# watch below, not the thing to mirror. See LOCAL_SCOPE.md, "Branches".
TRACKED="crosspoint/develop"
TRIAL="${TMPDIR:-/tmp}/xteink-sync-trial"

# --ignore-submodules=untracked: the icon tools drop a __pycache__/ inside
# freeink-sdk, which is untracked content in a submodule. Without this the tree
# reads as permanently dirty and the sync never runs. Submodule *pointer* moves
# are still reported, which is the part that matters.
dirty_files() {
  git status --porcelain --ignore-submodules=untracked | sed 's/^...//'
}

# --prune matters: without it a branch deleted upstream lingers as a local
# remote-tracking ref forever. feat-touch-ui was deleted upstream and this
# script kept reporting against the corpse for a week before anyone noticed.
git fetch --quiet --prune crosspoint

# --- the branch lifecycle ---------------------------------------------------
if ! git rev-parse --quiet --verify "$TRACKED" >/dev/null; then
  echo "!! $TRACKED is gone from the remote."
  echo "   That should not happen to a trunk. Look at the remote's branches"
  echo "   before anything else; re-point TRACKED and LOCAL_SCOPE.md at"
  echo "   whatever succeeded it, and confirm it still builds -e x4pro."
  exit 1
fi
# The X4 Pro work upstream is rebuilding (after deleting the line this fork is
# based on) rides this branch. Merging it is a sit-down event -- it is a second
# touch implementation competing with the one xteink inherited, 80 conflicting
# files at last measure -- so surface its movement here, where a sync starts.
PM="crosspoint/feat-x4-papermono-support"
if git rev-parse --quiet --verify "$PM" >/dev/null; then
  echo "watching: $PM is $(git rev-list --count "$TRACKED..$PM") commit(s) ahead of $TRACKED"
  echo "          (upstream's X4 Pro line; see LOCAL_SCOPE.md before merging it)"
  echo
else
  echo "note: $PM is gone from the remote. Upstream's X4 Pro work has"
  echo "      presumably landed in $TRACKED; the next sync carries it."
  echo "      Read LOCAL_SCOPE.md's branch section before running --apply."
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
echo "upstream changes touching files this side also changed:"
# Computed, never listed. A hand-kept OWNED list stood at seven entries while
# the true overlap had grown past twenty; a warning that misses the likeliest
# collision sites is worse than none. A file can conflict exactly when both
# sides changed it since the fork point, so ask git for that set.
FORK_POINT=$(git merge-base "$WORK_BRANCH" "$TRACKED")
HIT=0
while IFS= read -r f; do
  n=$(git rev-list --count "$MIRROR..$TRACKED" -- "$f")
  if [ "$n" -gt 0 ]; then
    echo "  !! $f ($n commit(s)) -- expect a conflict"
    HIT=1
  fi
done < <(comm -12 \
  <(git diff --name-only "$FORK_POINT..$TRACKED" | sort) \
  <(git diff --name-only "$FORK_POINT..$WORK_BRANCH" | sort))
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

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
#   ./scripts/sync.sh --apply    # fast-forward base, merge base into xteink
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

# The upstream-owned files this fork modifies. Kept in sync with LOCAL_SCOPE.md.
# Everything else we own is new files under src/apps_local/, host-tests/,
# scripts_local/ or docs/, which cannot conflict.
OWNED=(
  "src/activities/home/HomeActivity.cpp"
  ".skills/SKILL.md"
  ".gitignore"
  "platformio.ini"
  "SCOPE.md"
)

if [ -n "$(git status --porcelain)" ]; then
  echo "working tree is dirty; commit or stash first" >&2
  exit 1
fi

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

echo
git checkout --quiet "$MIRROR"
git merge --ff-only "$TRACKED"
git checkout --quiet "$WORK_BRANCH"
git merge --no-edit "$MIRROR"

echo
echo "merged. verifying..."
./scripts_local/check.sh

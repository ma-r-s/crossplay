#!/usr/bin/env bash
#
# Say whether this tree's submodules are the ones its commit describes.
#
# On 2026-08-31 a full gate ran green -- every suite, all three builds -- on a
# tree whose freeink-sdk was checked out at an unlanded upstream merge. It
# verified a combination that exists in no commit, and passing is exactly what
# made it worthless. check.sh's "uncommitted file(s)" note did fire, and was
# read past, because "1 uncommitted file" reads like a stray edit rather than
# the SDK moving underneath the build.
#
# THE PROBE IS `git submodule status`, NOT A rev-parse COMPARISON. Comparing
# `git -C <sub> rev-parse HEAD` against `git ls-tree HEAD <sub>` looks
# equivalent and is not: when the submodule directory has no .git of its own --
# an UNINITIALISED submodule, which is a plain `git worktree add` away -- git
# walks UP to the enclosing repository and answers with the SUPERPROJECT's
# HEAD. Every uninitialised tree then reports as drift, pointing at a commit
# that is not an SDK revision at all. Verified: in a tree at 9f8fa835,
# `git -C ./anything-without-dotgit rev-parse HEAD` returns 9f8fa835.
#
# The status characters distinguish what the comparison cannot:
#   '-' uninitialised   -- the build fails on missing headers; a different fix
#   '+' differs from what HEAD records -- the drift above
#   'U' merge conflicts inside the submodule
#   ' ' matches
#
#   scripts_local/submodule_state.sh [repo]   # default: $PWD
#
# Exit: 0 all match, 2 uninitialised, 3 drift, 4 conflicted. Highest wins, so a
# tree with both an uninitialised and a drifted submodule reports the one that
# stops the build.
set -uo pipefail

repo="${1:-$PWD}"

status="$(git -C "$repo" submodule status 2>/dev/null || true)"
[ -n "$status" ] || exit 0  # no submodules, or not a repo: nothing to say

rc=0
while IFS= read -r line; do
  [ -n "$line" ] || continue
  mark="${line:0:1}"
  rest="${line:1}"
  sha="${rest%% *}"
  path="$(echo "$rest" | awk '{print $2}')"
  case "$mark" in
    '-')
      echo "submodule $path is NOT INITIALISED."
      echo "  the build will fail on headers it cannot find, naming no file of ours."
      echo "  fix: git submodule update --init --recursive"
      [ "$rc" -lt 2 ] && rc=2
      ;;
    '+')
      recorded="$(git -C "$repo" ls-tree HEAD "$path" 2>/dev/null | awk '{print $3}')"
      echo "submodule $path is CHECKED OUT AT A DIFFERENT COMMIT than this tree records."
      echo "  checked out: ${sha}"
      echo "  recorded:    ${recorded:-unknown}"
      echo "  a green result here describes code that is in no commit. commit the"
      echo "  pointer if the move is deliberate, or: git submodule update --init --recursive"
      [ "$rc" -lt 3 ] && rc=3
      ;;
    'U')
      echo "submodule $path has MERGE CONFLICTS."
      echo "  fix: resolve them inside $path, then stage the pointer."
      rc=4
      ;;
  esac
done <<EOF
$status
EOF

exit "$rc"

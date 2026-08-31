#!/usr/bin/env bash
#
# Refuse to gate a tree that is still mid-conflict.
#
# 2026-08-31: a session merged, the merge CONFLICTED, and the failure was
# swallowed by `git merge ... | tail -2 && ...` -- which reports `tail`'s exit
# status, not the merge's. The gate then ran on a tree with conflict markers in
# platformio.ini and printed "all green", because `--tests` never parses
# platformio.ini. A vacuous pass over precisely the file being changed.
#
# Nothing in the gate could have caught it. The suites that ran were genuinely
# green; the file with the markers was simply not read by any of them. The only
# evidence available was `git status`, and nothing was looking at it.
#
# What this REFUSES and what it allows, because the line matters:
#
#   unmerged paths (`git diff --diff-filter=U`)  -> REFUSE. The tree contains
#       markers. Nothing it reports means anything.
#   a merge in progress with every conflict RESOLVED and staged -> ALLOW.
#       Gating before committing a merge is good practice and is the main
#       reason anyone runs check.sh by hand.
#   conflict markers COMMITTED into a tracked file -> REFUSE. Rarer, worse,
#       and invisible to `git status`.
#
#   scripts_local/merge_state.sh [repo]
#
# Exit: 0 clean, 2 unmerged paths, 3 markers committed into tracked files.
set -uo pipefail

repo="${1:-$PWD}"

git -C "$repo" rev-parse --git-dir >/dev/null 2>&1 || exit 0  # not a repo

unmerged="$(git -C "$repo" diff --name-only --diff-filter=U 2>/dev/null || true)"
if [ -n "$unmerged" ]; then
  echo "this tree has UNRESOLVED merge conflicts, so nothing it reports means anything."
  printf '%s\n' "$unmerged" | sed 's/^/  conflicted: /'
  echo "  a suite that does not read one of these files passes without seeing the markers,"
  echo "  which is how a tree with markers in platformio.ini once gated all green."
  echo "  fix: resolve them, then re-run. (git status, git mergetool, or git merge --abort)"
  exit 2
fi

# Both markers in one file, not either alone: `=======` is an ordinary Markdown
# rule and `<<<<<<<` shows up in documentation about conflicts. Requiring the
# opening AND closing marker in the same file keeps this from crying wolf on
# docs that merely discuss merging.
marked=""
while IFS= read -r f; do
  [ -n "$f" ] || continue
  if git -C "$repo" grep -q -E '^>>>>>>> ' -- "$f" 2>/dev/null; then
    marked="$marked$f
"
  fi
done <<EOF
$(git -C "$repo" grep -l -E '^<<<<<<< ' -- . 2>/dev/null || true)
EOF

if [ -n "$marked" ]; then
  echo "this tree has conflict markers COMMITTED into tracked files."
  printf '%s' "$marked" | sed '/^$/d' | sed 's/^/  marked: /'
  echo "  git status will not mention these: the conflict was resolved by committing it."
  exit 3
fi

exit 0

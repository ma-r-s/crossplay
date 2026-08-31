#!/bin/bash
# scripts_local/merge_state.sh, against real conflicts in real repositories.
#
# The case: a merge conflicted, the failure was swallowed by a pipe
# (`git merge ... | tail -2` reports tail's status), and the gate ran on a tree
# with markers in platformio.ini and reported all green -- because --tests never
# parses platformio.ini. The suites were honestly green. The broken file was
# simply not read by any of them.
#
# Case 3 is the one that keeps this honest: a merge whose conflicts are RESOLVED
# must still gate. Refusing it would break the main reason anyone runs check.sh
# by hand -- checking a merge before committing it -- and a guard that blocks
# the normal path gets switched off.
#
#   host-tests/mergestate/run.sh
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
PROBE="$ROOT/scripts_local/merge_state.sh"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

checks=0
failed=0
ok()  { checks=$((checks + 1)); }
bad() { checks=$((checks + 1)); failed=$((failed + 1)); echo "FAIL mergestate  $1"; }

[ -x "$PROBE" ] || { echo "FAIL mergestate  $PROBE missing or not executable"; exit 1; }

# A repo with two branches that touch the same line, so merging really conflicts
# rather than being staged to look like it.
build_conflict() {
  rm -rf "$WORK/repo"
  git init --quiet "$WORK/repo"
  git -C "$WORK/repo" config user.email t@t
  git -C "$WORK/repo" config user.name t
  git -C "$WORK/repo" checkout --quiet -b main
  printf 'extra_scripts =\n  pre:one.py\n' > "$WORK/repo/platformio.ini"
  git -C "$WORK/repo" add -A
  git -C "$WORK/repo" commit --quiet --no-verify -m base
  git -C "$WORK/repo" checkout --quiet -b other
  printf 'extra_scripts =\n  pre:theirs.py\n' > "$WORK/repo/platformio.ini"
  git -C "$WORK/repo" add -A
  git -C "$WORK/repo" commit --quiet --no-verify -m theirs
  git -C "$WORK/repo" checkout --quiet main
  printf 'extra_scripts =\n  pre:ours.py\n' > "$WORK/repo/platformio.ini"
  git -C "$WORK/repo" add -A
  git -C "$WORK/repo" commit --quiet --no-verify -m ours
  git -C "$WORK/repo" merge other >/dev/null 2>&1  # conflicts, on purpose
}

# 1. A clean tree says nothing.
rm -rf "$WORK/clean"
git init --quiet "$WORK/clean"
git -C "$WORK/clean" config user.email t@t
git -C "$WORK/clean" config user.name t
echo hello > "$WORK/clean/a.txt"
git -C "$WORK/clean" add -A
git -C "$WORK/clean" commit --quiet --no-verify -m base
out="$("$PROBE" "$WORK/clean" 2>&1)"; rc=$?
if [ "$rc" -eq 0 ] && [ -z "$out" ]; then ok; else
  bad "a clean tree was not silent+0 (rc=$rc): $out"
fi

# 2. THE CASE. Unresolved conflict: refuse, and name the file.
build_conflict
if [ -z "$(git -C "$WORK/repo" diff --name-only --diff-filter=U)" ]; then
  bad "fixture did not actually conflict; the rest of this suite would prove nothing"
else
  ok
fi
out="$("$PROBE" "$WORK/repo" 2>&1)"; rc=$?
if [ "$rc" -eq 2 ]; then ok; else
  bad "an unresolved conflict reported rc=$rc, expected 2: $out"
fi
case "$out" in
  *platformio.ini*) ok ;;
  *) bad "the refusal does not name the conflicted file: $out" ;;
esac

# 3. Conflicts RESOLVED but the merge not yet committed: must still gate. This
#    is the normal "check before committing a merge" path.
build_conflict
printf 'extra_scripts =\n  pre:ours.py\n  pre:theirs.py\n' > "$WORK/repo/platformio.ini"
git -C "$WORK/repo" add platformio.ini
out="$("$PROBE" "$WORK/repo" 2>&1)"; rc=$?
if [ "$rc" -eq 0 ] && [ -z "$out" ]; then ok; else
  bad "a RESOLVED merge was refused (rc=$rc); that blocks the main reason to run check.sh by hand: $out"
fi

# 3b. THE CASE THAT DISTINGUISHES THIS GUARD FROM THE OBVIOUS ONE: resolved on
#     disk, NOT staged. There is nothing left to scan and the tree is still
#     half-resolved. Measured, not reasoned:
#
#       after the conflict:    markers 1, index unmerged
#       resolved on disk only: markers 0, index unmerged   <-- this case
#       after git add:         index clean
#
#     Without this, a marker-SCANNING implementation passes every other case in
#     this file, because they all leave markers on disk alongside the unmerged
#     index. The suite could not then tell this guard from the weaker one it
#     exists to beat -- and a suite a weaker implementation would pass is not
#     evidence for the stronger one. Found on 2026-08-31, after the guard
#     refused its own branch in exactly this state during a merge resolve.
build_conflict
printf 'extra_scripts =\n  pre:ours.py\n  pre:theirs.py\n' > "$WORK/repo/platformio.ini"
# NO git add here. That is the entire point.
markers="$(grep -c '^<<<<<<<' "$WORK/repo/platformio.ini" || true)"
if [ "$markers" -eq 0 ]; then ok; else
  bad "fixture still has $markers marker(s) on disk, so it cannot prove a scanner would miss this state"
fi
out="$("$PROBE" "$WORK/repo" 2>&1)"; rc=$?
if [ "$rc" -eq 2 ]; then ok; else
  bad "resolved-on-disk-but-unstaged reported rc=$rc, expected 2; the index still holds it unmerged and a gate here runs on a half-resolved tree: $out"
fi

# 4. Markers committed into a tracked file: git status is silent, this is not.
rm -rf "$WORK/marked"
git init --quiet "$WORK/marked"
git -C "$WORK/marked" config user.email t@t
git -C "$WORK/marked" config user.name t
printf '<<<<<<< HEAD\nours\n=======\ntheirs\n>>>>>>> other\n' > "$WORK/marked/platformio.ini"
git -C "$WORK/marked" add -A
git -C "$WORK/marked" commit --quiet --no-verify -m "oops"
if [ -z "$(git -C "$WORK/marked" status --porcelain)" ]; then ok; else
  bad "fixture is dirty; this case must prove git status alone would say nothing"
fi
out="$("$PROBE" "$WORK/marked" 2>&1)"; rc=$?
if [ "$rc" -eq 3 ]; then ok; else
  bad "committed conflict markers reported rc=$rc, expected 3: $out"
fi

# 5. Documentation that merely TALKS about conflicts must not be refused. A
#    guard that fires on docs/ describing a merge is one people turn off.
rm -rf "$WORK/docs"
git init --quiet "$WORK/docs"
git -C "$WORK/docs" config user.email t@t
git -C "$WORK/docs" config user.name t
mkdir -p "$WORK/docs/docs"
# The marker must sit at COLUMN 0, or the probe's line-anchored pattern never
# matches it and this case passes for the wrong reason -- which is what the
# first draft did: dropping the closing-marker requirement altogether left the
# suite green, because the fixture had never tripped the opening one.
printf 'When git conflicts it writes:\n\n```\n<<<<<<< HEAD\nyours\n```\n\nResolve it by hand.\n' > "$WORK/docs/docs/merging.md"
printf '\n# heading\n\n=======\n' >> "$WORK/docs/docs/merging.md"
git -C "$WORK/docs" add -A
git -C "$WORK/docs" commit --quiet --no-verify -m docs
out="$("$PROBE" "$WORK/docs" 2>&1)"; rc=$?
if [ "$rc" -eq 0 ]; then ok; else
  bad "a doc describing conflict markers was refused (rc=$rc); the closing marker is what distinguishes them: $out"
fi

echo "$checks checks, $failed failed"
[ "$failed" -eq 0 ]

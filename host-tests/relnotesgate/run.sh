#!/bin/bash
# The release-notes gate enumerates tags reachable from THIS tree's HEAD, not
# every tag in the object store -- against repositories built for the purpose.
#
# host-tests/release/run.sh asserts docs/release-notes.md holds a "### x.y.z"
# block for every v1.12+ tagged release. It used to build that list with a bare
# `git tag --list 'v1.12.*'`, which lists EVERY tag in the repository. Every
# worktree shares one object store, so the instant crossplay-autorelease.yml
# cut v1.12.N its tag was visible from every tree at once -- including a tree
# whose branch was behind origin/xteink and had therefore NOT merged the bump
# commit that carries that release's ### entry. The gate then demanded an entry
# the tree could not have yet and went red on a release the tree did not even
# contain. It red-gated essentially every session the night of 2026-09-04/05.
#
# The fix scopes the enumeration to `--merged HEAD`: only tags whose commit is
# an ancestor of the tree's own HEAD. On xteink and in CI, HEAD contains every
# release, so the gate still asks for all of them -- the protection is unchanged
# where it ships. On a behind tree, the new tag drops out until the tree merges
# xteink, which brings the tag's commit AND its ### entry in together.
#
# This suite reproduces the enumeration loop of host-tests/release verbatim
# (same 1.12.0 skip, same `grep -qxF "### $ver"`), in BOTH the old bare form and
# the new --merged HEAD form, against a repo carrying the exact shape: a tag in
# the object store that the checked-out branch does not contain. It asserts the
# bare form red-gates the behind tree (the bug) while --merged HEAD does not
# (the fix), and that a genuinely missing entry on a CONTAINED tag still goes
# red under both (the defect the gate exists for is not weakened). A final guard
# asserts the production gate actually uses --merged HEAD, so reverting the
# source is caught even though the behavioural halves would stay green.
#
#   host-tests/relnotesgate/run.sh
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$HERE/../.." && pwd)"
GATE="$REPO_ROOT/host-tests/release/run.sh"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

PASS=0; FAIL=0
ok()  { PASS=$((PASS+1)); echo "  ok   $1"; }
bad() { FAIL=$((FAIL+1)); echo "  FAIL $1"; }
q()   { "$@" >/dev/null 2>&1; }

[ -f "$GATE" ] || { echo "FAIL relnotesgate cannot find $GATE"; exit 1; }

# The release gate's enumeration + missing-entry loop, verbatim except for the
# ONE thing under test: which tags are enumerated. mode=bare is the old code,
# mode=merged is the fix. Echoes the space-separated versions it would report
# as missing -- empty means the gate passes (green), non-empty means it fails
# (red). $repo's checked-out HEAD selects both the tag scope (for merged) and
# the working-tree release-notes.md the loop reads, exactly as $ROOT does.
missing_under() {  # <mode: bare|merged> <repo>
  local mode="$1" repo="$2" tags="" missing="" tagname ver
  local notes="$repo/docs/release-notes.md"
  if [ "$mode" = merged ]; then
    tags="$(git -C "$repo" tag --list 'v1.12.*' --merged HEAD 2>/dev/null)"
  else
    tags="$(git -C "$repo" tag --list 'v1.12.*' 2>/dev/null)"
  fi
  while IFS= read -r tagname; do
    [ -n "${tagname:-}" ] || continue
    ver="${tagname#v}"
    [ "$ver" = "1.12.0" ] && continue
    grep -qxF "### $ver" "$notes" || missing="$missing $ver"
  done <<EOF
$tags
EOF
  printf '%s' "${missing# }"
}

# A release-notes history holding a ### block for each version passed, newest
# first, in the shape release_notes.py writes.
write_notes() {  # <file> <version> [<version> ...]
  local f="$1"; shift
  {
    echo "# CrossPlay release history"
    echo
    echo "<!-- releases, newest first -->"
    echo
    local v
    for v in "$@"; do
      echo "### $v"
      echo
      echo "- what is new in $v"
      echo
    done
  } > "$f"
}

# -- build a repo that mirrors origin/xteink: a chain of tagged releases, each
#    bump commit adding its own ### entry, plus a worker branch that split off
#    at 1.12.27, BEFORE 1.12.28 was cut. --
R="$WORK/repo"; mkdir -p "$R/docs"; cd "$R" || exit 1
q git init -q -b xteink
q git config user.email t@t; q git config user.name t

write_notes docs/release-notes.md 1.12.26
echo base > a.txt; q git add -A; q git commit -qm "chore: crossplay 1.12.26"; q git tag v1.12.26

echo work27 >> a.txt; q git add -A; q git commit -qm "feat: work toward 27"
write_notes docs/release-notes.md 1.12.27 1.12.26
q git add -A; q git commit -qm "chore: crossplay 1.12.27"; q git tag v1.12.27

# A worker cut its tree from origin/xteink here, at the 1.12.27 bump. Its HEAD
# does not and will not contain anything cut after this point until it merges.
q git branch app/worker

# xteink moves on and autorelease cuts 1.12.28: a new bump commit with the new
# ### entry, and a new tag now visible from every worktree's shared object store.
echo work28 >> a.txt; q git add -A; q git commit -qm "feat: work toward 28"
write_notes docs/release-notes.md 1.12.28 1.12.27 1.12.26
q git add -A; q git commit -qm "chore: crossplay 1.12.28"; q git tag v1.12.28

# ============================================================================
# Scenario 1: the behind tree. Its working-tree notes stop at 1.12.27; the
# v1.12.28 tag exists in the object store it shares. This is the exact state
# that red-gated the sessions.
# ============================================================================
q git -C "$R" checkout app/worker
# Sanity: the shared object store really does expose the newer tag to this
# branch -- without that, there is no bug to reproduce.
if git -C "$R" tag --list 'v1.12.*' | grep -qx v1.12.28; then
  ok "the behind tree can see v1.12.28 in the shared object store"
else
  bad "the fixture does not expose v1.12.28 to the behind tree; nothing to reproduce"
fi

old="$(missing_under bare "$R")"
if [ "$old" = "1.12.28" ]; then
  ok "OLD bare enumeration red-gates the behind tree (reproduces the bug: demands $old)"
else
  bad "OLD enumeration did not reproduce the bug; missing='$old' expected '1.12.28'"
fi

new="$(missing_under merged "$R")"
if [ -z "$new" ]; then
  ok "NEW --merged HEAD enumeration passes the behind tree (fix: demands nothing)"
else
  bad "NEW enumeration still red-gates the behind tree; missing='$new' expected empty"
fi

# ============================================================================
# Scenario 2: a CONTAINED tag genuinely missing its entry. HEAD is xteink, which
# contains v1.12.28's commit, but the working-tree notes have lost the 1.12.28
# block. This is the real defect the gate exists to catch; both forms must.
# ============================================================================
q git -C "$R" checkout xteink
write_notes "$R/docs/release-notes.md" 1.12.27 1.12.26   # 1.12.28 entry removed, tag still in history

old2="$(missing_under bare "$R")"
if [ "$old2" = "1.12.28" ]; then
  ok "OLD enumeration still catches a genuinely missing entry on a contained tag"
else
  bad "OLD enumeration missed a genuine defect; missing='$old2' expected '1.12.28'"
fi

new2="$(missing_under merged "$R")"
if [ "$new2" = "1.12.28" ]; then
  ok "NEW --merged HEAD still catches a genuinely missing entry on a contained tag"
else
  bad "NEW enumeration missed a genuine defect; missing='$new2' expected '1.12.28'"
fi

# ============================================================================
# Scenario 3: a healthy xteink with every entry present. Neither form may report
# anything -- the fix must not suppress a real requirement on a contained tag.
# ============================================================================
write_notes "$R/docs/release-notes.md" 1.12.28 1.12.27 1.12.26

old3="$(missing_under bare "$R")"
new3="$(missing_under merged "$R")"
if [ -z "$old3" ] && [ -z "$new3" ]; then
  ok "a healthy xteink passes under both forms (fix does not over-suppress)"
else
  bad "a healthy xteink was red-gated; bare='$old3' merged='$new3' expected both empty"
fi

# ============================================================================
# The production gate must actually carry the fix. The behavioural halves above
# test git's semantics on a fixture; this asserts host-tests/release enumerates
# with --merged HEAD and still checks each tag the same way. Reverting the source
# would leave the fixture green and this red.
# ============================================================================
if grep -qE "tag --list 'v1\.12\.\*' --merged HEAD" "$GATE"; then
  ok "host-tests/release enumerates tags with --merged HEAD"
else
  bad "host-tests/release no longer scopes the tag list to --merged HEAD; the hole is reopened"
fi
if grep -qF 'grep -qxF "### $ver"' "$GATE"; then
  ok "host-tests/release still checks each tag with an exact whole-line grep"
else
  bad "host-tests/release changed how it checks each tag; this suite's reproduction may be stale"
fi

echo "$((PASS+FAIL)) checks, $FAIL failed"
[ "$FAIL" -eq 0 ]

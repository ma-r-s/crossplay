#!/bin/bash
# wt.sh cuts new trees from origin/xteink, and reports against it.
#
# The bug this exists for: `wt.sh new` defaulted to the LOCAL xteink branch.
# firmware-next's local xteink carries a release train's commits for the whole
# bump-to-push window, so a tree cut during a train silently adopted somebody
# else's unpushed release. On 2026-08-30 a session gated another session's
# unpushed release notes that way and filed the resulting failure as its own
# discovery, and two branches in the workspace were sitting on an in-flight
# train's commits at the moment this was written.
#
# The same constant was wrong twice more, in `list` and in `drop`, where it made
# every tree's "commits ahead" column read high while any train was in flight --
# a status column that lies exactly when the workspace is busiest.
#
# The test drives the REAL wt.sh against a synthetic workspace whose local
# branch is deliberately ahead of its origin, rather than asserting against the
# text of the script: a grep for "origin/xteink" would pass on a script that
# mentioned it in a comment.
#
#   host-tests/wtbase/run.sh
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
WT="$HERE/../../scripts_local/wt.sh"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

[ -f "$WT" ] || { echo "FAIL cannot find $WT"; exit 1; }

PASS=0; FAIL=0
ok()   { PASS=$((PASS+1)); echo "  ok   $1"; }
bad()  { FAIL=$((FAIL+1)); echo "  FAIL $1"; }
is()   { [ "$2" = "$3" ] && ok "$1" || bad "$1 (want '$3', got '$2')"; }

# Runs a command quietly. It does NOT prepend git: an earlier version did, so
# every "q git clone" ran "git git clone", every setup step failed silently, the
# cd into the fixture failed, and the rest of the script drove wt.sh against the
# REAL workspace -- creating a branch and a worktree in it. Hence also the hard
# cd guard below: in a script that builds a sandbox, a cd that fails must stop
# the run, not continue it somewhere else.
q() { "$@" >/dev/null 2>&1; }

cdd() { cd "$1" || { echo "FAIL cannot enter $1 -- refusing to run outside the fixture"; exit 1; }; }

# A workspace marker so wt.sh's find_workspace puts wt/ under $WORK and not
# somewhere inside the real one. Without this the test would create trees in
# Mario's actual workspace, which is a far worse failure than a red suite.
touch "$WORK/.xteink-workspace"

# origin: a bare repo whose xteink is one commit BEHIND the integration clone.
q git init --bare -b xteink "$WORK/origin.git"
q git clone "$WORK/origin.git" "$WORK/integration"
cdd "$WORK/integration"
q git config user.email t@t; q git config user.name t
mkdir -p scripts_local
echo published > file.txt
q git add -A; q git commit -m "published"
q git push -u origin xteink
PUBLISHED="$(git rev-parse HEAD)"

# The train: one commit that exists only locally, exactly like a version bump
# and its release notes sitting between the merge and the push.
echo unpushed >> file.txt
q git add -A; q git commit -m "chore: unpushed release train"
UNPUSHED="$(git rev-parse HEAD)"

cp "$WT" scripts_local/wt.sh
chmod +x scripts_local/wt.sh
q git add -A; q git commit -m "wt.sh"
# Committing wt.sh moved local xteink again; origin still has only PUBLISHED,
# so the local branch is now two ahead. That is the state under test.
LOCAL_TIP="$(git rev-parse HEAD)"

# If any of the setup above did not take, stop. Driving wt.sh from the wrong
# directory is not a failed test, it is a mutation of Mario's workspace.
case "$PWD" in
  "$WORK"/*) ;;
  *) echo "FAIL fixture not in place (cwd $PWD)"; exit 1 ;;
esac
[ -f "$WORK/.xteink-workspace" ] || { echo "FAIL workspace marker missing"; exit 1; }

echo "wt.sh base"

./scripts_local/wt.sh new probe >/dev/null 2>&1
BASE="$(git rev-parse app/probe 2>/dev/null || echo missing)"
is "new cuts from origin/xteink, not the local branch" "$BASE" "$PUBLISHED"
[ "$BASE" = "$UNPUSHED" ] && bad "new inherited the train's unpushed commit"
[ "$BASE" = "$LOCAL_TIP" ] && bad "new inherited the local tip"

# The status column must not count the train's commits against every tree.
LIST="$(./scripts_local/wt.sh list 2>/dev/null | grep '^probe ')"
case "$LIST" in
  *"commit(s) ahead"*) bad "list counts a clean tree as ahead: $LIST" ;;
  *)                   ok  "list reports a freshly cut tree as level" ;;
esac

# A tree that really is ahead must still say so, or the fix has just blinded
# the column in the other direction.
cdd "$WORK/wt/probe"
q git config user.email t@t; q git config user.name t
echo work >> file.txt
q git add -A; q git commit -m "real work"
cdd "$WORK/integration"
case "$(./scripts_local/wt.sh list 2>/dev/null | grep '^probe ')" in
  *"1 commit(s) ahead of origin/xteink"*) ok "list still reports genuine unmerged work" ;;
  *) bad "list lost a genuinely ahead tree: $(./scripts_local/wt.sh list 2>/dev/null | grep '^probe ')" ;;
esac

# --from must still win, or a deliberate stack on another branch is impossible.
./scripts_local/wt.sh new stacked --from app/probe >/dev/null 2>&1
is "--from still overrides the default" \
   "$(git rev-parse app/stacked 2>/dev/null || echo missing)" \
   "$(git rev-parse app/probe)"

echo "wt.sh prune"

# Four trees: probe and stacked carry a commit not in origin (kept), merged1 is
# level and clean (goes), dirtyone is level with an edit (kept). prune must
# never remove anything it cannot recreate.
./scripts_local/wt.sh new merged1 >/dev/null 2>&1
./scripts_local/wt.sh new dirtyone >/dev/null 2>&1
echo edit >> "$WORK/wt/dirtyone/file.txt"
DRY="$(./scripts_local/wt.sh prune --dry-run 2>&1)"
case "$DRY" in
  *"would drop merged1"*) ok "dry run names the merged, clean tree" ;;
  *) bad "dry run did not name merged1: $DRY" ;;
esac
[ -d "$WORK/wt/merged1" ] && ok "dry run removes nothing" || bad "dry run removed merged1"
OUT="$(./scripts_local/wt.sh prune 2>&1)"
[ ! -d "$WORK/wt/merged1" ] && ok "prune drops the merged, clean tree" || bad "prune kept merged1: $OUT"
git rev-parse -q --verify app/merged1 >/dev/null 2>&1 && bad "prune left the merged branch behind" || ok "and its branch"
[ -d "$WORK/wt/probe" ] && [ -d "$WORK/wt/stacked" ] && ok "prune keeps trees with unmerged commits" || bad "prune dropped an unmerged tree: $OUT"
[ -d "$WORK/wt/dirtyone" ] && ok "prune keeps a dirty tree" || bad "prune dropped a dirty tree: $OUT"
case "$OUT" in
  *"1 dropped, 3 kept"*) ok "and says what it did" ;;
  *) bad "summary line wrong: $OUT" ;;
esac

echo "$((PASS+FAIL)) checks, $FAIL failed"
[ "$FAIL" -eq 0 ]

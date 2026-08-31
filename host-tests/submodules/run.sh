#!/bin/bash
# scripts_local/submodule_state.sh, against real repositories with real
# submodules -- because the bug it exists to prevent is a probe that answers
# confidently about a directory it is not looking at.
#
# Case 3 is the whole reason this suite exists. An UNINITIALISED submodule has
# no .git of its own, so `git -C <sub> rev-parse HEAD` walks up and returns the
# SUPERPROJECT's HEAD. A probe built that way calls every uninitialised tree
# "drift" and names a commit that is not an SDK revision at all. To prove this
# suite can see that: replace the `git submodule status` call in the probe with
# a rev-parse comparison and case 3 flips from 2 to 3.
#
#   host-tests/submodules/run.sh
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
PROBE="$ROOT/scripts_local/submodule_state.sh"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

checks=0
failed=0
ok()  { checks=$((checks + 1)); }
bad() { checks=$((checks + 1)); failed=$((failed + 1)); echo "FAIL submodules  $1"; }

[ -x "$PROBE" ] || { echo "FAIL submodules  $PROBE missing or not executable"; exit 1; }

# Local-path submodules need the file protocol explicitly since CVE-2022-39253.
git_() { git -c protocol.file.allow=always "$@"; }

# An SDK with two revisions, and a superproject recording the FIRST.
build_fixtures() {
  mkdir -p "$WORK/sdk"
  # git -C throughout, not a subshell: the two SHAs have to escape this
  # function, and capturing them by branch name is how the first draft of this
  # fixture broke (init.defaultBranch is not the same on every machine).
  git -C "$WORK/sdk" init --quiet
  git -C "$WORK/sdk" config user.email t@t
  git -C "$WORK/sdk" config user.name t
  echo v1 > "$WORK/sdk/sdk.txt"
  git -C "$WORK/sdk" add -A
  git -C "$WORK/sdk" commit --quiet --no-verify -m "sdk A"
  SDK_A="$(git -C "$WORK/sdk" rev-parse HEAD)"
  echo v2 > "$WORK/sdk/sdk.txt"
  git -C "$WORK/sdk" add -A
  git -C "$WORK/sdk" commit --quiet --no-verify -m "sdk B"
  SDK_B="$(git -C "$WORK/sdk" rev-parse HEAD)"
  git -C "$WORK/sdk" checkout --quiet "$SDK_A"  # so a fresh add records A

  mkdir -p "$WORK/super"
  (
    cd "$WORK/super"
    git init --quiet
    git config user.email t@t; git config user.name t
    echo top > top.txt
    git add -A && git commit --quiet --no-verify -m "super base"
    git_ submodule add --quiet "$WORK/sdk" sdk 2>/dev/null
    git add -A && git commit --quiet --no-verify -m "add sdk at A"
  )
}
build_fixtures

# 1. Everything matching: silent, exit 0. A probe that cries wolf on a healthy
#    tree gets switched off within a day.
out="$("$PROBE" "$WORK/super" 2>&1)"; rc=$?
if [ "$rc" -eq 0 ] && [ -z "$out" ]; then ok; else
  bad "a healthy tree was not silent+0 (rc=$rc, out=$out)"
fi

# 2. No submodules at all: also silent, exit 0.
mkdir -p "$WORK/plain"
(cd "$WORK/plain" && git init --quiet && git config user.email t@t && git config user.name t &&
   echo x > a.txt && git add -A && git commit --quiet --no-verify -m base)
out="$("$PROBE" "$WORK/plain" 2>&1)"; rc=$?
if [ "$rc" -eq 0 ] && [ -z "$out" ]; then ok; else
  bad "a repo with no submodules was not silent+0 (rc=$rc, out=$out)"
fi

# 3. THE TRAP. Uninitialised must report UNINITIALISED (2), never drift (3).
#    A rev-parse probe reports the superproject's own HEAD here and calls it
#    drift, sending the reader hunting for an SDK revision that does not exist.
rm -rf "$WORK/fresh"
git_ clone --quiet "$WORK/super" "$WORK/fresh" 2>/dev/null
out="$("$PROBE" "$WORK/fresh" 2>&1)"; rc=$?
if [ "$rc" -eq 2 ]; then ok; else
  bad "an UNINITIALISED submodule reported rc=$rc, expected 2 (3 means the rev-parse walk-up bug is back): $out"
fi
case "$out" in
  *"submodule update --init"*) ok ;;
  *) bad "the uninitialised message does not name the fix command: $out" ;;
esac
super_head="$(cd "$WORK/fresh" && git rev-parse HEAD)"
case "$out" in
  *"$super_head"*) bad "the uninitialised message quotes the SUPERPROJECT's HEAD ($super_head) as if it were an SDK revision" ;;
  *) ok ;;
esac

# 4. Real drift: initialised, then moved off what the superproject records.
rm -rf "$WORK/drift"
git_ clone --quiet "$WORK/super" "$WORK/drift" 2>/dev/null
(cd "$WORK/drift" && git_ submodule update --init --quiet 2>/dev/null)
(cd "$WORK/drift/sdk" && git checkout --quiet "$SDK_B")
out="$("$PROBE" "$WORK/drift" 2>&1)"; rc=$?
if [ "$rc" -eq 3 ]; then ok; else
  bad "a drifted submodule reported rc=$rc, expected 3: $out"
fi
case "$out" in
  *"${SDK_B:0:8}"*) ok ;;
  *) bad "the drift message does not name the commit actually checked out (${SDK_B:0:8}): $out" ;;
esac
case "$out" in
  *"${SDK_A:0:8}"*) ok ;;
  *) bad "the drift message does not name the commit the tree records (${SDK_A:0:8}): $out" ;;
esac

# 5. Drift must be distinguishable from uninitialised by EXIT CODE alone --
#    check.sh stops the gate for one and merely qualifies the verdict for the
#    other, so collapsing them would either block legitimate SDK-bump work or
#    let a meaningless green through.
rm -rf "$WORK/fresh2"
git_ clone --quiet "$WORK/super" "$WORK/fresh2" 2>/dev/null
"$PROBE" "$WORK/fresh2" >/dev/null 2>&1; rc_uninit=$?
"$PROBE" "$WORK/drift"  >/dev/null 2>&1; rc_drift=$?
if [ "$rc_uninit" -ne "$rc_drift" ]; then ok; else
  bad "uninitialised and drift share exit code $rc_uninit; check.sh cannot tell them apart"
fi

echo "$checks checks, $failed failed"
[ "$failed" -eq 0 ]

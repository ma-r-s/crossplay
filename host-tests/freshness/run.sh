#!/bin/bash
# scripts_local/tree_freshness.sh, against real clones of a real remote.
#
# The failure it guards is a gate that passes while describing a tree nobody
# has: an armed watcher fired `--committed` on 2026-08-31 without merging
# origin first, because it was written before that requirement existed. It was
# caught at a few seconds old. Automation does not read messages, so the guard
# has to be on disk.
#
# The distinction cases (3 vs 4) are the ones with teeth. Both must be
# non-zero -- being behind by "only" host-tests/ or docs/ still changes what
# the suites check, so it still qualifies the verdict -- but they must DIFFER,
# because check.sh says something different about each and a probe that
# collapsed them would make the firmware case invisible.
#
#   host-tests/freshness/run.sh
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
PROBE="$ROOT/scripts_local/tree_freshness.sh"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

checks=0
failed=0
ok()  { checks=$((checks + 1)); }
bad() { checks=$((checks + 1)); failed=$((failed + 1)); echo "FAIL freshness  $1"; }

[ -x "$PROBE" ] || { echo "FAIL freshness  $PROBE missing or not executable"; exit 1; }

# The probe reuses scripts_local/device-build-needed.sh to tell firmware
# commits from inert ones, so the fixture clone needs it on disk.
seed_clone() {
  rm -rf "$WORK/remote" "$WORK/clone" "$WORK/seed"
  git init --quiet --bare "$WORK/remote"
  git init --quiet "$WORK/seed"
  git -C "$WORK/seed" config user.email t@t
  git -C "$WORK/seed" config user.name t
  git -C "$WORK/seed" checkout --quiet -b xteink
  mkdir -p "$WORK/seed/src" "$WORK/seed/docs"
  echo base > "$WORK/seed/src/main.cpp"
  echo base > "$WORK/seed/docs/readme.md"
  git -C "$WORK/seed" add -A
  git -C "$WORK/seed" commit --quiet --no-verify -m base
  git -C "$WORK/seed" remote add origin "$WORK/remote"
  git -C "$WORK/seed" push --quiet origin xteink 2>/dev/null
  # --branch matters: a bare init points HEAD at its own default branch, so a
  # plain clone lands on an unborn HEAD that has no relationship to
  # origin/xteink at all. That is a different bug from staleness and case 8
  # covers it deliberately.
  git clone --quiet --branch xteink "$WORK/remote" "$WORK/clone" 2>/dev/null
  git -C "$WORK/clone" config user.email t@t
  git -C "$WORK/clone" config user.name t
  mkdir -p "$WORK/clone/scripts_local"
  cp "$ROOT/scripts_local/device-build-needed.sh" "$WORK/clone/scripts_local/"
  chmod +x "$WORK/clone/scripts_local/device-build-needed.sh"
}

# Add a commit to the remote and make the clone AWARE of it without merging --
# which is exactly the state the armed watcher gated in.
advance_remote() {  # path, content
  echo "$2" > "$WORK/seed/$1"
  mkdir -p "$(dirname "$WORK/seed/$1")"
  echo "$2" > "$WORK/seed/$1"
  git -C "$WORK/seed" add -A
  git -C "$WORK/seed" commit --quiet --no-verify -m "change $1"
  git -C "$WORK/seed" push --quiet origin xteink 2>/dev/null
  git -C "$WORK/clone" fetch --quiet origin 2>/dev/null
}

# 1. Up to date: silent, exit 0. A probe that speaks on a healthy tree is one
#    people learn to scroll past, which is the failure it exists to prevent.
seed_clone
out="$("$PROBE" "$WORK/clone" 2>&1)"; rc=$?
if [ "$rc" -eq 0 ] && [ -z "$out" ]; then ok; else
  bad "an up-to-date tree was not silent+0 (rc=$rc, out=$out)"
fi

# 2. Behind by a FIRMWARE commit: exit 4.
seed_clone
advance_remote src/main.cpp changed
out="$("$PROBE" "$WORK/clone" 2>&1)"; rc=$?
if [ "$rc" -eq 4 ]; then ok; else
  bad "behind by a src/ commit reported rc=$rc, expected 4: $out"
fi
case "$out" in
  *"1 commit"*) ok ;;
  *) bad "the message does not say how far behind it is: $out" ;;
esac
case "$out" in
  *"git merge"*) ok ;;
  *) bad "the message does not name the fix: $out" ;;
esac

# 3. Behind by INERT commits only: exit 3, still non-zero. docs/ cannot change
#    a device image, but "cannot reach the binary" is not "cannot change what
#    this gate means", so it still qualifies.
seed_clone
advance_remote docs/readme.md changed
out="$("$PROBE" "$WORK/clone" 2>&1)"; rc=$?
if [ "$rc" -eq 3 ]; then ok; else
  bad "behind by a docs-only commit reported rc=$rc, expected 3: $out"
fi

# 4. The two must be told apart. Collapsing them hides the firmware case.
seed_clone; advance_remote src/main.cpp changed
"$PROBE" "$WORK/clone" >/dev/null 2>&1; rc_fw=$?
seed_clone; advance_remote docs/readme.md changed
"$PROBE" "$WORK/clone" >/dev/null 2>&1; rc_docs=$?
if [ "$rc_fw" -ne "$rc_docs" ] && [ "$rc_fw" -ne 0 ] && [ "$rc_docs" -ne 0 ]; then ok; else
  bad "firmware ($rc_fw) and inert ($rc_docs) staleness are not distinguishable non-zero codes"
fi

# 5. AHEAD of origin is not behind it: ordinary mid-work, must stay silent.
seed_clone
echo mine > "$WORK/clone/src/main.cpp"
git -C "$WORK/clone" add -A
git -C "$WORK/clone" commit --quiet --no-verify -m "my work"
out="$("$PROBE" "$WORK/clone" 2>&1)"; rc=$?
if [ "$rc" -eq 0 ] && [ -z "$out" ]; then ok; else
  bad "a tree merely AHEAD of origin was reported as stale (rc=$rc): $out"
fi

# 6. No such remote ref: silent, not a crash and not a nag.
rm -rf "$WORK/lonely"
git init --quiet "$WORK/lonely"
git -C "$WORK/lonely" config user.email t@t
git -C "$WORK/lonely" config user.name t
echo x > "$WORK/lonely/a.txt"
git -C "$WORK/lonely" add -A
git -C "$WORK/lonely" commit --quiet --no-verify -m base
out="$("$PROBE" "$WORK/lonely" 2>&1)"; rc=$?
if [ "$rc" -eq 0 ] && [ -z "$out" ]; then ok; else
  bad "a repo with no origin/xteink was not silent+0 (rc=$rc): $out"
fi

# 7. Not a git tree at all: silent, exit 0.
mkdir -p "$WORK/notrepo"
out="$("$PROBE" "$WORK/notrepo" 2>&1)"; rc=$?
if [ "$rc" -eq 0 ] && [ -z "$out" ]; then ok; else
  bad "a non-repo was not silent+0 (rc=$rc): $out"
fi

# 8. Unknown must NOT read as fresh. A HEAD that cannot be compared with the
#    ref (unborn, or an unrelated history) made the first draft of this probe
#    answer "up to date" via a `|| echo 0` fallback -- a measurement that never
#    ran, reported as a healthy result. Unknown qualifies instead.
rm -rf "$WORK/unrelated"
git init --quiet "$WORK/unrelated"
git -C "$WORK/unrelated" config user.email t@t
git -C "$WORK/unrelated" config user.name t
git -C "$WORK/unrelated" remote add origin "$WORK/remote" 2>/dev/null
git -C "$WORK/unrelated" fetch --quiet origin 2>/dev/null
out="$("$PROBE" "$WORK/unrelated" 2>&1)"; rc=$?
if [ "$rc" -ne 0 ]; then ok; else
  bad "an uncomparable HEAD reported rc=0, i.e. 'up to date' for a comparison that never happened: $out"
fi
case "$out" in
  *UNKNOWN*) ok ;;
  *) bad "the uncomparable case does not say the freshness is unknown: $out" ;;
esac

echo "$checks checks, $failed failed"
[ "$failed" -eq 0 ]

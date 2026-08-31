#!/usr/bin/env bash
#
# Say when the tree being gated is behind the branch it will be judged against.
#
# 2026-08-31: a session armed a watcher to run `--committed` the moment v1.11.1
# appeared. It fired, and did not merge origin first -- because it was written
# an hour before that requirement existed. Caught at a few seconds old; had it
# not been, it would have produced a fully green gate on 26-commit-stale code,
# describing a tree nobody has and nobody will ship.
#
# **Automation written before a rule does not know the rule, and it does not
# read messages.** A guard on disk is the only thing that reaches it. That is
# the third armed-watcher incident of one night.
#
# Same shape as scripts_local/submodule_state.sh one level up: there the
# SUBMODULE was not what the commit described, here the COMMIT is not what the
# branch describes. Both end the same way -- a green that describes nothing --
# and both are answered by qualifying the verdict rather than by a note at the
# top, because a warning 400 lines above "all green" is one this workspace has
# now demonstrably read past three times in a night.
#
# Being behind NEVER stops the gate. Unlike an uninitialised submodule, which
# cannot compile, working behind origin is ordinary: a branch cut yesterday is
# behind by definition, and a guard that refuses that would be switched off
# within the day.
#
#   scripts_local/tree_freshness.sh [--fetch] [--ref origin/xteink] [repo]
#
# Exit: 0 up to date (or nothing to compare against)
#       3 behind, and none of the missing commits can reach a device image
#       4 behind, and the missing commits touch firmware
#
# BOTH 3 AND 4 QUALIFY THE VERDICT. "Cannot reach a device image" is not the
# same as "cannot change this gate's meaning": host-tests/, scripts_local/ and
# .github/ are all inert for a firmware BINARY and all decide what the suites
# check. A stale tree missing a new suite passes by not running it.
set -uo pipefail

REF="origin/xteink"
FETCH=""
repo=""
while [ $# -gt 0 ]; do
  case "$1" in
    --fetch) FETCH=1; shift ;;
    --ref)   REF="${2:-}"; [ -n "$REF" ] || { echo "--ref needs a ref" >&2; exit 0; }; shift 2 ;;
    *)       repo="$1"; shift ;;
  esac
done
repo="${repo:-$PWD}"

git -C "$repo" rev-parse --git-dir >/dev/null 2>&1 || exit 0  # not a repo: nothing to say

# The remote ref is only as fresh as the last fetch, and a gate that measures
# staleness against a stale ref is the joke telling itself. Fetching is opt-in
# because `--tests` runs often and a hanging network should never be the reason
# a host suite is slow.
fetch_failed=""
if [ -n "$FETCH" ]; then
  git -C "$repo" fetch --quiet "${REF%%/*}" 2>/dev/null || fetch_failed=1
fi

git -C "$repo" rev-parse -q --verify "$REF" >/dev/null 2>&1 || exit 0  # no such ref: a clone with no origin

# NOT `|| echo 0`. An unborn HEAD, or one with no relationship to the ref,
# makes rev-list fail, and defaulting that to zero reports a tree as up to date
# precisely when nothing could be compared -- silence standing in for a
# measurement that never happened, which is the failure this whole guard is
# about. Unknown qualifies the verdict, the same direction
# device-build-needed.sh takes when it cannot answer.
if ! behind="$(git -C "$repo" rev-list --count "HEAD..$REF" 2>/dev/null)"; then
  echo "could not compare HEAD with $REF, so this tree's freshness is UNKNOWN."
  echo "  (an unborn HEAD, or a history unrelated to $REF)"
  exit 3
fi

if [ "$behind" -eq 0 ]; then
  # Say it only when the measurement itself is doubtful. Otherwise silence is
  # the right output for a healthy tree.
  if [ -n "$fetch_failed" ]; then
    echo "could not fetch ${REF%%/*}; freshness was measured against a possibly stale $REF."
    exit 0
  fi
  exit 0
fi

base="$(git -C "$repo" merge-base HEAD "$REF" 2>/dev/null || true)"
newest="$(git -C "$repo" log --format='%h %s' -1 "$REF" 2>/dev/null || true)"

rc=3
detail="none of them reach a device image, but they can still change what the suites check"
helper="$repo/scripts_local/device-build-needed.sh"
if [ -n "$base" ] && [ -x "$helper" ]; then
  # In a subshell cd'd to the repo: the helper uses plain `git`, so run from
  # anywhere else it classifies whatever tree the caller happens to be sitting
  # in and answers confidently about the wrong repository.
  if ( cd "$repo" && "$helper" --range "$base..$REF" --quiet >/dev/null 2>&1 ); then
    rc=4
    detail="and they touch firmware, so the binaries this gate builds are not the ones being shipped"
  fi
elif [ -z "$base" ]; then
  # No merge base: unrelated histories. Cannot classify, so assume the worse.
  rc=4
  detail="and they cannot be classified (no merge base), so assume they matter"
fi

echo "this tree is $behind commit(s) behind $REF, $detail."
[ -n "$newest" ] && echo "  newest missing: $newest"
[ -n "$fetch_failed" ] && echo "  (fetch failed, so it may be further behind than this)"
echo "  fix: git merge $REF"
exit "$rc"

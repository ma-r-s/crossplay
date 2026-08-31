#!/bin/bash
# Do the cross-compiled device builds need to run for this change?
#
# Why this exists: check.sh runs four device builds for every change, behind a
# workspace-wide lock, taking roughly twenty minutes. It runs them for a
# documentation edit and for a release-notes rewrite exactly as it runs them for
# a change to src/. With several trees working at once that is most of the
# contention in the workspace: sessions queue on a lock for builds that cannot
# produce a different byte, and one session abandoned the integration tree
# entirely to cherry-pick a site-only change through a throwaway worktree
# rather than pay it.
#
# The rule is an allowlist of paths that CANNOT reach a device image, and
# anything unrecognised means build. That direction matters: a denylist of
# firmware paths would silently skip the builds the day somebody adds a new
# top-level directory that feeds them. Unknown must mean build, always.
#
#   scripts_local/device-build-needed.sh [--base <ref>] [--quiet]
#
# Exit 0: device builds are needed (also the answer whenever anything is
#         uncertain -- an unresolvable base, a git that will not answer, a
#         release gate).
# Exit 1: nothing in this change can alter a device image.
set -uo pipefail

BASE_REF="origin/xteink"
QUIET=""
# --range asks the same question about somebody else's commits rather than
# ours: "do the commits this tree is MISSING touch a device image?" The
# allowlist below is the only correct answer to that, and copying it into a
# second script is how two spellings of one rule start disagreeing.
RANGE=""
while [ $# -gt 0 ]; do
  case "$1" in
    --base)  BASE_REF="${2:-}"; [ -n "$BASE_REF" ] || { echo "--base needs a ref" >&2; exit 0; }; shift 2 ;;
    --range) RANGE="${2:-}"; [ -n "$RANGE" ] || { echo "--range needs A..B" >&2; exit 0; }; shift 2 ;;
    --quiet) QUIET=1; shift ;;
    *)       echo "unknown option: $1" >&2; exit 0 ;;
  esac
done

say() { [ -n "$QUIET" ] || echo "$@"; }

# Paths that cannot reach a device image. Anything not matched here means the
# builds run. The device image is built from src/, lib/, freeink-sdk/,
# platformio*.ini, partitions.csv and the pre/post scripts in scripts/ -- and
# scripts/build_html.py and scripts/gen_i18n.py were each read to confirm they
# generate only from src/ and lib/ rather than from site/ or docs/.
#
# Defined up here rather than beside its first use because --range answers
# before the merge-base work below, and a function called above its definition
# is a runtime error, not a syntax one.
inert() {
  case "$1" in
    docs/*|site/*|host-tests/*|server/*|tools_local/*|scripts_local/*) return 0 ;;
    .github/*|.githooks/*|.skills/*|bin/*|nix/*)                       return 0 ;;
    *.md|LICENSE|.gitignore|.clang-format|.clangd|requirements.txt)    return 0 ;;
    *) return 1 ;;
  esac
}

# A release gate builds the images it is about to publish, whatever the diff
# says. Skipping there would mean tagging binaries nothing in this run built.
if [ -n "${CHECK_BUILD_RELEASE_ENVS:-}" ]; then
  say "device builds: needed (release envs requested)"
  exit 0
fi

# Everything below is fail-safe: any step that cannot answer exits 0.
git rev-parse --git-dir >/dev/null 2>&1 || { say "device builds: needed (not a git tree)"; exit 0; }

if [ -n "$RANGE" ]; then
  RANGE_A="${RANGE%%..*}"
  RANGE_B="${RANGE##*..}"
  if [ "$RANGE_A" = "$RANGE" ] || [ -z "$RANGE_A" ] || [ -z "$RANGE_B" ]; then
    say "device builds: needed (cannot read range $RANGE)"
    exit 0
  fi
  RANGE_CHANGED="$(git diff --name-only "$RANGE_A" "$RANGE_B" 2>/dev/null | sed 's/ -> /\n/' | sed '/^$/d' | sort -u)" || RANGE_CHANGED=""
  if ! git rev-parse -q --verify "$RANGE_A" >/dev/null 2>&1 || ! git rev-parse -q --verify "$RANGE_B" >/dev/null 2>&1; then
    say "device builds: needed (cannot resolve one end of $RANGE)"
    exit 0
  fi
  if [ -z "$RANGE_CHANGED" ]; then
    say "device builds: not needed (nothing changed in $RANGE)"
    exit 1
  fi
  WHY=""
  while IFS= read -r path; do
    [ -n "$path" ] || continue
    if ! inert "$path"; then WHY="$path"; break; fi
  done <<< "$RANGE_CHANGED"
  if [ -n "$WHY" ]; then
    say "device builds: needed ($WHY can reach a device image)"
    exit 0
  fi
  COUNT="$(printf '%s\n' "$RANGE_CHANGED" | grep -c '' || true)"
  say "device builds: not needed ($COUNT changed path(s) in $RANGE, none of which reach a device image)"
  exit 1
fi

BASE="$(git merge-base "$BASE_REF" HEAD 2>/dev/null)" || BASE=""
if [ -z "$BASE" ]; then
  say "device builds: needed (cannot resolve a merge base with $BASE_REF)"
  exit 0
fi

# Committed work since the base, plus anything uncommitted, plus untracked --
# a new file under src/ is untracked until it is added, and it is exactly the
# case where skipping would be worst.
CHANGED="$(
  { git diff --name-only "$BASE" HEAD 2>/dev/null
    git status --porcelain --ignore-submodules=untracked 2>/dev/null | sed 's/^...//'
    git status --porcelain 2>/dev/null | grep '^??' | sed 's/^?? //'
  } | sed 's/ -> /\n/' | sed '/^$/d' | sort -u
)"

if [ -z "$CHANGED" ]; then
  say "device builds: not needed (no changes against $BASE_REF)"
  exit 1
fi

WHY=""
while IFS= read -r path; do
  [ -n "$path" ] || continue
  if ! inert "$path"; then
    WHY="$path"
    break
  fi
done <<< "$CHANGED"

if [ -n "$WHY" ]; then
  say "device builds: needed ($WHY can reach a device image)"
  exit 0
fi

COUNT="$(printf '%s\n' "$CHANGED" | grep -c '' || true)"
say "device builds: not needed ($COUNT changed path(s), none of which reach a device image)"
exit 1

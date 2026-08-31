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
#   scripts_local/device-build-needed.sh --device-only [--base <ref>] [--quiet]
#
# --device-only answers the MIRROR question: does this change touch code the
# host gate cannot see at all? The simulator target does not compile what sits
# behind FREEINK_DEVICE_* guards, what calls ESP-IDF directly, or anything in
# the SDK driver layer. For such a branch a green host gate is not weak
# evidence, it is NO evidence -- so it must keep its device build BEFORE
# landing, even when the workspace is otherwise landing on host-green.
#
# app/sdkbump is the case: a submodule bump carrying 69 upstream commits of
# display driver, e-ink init and touch mirroring. Its host suites were green
# before either device build had run, and that green meant almost nothing.
#
# Both directions live here so they cannot drift apart.
#
# Exit 0: device builds are needed (also the answer whenever anything is
#         uncertain -- an unresolvable base, a git that will not answer, a
#         release gate).
# Exit 1: nothing in this change can alter a device image.
set -uo pipefail

BASE_REF="origin/xteink"
QUIET=""
DEVICE_ONLY=""
while [ $# -gt 0 ]; do
  case "$1" in
    --base)  BASE_REF="${2:-}"; [ -n "$BASE_REF" ] || { echo "--base needs a ref" >&2; exit 0; }; shift 2 ;;
    --device-only) DEVICE_ONLY=1; shift ;;
    --quiet) QUIET=1; shift ;;
    *)       echo "unknown option: $1" >&2; exit 0 ;;
  esac
done

say() { [ -n "$QUIET" ] || echo "$@"; }

# A release gate builds the images it is about to publish, whatever the diff
# says. Skipping there would mean tagging binaries nothing in this run built.
if [ -n "${CHECK_BUILD_RELEASE_ENVS:-}" ]; then
  say "device builds: needed (release envs requested)"
  exit 0
fi

# Everything below is fail-safe: any step that cannot answer exits 0.
git rev-parse --git-dir >/dev/null 2>&1 || { say "device builds: needed (not a git tree)"; exit 0; }

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

if [ -n "$DEVICE_ONLY" ]; then
  # Conservative by construction, same direction as the path gate: anything we
  # cannot rule out keeps its device build. A file is device-only if the host
  # target never compiles it, which we detect from the markers rather than from
  # a maintained list of filenames.
  while IFS= read -r path; do
    [ -n "$path" ] || continue
    case "$path" in
      freeink-sdk|platformio*.ini|partitions.csv)
        say "device gate required before landing ($path is not compiled by the host target)"
        exit 0 ;;
    esac
    case "$path" in
      src/*|lib/*)
        [ -f "$path" ] || continue
        if grep -qE 'FREEINK_DEVICE_|esp_[a-z_]+\(|#include <esp|driver/' "$path" 2>/dev/null; then
          say "device gate required before landing ($path contains code the host target does not compile)"
          exit 0
        fi ;;
    esac
  done <<< "$CHANGED"
  say "host-green is sufficient (nothing here is invisible to the host target)"
  exit 1
fi

# Paths that cannot reach a device image. Anything not matched here means the
# builds run. The device image is built from src/, lib/, freeink-sdk/,
# platformio*.ini, partitions.csv and the pre/post scripts in scripts/ -- and
# scripts/build_html.py and scripts/gen_i18n.py were each read to confirm they
# generate only from src/ and lib/ rather than from site/ or docs/.
inert() {
  case "$1" in
    docs/*|site/*|host-tests/*|server/*|tools_local/*|scripts_local/*) return 0 ;;
    .github/*|.githooks/*|.skills/*|bin/*|nix/*)                       return 0 ;;
    *.md|LICENSE|.gitignore|.clang-format|.clangd|requirements.txt)    return 0 ;;
    *) return 1 ;;
  esac
}

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

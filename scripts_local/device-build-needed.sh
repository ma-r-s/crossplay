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
#   scripts_local/device-build-needed.sh [--base <ref>] [--quiet] [--build-loop]
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
DEVICE_ONLY=""
# --build-loop is the DEFAULT question asked by the one caller that is not an
# observer of the build: check.sh's own loop, deciding whether to run the envs
# it was about to run. For everyone else CHECK_BUILD_RELEASE_ENVS is evidence
# ("some run is building release images, so answer conservatively"); for the
# build loop it is that run's OWN intent, and a question whose answer is
# derived from the asker's intent is circular. So this mode alone does not
# consult it. Everything else about the answer -- the allowlist, the fail-safe
# exits, the untracked-file handling -- is identical, because a second spelling
# of the rule is how two spellings start disagreeing.
BUILD_LOOP=""
while [ $# -gt 0 ]; do
  case "$1" in
    --base)  BASE_REF="${2:-}"; [ -n "$BASE_REF" ] || { echo "--base needs a ref" >&2; exit 0; }; shift 2 ;;
    --range) RANGE="${2:-}"; [ -n "$RANGE" ] || { echo "--range needs A..B" >&2; exit 0; }; shift 2 ;;
    --device-only) DEVICE_ONLY=1; shift ;;
    --build-loop)  BUILD_LOOP=1; shift ;;
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
# A cold audit re-derived this list from platformio.ini upwards rather than
# checking the list it was given. It independently reached the scripts_local/
# conclusion below, and moved three more entries OFF -- all of them cases it
# could not RULE OUT rather than cases it disproved:
#
#   nix/            flake.nix pins PlatformIO Core and redirects
#                   PLATFORMIO_CORE_DIR, so which `pio` and which toolchain runs
#                   is decided there for anyone building through `nix develop`.
#   requirements.txt installed into that same venv by flake.nix.
#   .gitignore      inert for a build in the working tree, but --committed
#                   materialises a trial worktree FROM GIT, and what is ignored
#                   decides what was ever tracked and therefore what exists to
#                   compile.
#
# None of the three is a proven hazard. All three are edited about once a
# quarter, so the conservative answer costs nothing measurable and removes three
# things nobody can reason about at 2am. That is the trade this whole file makes.
#
# .github/ STAYED, on a stronger argument than "cannot affect it" -- it can:
# crossplay-ci.yml chooses the envs and injects -fstack-usage. But those builds
# run in CI, and check.sh's four run HERE. Compiling x4pro locally tells you
# nothing about a workflow file, so skipping loses no verification that running
# would have provided. Inert for this question, not inert in general.
#
# Defined up here rather than beside its first use because --range answers
# before the merge-base work below, and a function called above its definition
# is a runtime error, not a syntax one.
#
# scripts_local/ IS NOT ON THIS LIST, and that is deliberate rather than an
# oversight. Two of its files are `pre:` extra_scripts in platformio.ini --
# require_build_lock.py and sconsign_per_tree.py -- so they RUN INSIDE every
# device build and a bad edit to either fails it. The rest of the directory is
# the gate's own machinery (check.sh, cache-guard.sh), and a change there that
# broke the build loop would be masked by a run that skipped the build loop:
# the one place a wrong "inert" cannot be caught later by anything. It was on
# the list until this rule started deciding whether builds actually run; while
# nothing consulted it for that, the mistake was invisible.
#
# A .md under scripts_local/ still falls to the `*.md` branch below and stays
# inert, which is the right answer and needs no special case.
inert() {
  case "$1" in
    docs/*|site/*|host-tests/*|server/*|tools_local/*) return 0 ;;
    .github/*|.githooks/*|.skills/*|bin/*)             return 0 ;;
    *.md|LICENSE|.clang-format|.clangd)                return 0 ;;
    *) return 1 ;;
  esac
}

# Everything below is fail-safe: any step that cannot answer exits 0.
git rev-parse --git-dir >/dev/null 2>&1 || { say "device builds: needed (not a git tree)"; exit 0; }

if [ -n "$RANGE" ]; then
  RANGE_A="${RANGE%%..*}"
  RANGE_B="${RANGE##*..}"
  if [ "$RANGE_A" = "$RANGE" ] || [ -z "$RANGE_A" ] || [ -z "$RANGE_B" ]; then
    say "device builds: needed (cannot read range $RANGE)"
    exit 0
  fi
  if ! git rev-parse -q --verify "$RANGE_A" >/dev/null 2>&1 || ! git rev-parse -q --verify "$RANGE_B" >/dev/null 2>&1; then
    say "device builds: needed (cannot resolve one end of $RANGE)"
    exit 0
  fi
  # Diff from the MERGE BASE, not from A. `git diff A B` is symmetric: given two
  # branch tips it reports what A changed as well as what B changed, so a caller
  # asking "do the commits I am MISSING touch firmware?" gets back its own
  # firmware changes and a confident wrong answer. That happened on the tool's
  # first hand use. tree_freshness.sh passes an ancestor as A and is unaffected
  # -- merge-base of an ancestor is itself -- so this only fixes the callers who
  # would otherwise have to know to do it themselves. A report that is evidence
  # only when the caller constructed it correctly is not evidence.
  RANGE_BASE="$(git merge-base "$RANGE_A" "$RANGE_B" 2>/dev/null || true)"
  if [ -z "$RANGE_BASE" ]; then
    say "device builds: needed (no merge base between the ends of $RANGE)"
    exit 0
  fi
  RANGE_CHANGED="$(git diff --name-only "$RANGE_BASE" "$RANGE_B" 2>/dev/null | sed 's/ -> /\n/' | sed '/^$/d' | sort -u)" || RANGE_CHANGED=""
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

# A release gate builds the images it is about to publish, whatever the diff
# says: skipping there would mean tagging binaries nothing in this run built.
#
# It sits BELOW --range and --device-only on purpose. Those ask different
# questions -- "do the commits I am missing touch firmware?" and "can the host
# gate see this at all?" -- and neither is answered by "this run happens to be
# building release images". While this check was above them, `--committed`
# exports CHECK_BUILD_RELEASE_ENVS, so every staleness check inside a committed
# gate reported FIRMWARE no matter what the commits touched, and the inert case
# could never fire. host-tests/freshness caught it; nothing else could have,
# because the suite passes standalone and only fails where it actually runs.
# --device-only cannot be moved above this the way --range was: it walks
# $CHANGED, which is computed below. So the guard is on the MODE rather than on
# the position.
#
# --build-loop is exempt for a different reason, and the difference matters.
# The premise here is "somebody requested release images, so build them". Under
# check.sh --committed the somebody is check.sh, which set the variable itself
# three hundred lines earlier -- so honouring it there means the build loop can
# never skip anything, which is the entire point of asking. And the images are
# not published by that run in any case: crossplay-release.yml builds
# gh_release_x4pro and gh_release_sticky itself, after the tag
# (.github/workflows/crossplay-release.yml:77-80). The --committed release
# builds are a pre-flight for a typo, and a diff that cannot reach a device
# image cannot have introduced one.
if [ -z "$DEVICE_ONLY" ] && [ -z "$BUILD_LOOP" ] && [ -n "${CHECK_BUILD_RELEASE_ENVS:-}" ]; then
  say "device builds: needed (release envs requested)"
  exit 0
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

if [ -n "$DEVICE_ONLY" ]; then
  # The MIRROR question: not "can this change a device image" but "can the host
  # gate see it at all". The simulator target does not compile what sits behind
  # FREEINK_DEVICE_* guards, what calls ESP-IDF directly, or the SDK driver
  # layer, so for such a branch a green host gate is not weak evidence, it is
  # NO evidence. Conservative by construction, same direction as everything
  # else here: anything it cannot rule out keeps its device build.
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

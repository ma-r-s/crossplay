#!/bin/bash
# One classification table, two questions asked of it.
#
# TWO QUESTIONS, and for a day they shared one predicate:
#
#   builds  Can this path change what a device build produces, or whether that
#           build can run? check.sh asks it before spending many minutes on the
#           cross-compiled envs behind a workspace-wide lock.
#   ships   Can this path change what a RELEASE delivers -- the firmware a
#           device runs, or the files the release publishes? release-needed.sh
#           asks it before bumping the version, tagging, and putting an update
#           prompt on every device in the field; release_notes.py asks it of
#           each landing before writing that landing into the notes.
#
# WHY THEY CANNOT SHARE ONE ANSWER. The old rule was a single allowlist of
# inert paths with `*) live` underneath it -- live by default, which is the
# right direction for one question and the wrong direction for the other:
#
#   question | an unknown path defaults to | cost of being wrong
#   ---------|-----------------------------|-----------------------------------
#   builds   | build it                    | wasted minutes on a runner
#   ships    | release it                  | every device offered an update for
#            |                             | nothing
#
# v1.12.21 is what that cost looks like. The only path in v1.12.20..v1.12.21
# that was not inert was `.gitignore`, which is genuinely live for a build --
# a `--committed` gate materialises a trial worktree FROM GIT, so what is
# ignored decides what exists there to compile -- and cannot alter one byte of
# a published image, because a clean clone checks out every tracked file
# whatever the ignore rules say. One answer, read for both questions, cut a
# release whose only device-reaching change was the version number the release
# itself wrote.
#
# It fails in the other direction too, and that half is board card #190:
# `.github/**` is inert for a build, so PR #42 -- the fix for the packaging
# defect that shipped v1.12.14 and v1.12.15 with bootloader.bin, firmware.bin
# and partitions.bin MISSING from the published image -- was classified "no
# device can see it". A repair to what gets published was invisible to the
# thing deciding whether to publish, and to the notes describing what was
# published.
#
# So: one table, two columns, and both questions read the table rather than
# each other. There is still exactly one definition of each, which is the
# property the old single predicate was protecting and this keeps.
#
# AN UNCLASSIFIED PATH IS NOT A DEFAULT. `*) return 1` answered confidently
# about a file nobody had ever classified, and that confidence is why this went
# a day unnoticed. A path in no row now:
#
#   builds  runs the builds AND names itself. Never skip verification, and
#           never do it silently -- the loud line is what gets the row added.
#   ships   REFUSES. Exit 2, naming the path. Releasing and withholding are
#           both bad answers to a question nobody has answered, so it declines
#           to pick one and says which path it could not classify.
#
#   scripts_local/device-build-needed.sh [--base <ref>] [--quiet] [--build-loop]
#   scripts_local/device-build-needed.sh --range <A..B> [--ships]
#   scripts_local/device-build-needed.sh --ships
#
# Exit 0: yes (for --ships, something in the change reaches a user).
# Exit 1: no.
# Exit 2: --ships only -- cannot answer; the message names the path.
#
# For the build question, exit 0 is also the answer whenever anything is
# uncertain: an unresolvable base, a git that will not answer, a release gate.
set -uo pipefail

BASE_REF="origin/xteink"
QUIET=""
# --range asks the same question about somebody else's commits rather than
# ours: "do the commits this tree is MISSING touch a device image?" The table
# below is the only correct answer to that, and copying it into a second script
# is how two spellings of one rule start disagreeing.
RANGE=""
DEVICE_ONLY=""
# --ships reads the OTHER column. release-needed.sh and release_notes.py ask it;
# nothing about a build does.
SHIPS=""
# --build-loop is the DEFAULT question asked by the one caller that is not an
# observer of the build: check.sh's own loop, deciding whether to run the envs
# it was about to run. For everyone else CHECK_BUILD_RELEASE_ENVS is evidence
# ("some run is building release images, so answer conservatively"); for the
# build loop it is that run's OWN intent, and a question whose answer is
# derived from the asker's intent is circular. So this mode alone does not
# consult it. Everything else about the answer -- the table, the fail-safe
# exits, the untracked-file handling -- is identical, because a second spelling
# of the rule is how two spellings start disagreeing.
BUILD_LOOP=""
while [ $# -gt 0 ]; do
  case "$1" in
    --base)  BASE_REF="${2:-}"; [ -n "$BASE_REF" ] || { echo "--base needs a ref" >&2; exit 0; }; shift 2 ;;
    --range) RANGE="${2:-}"; [ -n "$RANGE" ] || { echo "--range needs A..B" >&2; exit 0; }; shift 2 ;;
    --device-only) DEVICE_ONLY=1; shift ;;
    --ships)       SHIPS=1; shift ;;
    --build-loop)  BUILD_LOOP=1; shift ;;
    --quiet) QUIET=1; shift ;;
    *)       echo "unknown option: $1" >&2; exit 0 ;;
  esac
done

say() { [ -n "$QUIET" ] || echo "$@"; }
# A refusal is never quiet. --quiet exists so a caller can use the exit code
# without the narration; it does not exist so a caller can be told nothing
# while the tool declines to answer.
loud() { echo "$@" >&2; }

# ---------------------------------------------------------------------------
# THE TABLE. One row per path prefix; two independent attributes per row.
#
# classify prints "<builds> <ships>", or NOTHING when the path is in no row.
# Nothing is a refusal, not a default: see builds(), ships() and unclassified()
# below, which are the only three readers of it.
#
# The `builds` column is a refactor and not a change: it reproduces, path for
# path, the answer the previous single predicate gave, including its three
# conservative entries (nix/, requirements.txt, .gitignore) and its treatment
# of scripts_local/ as live. host-tests/gatepath asserts every one of them and
# went green unchanged. Loosening that column is a separate change needing its
# own evidence; this one only stops the release questions reading it.
#
# The `ships` column is new. It means: could a person receive something
# different because of this? The firmware their device runs, or the files the
# release publishes. NOT the website -- crossplay.ma-r-s.com deploys
# continuously from site/ and is not part of a release, so a site change must
# never cut one.
#
# Order matters: the first matching row wins.
# ---------------------------------------------------------------------------
classify() {
  case "$1" in
    # Prose is prose wherever it sits. FIRST, so a README under a live
    # directory is not read as live -- which is what the previous predicate
    # did, by listing *.md after the directory branches in a case that could
    # only ever match inert ones.
    *.md|LICENSE|.clang-format|.clangd)               echo "no no"   ;;

    # THE ONE WORKFLOW THAT PUBLISHES. crossplay-release.yml builds both
    # devices, merges the images and uploads the assets a person downloads;
    # PR #42's fix for the missing bootloader lived here. Every other workflow
    # verifies (crossplay-ci.yml, ci.yml, pr-formatting-check.yml), schedules
    # (crossplay-autorelease.yml decides WHEN, never what) or rebuilds the
    # website (crossplay-emulator.yml). Checked rather than assumed: it is the
    # only workflow in the tree that references a release-publishing action.
    #
    # This is the whole of card #190's fix, and the separation is a file rather
    # than a judgement about a file's contents -- which is why it is clean. A
    # cosmetic edit to this one workflow will cut a release it did not need to;
    # that is the conservative direction, and it is edited a few times a year.
    .github/workflows/crossplay-release.yml)          echo "no yes"  ;;

    # Compiled, generated into the image, or naming what gets compiled.
    # scripts/ holds the pre:/post: extra_scripts that GENERATE source
    # (build_html.py, gen_i18n.py) and stamp the version into the binary
    # (git_branch.py). .gitmodules names which SDK a checkout gets.
    src/*|lib/*|scripts/*)                            echo "yes yes" ;;
    freeink-sdk|freeink-sdk/*|.gitmodules)            echo "yes yes" ;;
    platformio*.ini|partitions.csv)                   echo "yes yes" ;;

    # LIVE FOR A BUILD, INVISIBLE TO A RELEASE. Each of these can change what a
    # build here does and cannot change what anybody receives.
    #
    #   .gitignore       decides what a --committed trial worktree contains,
    #                    because that worktree is materialised from git. A
    #                    release is built from a clean clone, which checks out
    #                    every tracked file whatever the ignore rules say. This
    #                    is the row that would have stopped v1.12.21.
    #   nix/, requirements.txt
    #                    pin PlatformIO Core and the toolchain root for anyone
    #                    building through `nix develop`. CI installs PlatformIO
    #                    with pip and never enters the flake, so neither can
    #                    reach a published image.
    #   scripts_local/   the workspace's own machinery: a build lock, an SCons
    #                    signature shard and the gate itself. Two of them are
    #                    `pre:` extra_scripts, so they run INSIDE every device
    #                    build including CI's -- they can refuse a build or
    #                    slow it, which is why they build. They cannot put
    #                    different source into one, which is why they do not
    #                    ship. Anything that decides CONTENT lives in scripts/.
    #   assets_local/, sim-stubs/, test/, playground-submission/
    #                    nothing in a device build reads them (grepped:
    #                    sim-stubs is on the simulator env's include path only,
    #                    test/ is reached by `pio run -t unit-tests`). They are
    #                    kept live on the build axis purely because that is the
    #                    answer today; see the note above about loosening it.
    .gitignore)                                       echo "yes no"  ;;
    nix/*|requirements.txt)                           echo "yes no"  ;;
    scripts_local/*)                                  echo "yes no"  ;;
    assets_local/*|sim-stubs/*|test/*)                echo "yes no"  ;;
    playground-submission/*)                          echo "yes no"  ;;

    # Neither. docs/ includes docs/release-notes.md, which is this pipeline's
    # own output: the autorelease writes it, so a row making it ship would mean
    # every release caused the next one.
    docs/*|site/*|host-tests/*|server/*|tools_local/*) echo "no no"   ;;
    .github/*|.githooks/*|.skills/*|bin/*)            echo "no no"   ;;
  esac
}

# The three readers. Nothing else may look at classify's output.
builds() {  # 0 = this path can change a device build. Unclassified builds.
  local row; row="$(classify "$1")"
  [ -z "$row" ] && return 0
  [ "${row%% *}" = "yes" ]
}
ships() {   # 0 = a person could receive something different. Unclassified is
            # NOT answered here; ask unclassified() first.
  local row; row="$(classify "$1")"
  [ "${row##* }" = "yes" ]
}
unclassified() { [ -z "$(classify "$1")" ]; }

# The first path in a newline-separated list that is in no row, or nothing.
first_unclassified() {
  local path
  while IFS= read -r path; do
    [ -n "$path" ] || continue
    if unclassified "$path"; then printf '%s' "$path"; return 0; fi
  done <<< "$1"
  return 1
}

# The verdict for the ships column over a set of paths. Exit 0 ships, 1 does
# not, 2 cannot say.
ships_verdict() {  # <paths> <what the set is, for the message>
  local paths="$1" what="$2" path unknown
  if unknown="$(first_unclassified "$paths")"; then
    loud "cannot answer whether $what reaches a user: $unknown is in no row of"
    loud "the classification table in scripts_local/device-build-needed.sh."
    loud "Add a row for it -- releasing and withholding are both wrong answers"
    loud "to a question nobody has answered."
    return 2
  fi
  while IFS= read -r path; do
    [ -n "$path" ] || continue
    if ships "$path"; then
      say "reaches a user: yes ($path)"
      return 0
    fi
  done <<< "$paths"
  say "reaches a user: no (nothing in $what changes the firmware or what a release publishes)"
  return 1
}

# The verdict for the builds column. Exit 0 builds, 1 skips. Never 2: skipping
# verification is the one thing this must not do when it is unsure, so an
# unclassified path builds -- loudly, so the row gets added.
builds_verdict() {  # <paths> <what the set is, for the message>
  local paths="$1" what="$2" path
  if path="$(first_unclassified "$paths")"; then
    loud "$path is in no row of the classification table in"
    loud "scripts_local/device-build-needed.sh. Building, because skipping"
    loud "verification is the worse way to be wrong -- but add a row: the"
    loud "release questions REFUSE on an unclassified path."
    say "device builds: needed ($path is unclassified)"
    return 0
  fi
  while IFS= read -r path; do
    [ -n "$path" ] || continue
    if builds "$path"; then
      say "device builds: needed ($path can reach a device image)"
      return 0
    fi
  done <<< "$paths"
  local count; count="$(printf '%s\n' "$paths" | grep -c '' || true)"
  say "device builds: not needed ($count changed path(s) in $what, none of which reach a device image)"
  return 1
}

# Everything below is fail-safe: any step that cannot answer exits 0 for the
# build question and 2 for the ships question, which are the same answer in
# different currencies -- "do not act on me".
cannot_answer() {  # <one-line reason>
  if [ -n "$SHIPS" ]; then
    loud "cannot answer whether anything reaches a user: $1"
    exit 2
  fi
  say "device builds: needed ($1)"
  exit 0
}

git rev-parse --git-dir >/dev/null 2>&1 || cannot_answer "not a git tree"

if [ -n "$RANGE" ]; then
  RANGE_A="${RANGE%%..*}"
  RANGE_B="${RANGE##*..}"
  if [ "$RANGE_A" = "$RANGE" ] || [ -z "$RANGE_A" ] || [ -z "$RANGE_B" ]; then
    cannot_answer "cannot read range $RANGE"
  fi
  if ! git rev-parse -q --verify "$RANGE_A" >/dev/null 2>&1 || ! git rev-parse -q --verify "$RANGE_B" >/dev/null 2>&1; then
    cannot_answer "cannot resolve one end of $RANGE"
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
    cannot_answer "no merge base between the ends of $RANGE"
  fi
  RANGE_CHANGED="$(git diff --name-only --no-renames "$RANGE_BASE" "$RANGE_B" 2>/dev/null | sed 's/ -> /\n/' | sed '/^$/d' | sort -u)" || RANGE_CHANGED=""
  if [ -z "$RANGE_CHANGED" ]; then
    if [ -n "$SHIPS" ]; then
      say "reaches a user: no (nothing changed in $RANGE)"
    else
      say "device builds: not needed (nothing changed in $RANGE)"
    fi
    exit 1
  fi
  if [ -n "$SHIPS" ]; then
    ships_verdict "$RANGE_CHANGED" "$RANGE"
  else
    builds_verdict "$RANGE_CHANGED" "$RANGE"
  fi
  exit $?
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
# the position. --ships is exempt for the plainest reason of the three: it is
# not a question about a build at all.
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
if [ -z "$DEVICE_ONLY" ] && [ -z "$BUILD_LOOP" ] && [ -z "$SHIPS" ] && [ -n "${CHECK_BUILD_RELEASE_ENVS:-}" ]; then
  say "device builds: needed (release envs requested)"
  exit 0
fi

BASE="$(git merge-base "$BASE_REF" HEAD 2>/dev/null)" || BASE=""
if [ -z "$BASE" ]; then
  cannot_answer "cannot resolve a merge base with $BASE_REF"
fi

# Committed work since the base, plus anything uncommitted, plus untracked --
# a new file under src/ is untracked until it is added, and it is exactly the
# case where skipping would be worst.
# --no-renames on the diff, and it is the difference between a rule and a
# hazard. `git diff --name-only` runs rename DETECTION by default and collapses
# a detected rename to the NEW path only -- the old one never appears, with no
# arrow to split on. So `git mv src/main.cpp docs/main.cpp` presented as one
# inert path, and the rule cheerfully skipped every device build for a commit
# that removed a compiled translation unit. `git mv
# scripts_local/require_build_lock.py docs/x.py` did the same for a file that
# runs INSIDE every device build; that commit breaks the build for the whole
# workspace and this said it needed no verification.
#
# --no-renames reports the pair as a delete plus an add, so BOTH endpoints are
# classified and the old path forces the build. The `sed 's/ -> /\n/'` below
# was already there for the same hazard in `git status --porcelain`, whose
# rename format DOES carry an arrow -- and that naive first-occurrence split is
# safe for a structural reason worth writing down, because it looks unsafe: a
# path containing the literal " -> " makes git quote the endpoint
# UNCONDITIONALLY, regardless of core.quotePath, and a quoted fragment begins
# with a `"` that no row of the table can match. So a mangled split always
# leaves at least one fragment that falls through to unclassified -- which
# builds, and refuses to say whether it ships. A review tried core.quotePath,
# spaces, quotes, embedded newlines and non-ASCII against it and could not hide
# a firmware path -- so the author had seen this coming and the fix simply never
# reached the committed half, which is the half --committed uses exclusively
# (its trial worktree is a clean checkout, so `git status` there is empty by
# construction).
CHANGED="$(
  { git diff --name-only --no-renames "$BASE" HEAD 2>/dev/null
    git status --porcelain --ignore-submodules=untracked 2>/dev/null | sed 's/^...//'
    git status --porcelain 2>/dev/null | grep '^??' | sed 's/^?? //'
  } | sed 's/ -> /\n/' | sed '/^$/d' | sort -u
)"

if [ -z "$CHANGED" ]; then
  if [ -n "$SHIPS" ]; then
    say "reaches a user: no (no changes against $BASE_REF)"
  else
    say "device builds: not needed (no changes against $BASE_REF)"
  fi
  exit 1
fi

if [ -n "$SHIPS" ]; then
  ships_verdict "$CHANGED" "the changes against $BASE_REF"
  exit $?
fi

if [ -n "$DEVICE_ONLY" ]; then
  # The MIRROR question: not "can this change a device image" but "can the host
  # gate see it at all". The simulator target does not compile what sits behind
  # FREEINK_DEVICE_* guards, what calls ESP-IDF directly, or the SDK driver
  # layer, so for such a branch a green host gate is not weak evidence, it is
  # NO evidence.
  #
  # THIS WAS A DENYLIST UNTIL 2026-09-01, while its comment claimed it was
  # "conservative by construction... anything it cannot rule out keeps its
  # device build". That sentence described the behaviour below; the code did
  # the opposite. It demanded a device gate ONLY for freeink-sdk,
  # platformio*.ini, partitions.csv, and src|lib files carrying an ESP-IDF
  # marker -- and answered "host-green is sufficient" for EVERYTHING else, with
  # no crafted input required:
  #
  #   scripts_local/require_build_lock.py  runs INSIDE every device build
  #   scripts_local/check.sh               the gate's own machinery
  #   scripts/build_html.py                a pre: generator, runs in the build
  #   nix/flake.nix                        pins which pio and which toolchain
  #   brandnew/x.c                         a new top-level directory
  #   deleting src/main.cpp                `[ -f ]` failed, so it was skipped
  #
  # That is not plumbing: docs/contributing/landing-and-integration.md points
  # humans at this exact command as the bar for landing on host-green. Someone
  # editing build infrastructure was told it was safe to land, and no device
  # build ever confirmed the edit. The comment being right is precisely why
  # nobody read the code.
  #
  # It is an ALLOWLIST now, and it delegates the first question to the table's
  # builds column rather than restating it -- one spelling of the rule, for the
  # reason this file keeps repeating. Three ways to be sufficient, and unknown
  # is not one:
  #
  #   1. builds() says no  cannot reach a device image, so no device build
  #                        could report anything about it either way.
  #   2. src/ or lib/ WITH the file present and free of device-only markers:
  #                        the host target really does compile it.
  #   3. nothing else.
  while IFS= read -r path; do
    [ -n "$path" ] || continue

    builds "$path" || continue

    case "$path" in
      freeink-sdk|platformio*.ini|partitions.csv)
        say "device gate required before landing ($path is not compiled by the host target)"
        exit 0 ;;
    esac

    case "$path" in
      src/*|lib/*)
        # A deleted or renamed-away file cannot be inspected, and "I could not
        # look" must never read as "I looked and it was fine". The old code
        # `continue`d here, so removing a translation unit landed on host-green.
        if [ ! -f "$path" ]; then
          say "device gate required before landing ($path is gone from the tree; nothing here can be inspected)"
          exit 0
        fi
        if grep -qE 'FREEINK_DEVICE_|esp_[a-z_]+\(|#include <esp|driver/' "$path" 2>/dev/null; then
          say "device gate required before landing ($path contains code the host target does not compile)"
          exit 0
        fi
        continue ;;
    esac

    # Unknown. The entire point of the shape.
    say "device gate required before landing ($path is not covered by the host target)"
    exit 0
  done <<< "$CHANGED"
  say "host-green is sufficient (every changed path is inert or compiled by the host target)"
  exit 1
fi

builds_verdict "$CHANGED" "the changes against $BASE_REF"
exit $?

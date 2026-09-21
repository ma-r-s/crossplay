#!/bin/bash
# Land a reviewed branch and publish its release, from this Mac, in one command.
#
# WHY THIS EXISTS. Measured 2026-09-08..09-21 over 400 runs and 54 merged pull
# requests: one change was compiled FOUR times from cold on GitHub (the pull
# request, the merge, the tag, and the autorelease's own version-bump commit),
# about 4,955 runner-minutes, 92 per merged pull request, and 40 minutes of
# wall clock between a merge and the assets existing. Mario, 2026-09-21: "it
# takes over an hour... I am the only developer and everything runs on my
# laptop... I want the whole thing to take under a minute."
#
# None of those four builds was new work. `check.sh --committed` already clones
# the committed tree detached into TMPDIR, inits submodules, and builds
# `gh_release_x4pro` and `gh_release_sticky` against the shared object cache --
# the exact two envs crossplay-release.yml recompiled forty minutes later. The
# binary a user installs already existed on this disk and was thrown away.
#
# THE ORDER IS THE WHOLE DESIGN, and it is not the order the pipeline used.
#
# `platformio.ini` gives both release envs -DCROSSPOINT_VERSION="${crossplay
# .version}", so the version is COMPILED IN, and OtaUpdater.cpp:119 compares a
# release's tag (minus the v) against that compiled string. Publish a binary
# built before the bump under the tag after it and every device that installs
# it still reads its own version as the old one, so the update it just applied
# stays on offer forever. That is why the old pipeline rebuilt after the bump,
# and it is the one thing "just ship the gate's output" cannot do naively.
#
# So: bump FIRST, gate SECOND, and the images the gate leaves behind are the
# images that ship. Nothing is rebuilt and nothing is stale.
#
# THE GATE ALWAYS RUNS, and an earlier draft of this skipped it when the bump
# turned out to be a no-op. That was wrong twice over. check.sh --committed
# builds in a throwaway worktree that its own trap deletes, so a skipped gate
# leaves NOTHING to package; and on a tree where somebody had run `check.sh
# --flash gh_release_x4pro`, it left a PRE-BUMP image sitting in
# $REPO/.pio/build that would have passed every check here and published
# under the new tag. One gate, every time, and the images come from that
# gate's own handover directory named after the commit. That is still three
# builds fewer than the pipeline this replaced.
#
#   ./scripts_local/ship.sh --dry-run        # say what would happen, touch nothing
#   ./scripts_local/ship.sh                  # land this branch and publish
#
# WHAT THIS REFUSES TO DO. It does not land a branch that is behind xteink.
# The squash it performs produces a commit whose tree equals the branch tip's
# only while the branch is current; behind trunk, the squash resolves a merge
# and lands a tree nobody built. Rebase and re-gate; the message says so, and
# the trees are compared again after the merge rather than assumed.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
cd "$REPO" || exit 1

DRY=0
for arg in "$@"; do
  case "$arg" in
    --dry-run) DRY=1 ;;
    -h|--help) sed -n '2,45p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "ship: unknown argument '$arg'" >&2; exit 2 ;;
  esac
done

say()  { printf '%s\n' "$*"; }
step() { printf '\n== %s\n' "$*"; }
die()  { printf '\nship: %s\n' "$*" >&2; exit 1; }
# EVERY run() FAILURE STOPS THE SCRIPT.
#
# This is not `set -e` (which pipefail-with-tee and the `case` arms below
# both interact badly with); it is an explicit check on the one helper every
# state-changing command goes through. Without it, and this was the shape of
# the first version, a REJECTED `git push origin app/x:xteink` -- trunk moved
# during the fifteen-minute gate, or branch protection refused it -- printed
# "xteink is now <sha>", then tagged, then pushed the tag, then published a
# release of code that is on no branch, and exited 0 with nothing red.
run()  {
  if [ "$DRY" = 1 ]; then printf '   would: %s\n' "$*"; return 0; fi
  eval "$@" || die "this command failed, so nothing after it ran:
        $*
    Anything already done is listed above. A tag or a release that exists
    from a part-finished run has to be removed by hand before retrying."
}

# ---------------------------------------------------------------- preflight
#
# Every check here is one that has already gone wrong once on this pipeline,
# and each fails BEFORE anything is written, because the expensive half of
# this script is irreversible in public.
step "preflight"

BRANCH="$(git branch --show-current)"
[ -n "$BRANCH" ] || die "detached HEAD. Check out the branch you mean to land."
[ "$BRANCH" != "xteink" ] || die "you are on xteink. Run this from the branch you are landing."

# An uncommitted file is not in the commit the gate verifies, so the images it
# leaves behind are not the images this working tree describes. check.sh
# --committed says the same thing about itself; say it here, before the bump
# writes three files into a tree that was already dirty and makes the two
# impossible to tell apart.
[ -z "$(git status --porcelain)" ] || die "working tree is dirty. Commit or set aside first; ship.sh writes the version bump itself and will not mix it with your edits."

command -v gh >/dev/null   || die "gh is not on PATH; this script publishes with it."
gh auth status >/dev/null 2>&1 || die "gh is not authenticated. Run: gh auth login"

# THE TOOLCHAIN THAT BUILT THE IMAGES MUST BE THE PINNED ONE.
#
# This is the one thing that genuinely got weaker when publishing moved off
# GitHub. crossplay-release.yml installed
# platformio-core/archive/refs/tags/v6.1.19.zip on a fresh runner every time,
# so the published image was built by a known compiler by construction. Here
# it is built by whatever `pio` this Mac happens to have, which is a thing
# that drifts silently -- the fork already lost a day to clang-format 22
# reformatting 44 files that CI's 21 did not.
#
# So assert it instead of inheriting it. The pin is read out of the remaining
# workflows rather than written down twice.
PIN="$(sed 's/#.*//' "$REPO"/.github/workflows/*.yml 2>/dev/null \
       | grep -oE 'platformio-core/archive/refs/tags/v[0-9.]+\.zip' \
       | sed -E 's#.*/v([0-9.]+)\.zip#\1#' | sort | uniq -c | sort -rn | head -1 | sed 's/^ *[0-9]* *//')"
HAVE="$(pio --version 2>/dev/null | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)"
if [ -n "$PIN" ] && [ -n "$HAVE" ] && [ "$PIN" != "$HAVE" ]; then
  die "this Mac has PlatformIO $HAVE and the repository pins $PIN.
    The images you are about to publish were built by the wrong compiler, and
    nothing downstream would ever say so. Match it:
        uv pip install --system -U https://github.com/pioarduino/platformio-core/archive/refs/tags/v$PIN.zip"
fi
[ -n "$PIN" ] || say "  WARNING: no PlatformIO pin found in .github/workflows; the toolchain is unchecked"

# PlatformIO's own esptool, reached through PlatformIO's own interpreter,
# exactly as scripts_local/usb-flash.sh reaches it. Nothing is installed on
# this Mac for this script: the toolchain that built the images is the
# toolchain that packages them, and it is already here (v5.3.0, and it spells
# the subcommand `merge-bin` the way crossplay-release.yml did).
PIO_PY="$HOME/.platformio/penv/bin/python"
ESPTOOL="$HOME/.platformio/packages/tool-esptoolpy/esptool.py"
[ -x "$PIO_PY" ] && [ -f "$ESPTOOL" ] \
  || die "no PlatformIO esptool at $ESPTOOL. Run any build once to install the toolchain."

# ONE PUBLISH AT A TIME, ACROSS THE WHOLE WORKSPACE.
#
# crossplay-release.yml carried a concurrency group for this and its comment
# records why: v1.12.16 was built and published TWICE, one second apart, two
# runs racing to upload the same files, both exiting 0. Moving the publisher
# to this Mac does not remove that race, it renames it -- a dozen sessions
# share this workspace and any of them can reach ship.sh.
#
# mkdir is the atomic primitive here: macOS ships no flock(1), and a test
# followed by a write is exactly the race being closed. The lock records the
# pid so a crashed run can be told from a live one, and a stale lock says how
# to clear it rather than requiring anyone to guess.
LOCK="${TMPDIR:-/tmp}/xteink-ship.lock"
if [ "$DRY" = 0 ]; then
  if ! mkdir "$LOCK" 2>/dev/null; then
    holder="$(cat "$LOCK/pid" 2>/dev/null || echo unknown)"
    if [ "$holder" != unknown ] && kill -0 "$holder" 2>/dev/null; then
      die "another ship.sh is publishing right now (pid $holder). Wait for it: two publishes of one tag race to upload the same assets and both exit 0, which is how v1.12.16 shipped twice."
    fi
    die "a stale publish lock is in the way: $LOCK, left by pid $holder, which is not running.
    Check no release is half-published, then: rm -rf '$LOCK'"
  fi
  echo $$ > "$LOCK/pid"
  trap 'rm -rf "$LOCK"' EXIT INT TERM
fi

# THE FORK'S OWN TAGS ONLY, and this is not tidiness.
#
# `git fetch origin xteink --tags` EXITS 1 IN THIS REPOSITORY, always. The
# fork carries CrossPoint's whole history including its tags (0.1.0, 1.3.0,
# 1.4.0 ...), and fetching all tags tries to update those local copies:
#   ! [rejected] 1.3.0 -> 1.3.0 (would clobber existing tag)
# Nothing is wrong and nothing needs fixing; git just reports a non-zero
# status. run() then refuses, so the very first real run of this script died
# on its first command having done nothing at all.
#
# refs/tags/v* is the fork's own namespace -- upstream's carry no v -- so
# there is nothing to clobber, and v* is the only namespace last_tag() and
# release-needed.sh ever read.
run "git fetch -q origin xteink 'refs/tags/v*:refs/tags/v*'"

# UP TO DATE WITH TRUNK, OR NOTHING. The squash below produces a commit whose
# tree equals this branch's tip ONLY while the branch is already current with
# its base; behind trunk, the squash resolves a merge and lands a tree nobody
# built. The land step compares the trees afterwards too -- this is the cheap
# check that fails before fifteen minutes of gate rather than after.
BASE="$(git merge-base HEAD origin/xteink)"
TRUNK="$(git rev-parse origin/xteink)"
if [ "$BASE" != "$TRUNK" ]; then
  die "xteink has moved since this branch left it ($(git rev-list --count "$BASE".."$TRUNK") commits).
    A squash from here resolves a merge, and its tree is not the tree the gate
    built. Rebase and re-gate:
        git rebase origin/xteink && ./scripts_local/check.sh --committed"
fi

# The squash goes through GitHub so the pull request ends up MERGED with its
# mergeCommit set; release_notes.py maps commits to pull requests by that oid
# and by nothing else. No pull request, no mapping, no notes.
PR_NUMBER="$(gh pr list --repo ma-r-s/crossplay --head "$BRANCH" --state open --json number -q '.[0].number' 2>/dev/null)"
[ -n "$PR_NUMBER" ] || die "no open pull request for $BRANCH.
    ship.sh squashes through GitHub rather than pushing, because that is what
    marks the pull request merged and sets the mergeCommit oid the release
    notes are built from. Open one first:
        gh pr create --base xteink --head $BRANCH"
say "  pull request  #$PR_NUMBER"

# The hold is ONE GLOBAL SWITCH shared by every session. Read it rather than
# flip it, and treat any non-empty value as held: card #572 found the
# autorelease comparing it against the literal "1" while CLAUDE.md tells
# everyone to write "<card>:<session>:<why>", so the documented format sailed
# straight through the brake and two releases shipped under a hold.
# FAIL CLOSED. `|| echo ""` made any gh failure -- a network blip, a token
# without variables:read -- read as "no hold", so the one global brake on
# releasing was fail-open. The old workflow took it from the event context,
# which could not fail that way. A variable that is genuinely unset is not an
# error and gh says so with an empty result and status 0.
if ! HOLD="$(gh variable get RELEASE_HOLD --repo ma-r-s/crossplay 2>&1)"; then
  case "$HOLD" in
    *"not found"*|*"HTTP 404"*) HOLD="" ;;
    *) die "could not read RELEASE_HOLD, so it is not known whether releases are held:
$HOLD
    Refusing rather than assuming clear. The hold is the one global brake and
    it is shared by every session." ;;
  esac
fi
case "${HOLD:-0}" in
  0|false|"") ;;
  *) die "RELEASE_HOLD is set, by: $HOLD
    Clear it only if that reason has stopped being true:
        gh variable set RELEASE_HOLD --repo ma-r-s/crossplay --body 0" ;;
esac

say "  branch    $BRANCH -> xteink (current with trunk, $(git rev-list --count "$TRUNK"..HEAD) commit(s) to squash)"
say "  hold      clear"

# ----------------------------------------------------------------- version
#
# THE VERSION IS COMPILED IN, SO IT MUST PRECEDE THE BUILD. platformio.ini
# gives both release envs -DCROSSPOINT_VERSION="${crossplay.version}" and
# OtaUpdater.cpp:119 compares a release's tag against that compiled string,
# so an image built before the bump and published under the tag after it
# leaves every device offering an update it already installed.
#
# THE NOTES ARE NOT COMPILED IN, and that is why they are no longer written
# here. They need the pull request to be MERGED -- release_notes.py maps
# commits to pull requests by mergeCommit.oid -- so they are written after
# the squash, further down. Generating them here matched nothing, fell back
# to raw commit subjects, and never saw the release:minor label, which made
# every release silently a patch bump.
step "version"

if ! ./scripts_local/release-needed.sh >/dev/null 2>&1; then
  REL_OUT="$(./scripts_local/release-needed.sh 2>&1)"; REL_RC=$?
  [ "$REL_RC" = 2 ] && die "release-needed.sh REFUSED, because a changed path is in no row of the classification table:
$REL_OUT
    Add the row (scripts_local/device-build-needed.sh) saying whether that path
    builds and whether it ships, then run this again. Releasing for nothing and
    silently withholding a real fix are both wrong answers to a question nobody
    has answered."
  say "  nothing since the last tag reaches a user. Landing without a release."
  NEXT=""
else
  HAVE_VER="$(sed -n '/^\[crossplay\]/,/^\[/s/^version *= *//p' platformio.ini | head -1 | tr -d ' ')"
  LAST_TAG="$(git tag --list 'v*' --sort=-v:refname | head -1 | sed 's/^v//')"
  # A RETRY MUST NOT BURN A VERSION. release_notes.py derives the next one as
  # bump(what platformio.ini says), which is right on a first attempt and
  # wrong on every one after: this script's own version step has already
  # written the target there, so a re-run after a failed gate aims at
  # target+1 and the one after that at target+2. Failures here are expected
  # and are meant to be retried; the first live run hit two in a row.
  #
  # So: if platformio.ini is already ahead of the newest tag, an earlier
  # attempt set it and IT is the target. Otherwise ask for the bump.
  if [ -n "$LAST_TAG" ] && [ "$HAVE_VER" != "$LAST_TAG" ] \
     && [ "$(printf '%s\n%s\n' "$LAST_TAG" "$HAVE_VER" | sort -t. -k1,1n -k2,2n -k3,3n | tail -1)" = "$HAVE_VER" ]; then
    NEXT="$HAVE_VER"
    say "  next version  $NEXT (platformio.ini is already ahead of v$LAST_TAG; an earlier attempt set it)"
  else
    NEXT="$(python3 scripts_local/release_notes.py --repo ma-r-s/crossplay --dry-run 2>/dev/null | sed -n 's/^NEXT_VERSION=//p')"
    [ -n "$NEXT" ] || die "release_notes.py named no next version. Run it with --dry-run and read why."
    say "  next version  $NEXT"
  fi
fi
TAG="v${NEXT:-0.0.0}"

# THE VERSION AND THE NOTES ARE TWO CONDITIONS, NOT ONE.
#
# This was `if the version needs bumping`, with the notes written inside it.
# A retry whose earlier attempt had already bumped -- or, as here, a branch
# off a trunk that carries an unreleased bump -- then skipped the block
# entirely and left the notes a version behind, which is the exact tree the
# gate refuses. Two failed live runs in a row died that way, the second
# after the first was supposedly fixed.
#
# So ask about each separately: the version, because it is compiled in, and
# the notes, because the gate compares them against it.
NOTES_CURRENT=0
[ -n "$NEXT" ] && grep -q "### What is new in $NEXT" docs/release-body.md 2>/dev/null && NOTES_CURRENT=1

if [ -n "$NEXT" ]; then
  if [ "$HAVE_VER" != "$NEXT" ] || [ "$NOTES_CURRENT" = 0 ]; then
    # platformio.ini ONLY. A dry run cannot ask "did the bump change
    # something?" of the working tree, because in a dry run the bump did not
    # run; comparing the versions is the same question and answerable in both
    # modes.
    if [ "$HAVE_VER" != "$NEXT" ]; then
      run "python3 -c \"import pathlib,re; p=pathlib.Path('platformio.ini'); t=p.read_text(); p.write_text(re.sub(r'(?m)^(\\[crossplay\\](?:[^\\[]*?\\n)version *= *).*$', r'\\g<1>$NEXT', t, count=1))\""
    fi
    # AND THE NOTES, IN THE SAME COMMIT, because the gate checks them against
    # the version. host-tests/release refuses a tree whose release-body.md
    # names a different version than platformio.ini -- rightly, since that is
    # a release about to publish the previous one's text. Bumping the version
    # here and writing the notes only after the squash left exactly that
    # tree in front of the gate, so ship.sh could never pass its own gate.
    # Caught on the first live run; no dry run reaches it, because the gate
    # does not execute in dry mode.
    #
    # The pull request is not merged yet, so its data is handed over rather
    # than looked up: release_notes.py maps by mergeCommit.oid and there is
    # no merge commit until the squash. Pointing it at this branch's tip is
    # what makes the mapping land on the pull request that is being shipped.
    # These notes are provisional -- the `release notes` step regenerates
    # them after the squash, when GitHub has set the real oid -- and only
    # docs differ between the two, which nothing compiles.
    PRJSON="$(mktemp -t ship-prs)"
    run "gh pr view '$PR_NUMBER' --repo ma-r-s/crossplay --json number,title,body,labels \
        | python3 -c 'import json,sys; d=json.load(sys.stdin); d[\"mergeCommit\"]={\"oid\": sys.argv[1]}; print(json.dumps([d]))' \
          \"\$(git rev-parse HEAD)\" > '$PRJSON'"
    run "python3 scripts_local/release_notes.py --repo ma-r-s/crossplay --version '$NEXT' --pr-json '$PRJSON' --write"
    run "git add platformio.ini docs/release-notes.md docs/release-body.md"
    run "git commit -q -m 'chore: crossplay $NEXT'"
    run "git push -q origin '$BRANCH'"
    if [ "$HAVE_VER" != "$NEXT" ]; then
      say "  bumped $HAVE_VER -> $NEXT with its notes, pushed. The gate below builds the images that ship."
    else
      say "  version was already $NEXT; wrote the notes it was missing, pushed."
    fi
  else
    say "  already at $NEXT, notes current."
  fi
fi

# -------------------------------------------------------------------- gate
#
# Only when the bump changed something. The images this publishes are the ones
# check.sh leaves in .pio/build, so the gate is not a formality here: it is the
# build step. CHECK_FORCE_DEVICE_BUILDS because a bump-only diff touches
# platformio.ini and two documents, which device-build-needed.sh reads as
# reaching no device -- true of the diff, false of the version it carries.
#
# The verdict is a TOKEN YOU GREP FOR. `tail -1` returns a background
# wrapper's "[exited with code 0]" and $? is whatever the pipeline ended with;
# both have read a red gate as a pass in this workspace before.
step "gate"

GATE_LOG="$(mktemp -t ship-gate)"

# THE GATE IS THE BUILD, AND IT DOES NOT BUILD HERE.
#
# check.sh --committed builds in a throwaway worktree under TMPDIR and its
# own trap removes that worktree on exit, so $REPO/.pio/build holds nothing
# this run produced -- on a clean tree it does not exist at all. The first
# version of this script packaged from there anyway. On a normal tree that
# only fails; on a tree where somebody had run `check.sh --flash
# gh_release_x4pro` it would have found a PRE-BUMP image, passed every
# existence and magic-number check, and published firmware reporting the old
# version under the new tag. Every device that installed it would have gone
# on being offered the update it had just applied.
#
# So the gate hands them over explicitly, in a directory named after the
# commit, and ship.sh refuses any other source.
run "CHECK_KEEP_RELEASE_IMAGES=1 CHECK_FORCE_DEVICE_BUILDS=1 ./scripts_local/check.sh --committed 2>&1 | tee '$GATE_LOG'"
if [ "$DRY" = 0 ]; then
  VERDICT="$(grep -o 'CHECKSH-VERDICT: [a-z-]*' "$GATE_LOG" | tail -1 | sed 's/CHECKSH-VERDICT: //')"
  case "$VERDICT" in
    green) say "  verdict   green" ;;
    host-green-device-skipped) die "the gate skipped the device builds, so there are no images to publish. CHECK_FORCE_DEVICE_BUILDS did not take; read $GATE_LOG." ;;
    "")    die "the gate printed no verdict at all, which is not a pass. Read $GATE_LOG." ;;
    *)
      # The undo guard is the one gate refusal a CORRECT branch hits, and its
      # explanation is a hundred lines up in the log. A branch that deletes
      # code deliberately looks exactly like a stale tree committed over a
      # moved trunk, which is what that guard is for. Surface the escape
      # hatch here rather than making the reader dig for it.
      if grep -q 'UNDOES trunk' "$GATE_LOG"; then
        die "the gate refused because this branch REMOVES lines that are on trunk, which is the shape of a stale tree committed over a moved trunk.
    If the deletions are deliberate, say so and run this again:
        CHECK_ALLOW_UNDO=1 ./scripts_local/ship.sh
    If they are not, rebase first. The lines it named are in $GATE_LOG."
      fi
      die "gate verdict: $VERDICT. Nothing published. Read $GATE_LOG." ;;
  esac
  IMAGES="$(grep -o 'CHECKSH-IMAGES: .*' "$GATE_LOG" | tail -1 | sed 's/CHECKSH-IMAGES: //')"
  case "$IMAGES" in
    ""|none*) die "the gate published no images to package (CHECKSH-IMAGES: ${IMAGES:-absent}). Nothing published; read $GATE_LOG." ;;
  esac
  # The directory is named for the commit it was built from. Comparing that
  # against HEAD is the probe that catches a reused or stale handover, which
  # is the whole class the old in-tree read fell into.
  [ "$(basename "$IMAGES")" = "$(git rev-parse HEAD)" ] \
    || die "the gate's images are from $(basename "$IMAGES") and HEAD is $(git rev-parse HEAD). Nothing published."
  say "  images    $IMAGES"
else
  IMAGES="<the gate's output directory>"
fi

# -------------------------------------------------------------------- land
#
# SQUASH, NOT FAST-FORWARD, and the reason is the release notes rather than
# taste. release_notes.py walks `git log --first-parent <tag>..HEAD` and maps
# each commit to a pull request by mergeCommit.oid. Over a fast-forward that
# walk is EVERY COMMIT ON THE BRANCH, so a ten-commit branch becomes ten note
# lines instead of the one line its author wrote. A squash puts exactly one
# commit on trunk per pull request, which is the shape that walk was built
# for and has always assumed.
#
# Through `gh pr merge`, not a local squash-and-push, because GitHub has to
# be the one that does it: only then is the pull request MERGED with its
# mergeCommit set, and that oid is the whole of the mapping above. A local
# squash leaves the pull request open with its head unreachable and
# release_notes.py matching nothing, which is the bug this replaces.
#
# AND THE IMAGES ARE STILL THE RIGHT BYTES, proved rather than argued. A
# squash of a branch already up to date with its base produces a commit whose
# TREE equals the branch tip's. That is the property the fast-forward rule
# was protecting, and it survives -- but "should equal" is how the last defect
# got in, so the trees are compared.
step "land"

if [ "$DRY" = 1 ]; then
  say "   would: gh pr merge $PR_NUMBER --squash"
  say "   would: compare the new trunk tree against $(git rev-parse --short HEAD)'s"
else
  BRANCH_TREE="$(git rev-parse 'HEAD^{tree}')"
  run "gh pr merge '$PR_NUMBER' --repo ma-r-s/crossplay --squash --delete-branch=false"
  run "git fetch -q origin xteink"
  TRUNK_NEW="$(git rev-parse origin/xteink)"
  TRUNK_TREE="$(git rev-parse "$TRUNK_NEW^{tree}")"
  if [ "$BRANCH_TREE" != "$TRUNK_TREE" ]; then
    die "the squash landed a different tree than the one the gate built.
    branch $BRANCH_TREE
    trunk  $TRUNK_TREE
    Something else landed between the gate and the merge, so the images in
    the handover are not what is on xteink. Nothing tagged, nothing
    published. Re-run: the gate will rebuild against the new trunk."
  fi
  say "  xteink is now $(git rev-parse --short "$TRUNK_NEW"), same tree the gate built"
  run "git checkout -q --detach '$TRUNK_NEW'"
fi

if [ -z "$NEXT" ]; then
  say ""
  say "Landed. No release: nothing since the last tag reaches a user."
  exit 0
fi

# ------------------------------------------------------------------- notes
#
# HERE, because the pull request is merged only now and release_notes.py
# needs that: it maps commits to pull requests by mergeCommit.oid, and the
# oid did not exist until the squash above. Written before it, every line
# fell back to humanize(commit subject) and the release:minor label was never
# seen, so every release was silently a patch bump.
#
# Safe to change the tree after the build because NOTHING HERE IS COMPILED.
# release_notes.py touches platformio.ini, docs/release-body.md and
# docs/release-notes.md.
#
# --version IS NOT OPTIONAL HERE. Left to itself release_notes.py computes
# bump(whatever platformio.ini says), and by this point platformio.ini says
# $NEXT because the version step wrote it -- so it produced $NEXT + 1,
# rewrote both notes files for a release that would never exist, pushed
# that to xteink, and only then died on the tag guard. After the squash,
# which is not undoable by re-running. The tag lands on this
# commit, so the tagged tree's CODE is byte-identical to what was built and
# the tag carries the notes it publishes -- which is what host-tests/release
# wants when it compares a release against the previous tag.
step "release notes"

run "python3 scripts_local/release_notes.py --repo ma-r-s/crossplay --version '$NEXT' --write"
if [ "$DRY" = 0 ]; then
  if [ -n "$(git status --porcelain)" ]; then
    run "git add platformio.ini docs/release-notes.md docs/release-body.md"
    run "git commit -q -m 'chore: crossplay $NEXT notes'"
    run "git push -q origin 'HEAD:xteink'"
    say "  written and pushed"
  else
    say "  already current"
  fi

  # THE BODY MUST NAME THIS RELEASE. release_notes.py returns early without
  # writing when merges_since() finds nothing (it filters `chore: crossplay`
  # and `chore: emulator rebuilt` subjects), which leaves a clean tree, an
  # "already current" that is not true, and docs/release-body.md still
  # holding the PREVIOUS release's text -- which `gh release create
  # --notes-file` would then publish under this tag. Nothing else here looks
  # at the body's contents.
  grep -q "### What is new in $NEXT" docs/release-body.md \
    || die "docs/release-body.md does not name $NEXT, so publishing would put the previous release's text on this one's page. release_notes.py wrote nothing: most likely everything since the last tag is a chore commit it filters out. Nothing tagged, nothing published."
fi

# The tag must be the version the images were BUILT with, read out of
# platformio.ini rather than out of $NEXT, because $NEXT is the variable
# under suspicion. The probe that reads the firmware itself is in `package`.
BUILT="$(sed -n '/^\[crossplay\]/,/^\[/s/^version *= *//p' platformio.ini | head -1 | tr -d ' ')"
if [ "$DRY" = 0 ] && [ "$TAG" != "v$BUILT" ]; then
  die "the tag ($TAG) is not the version the images were built with (v$BUILT).
    platformio.ini compiles that string in and OtaUpdater compares a release's
    tag against it, so publishing this pair would leave every device that
    installs it still being offered the same update. Nothing published."
fi

# THE TAG IS PUSHED AFTER PACKAGING, down in `publish`. It used to be
# pushed here, which put it before every check that can still refuse: the
# eight-file handover, the three magic numbers, and the probe that reads the
# version out of the firmware. A packaging failure then left a pushed tag
# with no release against it -- a version in the history that never shipped,
# and a delete-and-retag to clear. Nothing between here and there moves HEAD,
# so the tag still lands on this commit.

# ----------------------------------------------------------------- package
#
# Ported from crossplay-release.yml, which did this in about fifteen seconds
# after fourteen minutes of rebuilding what was already here.
#
# THE NAME firmware.bin IS LOAD-BEARING. ReleaseJsonParser.cpp:19 matches that
# literal and nothing else, so an asset under any other name means every
# device's "Check for updates" reports no update, forever, and says nothing
# about why. That shipped once already (v1.0.1).
step "package"

DIST="$REPO/dist"
run "rm -rf '$DIST' && mkdir -p '$DIST'"

for env_name in gh_release_x4pro gh_release_sticky; do
  for f in firmware.bin firmware.elf partitions.bin bootloader.bin; do
    if [ "$DRY" = 0 ] && [ ! -f "$IMAGES/$env_name/$f" ]; then
      die "$IMAGES/$env_name/$f is missing. The gate reported success and handed over an incomplete set, which is how v1.12.14 and v1.12.15 shipped without a bootloader."
    fi
  done
done


# BOTH BOARDS SPELLED OUT, and that is deliberate rather than lazy. A loop
# over the two envs reads better and hides the two things worth reading: the
# offsets the S3 boot ROM expects, and which env each artefact came from.
# crossplay-release.yml spelled them out for the same reason, and a comment in
# it records why -- gh_release_x4pro and gh_release_sticky were appended to
# one hardcoded list once and the release published the wrong pair. Anything
# auditing this (host-tests/ship) can then read the offsets rather than
# re-derive them from a loop variable.
run "'$PIO_PY' '$ESPTOOL' --chip esp32s3 merge-bin --format raw \
    -o '$DIST/crossplay-$TAG-x4pro-full.bin' \
    -fm keep -fs keep -ff keep \
    0x0     $IMAGES/gh_release_x4pro/bootloader.bin \
    0x8000  $IMAGES/gh_release_x4pro/partitions.bin \
    0x10000 $IMAGES/gh_release_x4pro/firmware.bin"
# The OTA updater matches this literal name and nothing else
# (ReleaseJsonParser.cpp). It is the x4pro image, unmerged, under the plain
# name; v1.0.1 renamed it and every device went quiet about updates.
run "cp $IMAGES/gh_release_x4pro/firmware.bin '$DIST/firmware.bin'"
run "cp $IMAGES/gh_release_x4pro/firmware.elf '$DIST/crossplay-$TAG-x4pro.elf'"

run "'$PIO_PY' '$ESPTOOL' --chip esp32s3 merge-bin --format raw \
    -o '$DIST/crossplay-$TAG-sticky-full.bin' \
    -fm keep -fs keep -ff keep \
    0x0     $IMAGES/gh_release_sticky/bootloader.bin \
    0x8000  $IMAGES/gh_release_sticky/partitions.bin \
    0x10000 $IMAGES/gh_release_sticky/firmware.bin"
run "cp $IMAGES/gh_release_sticky/firmware.bin '$DIST/firmware-sticky.bin'"
run "cp $IMAGES/gh_release_sticky/firmware.elf '$DIST/crossplay-$TAG-sticky.elf'"

# A merged image that is not actually merged is indistinguishable from the app
# image it replaces until somebody bricks a device with it. Check the three
# magic numbers rather than trust an exit code.
if [ "$DRY" = 0 ]; then
  fail=0
  for full in "$DIST/crossplay-$TAG-x4pro-full.bin" "$DIST/crossplay-$TAG-sticky-full.bin"; do
    for probe in "0:e903:bootloader" "32768:aa50:partition table" "65536:e907:app"; do
      off="${probe%%:*}"; rest="${probe#*:}"; want="${rest%%:*}"; what="${rest#*:}"
      got="$(dd if="$full" bs=1 skip="$off" count=2 2>/dev/null | xxd -p)"
      if [ "$got" != "$want" ]; then
        say "  FAIL $(basename "$full") at $(printf '0x%x' "$off"): expected $want ($what), got $got"
        fail=1
      fi
    done
  done
  [ "$fail" = 0 ] || die "a published 'full' image that is not merged bricks the device that installs it. Nothing published."
  [ -f "$DIST/firmware.bin" ] || die "dist/firmware.bin is missing, and the OTA updater matches that literal name and nothing else."

  # THE ONE PROBE THAT READS THE BINARY RATHER THAN A DESCRIPTION OF IT.
  #
  # Everything else here checks a file's name, its length or a header's magic
  # number, and a stale image passes all of those -- it IS a real image, just
  # of the wrong commit. This asks the thing nobody else asks: does the
  # firmware about to be published report the version the tag names?
  #
  # That is the question OtaUpdater.cpp:119 asks on every device, so getting
  # it wrong is not a build error, it is an update prompt that never goes
  # away, on every unit in the field, with nothing red anywhere. It is the
  # failure this whole script is ordered around, and until a cold review
  # found ship.sh packaging from the wrong directory entirely, nothing here
  # could have detected it.
  #
  # The string is the User-Agent that BridgeHttp.cpp and StudySync.cpp build
  # from CROSSPOINT_VERSION, so it is in every release image by construction
  # and is not a debug line a LOG_LEVEL could compile out.
  for _img in "$DIST/firmware.bin" "$DIST/firmware-sticky.bin"; do
    # READ THE WHOLE STREAM, and do not reach for `grep -q` here.
    #
    # `strings -a "$_img" | grep -qxF ...` is the obvious spelling and it is
    # wrong under `set -o pipefail`, which this script sets: grep -q exits at
    # the first match, strings takes SIGPIPE on a 7MB image because it is
    # still writing, and the PIPELINE reports 141 even though the match
    # succeeded. So the probe refused a correct pair -- 1.13.14 against
    # v1.13.14 -- and said the images were built from the wrong commit.
    #
    # It cost a live run to find, and a shell test could not have: an
    # interactive shell has no pipefail, so the same command by hand returns
    # 0 and the probe looks right. Comparing the extracted value reads every
    # byte, so nothing exits early and there is no pipe to break.
    _found="$(strings -a "$_img" | sed -n 's/^CrossPlay-ESP32-//p' | sort -u | tr '\n' ' ')"
    _found="${_found% }"
    if [ "$_found" != "$NEXT" ]; then
      # Resolved before the message rather than inside it: a $( ) with its own
      # quotes nested in a die string is unreadable to anything auditing which
      # guards refuse, and host-tests/ship audits exactly that.
      _name="$(basename "$_img")"
      die "$_name reports version [${_found:-none found}] and the tag is $TAG.
    These images were not built from the commit being published. Every device
    that installed this would keep being offered the update it had just
    applied, forever, with nothing anywhere saying why. Nothing published."
    fi
  done
  say "  version   both images report $NEXT"
  say "  $(ls "$DIST" | wc -l | tr -d ' ') artefacts, magic numbers verified"
fi

# ----------------------------------------------------------------- publish
#
# generate_release_notes stays off, as it was in the workflow: this fork
# carries upstream's whole history, and the generator with no floor listed
# 1,319 of CrossPoint's commits as what is new in CrossPlay. The body is
# docs/release-body.md, which release_notes.py just wrote.
step "publish"

# Everything that can refuse has refused by now: the handover matched HEAD,
# all eight files were present, the three magic numbers checked out, and both
# images reported $NEXT. This is the first irreversible step.
run "git tag '$TAG'"
run "git push -q origin '$TAG'"

run "gh release create '$TAG' --repo ma-r-s/crossplay \
    --title '$TAG' \
    --notes-file docs/release-body.md \
    '$DIST'/*"

# The board watches releases and the autorelease used to post this. Best
# effort, and never a reason to fail a release that is already public.
SUPA_URL="$(gh variable get SUPABASE_URL --repo ma-r-s/crossplay 2>/dev/null || echo "")"
SUPA_KEY="$(gh variable get SUPABASE_ANON_KEY --repo ma-r-s/crossplay 2>/dev/null || echo "")"
if [ -n "$SUPA_URL" ] && [ -n "$SUPA_KEY" ]; then
  run "curl -sS -o /dev/null -w '  board: %{http_code}\n' -X POST '$SUPA_URL/rest/v1/events' \
      -H 'apikey: $SUPA_KEY' -H 'Authorization: Bearer $SUPA_KEY' \
      -H 'Content-Type: application/json' -H 'Prefer: return=minimal' \
      -d '{\"service\":\"release\",\"event\":\"release\",\"version\":\"$NEXT\",\"props\":{\"tag\":\"$TAG\"}}' || true"
fi

say ""
say "Published $TAG  https://github.com/ma-r-s/crossplay/releases/tag/$TAG"

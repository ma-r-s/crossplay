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
# WHAT THIS REFUSES TO DO. It does not merge a branch that is not a
# fast-forward onto xteink. A merge commit's tree is not the tree the gate
# verified, and republishing under that tag ships bytes nothing built. Rebase
# and re-gate; the message says so.
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

run "git fetch -q origin xteink --tags"

# FAST-FORWARD OR NOTHING. See the header: a merge commit's tree was never
# built, and the tag would name bytes nothing compiled. This is the check that
# makes "ship the gate's output" sound rather than usually-sound.
BASE="$(git merge-base HEAD origin/xteink)"
TRUNK="$(git rev-parse origin/xteink)"
if [ "$BASE" != "$TRUNK" ]; then
  die "xteink has moved since this branch left it ($(git rev-list --count "$BASE".."$TRUNK") commits).
    A merge commit's tree is not the tree the gate verified, so its images would
    be bytes nothing built. Rebase and re-gate:
        git rebase origin/xteink && ./scripts_local/check.sh --committed"
fi

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

say "  branch    $BRANCH -> xteink (fast-forward, $(git rev-list --count "$TRUNK"..HEAD) commit(s))"
say "  hold      clear"

# ------------------------------------------------------------------- notes
#
# release_notes.py owns the whole bump: the [crossplay] version in
# platformio.ini, docs/release-body.md (the page this release publishes) and
# docs/release-notes.md (the history it is prepended to). It decides the
# number, reads the merged pull requests' own lines, and is idempotent on a
# version already written -- which is what lets a branch that bumped earlier
# pass through here without a second gate.
step "release notes"

# `if ! cmd; then rc=$?` captures the NEGATION's status, so rc is 0 or 1 and
# never 2 -- and 2 is the answer that matters. It means a changed path is in
# no row of device-build-needed.sh's table, which is the case release-needed.sh
# exists to make somebody stop and look at; the old autorelease failed loudly
# on it. Read badly, a real user-facing fix on an unclassified path lands and
# is never released, silently. Capture the status directly.
#
# And keep its stdout: the message names the path, and the die below tells
# you to go read it.
REL_OUT="$(./scripts_local/release-needed.sh 2>&1)"; REL_RC=$?
if [ "$REL_RC" != 0 ]; then
  [ "$REL_RC" = 2 ] && die "release-needed.sh REFUSED, because a changed path is in no row of the classification table:
$REL_OUT
    Add the row (scripts_local/device-build-needed.sh) saying whether that path
    builds and whether it ships, then run this again. Releasing for nothing and
    silently withholding a real fix are both wrong answers to a question nobody
    has answered."
  say "  nothing since the last tag reaches a user. Landing without a release."
  NEXT=""
else
  NEXT="$(python3 scripts_local/release_notes.py --repo ma-r-s/crossplay --dry-run 2>/dev/null | sed -n 's/^NEXT_VERSION=//p')"
  [ -n "$NEXT" ] || die "release_notes.py named no next version. Run it with --dry-run and read why."
  say "  next version  $NEXT"
fi

if [ -n "$NEXT" ]; then
  # WHAT THE VERSION ALREADY IS, read before the bump writes anything.
  #
  # A dry run cannot answer "did the bump change something?" by looking at
  # the working tree, because in a dry run the bump did not run. The first
  # version of this asked exactly that and so always reported "the bump is a
  # no-op, the gate is skipped" -- the opposite of the truth for every real
  # release, from the one mode whose entire job is to say what would happen.
  # So compare the versions instead, which is the same question and is
  # answerable in both modes.
  HAVE_VER="$(sed -n '/^\[crossplay\]/,/^\[/s/^version *= *//p' platformio.ini | head -1 | tr -d ' ')"
  run "python3 scripts_local/release_notes.py --repo ma-r-s/crossplay --write"
  if [ "$HAVE_VER" != "$NEXT" ]; then
    run "git add platformio.ini docs/release-notes.md docs/release-body.md"
    run "git commit -q -m 'chore: crossplay $NEXT'"
    say "  bumped $HAVE_VER -> $NEXT. The gate below builds the images that ship."
  else
    say "  already at $NEXT: the bump is a no-op, so the gate's images are current."
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
    *)     die "gate verdict: $VERDICT. Nothing published. Read $GATE_LOG." ;;
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

TAG="v${NEXT:-0.0.0}"

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
    if ! strings -a "$_img" | grep -qxF "CrossPlay-ESP32-$NEXT"; then
      _found="$(strings -a "$_img" | sed -n 's/^CrossPlay-ESP32-//p' | sort -u | tr '\n' ' ')"
      die "$(basename "$_img") reports version [${_found:-none found}] and the tag is $TAG.
    These images were not built from the commit being published. Every device
    that installed this would keep being offered the update it had just
    applied, forever, with nothing anywhere saying why. Nothing published."
    fi
  done
  say "  version   both images report $NEXT"
  say "  $(ls "$DIST" | wc -l | tr -d ' ') artefacts, magic numbers verified"
fi

# -------------------------------------------------------------------- land
#
# Push before tagging, and tag the commit that is now trunk's tip: the tag has
# to name the tree whose images are sitting in dist/, or the release describes
# something nobody built.
step "land"
run "git push origin '$BRANCH:xteink'"
if [ "$DRY" = 1 ]; then
  say "  xteink would become $(git rev-parse --short HEAD) (plus the bump commit above)"
else
  say "  xteink is now $(git rev-parse --short HEAD)"
fi

if [ -z "$NEXT" ]; then
  say "\nLanded. No release: nothing since the last tag reaches a user."
  exit 0
fi

# The tag must be the version being built, asserted rather than assumed.
#
# TAG is derived from NEXT a hundred lines up, so they agree by construction
# and this can only fire if something between here and there rewrote one of
# them. That is precisely when it is worth having: crossplay-release.yml
# carried the same step because a tag naming a version the binary does not
# report is the OTA bug this whole ordering exists to prevent, and by the
# time a tag is pushed it costs a delete-and-retag.
#
# Read out of platformio.ini, not out of the variable, because the variable
# is the thing under suspicion. This is what the firmware compiled.
BUILT="$(sed -n '/^\[crossplay\]/,/^\[/s/^version *= *//p' platformio.ini | head -1 | tr -d ' ')"
if [ "$DRY" = 0 ] && [ "$TAG" != "v$BUILT" ]; then
  die "the tag ($TAG) is not the version the images were built with (v$BUILT).
    platformio.ini compiles that string in and OtaUpdater compares a release's
    tag against it, so publishing this pair would leave every device that
    installs it still being offered the same update. Nothing published."
fi

run "git tag '$TAG'"
run "git push origin '$TAG'"

# ----------------------------------------------------------------- publish
#
# generate_release_notes stays off, as it was in the workflow: this fork
# carries upstream's whole history, and the generator with no floor listed
# 1,319 of CrossPoint's commits as what is new in CrossPlay. The body is
# docs/release-body.md, which release_notes.py just wrote.
step "publish"
run "gh release create '$TAG' --repo ma-r-s/crossplay \
    --title 'CrossPlay $NEXT' \
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

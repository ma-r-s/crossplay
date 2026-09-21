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
# And because release_notes.py is idempotent on the version, this shape also
# rewards doing the bump earlier. If the branch already carries the bump and
# its gate already ran, `--write` changes nothing, the gate is skipped as
# having nothing to verify, and landing costs an upload. If it does not, the
# gate runs here, once, which is still three builds fewer than before.
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
run()  { if [ "$DRY" = 1 ]; then printf '   would: %s\n' "$*"; else eval "$@"; fi; }

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

# PlatformIO's own esptool, reached through PlatformIO's own interpreter,
# exactly as scripts_local/usb-flash.sh reaches it. Nothing is installed on
# this Mac for this script: the toolchain that built the images is the
# toolchain that packages them, and it is already here (v5.3.0, and it spells
# the subcommand `merge-bin` the way crossplay-release.yml did).
PIO_PY="$HOME/.platformio/penv/bin/python"
ESPTOOL="$HOME/.platformio/packages/tool-esptoolpy/esptool.py"
[ -x "$PIO_PY" ] && [ -f "$ESPTOOL" ] \
  || die "no PlatformIO esptool at $ESPTOOL. Run any build once to install the toolchain."

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
HOLD="$(gh variable get RELEASE_HOLD --repo ma-r-s/crossplay 2>/dev/null || echo "")"
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

if ! ./scripts_local/release-needed.sh >/dev/null 2>&1; then
  rc=$?
  [ "$rc" = 2 ] && die "release-needed.sh REFUSED: a changed path is in no row of the table. Run it directly and read which path it names; do not guess."
  say "  nothing since the last tag reaches a user. Landing without a release."
  NEXT=""
else
  NEXT="$(python3 scripts_local/release_notes.py --repo ma-r-s/crossplay --dry-run 2>/dev/null | sed -n 's/^NEXT_VERSION=//p')"
  [ -n "$NEXT" ] || die "release_notes.py named no next version. Run it with --dry-run and read why."
  say "  next version  $NEXT"
fi

NEEDS_GATE=0
if [ -n "$NEXT" ]; then
  run "python3 scripts_local/release_notes.py --repo ma-r-s/crossplay --write"
  if [ "$DRY" = 0 ] && [ -n "$(git status --porcelain)" ]; then
    NEEDS_GATE=1
    run "git add platformio.ini docs/release-notes.md docs/release-body.md"
    run "git commit -q -m 'chore: crossplay $NEXT'"
    say "  bumped, committed. The gate below builds the images that ship."
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
if [ "$NEEDS_GATE" = 1 ]; then
  say "  building the images that ship (transcript below)"
  run "CHECK_FORCE_DEVICE_BUILDS=1 ./scripts_local/check.sh --committed 2>&1 | tee '$GATE_LOG'"
  if [ "$DRY" = 0 ]; then
    VERDICT="$(grep -o 'CHECKSH-VERDICT: [a-z-]*' "$GATE_LOG" | tail -1 | sed 's/CHECKSH-VERDICT: //')"
    case "$VERDICT" in
      green) say "  verdict   green" ;;
      host-green-device-skipped) die "the gate skipped the device builds, so there are no images to publish. CHECK_FORCE_DEVICE_BUILDS did not take; read $GATE_LOG." ;;
      "")    die "the gate printed no verdict at all, which is not a pass. Read $GATE_LOG." ;;
      *)     die "gate verdict: $VERDICT. Nothing published. Read $GATE_LOG." ;;
    esac
  fi
else
  say "  skipped: nothing was rebuilt, so the images already on disk are this commit's."
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
    if [ "$DRY" = 0 ] && [ ! -f ".pio/build/$env_name/$f" ]; then
      die ".pio/build/$env_name/$f is missing. The gate reported success and its output is not on disk, which is how v1.12.14 and v1.12.15 shipped without a bootloader. Re-run with the gate forced."
    fi
  done
done

TAG="v${NEXT:-0.0.0}"
for pair in "x4pro:gh_release_x4pro:firmware.bin" "sticky:gh_release_sticky:firmware-sticky.bin"; do
  label="${pair%%:*}"; rest="${pair#*:}"; env_name="${rest%%:*}"; plain="${rest#*:}"
  run "'$PIO_PY' '$ESPTOOL' --chip esp32s3 merge-bin --format raw \
      -o '$DIST/crossplay-$TAG-$label-full.bin' \
      -fm keep -fs keep -ff keep \
      0x0     .pio/build/$env_name/bootloader.bin \
      0x8000  .pio/build/$env_name/partitions.bin \
      0x10000 .pio/build/$env_name/firmware.bin"
  run "cp .pio/build/$env_name/firmware.bin '$DIST/$plain'"
  run "cp .pio/build/$env_name/firmware.elf '$DIST/crossplay-$TAG-$label.elf'"
done

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
  say "  $(ls "$DIST" | wc -l | tr -d ' ') artefacts, magic numbers verified"
fi

# -------------------------------------------------------------------- land
#
# Push before tagging, and tag the commit that is now trunk's tip: the tag has
# to name the tree whose images are sitting in dist/, or the release describes
# something nobody built.
step "land"
run "git push origin '$BRANCH:xteink'"
say "  xteink is now $(git rev-parse --short HEAD)"

if [ -z "$NEXT" ]; then
  say "\nLanded. No release: nothing since the last tag reaches a user."
  exit 0
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

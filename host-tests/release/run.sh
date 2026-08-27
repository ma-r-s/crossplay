#!/bin/bash
# What the release publishes, and what the docs tell people to do with it.
#
# Three separate mistakes shipped in v1.0.0 and v1.0.1, and every one of them
# was invisible to every check that existed:
#
#   1. The release published only the application image, and both README.md and
#      the site told people to `write_flash 0x0` it. On an ESP32-S3 the
#      second-stage bootloader lives at 0x0, so that writes the app over it.
#   2. Nothing published a partition table, so a first install could not work at
#      all on a device that had never run CrossPoint.
#   3. The asset was renamed to crossplay-<tag>-x4pro.bin, but the over-the-air
#      updater matches the literal "firmware.bin" and nothing else
#      (lib/JsonParser/ReleaseJsonParser.cpp). "Check for updates" therefore
#      reported no update, forever, and said nothing about why.
#
# None of that is reachable by compiling or by running the firmware, and a
# release happens rarely enough that nobody notices in time. So it is asserted
# here, against the real files rather than a description of them, in the same
# spirit as host-tests/ci.
#
#   host-tests/release/run.sh
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
WF="$ROOT/.github/workflows/crossplay-release.yml"
PARSER="$ROOT/lib/JsonParser/ReleaseJsonParser.cpp"

checks=0
failed=0
ok()   { checks=$((checks + 1)); }
bad()  { checks=$((checks + 1)); failed=$((failed + 1)); echo "FAIL release  $1"; }

for f in "$WF" "$PARSER" "$ROOT/README.md" "$ROOT/site/index.html"; do
  [ -f "$f" ] || { echo "FAIL release  missing $f"; exit 1; }
done

# -- 1. each merged image is assembled at the offsets the S3 boot ROM expects --
#
# Matched as offset-then-file pairs rather than as one literal line, so
# reformatting the workflow does not turn a passing check into a failing one,
# and reordering the arguments does not turn a failing one into a pass. One
# merge-bin per board; each must place all three parts.
for board in x4pro sticky; do
  merge="$(tr '\n' ' ' < "$WF" | grep -o "merge-bin[^;]*gh_release_$board/firmware\.bin" || true)"
  if [ -z "$merge" ]; then
    bad "the release workflow never calls esptool merge-bin for $board"
  else
    ok
    for pair in "0x0:bootloader.bin" "0x8000:partitions.bin" "0x10000:firmware.bin"; do
      off="${pair%%:*}"; want="${pair##*:}"
      if printf '%s' "$merge" | grep -qE "$off +[^ ]*/$want"; then
        ok
      else
        bad "merge-bin ($board) does not place $want at $off"
      fi
    done
  fi
done

# -- 2. each device's app image keeps the name its own updater will match ------
#
# Read out of the C++ rather than hardcoded here: if someone changes what the
# updater compares against, this test must follow it, not contradict it.
# CROSSPOINT_RELEASE_ASSET in FirmwareBoardTag.h encodes both rules -- the
# x4pro fleet asks for the literal legacy name, every later board for
# firmware-<board>.bin -- and OtaUpdater must request exactly that macro.
TAGH="$ROOT/src/network/FirmwareBoardTag.h"
if grep -q 'setFirmwareAssetName(CROSSPOINT_RELEASE_ASSET)' "$ROOT/src/network/OtaUpdater.cpp"; then
  ok
else
  bad "OtaUpdater no longer requests CROSSPOINT_RELEASE_ASSET; the asset-name contract has no pin site"
fi
legacy="$(grep -oE '#define CROSSPOINT_RELEASE_ASSET "[^"]+"$' "$TAGH" | sed 's/.*"\(.*\)"$/\1/')"
if [ -z "$legacy" ]; then
  bad "cannot find the x4pro legacy asset literal in FirmwareBoardTag.h"
elif grep -qE "dist/$legacy( |\"|$)" "$WF"; then
  ok
else
  bad "the release does not publish '$legacy', so every fielded X4 Pro's Check for updates finds nothing"
fi
sticky_name="$(grep -A1 'FREEINK_DEVICE_STICKY$' "$TAGH" | grep -oE 'CROSSPOINT_BOARD_NAME "[^"]+"' | sed 's/.*"\(.*\)"/\1/')"
if [ -z "$sticky_name" ]; then
  bad "cannot find the sticky board name in FirmwareBoardTag.h"
elif grep -qE "dist/firmware-$sticky_name\.bin( |\"|$)" "$WF"; then
  ok
else
  bad "the release does not publish 'firmware-$sticky_name.bin', so a Sticky's Check for updates finds nothing"
fi

# -- 3. every documented flash command names a file the release actually makes -
#
# The pairing is the point: 0x0 is only correct for the merged image, and the
# app image is only correct at 0x10000. Checking the offset alone or the
# filename alone would have passed on the version that shipped.
while IFS= read -r line; do
  # Take the token after the offset and trim what markup or markdown put around
  # it. Not [^ <]*: README spells the placeholder <version>, so stopping at the
  # first '<' truncated every filename to "crossplay-" and the check passed on
  # a name that does not exist.
  file="$(printf '%s' "$line" | sed -E 's/.*write_flash +0x0 +([^ ]*).*/\1/; s#</code>.*##; s/[`"'"'"']//g')"
  case "$file" in
    *-full.bin)
      if grep -q -- "-x4pro-full.bin" "$WF"; then ok; else
        bad "docs flash '$file' at 0x0 but the workflow builds no -full.bin"
      fi
      ;;
    *)
      bad "flashing '$file' at 0x0 -- 0x0 is the bootloader on an S3, and only the merged image belongs there"
      ;;
  esac
done < <(grep -rhE "write_flash +0x0" "$ROOT/README.md" "$ROOT/site/index.html")

# -- 4. nothing still points at the old app-only asset name --------------------
if grep -rqE "crossplay-[^ ]*x4pro\.bin" "$ROOT/README.md" "$ROOT/site/index.html"; then
  bad "the docs still name crossplay-<version>-x4pro.bin, which the release no longer publishes"
else
  ok
fi

# -- 5. the partition table is one an OTA cannot walk off the end of -----------
#
# partitions.csv is the one file in the tree where a bad merge is not a failed
# build, it is a brick: the table is written once, at install time, at 0x8000,
# and nothing on the device ever checks it again. Upstream still ships a 3.375MB
# spiffs partition that this fork gave to the app slots, so this file conflicts
# on every upstream merge, and "resolve by taking theirs" silently halves the
# room every image has to fit in.
#
# Asserted against the arithmetic rather than against the expected literals, so
# a deliberate future re-split still passes and only a broken one fails.
TABLE="$ROOT/partitions.csv"
[ -f "$TABLE" ] || { echo "FAIL release  missing $TABLE"; exit 1; }

CHIP=$((0x1000000))  # 16MB, the flash on both boards (board_upload.flash_size)
app_sizes=""
prev_end=0
overlap=0
for row in $(grep -vE '^\s*#' "$TABLE" | grep -vE '^\s*$' | tr -d ' \t' ); do
  IFS=',' read -r name type sub off size _ <<< "$row"
  [ -n "${off:-}" ] && [ -n "${size:-}" ] || continue
  o=$((off)); z=$((size))
  # Regions are listed in ascending order; a row starting before the previous
  # one ended is an overlap, which esptool does not reject and the bootloader
  # discovers by loading garbage.
  [ "$o" -lt "$prev_end" ] && overlap=1
  prev_end=$((o + z))
  if [ "$type" = "app" ]; then
    app_sizes="$app_sizes $z"
    if [ $((o % 0x10000)) -eq 0 ]; then ok; else
      bad "app partition '$name' starts at $off, which is not 64KB-aligned; the S3 maps app flash in 64KB pages"
    fi
  fi
done

if [ "$overlap" -eq 0 ]; then ok; else
  bad "partitions.csv has overlapping regions"
fi

if [ "$prev_end" -le "$CHIP" ]; then ok; else
  bad "partitions.csv allocates $prev_end bytes on a $CHIP byte chip; the last region runs off the end"
fi

# Two app slots, both the same size. Unequal slots are worse than small ones:
# an image that fits the slot it was flashed into but not the other one gets
# OTA'd once and then can never be updated again from the slot it landed in.
set -- $app_sizes
if [ "$#" -eq 2 ]; then
  ok
  if [ "$1" -eq "$2" ]; then ok; else
    bad "the two app slots differ ($1 vs $2); an OTA can land an image that fits one and not the other"
  fi
else
  bad "expected exactly 2 app partitions (ota_0/ota_1), found $#; OTA needs a second slot to land in"
fi

# The reason this fork's table differs from upstream's at all. If a merge ever
# puts spiffs back, every image loses 1.9MB of room with no other symptom than
# a link failure months later.
if grep -qE '^\s*spiffs' "$TABLE"; then
  bad "spiffs is back in partitions.csv -- nothing in this firmware mounts it, and it costs both app slots 1.9MB each"
else
  ok
fi

echo "$checks checks, $failed failed"
[ "$failed" -eq 0 ]

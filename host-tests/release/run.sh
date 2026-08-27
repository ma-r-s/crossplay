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

# -- 6. dev-only flags never reach an env a tag builds -------------------------
#
# CROSSPOINT_DEV_SERIAL_BRIDGE lets anything on the USB line drive the UI. It is
# compile-time so a release image cannot contain it at all -- but only as long
# as nobody moves it up into a *_common section, which the release envs inherit
# from. That single edit is invisible in review and ships the hole. So the check
# is on which SECTION the flag sits in, not merely on whether the release env
# spells it out.
#
# Developer Mode is deliberately NOT on this list. It ships in every build by
# design, because a compile-time gate meant the only way into dev mode was the
# cable it exists to remove. What protects a release is asserted below instead:
# it is off by default, and its endpoints require pairing.
INI="$ROOT/platformio.ini"
[ -f "$INI" ] || { echo "FAIL release  missing $INI"; exit 1; }

for flag in CROSSPOINT_DEV_SERIAL_BRIDGE; do
  # Print the section each occurrence of $flag lives in.
  homes="$(awk -v want="$flag" '
    /^\[/ { section = $0; sub(/^\[/, "", section); sub(/\].*$/, "", section) }
    index($0, want) && $0 !~ /^[[:space:]]*;/ { print section }
  ' "$INI" | sort -u)"

  if [ -z "$homes" ]; then
    bad "$flag appears nowhere in platformio.ini; the dev tooling that needs it is dead"
    continue
  fi

  bad_home=""
  for section in $homes; do
    case "$section" in
      # Only these two. A *_common section is inherited by its release env, and
      # any gh_release/slim env is something a tag actually builds.
      env:x4pro|env:sticky) ;;
      *) bad_home="${bad_home:+$bad_home }$section" ;;
    esac
  done

  if [ -z "$bad_home" ]; then
    ok
  else
    bad "$flag is set in [$bad_home] -- release envs inherit that, so a tag would ship it"
  fi
done

# -- 7. Developer Mode ships off, and its endpoints are not open ---------------
#
# Dev mode is a runtime setting present in release builds, so the compile-time
# argument that used to protect users does not apply and something else has to.
# Three things, each of which has to stay true in a shipped image:
#   - it defaults to OFF, so nobody gets it by accident;
#   - every /api/dev/ route except pairing goes through the auth gate;
#   - the gate is not satisfied by merely being on.
# A regression in any of these turns every reader whose owner once flipped the
# toggle into an open firmware-replacement endpoint.
SETTINGS_H="$ROOT/src/CrossPointSettings.h"
WEBSERVER="$ROOT/src/network/CrossPointWebServer.cpp"
for f in "$SETTINGS_H" "$WEBSERVER" "$ROOT/src/DevMode.cpp"; do
  [ -f "$f" ] || { echo "FAIL release  missing $f"; exit 1; }
done

if grep -qE '^\s*uint8_t devMode = 0;' "$SETTINGS_H"; then
  ok
else
  bad "devMode does not default to 0 in CrossPointSettings.h -- dev mode would ship ON"
fi

# Every route registered under /api/dev/ must be either the pair endpoint or a
# handler whose body calls devAuthorised(). Read the routes out of the source
# rather than listing them here, so a new endpoint cannot be added without
# either passing this or failing it.
dev_routes="$(grep -oE '"/api/dev/[A-Za-z0-9/_-]+"' "$WEBSERVER" | tr -d '"' | sort -u)"
if [ -z "$dev_routes" ]; then
  bad "no /api/dev/ routes found; the dev-mode control surface has vanished"
else
  for route in $dev_routes; do
    name="${route##*/}"
    case "$name" in
      pair) ok ;;  # pairing is the way IN, so it cannot require a token
      *)
        # Find the handler this route dispatches to, then check that handler's body.
        # EVERY handler on the line, not the first. A route registered with a
        # body callback has two, and the second is the one that streams
        # megabytes to the SD card -- checking only the first left the
        # dangerous half of /api/dev/upload unasserted while the suite
        # reported green.
        # The WHOLE registration line. An earlier version stopped at the first
        # ";", which falls inside the first lambda body -- so on a route with a
        # body callback it silently saw only handler one, and the streaming
        # handler that writes megabytes to the card went unchecked while the
        # suite printed green. Found by mutation-testing the assertion itself.
        handlers="$(grep -F "\"$route\"" "$WEBSERVER" | grep -oE 'handle[A-Za-z]+' | sort -u)"
        if [ -z "$handlers" ]; then
          bad "cannot find any handler for $route"
        else
          for handler in $handlers; do
            # Either gate counts: devAuthorised() answers, devTokenOk() decides
            # silently for callbacks that must not send mid-parse.
            if awk "/void CrossPointWebServer::$handler\(/,/^}/" "$WEBSERVER" |
              grep -qE 'devAuthorised\(\)|devTokenOk\(\)'; then
              ok
            else
              bad "$route ($handler) checks neither devAuthorised() nor devTokenOk() -- open to anyone on the network"
            fi
          done
        fi
        ;;
    esac
  done
fi

# Being switched on must not be sufficient. The gate has to check a token too.
if awk '/bool CrossPointWebServer::devAuthorised\(/,/^}/' "$WEBSERVER" | grep -q 'tokenValid'; then
  ok
else
  bad "devAuthorised() does not check a token; turning dev mode on would be enough to flash the device"
fi

# -- 8. the two Developer Mode invariants that only exist in one line each ------
#
# Both of these are load-bearing and both are one edit away from silently
# reverting, which is exactly the shape that wants a test rather than a comment.

# (a) The upload route must stay HTTP_PUT. This core's FunctionRequestHandler
# hands the same callback to the upload path and the raw path with nothing to
# distinguish them, and server->upload() in raw mode dereferences a null
# _currentUpload and RESETS THE DEVICE -- unauthenticated. PUT makes canUpload()
# unreachable because it requires HTTP_POST. Changing this line to HTTP_POST or
# HTTP_ANY restores a remote reboot that already shipped twice.
if grep -qE '"/api/dev/upload",[[:space:]]*HTTP_PUT' "$WEBSERVER"; then
  ok
else
  bad "/api/dev/upload is not registered HTTP_PUT -- the raw/upload ambiguity that reboots the device is back"
fi

# (b) Developer Mode must not be writable over the network. The settings list
# drives the menu, the JSON file AND the web API from one entry, so adding the
# toggle gave it a web setter for free -- on the reader's UNAUTHENTICATED web
# UI. That turned the temporary surface into a way to enable the permanent one.
if awk '/bool CrossPointWebServer::isLocalOnlySetting\(/,/^}/' "$WEBSERVER" | grep -q '"devMode"'; then
  ok
else
  bad "devMode is not in isLocalOnlySetting() -- anyone on the LAN could enable Developer Mode via /api/settings"
fi
if awk '/void CrossPointWebServer::handlePostSettings\(/,/^}/' "$WEBSERVER" | grep -q 'isLocalOnlySetting'; then
  ok
else
  bad "handlePostSettings() no longer consults isLocalOnlySetting() -- local-only settings are network-writable again"
fi

# (c) The pairing code has to be reachable on the device. Without a screen that
# renders it, the code exists only in a log line read over the cable this
# feature exists to remove -- which is how it was first written.
DEVACT="$ROOT/src/activities/settings/DeveloperModeActivity.cpp"
if [ -f "$DEVACT" ] && grep -q 'st\.code' "$DEVACT"; then
  ok
else
  bad "no on-device screen renders the pairing code; Developer Mode cannot be paired without a serial cable"
fi

# -- 9. nothing reboots the device to tear down a radio it does not own --------
#
# Five activities used `WiFi.getMode() != WIFI_MODE_NULL` as shorthand for "did
# I turn Wi-Fi on?" and answered yes by calling silentRestart(). That held while
# nothing else ever owned the radio -- LinkRadio.cpp still carries a comment
# saying exactly that -- and stopped holding the moment Developer Mode kept it
# up for as long as its toggle is on. Without the devmode::holdsRadio() guard,
# closing the OPDS browser or the font downloader reboots the reader mid-use,
# which reads as a crash rather than as a feature interacting badly.
for f in src/apps_local/hackernews/HackerNewsActivity.cpp \
  src/activities/settings/OtaUpdateActivity.cpp \
  src/activities/browser/OpdsBookBrowserActivity.cpp \
  src/activities/settings/FontDownloadActivity.cpp \
  src/activities/network/CrossPointWebServerActivity.cpp; do
  path="$ROOT/$f"
  if [ ! -f "$path" ]; then
    bad "missing $f"
    continue
  fi
  # Only interesting if this file still reboots to tear the radio down.
  if ! grep -q 'silentRestart()' "$path"; then
    ok
    continue
  fi
  if grep -q 'devmode::holdsRadio()' "$path"; then
    ok
  else
    bad "$(basename "$f") reboots to tear down Wi-Fi without asking devmode::holdsRadio() -- it will restart the device whenever Developer Mode is on"
  fi
done

echo "$checks checks, $failed failed"
[ "$failed" -eq 0 ]

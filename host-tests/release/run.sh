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
# Locally a SKIP is information: a full clone may legitimately lack a tag. In
# CI every input is supposed to be there, so a check that did not run is a
# FAILURE -- otherwise the fix for "this gate never ran in CI" is itself
# unguarded, and deleting fetch-tags puts it right back to silently skipping.
skip() {
  if [ -n "${CI:-}" ]; then
    bad "$1 (a skip is a failure in CI: the inputs should be present here)"
  else
    echo "SKIP release  $1"
  fi
}

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

# -- 9. `WiFi.getMode()` is never the only test of who owns the radio ----------
#
# Files here tear the radio down on exit -- disconnect, then silentRestart() to
# clear the fragmentation a TLS session leaves. They used to decide whether to
# do that from `WiFi.getMode() != WIFI_MODE_NULL`, which answers "somebody has
# the radio", not "I do". That was true enough while nothing else ever held it:
# LinkRadio.cpp still carries a comment saying exactly that. Developer Mode
# holds it for as long as its toggle is on, so a bare getMode() check now means
# every one of these tears down a connection it did not raise.
#
# The rule: that expression must never stand alone. Either the file tracks its
# own ownership (`wifiActivated && WiFi.getMode() ...`, which several already
# did and which is the better pattern) or it asks devmode::holdsRadio().
#
# DISCOVERED, not listed. An earlier version of this check walked a hardcoded
# five-file list while its commit message claimed it caught any such file --
# which meant six more files carried the bug and the suite reported green. A
# test that cannot find a new violation is not testing the rule, it is
# restating the fix.
while IFS= read -r path; do
  rel="${path#$ROOT/}"
  # Every occurrence in the file has to be qualified, not just one.
  unqualified=0
  while IFS= read -r line; do
    # Prose about the rule is not the rule. DevMode.h explains this very check
    # in a comment, and an earlier version of this test failed on it.
    trimmed="$(printf '%s' "$line" | sed 's/^[[:space:]]*//')"
    case "$trimmed" in
      '//'*|'*'*|'/*'*) continue ;;
    esac
    # A second condition on the same line is the file vouching for itself.
    case "$line" in
      *"&&"*) continue ;;
    esac
    unqualified=1
  done < <(grep -F 'WiFi.getMode() != WIFI_MODE_NULL' "$path")

  if [ "$unqualified" -eq 0 ]; then
    ok
  elif grep -q 'devmode::holdsRadio()' "$path"; then
    ok
  else
    bad "$rel decides who owns the radio from WiFi.getMode() alone -- it will tear down Developer Mode's connection"
  fi
done < <(grep -rl 'WiFi.getMode() != WIFI_MODE_NULL' "$ROOT/src" 2>/dev/null | sort)

# Lines that are not comments. Check 9 strips them for the reason its comment
# gives ("Prose about the rule is not the rule"); checks 10 to 12 need the same
# thing, because a file that only MENTIONS devmode:: in a comment explaining why
# it does not need to yield was passing every one of them.
code_lines() {
  # Comment-stripped source. A character scanner, because four cheaper versions
  # were each walked around in turn: same-line /* */ only; leading-/* only;
  # blind to string literals (a `/*` in a string latched the block state to end
  # of file, and since radio_takers() filters candidates THROUGH this, the file
  # left the check set entirely); and then blind to CHAR literals, where '"'
  # opened a string that swallowed the rest of the line -- a strict regression,
  # because the version before it caught that case.
  #
  # A quote straight after an alphanumeric is a C++ DIGIT SEPARATOR, not a char
  # literal -- see the prev check below. Reading 0x70B0.0001 as a literal opened
  # one that swallowed the rest of the line, comment and all, and 13 files in
  # this tree use separators. That rule is safe here only because no L or u8
  # prefixed char literal exists in the tree; if one appears, revisit it.
  #
  # Known limits, stated rather than pretended away, because one version quietly
  # dropped this paragraph while ADDING a limit, and another claimed separators
  # did not appear here when they appear 13 times:
  #   - a raw string spanning lines will latch the block state (none in tree);
  #   - a prefixed char literal would be read as a separator (none in tree).
  # Both would show up the same way: the check COUNT stops moving. That is the
  # tell, every time, which is what the canaries above are for.
  awk '
    {
      line = $0; out = ""; i = 1; n = length(line); prev = ""
      while (i <= n) {
        c = substr(line, i, 1); d = substr(line, i, 2)
        if (inblock) { if (d == "*/") { inblock = 0; i += 2 } else { i++ } continue }
        if (instr || inchar) {
          if (c == "\\") { out = out substr(line, i, 2); i += 2; continue }
          out = out c; prev = c; i++
          if (instr && c == "\"") instr = 0
          if (inchar && c == "'"'"'") inchar = 0
          continue
        }
        if (d == "//") break
        if (d == "/*") { inblock = 1; i += 2; continue }
        if (c == "\"") { instr = 1; out = out c; i++; prev = c; continue }
        if (c == "'"'"'" && prev !~ /[0-9A-Za-z_]/) { inchar = 1; out = out c; i++; prev = c; continue }
        out = out c; prev = c; i++
      }
      instr = 0; inchar = 0; prev = ""
      print out
    }' "$1"
}

# Counts matches on non-comment lines. A COUNT rather than `code_lines | grep -q`
# because this file runs under `set -o pipefail`: grep -q exits on its first
# match, SIGPIPEs code_lines mid-write, and pipefail then reports the whole
# pipeline as failed -- so a large file whose match came early read as "no match"
# while a small one passed. That cost an hour and looked like a sed bug.
count_code() { code_lines "$1" | grep -c "$2"; }

# Files that put the radio out of service, in CODE rather than in prose.
#
# A function, NOT an inline `done < <(for ...)`: the pattern contains escaped
# parens, and bash could not find the closing paren of the process substitution.
# It failed with "bad substitution", the while-loop read nothing, and check 10
# reported green while examining ZERO files -- a check that did not run looks
# exactly like a check that passed.
RADIO_TAKERS_RE='esp_wifi_set_channel\(|esp_wifi_set_mode\(|esp_wifi_stop\(|esp_wifi_deinit\(|WiFi\.scanNetworks\(|WiFi\.softAP\(|WiFi\.softAPConfig\(|WiFi\.enableAP\(|WiFi\.AP\.begin\(|WiFi\.STA\.end\(|esp_now_init\(|WiFi\.mode\(WIFI_OFF\)|WiFi\.mode\(WIFI_MODE_NULL\)|WiFi\.mode\(WIFI_AP\)|WiFi\.mode\(WIFI_AP_STA\)'
radio_takers() {
  local f
  grep -rlE "$RADIO_TAKERS_RE" "$ROOT/src" "$ROOT/lib" 2>/dev/null | sort | while IFS= read -r f; do
    # A header that merely NAMES esp_wifi_stop() in a comment about somebody
    # else's teardown is not taking the radio.
    if [ "$(code_lines "$f" | grep -cE "$RADIO_TAKERS_RE")" -gt 0 ]; then echo "$f"; fi
  done
}

# -- 10. taking the radio out of service means yielding Developer Mode --------
#
# Check 9 is about READING who owns the radio. This is about TAKING it.
#
# These operations leave the radio unusable to anyone else, because they end the
# AP association rather than merely closing a socket on top of it: the channel
# and mode setters -- including the spellings that are the same thing under a
# different name, since WIFI_MODE_NULL is WIFI_OFF and WIFI_AP_STA is an AP --
# softAP(), which raises an AP without ever naming a mode, esp_now_init(),
# scanNetworks(), which walks off the channel for seconds at a time, and
# esp_wifi_stop(), which is the least ambiguous of the lot -- the radio is off
# for everyone, and it desyncs the Arduino core's own _esp_wifi_started besides.
#
# WiFi.disconnect( is deliberately NOT here: a self-owned teardown is a
# legitimate use of it, and four files use it that way behind their own
# ownership flag. Two of those flags are not real ownership; see Known limits in
# docs/developer-mode.md.
#
# lib/ is scanned as well as src/. LinkRadio's own comment warns that the
# FreeInk SDK ships NearbyTransfer, an ESP-NOW library, and that "whichever
# registered last silently wins": an SDK file adopting it is precisely the
# violation this exists for, and it would never live under src/.
#
# Developer Mode cannot detect any of them. Its own "is somebody else using
# this?" test is `WiFi.status() == WL_CONNECTED`, so an owner that never
# associates -- ESP-NOW, exactly -- is invisible to it, and it responds by
# rejoining the AP kMinBackoffMs later and dragging the radio back to the
# router channel.
#
# That shipped. Link multiplayer paired, played one move, and lost the peer ten
# seconds afterwards, on two devices sitting next to each other, because
# LinkRadio pinned channel 1 while dev mode pulled the radio to channel 9. The
# 5s backoff is the reason the FIRST move always landed: it is the width of the
# window before dev mode noticed.
#
# The rule: a file that can put the radio out of service must reason about
# Developer Mode somewhere -- pause() it, or ask holdsRadio(). That is a coarse
# bar deliberately; it does not try to prove the reasoning is right, only that
# it happened. A new file that does none of it is the case this exists to catch.
#
# DISCOVERED, not listed, for the reason spelled out in check 9.
#
# FIRST, the canaries. Three times on this branch a check stopped examining
# files and went on reporting green -- a broken process substitution, then two
# generations of comment scanner that swallowed whole files. Every time the only
# tell was the check COUNT not moving, and every time a human noticed rather
# than the suite.
#
# A COUNT floor was the obvious guard and it is worthless: measured against all
# four broken scanner generations, the taker count stayed at 7 for every one of
# them, so it caught none of the bugs it was written for. Worse, its slack was
# one file, and the affordable one was LinkRadio.cpp -- the subject of this
# entire branch could leave the check set with the floor still green. And
# consolidating two teardowns into a helper would have failed it for no reason.
#
# So: name the files that MUST be discovered. This is not the hardcoded list
# check 9 condemns -- that one REPLACED the discovery, this one WATCHES it. If a
# named file stops taking the radio, that is a deliberate change and this list
# is the right place to notice it.
# Captured ONCE, and matched with `case`, not `radio_takers | grep -q`: this
# file runs under pipefail, and grep -q exits on its first match, SIGPIPEs the
# producer and reports the pipeline as failed. That trap is documented forty
# lines above and I walked straight into it writing this.
discovered_takers="$(radio_takers)"
for must in "src/apps_local/link/LinkRadio.cpp" \
            "src/activities/network/WifiSelectionActivity.cpp" \
            "src/activities/network/CrossPointWebServerActivity.cpp"; do
  case "
$discovered_takers" in
    *"
$ROOT/$must"*) ok ;;
    *) bad "check 10's discovery lost $must -- the DISCOVERY is broken, or that file genuinely stopped taking the radio and this list needs updating" ;;
  esac
done

while IFS= read -r path; do
  rel="${path#$ROOT/}"
  # DevMode.cpp is Developer Mode. Asking it to consult itself is circular, and
  # exempting it by name is honest in a way that a silent skip would not be.
  [ "$rel" = "src/DevMode.cpp" ] && continue
  if [ "$(count_code "$path" 'devmode::')" -gt 0 ]; then
    ok
  else
    bad "$rel takes the radio out of service without ever mentioning devmode:: -- it will cut Developer Mode off, or be cut off by it mid-use"
  fi
done < <(radio_takers)

# -- 11. every pause() has its resume(), in the same file --------------------
#
# Check 10 only proves a file says the word. This proves the pairing. Both ways
# of getting it wrong are live bugs: pause with no resume strands Developer Mode
# off the network until a reboot, and resume with no pause is the shipped
# behaviour that broke link multiplayer.
#
# Same file, because these are acquire/release around one owner's lifetime --
# LinkRadio begin()/end(), an activity's onEnter()/onExit(). A pause handed to
# another file to release is not a pattern here and should not become one
# quietly.
#
# The count has to match, not merely be non-zero. yieldDepth is a counter
# precisely so the holders can nest (the web screen yields, then opens the
# Wi-Fi picker, which yields again), and a counter is what makes an unbalanced
# pair leak instead of fail loudly.
#
# The input set is files with EITHER call. A file carrying only a resume() is
# the more dangerous half -- it releases a yield it never took, dropping the
# count out from under a holder that still has the ports -- and keying the
# search on pause() alone put exactly that case beyond this check's reach.
#
# WHAT THIS CANNOT SEE, said plainly: it counts source lines, not calls, and it
# cannot see REACHABILITY at all. An onExit() with an early `return` above its
# resume(), or a resume() behind an `if`, reads 1:1 here and still strands
# Developer Mode until a reboot.
# LinkRadio.cpp reads 1:1 here while end() runs three times per match, and what
# actually makes that pairing correct at runtime is the held_ flag, not this
# check. A grep cannot count calls. Do not read a pass as proof of balance.
while IFS= read -r path; do
  rel="${path#$ROOT/}"
  p_count="$(count_code "$path" 'devmode::pause()')"
  r_count="$(count_code "$path" 'devmode::resume()')"
  if [ "$p_count" = "$r_count" ]; then
    ok
  else
    bad "$rel calls devmode::pause() $p_count time(s) but devmode::resume() $r_count time(s) -- Developer Mode is left yielded or released early"
  fi
done < <(grep -rlE 'devmode::(pause|resume)\(\)' "$ROOT/src" "$ROOT/lib" 2>/dev/null | sort)

# The same canary, for the same reason: a file swallowed by the scanner reads
# 0 pauses against 0 resumes here, which compare EQUAL and report ok.
yielders="$(grep -rlE 'devmode::(pause|resume)\(\)' "$ROOT/src" "$ROOT/lib" 2>/dev/null | sort)"
for must in "src/apps_local/link/LinkRadio.cpp" \
            "src/activities/network/WifiSelectionActivity.cpp" \
            "src/activities/network/CrossPointWebServerActivity.cpp"; do
  case "
$yielders" in
    *"
$ROOT/$must"*) ok ;;
    *) bad "check 11 no longer sees $must yielding at all" ;;
  esac
  if [ "$(count_code "$ROOT/$must" 'devmode::pause()')" -gt 0 ]; then
    ok
  else
    bad "check 11 reads zero pause() calls in $must -- 0 == 0 would report balanced"
  fi
done

# -- 12. the input vocabulary has exactly one implementation ------------------
#
# lib/DevInput/DevInputCommands.cpp exists so the serial bridge and Developer
# Mode's /api/dev/input cannot drift: a device that answers TAP down a cable but
# not over Wi-Fi, or takes the arguments in a different order on each, is a trap
# that only springs while somebody is already debugging something else.
#
# Nothing enforced that. host-tests/devinput exercises the shared core with a
# stub injector and neither transport in the build, so it proves the core
# behaves and says nothing about who calls it. This is the other half: schedule
# onto the injector from anywhere else and you have started a second dialect.
while IFS= read -r path; do
  rel="${path#$ROOT/}"
  if [ "$rel" = "lib/DevInput/DevInputCommands.cpp" ]; then
    ok
  else
    bad "$rel schedules input directly instead of going through devinput::runCommand() -- that is a second vocabulary"
  fi
done < <(grep -rlE 'devinput::(tap|longPress|swipe|button)\(' "$ROOT/src" "$ROOT/lib" 2>/dev/null | sort |
  while IFS= read -r f; do
    # In code, not in prose: a comment naming devinput::tap() is not a caller.
    if [ "$(code_lines "$f" | grep -cE 'devinput::(tap|longPress|swipe|button)\(')" -gt 0 ]; then echo "$f"; fi
  done)

# The shared unit must still be visible to the scanner at all: swallowed, it
# emits zero checks here and the whole rule evaporates.
if [ "$(count_code "$ROOT/lib/DevInput/DevInputCommands.cpp" 'devinput::tap(')" -gt 0 ]; then
  ok
else
  bad "check 12 cannot see lib/DevInput/DevInputCommands.cpp scheduling input -- the scanner swallowed it"
fi

# And both transports must actually route through it, or the shared unit is just
# an unused library that happens to compile.
#
# This half is a REGRESSION GUARD over the two transports that exist, not a
# discovery: a third one that hand-rolls a parser is caught by the loop above
# instead, because it would have to schedule onto the injector to do anything.
# What neither half catches is a caller inside `namespace devinput` calling
# tap() unqualified.
for caller in "src/DevSerialBridge.cpp" "src/network/CrossPointWebServer.cpp"; do
  if [ "$(count_code "$ROOT/$caller" 'devinput::runCommand(')" -gt 0 ]; then
    ok
  else
    bad "$caller does not call devinput::runCommand() -- it has its own input parsing again"
  fi
done

# -- 13. the comment scanner itself, against a fixture ------------------------
#
# Four generations of code_lines() have each been walked around, and each was
# found by a person reading it rather than by anything here. That is the wrong
# way round: the scanner decides which files checks 10 to 12 even look at, so a
# hole in it silences them without failing anything.
#
# So it gets a fixture with every shape that has caught it out, and the
# assertions are on MEANING -- "the word inside this comment is gone", "the code
# either side of it survived" -- rather than on exact output, so reformatting
# the scanner does not rewrite the test.
scanner_fixture="$(mktemp)"
trap 'rm -f "$scanner_fixture"' EXIT  # as host-tests/checksh does
cat > "$scanner_fixture" <<'FIXTURE'
int keepA(); // GONE_LINE_COMMENT
int keepB(); /* GONE_SAME_LINE */ int keepC();
int keepD(); /* GONE_TRAILING_OPEN
   GONE_BLOCK_BODY
   */ int keepE();
const char* u = "http://KEEP_IN_STRING";
const char* k = "/*KEEP_STAR_IN_STRING";
int keepF();
void f(char c) { if (c == '"') keepG(); } // GONE_AFTER_CHAR_LITERAL
char esc = '\''; int keepH(); // GONE_AFTER_ESCAPED_QUOTE
constexpr int keepI = 0x70B0'0001; // GONE_AFTER_DIGIT_SEPARATOR
FIXTURE
scanner_raw="$(cat "$scanner_fixture")"
scanner_out="$(code_lines "$scanner_fixture")"
rm -f "$scanner_fixture"

scanner_ok=1
scanner_why=""
# Every token must actually BE in the fixture. A GONE_ token that is missing --
# a typo, or a line edit that did not land -- would otherwise pass trivially,
# because absence is exactly what the assertion below wants. This test asserted
# nothing at all for one commit for precisely that reason.
for token in GONE_LINE_COMMENT GONE_SAME_LINE GONE_TRAILING_OPEN GONE_BLOCK_BODY GONE_AFTER_CHAR_LITERAL \
             GONE_AFTER_ESCAPED_QUOTE GONE_AFTER_DIGIT_SEPARATOR KEEP_IN_STRING KEEP_STAR_IN_STRING; do
  case "$scanner_raw" in
    *"$token"*) ;;
    *) scanner_ok=0; scanner_why="$scanner_why absent-from-fixture:$token" ;;
  esac
done
# Everything named GONE_ must be stripped; everything named keep must survive.
for token in GONE_LINE_COMMENT GONE_SAME_LINE GONE_TRAILING_OPEN GONE_BLOCK_BODY GONE_AFTER_CHAR_LITERAL \
             GONE_AFTER_ESCAPED_QUOTE GONE_AFTER_DIGIT_SEPARATOR; do
  case "$scanner_out" in
    *"$token"*) scanner_ok=0; scanner_why="$scanner_why kept:$token" ;;
  esac
done
for token in keepA keepB keepC keepD keepE keepF keepG keepH keepI KEEP_IN_STRING KEEP_STAR_IN_STRING; do
  case "$scanner_out" in
    *"$token"*) ;;
    *) scanner_ok=0; scanner_why="$scanner_why lost:$token" ;;
  esac
done
if [ "$scanner_ok" -eq 1 ]; then
  ok
else
  bad "code_lines mishandled the fixture --$scanner_why"
fi

# -- 14. the release notes are about the release being made ------------------
#
# v1.6.2 shipped v1.6.1's notes verbatim. The heading still said "What is new
# in 1.6.1", so the multiplayer fix that release existed for was announced to
# nobody, and it was found days later by someone reading the published page
# rather than by anything here.
#
# The notes live in docs/release-notes.md, which is exactly the kind
# of place a version number goes stale: nothing about tagging touches them, and
# the release still builds and publishes perfectly. So assert the one thing that
# cannot be true of stale notes -- that they name the version being released.
# Anchored to [crossplay], not positional. `tail -1` picked whichever version=
# key came last, and platformio.ini has two -- [crosspoint] is upstream's. A
# merge that adds or reorders a section would have silently repointed this at
# the wrong number and stayed green.
NOTES_VERSION="$(awk '/^\[crossplay\]/{f=1;next} /^\[/{f=0} f && /^version *=/{print $3; exit}' "$ROOT/platformio.ini")"
if [ -z "$NOTES_VERSION" ]; then
  bad "could not read the crossplay version out of platformio.ini"
else
  # The notes block only, not the whole workflow. A cold reviewer defeated the
  # file-wide version of this with a one-line YAML comment -- `# TODO: What is
  # new in 1.6.4 -- write the notes` satisfied the gate while every bullet
  # below it stayed the previous release's.
  # docs/release-BODY.md, which is what body_path publishes. docs/release-notes.md
  # is the history and is deliberately not this file: they were one until
  # 2026-09-04, and every release page carried every earlier release.
  NOTES_FILE="$ROOT/docs/release-body.md"
  NOTES_BODY="$(cat "$NOTES_FILE" 2>/dev/null)"
  # The workflow must publish the body, not the history. A body_path pointing
  # back at docs/release-notes.md would restore the 20,402-character page and
  # every check below would still pass, because the history's newest block does
  # name the version being released.
  if grep -qE '^ *body_path: *docs/release-body\.md *$' "$WF"; then
    ok
  else
    bad "$(basename "$WF") does not publish docs/release-body.md; if it publishes the history, every release page carries every earlier release"
  fi
  # And the body carries THIS release only. One "What is new in" heading, and
  # no `### <version>` heading of the kind the history uses.
  HEADS="$(printf '%s' "$NOTES_BODY" | grep -cE '^### What is new in ' || true)"
  OLDHEADS="$(printf '%s' "$NOTES_BODY" | grep -cE '^### (What (WAS|was) new in|[0-9]+\.[0-9]+\.[0-9]+ *$)' || true)"
  if [ "$HEADS" = "1" ] && [ "$OLDHEADS" = "0" ]; then
    ok
  else
    bad "docs/release-body.md carries $HEADS 'What is new' heading(s) and $OLDHEADS past-release heading(s); a release page says what changed in THAT release"
  fi
  # -- and the page is TINY -------------------------------------------------
  #
  # Twice now the page has been cut and twice Mario has called it impossibly
  # long: 20,402 characters, then 4,610. Both cuts were judgement, and judgement
  # is what grew it back both times -- every paragraph on that page was added by
  # someone who could name a reader it would help. The install steps help a
  # first-timer. The asset list helps whoever has to pick a file. The slot
  # warning helps a device that will refuse the update. None of that is wrong,
  # and all of it is already in README.md, docs/install.md and on the site, one
  # click away, maintained in one place instead of copied onto every tag.
  #
  # So it is a number here rather than an opinion there. A release page is this
  # release's bullets plus one line of links. Anything that needs a paragraph
  # needs a different file.
  #
  # TWO ceilings, because they fail differently. The whole body catches a
  # section coming back. The standing text -- everything outside the generated
  # block -- catches the slower version: a sentence at a time, each one
  # defensible, none of them regenerated by anything, which is exactly how the
  # 20,402-character page was written.
  BODY_CHARS="$(printf '%s' "$NOTES_BODY" | wc -m | tr -d ' ')"
  if [ "$BODY_CHARS" -le 1200 ]; then
    ok
  else
    bad "docs/release-body.md is $BODY_CHARS characters (ceiling 1200); a release page is this release's bullets and a line of links -- the install steps, the asset list and the project description belong in README.md and docs/install.md, which already carry them"
  fi
  # "Outside the block" is the heading plus its bullets and NOTHING else --
  # written as "from the heading to the next ###" first, which is how
  # release_notes.py finds it, and that version had no teeth. A paragraph
  # appended below the bullets, with no heading between, counted as part of the
  # block and the ceiling never saw it. That is the exact shape the check exists
  # to catch, so the block is bounded by what it contains (bullets and blanks)
  # rather than by what follows it.
  block_stripped() {
    awk '/^### What is new in /{b=1;next}
         b && ($0 ~ /^- / || $0 ~ /^[[:space:]]*$/){next}
         {b=0; print}'
  }
  STANDING="$(printf '%s\n' "$NOTES_BODY" | block_stripped)"
  STANDING_CHARS="$(printf '%s' "$STANDING" | wc -m | tr -d ' ')"
  if [ "$STANDING_CHARS" -le 320 ]; then
    ok
  else
    bad "docs/release-body.md carries $STANDING_CHARS characters outside the generated block (ceiling 320); nothing regenerates that text, so it goes stale on every tag that does not touch it"
  fi

  # And the standing text is ABOVE the heading, all of it. This is not style.
  # rewrite_notes() scans from the heading to the next `###` or the end of the
  # file, and the "or the end" is reached in a body with one heading -- so a
  # line written below the bullets is deleted by the next release, in a commit
  # the autorelease pushes by itself, leaving a body that still parses and still
  # passes every other check here. Silent loss, once per tag.
  BELOW="$(printf '%s\n' "$NOTES_BODY" |
    awk '/^### What is new in /{b=1;next} b && $0 !~ /^- / && $0 !~ /^[[:space:]]*$/{print}')"
  if [ -z "$BELOW" ]; then
    ok
  else
    bad "docs/release-body.md has text below the What-is-new bullets ($(printf '%s' "$BELOW" | head -1)); release_notes.py replaces the block through to the end of the file, so the next release deletes it without saying so -- put standing text above the heading"
  fi

  # And no install steps, named rather than measured. A ceiling alone would let
  # a terse `esptool` one-liner back on, which is the shape the page had before
  # anyone wrote a paragraph around it -- and a command on a release page is a
  # command maintained in two places, which is how v1.0.0 and v1.0.1 told people
  # to write the application image over the bootloader.
  # `grep -qF -- "$pat"`, and the -- is the whole check. Without it grep read
  # `-full.bin` as `-f ull.bin`, went looking for a pattern file of that name,
  # printed "ull.bin: No such file or directory" into the suite's own output and
  # returned non-zero -- so the one pattern most likely to come back was the one
  # pattern that could never match, and the check passed by failing to run.
  STEPS=""
  while IFS= read -r pat; do
    [ -n "$pat" ] || continue
    printf '%s' "$NOTES_BODY" | grep -qF -- "$pat" && STEPS="$STEPS $pat"
  done <<'PATS'
esptool
write_flash
pip install
```
-full.bin
firmware.bin
Web Serial
6.25MB
PATS
  if [ -z "${STEPS// /}" ]; then
    ok
  else
    bad "docs/release-body.md is explaining the install again ($(echo $STEPS)); docs/install.md is the file for that and the body links to it"
  fi

  # The link has to be there, or the cut above is a deletion. Everything this
  # page stopped saying is reachable in one hop: install.md for the files, the
  # by-hand steps and the 6.25MB slot; release-notes.md for every earlier
  # release. If either link goes, the page got smaller by losing something.
  MISSING_LINKS=""
  for target in docs/install.md docs/release-notes.md; do
    printf '%s' "$NOTES_BODY" | grep -qF "$target" || MISSING_LINKS="$MISSING_LINKS $target"
  done
  if [ -z "${MISSING_LINKS// /}" ]; then
    ok
  else
    bad "docs/release-body.md no longer links to:$MISSING_LINKS -- the page is short because those files carry what it stopped saying, and an unlinked reader just lost it"
  fi

  # The history has to exist, and has to hold what the body no longer does.
  if [ -f "$ROOT/docs/release-notes.md" ] &&
     grep -q '<!-- releases, newest first -->' "$ROOT/docs/release-notes.md" &&
     [ "$(grep -cE '^### [0-9]+\.[0-9]+\.[0-9]+ *$' "$ROOT/docs/release-notes.md")" -ge 2 ]; then
    ok
  else
    bad "docs/release-notes.md is not a history with an insertion marker and at least two releases in it; the body links to it and the link would go nowhere useful"
  fi

  # AND IT HAS TO BE A HISTORY OF THIS REPOSITORY, not of whichever releases
  # somebody remembered. docs/release-notes.md opens by telling a reader it
  # holds "every tagged release from 1.12.1 onward" -- the release page used to
  # carry that sentence and no longer carries any sentence at all, so the file's
  # own preamble is the only place the promise is made now. The first version of
  # it was false the day it was written: the file jumped 1.12.21 -> 1.12.12,
  # with six tagged releases missing in between. prepend_history()
  # only ever adds the release being made, so nothing self-corrects -- a gap
  # opened by hand stays open, and the claim quietly goes on being wrong.
  #
  # Every v1.12+ tag reachable from THIS tree's HEAD must have a block. Older
  # tags are excluded deliberately: 1.12.1 is where this file starts and the
  # sentence says so.
  # 1.12.14 and 1.12.15 have no tags in this repository or on the remote --
  # they are the two releases that published a -full.bin with its bootloader
  # missing -- so a tag-driven check cannot ask for them and the preamble
  # explains their absence in prose instead.
  # grep -xF, not -E: the version is a fixed string and escaping its dots into
  # a regex inside a nested command substitution is how the first attempt at
  # this check died on `set -u`.
  # --merged HEAD, not the bare list, and that scoping is the point. Every
  # worktree shares one object store, so the instant autorelease cuts v1.12.N
  # its tag is visible from every tree at once -- but a tree behind
  # origin/xteink has NOT merged the bump commit that carries that release's
  # ### entry, so a bare `tag --list` would demand an entry the tree cannot
  # have yet and red-gate a release it does not even contain. --merged HEAD
  # lists only tags whose commit is an ancestor of HEAD: on xteink (and in CI)
  # HEAD contains every release, so the gate still asks for all of them and is
  # unchanged where it ships; on a behind tree the new tag drops out until the
  # tree merges xteink, which brings the tag's commit AND its ### entry in
  # together, so the tag reappears in the list already satisfied. An
  # orphan/upstream tag never in xteink's history stays excluded, correctly.
  MISSING_TAGS=""
  while IFS= read -r tagname; do
    [ -n "${tagname:-}" ] || continue
    ver="${tagname#v}"
    [ "$ver" = "1.12.0" ] && continue
    grep -qxF "### $ver" "$ROOT/docs/release-notes.md" || MISSING_TAGS="$MISSING_TAGS $ver"
  done <<EOF
$(git -C "$ROOT" tag --list 'v1.12.*' --merged HEAD 2>/dev/null)
EOF
  if [ -z "${MISSING_TAGS// /}" ]; then
    ok
  else
    bad "docs/release-notes.md has no entry for tagged release(s): $MISSING_TAGS -- the body tells readers it holds every tagged release back to 1.12.1"
  fi
  # Escaped dots AND a boundary. Two attempts got this wrong: an unanchored grep
  # treats . as a wildcard, and a "not followed by a dot" guard still passed
  # "1.6.31" because what follows 1.6.3 there is a digit. The version must be the
  # whole number -- followed by end of line or by something that is not a digit
  # or a dot.
  ESCAPED="$(printf '%s' "$NOTES_VERSION" | sed 's/\./\\./g')"
  if ! printf '%s' "$NOTES_BODY" | grep -qE "What is new in $ESCAPED([^0-9.]|$)"; then
    bad "$(basename "$NOTES_FILE") does not say \"What is new in $NOTES_VERSION\" -- it is the previous release's notes, and the tag will publish them"
  else
    ok
  fi

  # And the heading is not the notes. v1.6.2 shipped v1.6.1's text verbatim;
  # renaming the heading and leaving the bullets is that same failure minus one
  # line of editing, and the gate above cannot see it. Compare against what the
  # previous tag actually published.
  # Strict vMAJOR.MINOR.PATCH only. 'v*' also matched release candidates and any
  # stray tag, either of which becomes the baseline, differs from the real notes,
  # and passes the check by accident rather than on merit.
  # EXCLUDING the version being released. The README has the bump and the tag
  # going out together, so by the time CI runs, the newest tag IS this version
  # -- and comparing the notes against themselves skipped the gate in the only
  # run that could have caught anything.
  PREV_TAG="$(git -C "$ROOT" tag --list --sort=-v:refname |
    grep -E '^v[0-9]+\.[0-9]+\.[0-9]+$' | grep -vx "v$NOTES_VERSION" | head -1)"
  if [ -z "$PREV_TAG" ]; then
    skip "no previous tag to compare the notes against"
  elif ! git -C "$ROOT" cat-file -e "$PREV_TAG:docs/release-body.md" 2>/dev/null &&
       ! git -C "$ROOT" cat-file -e "$PREV_TAG:docs/release-notes.md" 2>/dev/null &&
       ! git -C "$ROOT" cat-file -e "$PREV_TAG:.github/workflows/crossplay-release.yml" 2>/dev/null; then
    skip "$PREV_TAG has no release notes to compare against"
  else
    # Whitespace-normalised on both sides. Comparing raw text let one inserted
    # blank line pass a body that was otherwise the previous release's word for
    # word -- which is the same mistake with one keystroke of camouflage.
    norm() { grep -v 'What is new in' | tr -s '[:space:]' ' ' | sed 's/^ //; s/ $//'; }
    # The body has lived in three places: inside the workflow, then
    # docs/release-notes.md, then docs/release-body.md. Read whichever the
    # previous tag had.
    if git -C "$ROOT" cat-file -e "$PREV_TAG:docs/release-body.md" 2>/dev/null; then
      PREV_BODY="$(git -C "$ROOT" show "$PREV_TAG:docs/release-body.md" | norm)"
    elif git -C "$ROOT" cat-file -e "$PREV_TAG:docs/release-notes.md" 2>/dev/null; then
      PREV_BODY="$(git -C "$ROOT" show "$PREV_TAG:docs/release-notes.md" | norm)"
    else
      PREV_BODY="$(git -C "$ROOT" show "$PREV_TAG:.github/workflows/crossplay-release.yml" |
        awk '/^ *body: \|/{f=1;next} f && /^ *[a-z_-]+:/{f=0} f' | norm)"
    fi
    THIS_BODY="$(printf '%s' "$NOTES_BODY" | norm)"
    if [ "$PREV_BODY" = "$THIS_BODY" ]; then
      bad "the release notes are byte-identical to $PREV_TAG's below the heading -- only the version was renamed, and $NOTES_VERSION would publish that tag's text again"
    else
      ok
    fi
  fi
fi

# The tag and the version are bound by a step in the release workflow, and
# nothing here noticed if that step disappeared -- while scripts_local/README.md
# told people this suite enforced it. Assert the step's substance, the way
# host-tests/ci asserts CI's.
# EXECUTE the step, do not grep it. Four greps for its ingredients passed a
# version of it ending in `&& false`, which can never fire -- the ingredients
# were all still there. host-tests/ci runs CI's step text for the same reason.
GUARD="$(awk '/- name: The tag must be the version being built/{f=1;next}
              f && /^      - /{exit}
              f && /run: \|/{g=1;next}
              g' "$WF")"
if [ -z "$GUARD" ]; then
  bad "$(basename "$WF") has no tag-versus-version step"
else
  guard_says() { (cd "$ROOT" && GITHUB_REF_NAME="$1" bash -c "$GUARD" > /dev/null 2>&1); }
  if guard_says "v$NOTES_VERSION"; then
    ok
  else
    bad "the release workflow's tag check rejects the correct tag v$NOTES_VERSION"
  fi
  # The half that matters, and the half a grep cannot see.
  if guard_says "v0.0.1-wrong"; then
    bad "the release workflow's tag check ACCEPTS a tag that disagrees with [crossplay] version"
  else
    ok
  fi
fi

# The host must still be listening when the device gives up, or a precise
# "device reported it gave up" degrades into "device stopped talking". The two
# numbers live in different languages in different directories and nothing made
# them agree; they were equal, which is not ordered.
GRACE_S="$(sed -n 's/^GRACE_S = \([0-9.]*\).*/\1/p' "$ROOT/tools_local/device/drive.py" | head -1)"
STALL_MS="$(sed -n 's/^constexpr unsigned long kBulkStallMs = \([0-9]*\).*/\1/p' "$ROOT/src/DevSerialBridge.cpp" | head -1)"
if [ -z "$GRACE_S" ] || [ -z "$STALL_MS" ]; then
  bad "cannot read GRACE_S (drive.py) or kBulkStallMs (DevSerialBridge.cpp); one of them was renamed"
elif [ "$(awk -v g="$GRACE_S" -v s="$STALL_MS" 'BEGIN{print (g*1000 > s) ? 1 : 0}')" = "1" ]; then
  ok
else
  bad "drive.py GRACE_S (${GRACE_S}s) must exceed DevSerialBridge kBulkStallMs (${STALL_MS}ms): the device gives up after the host stopped listening"
fi

# -- the Install button asks for a file the release actually publishes ---------
#
# site/api/firmware.js builds the download URL from a filename template rather
# than looking the asset up, which is the trade that keeps the GitHub API's
# per-IP rate limit off a shared server address. The cost is a literal filename
# in a second file, and nothing else would ever notice it drifting: renaming the
# artefact in the workflow leaves the site rendering perfectly, the button
# reaching a 404, and every other check green.
#
# Read from the JS rather than listed here, so adding a third board fails until
# the workflow names its image too.
API="$ROOT/site/api/firmware.js"
if [ ! -f "$API" ]; then
  bad "site/api/firmware.js is gone -- the Install button has nothing to download from"
else
  ok
  api_boards="$(grep -oE 'crossplay-\{tag\}-[a-z0-9]+-full\.bin' "$API" \
                | sed -E 's/^crossplay-\{tag\}-//; s/-full\.bin$//' | sort -u)"
  wf_boards="$(grep -oE 'crossplay-\$\{GITHUB_REF_NAME\}-[a-z0-9]+-full\.bin' "$WF" \
               | sed -E 's/^crossplay-\$\{GITHUB_REF_NAME\}-//; s/-full\.bin$//' | sort -u)"
  if [ -z "$api_boards" ]; then
    bad "api/firmware.js names no -full.bin image, so the Install button can never download one"
  elif [ "$api_boards" = "$wf_boards" ]; then
    ok
  else
    bad "the Install button asks for [$(echo $api_boards)] and the release publishes [$(echo $wf_boards)]"
  fi
fi

# One pio run invocation for both devices, and the reason is three files away.
#
# Read this before deciding a local gate would have caught it: it could not,
# structurally. check.sh builds one environment at a time, in a tree that
# already contains the generated headers. The wipe needs a SECOND pio run in a
# tree where the first one created them -- which is a fresh checkout, i.e. CI
# and only CI. The gate was blind to the exact failure it exists to prevent,
# and stayed green through four failed releases while being blind to it. That
# is why this assertion lives here, reading the workflow text, rather than
# being left to a build that cannot reach the condition.
#
# pio run calls clean_build_dir() once per invocation against the whole
# .pio/build root, and rmtree's it when compute_project_checksum() differs from
# the stored one. That checksum covers the .h files under src/ and include/,
# and this project generates some of those during the build -- gitignored, so
# absent on a fresh checkout. A SECOND invocation therefore always wipes the
# first one's output before compiling anything. That is what emptied
# .pio/build/gh_release_x4pro/ under v1.12.14 and v1.12.15, taking firmware.bin
# and partitions.bin with the bootloader.
#
# Splitting the build back into one invocation per device restores it exactly,
# whether or not the artefacts are named in between.
checks=$((checks + 1))
runs=$(grep -c "run: pio run -e gh_release" "$WF")
if [ "$runs" -eq 1 ] && grep -q "pio run -e gh_release_x4pro -e gh_release_sticky" "$WF"; then
  ok
else
  failed=$((failed + 1))
  echo "FAIL release  the release must build both devices in ONE pio run invocation (found $runs); a second invocation wipes the first one's .pio/build"
fi

# ...and a step ON THE RUNNER that checks the build outputs are really there.
#
# Read the two together, because they are not the same kind of check and the
# difference is the point. The one above reads the workflow TEXT, and its scope
# rests on an assumption: that a second pio run is the only way .pio/build
# empties. That is the cause we know about, not the condition we care about.
# The workflow STEP asserted below is the half that tests the condition, on the
# runner, where the files either exist or do not.
#
# THIS assertion is text on text like everything else in this file, so it has
# to be written to fail. The first version of it parsed the step's `for`
# headers and never looked at what the `[ -f ... ]` expression referenced, so a
# guard whose loops promised two envs by four files while its body tested
# firmware.bin four times passed it -- the same error the guard exists to
# correct, one layer down. So: derive coverage from the test EXPRESSION,
# expanded over the loops its own variables come from, and assert separately
# that the step is ARMED. A guard that prints ::error:: and lets the job carry
# on is the failure being guarded against, wearing the guard's clothes.
guard_hits=$(grep -nE '\[ +-f +"?\.pio/build' "$WF" | cut -d: -f1)
n_guard=$(printf '%s' "$guard_hits" | grep -c . || true)
if [ "$n_guard" -eq 0 ]; then
  bad "no step checks that .pio/build still holds the build outputs before the merge steps read them; both builds report SUCCESS when it is empty (v1.12.14, v1.12.15)"
elif [ "$n_guard" -gt 1 ]; then
  bad "$n_guard separate .pio/build existence tests; which one is the guard is ambiguous, and a decoy above the real one would be all this ever inspected"
else
  guard="$guard_hits"

  # The step that line belongs to, so the arming checks read the right block.
  step_start=$(awk -v g="$guard" 'NR<=g && /^      - name:/ {n=NR} END {print n+0}' "$WF")
  step_end=$(awk -v g="$guard" 'NR>g && /^      - name:/ {print NR; exit}' "$WF")
  [ -n "$step_end" ] || step_end=$(wc -l < "$WF")
  step=$(sed -n "${step_start},${step_end}p" "$WF")

  # ARMED, part one: it must be able to fail the job at all. `exit 0`, or a
  # flag that is never set to anything, is a guard that reports and shrugs.
  exit_var=$(printf '%s\n' "$step" | sed -nE 's/^ *exit +\$\{?([A-Za-z_][A-Za-z0-9_]*)\}? *$/\1/p' | tail -1)
  if [ -z "$exit_var" ]; then
    bad "the on-disk check does not end in 'exit \$<var>', so nothing it finds can fail the release build"
  elif ! printf '%s\n' "$step" | grep -qE "^ *$exit_var=[0-9]*[1-9][0-9]* *$"; then
    bad "the on-disk check exits \$$exit_var but never assigns it a non-zero value, so a missing file cannot fail the release build"
  else
    ok
  fi

  # ARMED, part two: the step must run, and its failure must count.
  if printf '%s\n' "$step" | grep -qE '^ *continue-on-error:'; then
    bad "the on-disk check carries continue-on-error, so the release proceeds over a missing build output"
  elif printf '%s\n' "$step" | grep -qE '^ {8}if:'; then
    bad "the on-disk check is conditional; it must run on every release build"
  else
    ok
  fi

  # COVERAGE, derived from the expression itself. Every variable in it is
  # expanded over the nearest `for <var> in ...` above the test, longest name
  # first so $f cannot eat the tail of $file. A body that stops mentioning one
  # of the loop variables collapses the cross product and fails right here.
  gexpr=$(sed -n "${guard}p" "$WF" | grep -oE '\.pio/build/[^"]*' | head -1)
  covered="$gexpr"
  for v in $(printf '%s' "$gexpr" | grep -oE '\$\{?[A-Za-z_][A-Za-z0-9_]*\}?' | tr -d '${}' \
             | awk '{print length, $0}' | sort -rn | cut -d' ' -f2- | awk '!seen[$0]++'); do
    list=$(sed -n "1,${guard}p" "$WF" | grep -oE "for +$v +in +[^;]*" | tail -1 | sed -E "s/for +$v +in +//")
    next=""
    for item in $list; do
      for c in $covered; do
        next="$next $(printf '%s' "$c" | sed -e "s/\${$v}/$item/g" -e "s/\$$v/$item/g")"
      done
    done
    covered="$next"
  done

  # What the workflow READS out of .pio/build, taken from the workflow with
  # comments excluded. The env pattern is deliberately wide: papermono-gh_release
  # is already an env in platformio.ini, and a pattern of gh_release_[a-z0-9]+
  # would not see a third board's artefacts at all -- the coverage loop would
  # simply never ask about them, and report clean.
  for path in $(grep -vE '^ *#' "$WF" \
                | grep -oE '\.pio/build/[A-Za-z0-9_.-]+/[A-Za-z0-9._-]+' | sort -u); do
    consumer=$(grep -nF -- "$path" "$WF" | grep -vE ':[[:space:]]*#' | head -1 | cut -d: -f1)
    if ! printf ' %s ' $covered | grep -qF " $path "; then
      bad "the on-disk check never tests $path, which the workflow reads; a guard that misses a file clears a build it never looked at"
    elif [ -n "$consumer" ] && [ "$guard" -gt "$consumer" ]; then
      bad "the on-disk check (line $guard) runs after $path is read (line $consumer)"
    else
      ok
    fi
  done
fi

# The release must not be started twice for one tag.
#
# crossplay-release.yml triggers on a tag push AND on workflow_dispatch. A tag
# pushed with GITHUB_TOKEN starts nothing, so autorelease dispatches; a tag
# pushed with RELEASE_TOKEN starts it, and dispatching as well builds the same
# tag twice. v1.12.16 did: event=push and event=workflow_dispatch, one second
# apart, both ~14 minutes, both exiting 0 having raced to publish the same
# assets. Silent in both directions, which is why it needs asserting.
checks=$((checks + 1))
AR="$ROOT/.github/workflows/crossplay-autorelease.yml"
if ! grep -q "gh workflow run crossplay-release.yml" "$AR"; then
  ok  # nothing dispatches, so nothing can double up
elif grep -q "RELEASE_TOKEN != ''" "$AR"; then
  ok
else
  failed=$((failed + 1))
  echo "FAIL release  crossplay-autorelease.yml dispatches crossplay-release.yml unconditionally; with RELEASE_TOKEN set the tag push starts it too, and the tag builds twice"
fi

# And the other direction, which is worse. Skipping the dispatch is only safe
# because the tag push starts the build instead. Remove that trigger and
# NOTHING starts it: no duplicate, no build, no release, and no error either --
# strictly worse than the double build this guards against. So the skip and the
# trigger are asserted together, or the skip is not safe to assert at all.
checks=$((checks + 1))
if grep -q "secrets.RELEASE_TOKEN" "$AR"; then
  if awk '/^on:/,/^jobs:/' "$WF" | grep -q '"v\*"'; then
    ok
  else
    failed=$((failed + 1))
    echo "FAIL release  autorelease skips its dispatch when RELEASE_TOKEN is set, but crossplay-release.yml has no push trigger on v* tags, so nothing would start the build at all"
  fi
else
  ok
fi

# -- the workflow that PUBLISHES must serialise against itself ----------------
#
# A concurrency group does not span workflows. crossplay-autorelease.yml carries
# `group: crossplay-release`, which reads like the release is serialised and
# serialises only the autorelease; this file -- the one that builds the images
# and uploads them to the release -- had no `concurrency:` at all.
#
# It has already gone wrong. v1.12.16 was built and published TWICE, one second
# apart: run 33884760714 on the tag push and 33884760111 on the dispatch, same
# tag, twelve minutes each, both racing to upload the same assets, and both
# exiting 0. The dispatch condition asserted above closes the
# path that caused THAT one, but it makes the collision avoidable rather than
# impossible: a hand dispatch during a tag build still overlaps, and the guard
# lives in a different file from the workflow it protects.
#
# Read out of the workflow's own top-level block, not out of any job's.
CONC="$(awk '/^concurrency:/{f=1;next} /^[a-z]/{f=0} f' "$WF")"
checks=$((checks + 1))
GROUP="$(printf '%s\n' "$CONC" | sed -n 's/^ *group: *//p')"
if [ -z "$GROUP" ]; then
  failed=$((failed + 1))
  echo "FAIL release  crossplay-release.yml has no top-level concurrency group, so two starts on one tag build and publish the same release side by side -- which is what v1.12.16 did"
else
  ok
fi

# Grouped by the ref. A single fixed group would serialise two DIFFERENT tags
# against each other, which is not the problem being solved and would leave a
# release waiting on an unrelated one.
checks=$((checks + 1))
case "$GROUP" in
  "") : ;;  # already failed above
  *'github.ref'*) ok ;;
  *)
    failed=$((failed + 1))
    echo "FAIL release  crossplay-release.yml's concurrency group ('$GROUP') is not keyed by the ref, so two different tags queue behind each other instead of two runs of the same one"
    ;;
esac

# And it must not cancel. A publish killed half way leaves a GitHub release
# carrying some of its assets, and the fleet's updater matches asset names: a
# release with firmware.bin and no firmware-sticky.bin is not a smaller
# release, it is a broken one for every Sticky in the field.
checks=$((checks + 1))
case "$(printf '%s\n' "$CONC" | sed -n 's/^ *cancel-in-progress: *//p')" in
  false) ok ;;
  "")
    failed=$((failed + 1))
    echo "FAIL release  crossplay-release.yml does not set cancel-in-progress, and the default cancels: a superseded publish can leave a release with only some of its assets"
    ;;
  *)
    failed=$((failed + 1))
    echo "FAIL release  crossplay-release.yml cancels its own in-progress publish; a half-uploaded release is worse than a duplicated one"
    ;;
esac

# -- the shipping binary is built by the toolchain everything else is built by -
#
# This workflow installed `platformio` from PyPI, unpinned: a different
# distribution from the pioarduino fork every other build workflow in this
# repository pins, and free to change between two tags with no commit of ours.
# The one binary that reaches devices was the one binary nothing had verified
# the toolchain of.
#
# The expected pin is DISCOVERED from the other workflows rather than written
# here, so a deliberate bump moves this file's answer with them instead of
# turning a version upgrade into a failing test that names the old number.
#
# Read the INSTALL COMMANDS, never the file. A whole-file grep for the pin is
# satisfied by a comment mentioning it -- and this file now carries a comment
# that explains the pin, so the check would have been one reword away from
# passing over `pip install platformio`. Strip comments first, then look only
# at lines that install something.
INSTALLS="$(sed 's/#.*//' "$WF" | grep -E 'pip +install')"
ALL_INSTALLS="$(sed 's/#.*//' "$ROOT"/.github/workflows/*.yml | grep -E 'pip +install')"

checks=$((checks + 1))
WANT="$(printf '%s\n' "$ALL_INSTALLS" | grep -o 'platformio-core/archive/refs/tags/[^ ]*\.zip' | sort | uniq -c | sort -rn | head -1 | sed 's/^ *[0-9]* *//')"
if [ -z "$WANT" ]; then
  failed=$((failed + 1))
  echo "FAIL release  no workflow in this repository pins platformio-core by tag on an install line, so there is nothing to hold the release build against"
elif printf '%s\n' "$INSTALLS" | grep -qF "$WANT"; then
  ok
else
  failed=$((failed + 1))
  echo "FAIL release  crossplay-release.yml does not install $WANT, the pinned PlatformIO every other build workflow uses: the image that ships to devices is built by a toolchain nothing else in this repository has verified"
fi

# And the other direction, because the check above only asks whether the right
# pin appears SOMEWHERE among the installs. An install of bare `platformio`
# beside it is still an unpinned toolchain, in whatever spelling: with a flag,
# without one, quoted, or version-pinned to something else on PyPI.
checks=$((checks + 1))
if printf '%s\n' "$INSTALLS" | grep -qE 'pip +install +([^|&;]*[[:space:]])?("|'"'"')?platformio("|'"'"')?([=<>!~][^[:space:]]*)?[[:space:]]*$'; then
  failed=$((failed + 1))
  echo "FAIL release  crossplay-release.yml installs PyPI 'platformio' rather than the pioarduino archive; whatever PyPI published most recently would build the release"
else
  ok
fi

echo "$checks checks, $failed failed"
[ "$failed" -eq 0 ]

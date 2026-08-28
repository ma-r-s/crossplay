#!/bin/bash
# Flash this tree's build to a device over Wi-Fi. No cable.
#
#   ./scripts_local/wifi-flash.sh --pair 123456     # once per device session
#   ./scripts_local/wifi-flash.sh                   # x4pro build, finds the device
#   ./scripts_local/wifi-flash.sh --env sticky
#   ./scripts_local/wifi-flash.sh --ip 192.168.1.42 --build
#
# The device needs Developer Mode on: Settings > System > Developer Mode. That
# screen shows an address and a six-digit code. Pair once with --pair <code>;
# the token is cached in ~/.crossplay-devtoken and reused until the device
# reboots, which is when it stops being valid.
#
# Works on ANY build, including a shipped release -- Developer Mode is a runtime
# setting, not a build flag.
#
# Turning it off:
#
#   ./scripts_local/wifi-flash.sh --disable
#
# NOT by flashing a release. The setting lives in /.crosspoint/settings.json on
# the SD CARD, and flashing replaces the firmware, not the card -- and every
# build carries the dev routes, which is the whole point of it being a runtime
# setting. A device flashed "back to a release" stays open.
#
# Two requests underneath: PUT /api/dev/upload streams the image to the card,
# POST /api/dev/flash validates and installs it through the same firmware_flash
# path the SD-card and over-the-air updates use.
set -uo pipefail
source "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/lib-sim.sh"
require_same_tree

ENV_NAME_FW="x4pro"
IP=""
DO_BUILD=0
PAIR_CODE=""
DO_DISABLE=0
TOKEN_FILE="$HOME/.crossplay-devtoken"
CODE_FILE="$HOME/.crossplay-devcode"
while [ $# -gt 0 ]; do
  case "$1" in
    --env) ENV_NAME_FW="${2:?--env needs a value}"; shift 2 ;;
    --ip)  IP="${2:?--ip needs a value}"; shift 2 ;;
    --build) DO_BUILD=1; shift ;;
    --pair) PAIR_CODE="${2:?--pair needs the six digits shown on the device}"; shift 2 ;;
    --disable) DO_DISABLE=1; shift ;;
    -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
    *) echo "error: unknown option: $1" >&2; exit 2 ;;
  esac
done

# A RELEASE image is a legitimate thing to send, and it is how you turn dev mode
# off. The dev route has to exist on the DEVICE doing the flashing, not in the
# image being flashed -- an earlier version of this script conflated the two and
# refused exactly the flash you most want: putting a unit back to a clean
# release without a cable. It is one-way, so it says so out loud.
FAREWELL=0
case "$ENV_NAME_FW" in
  x4pro|sticky) ;;
  gh_release_x4pro|gh_release_sticky)
    FAREWELL=1 ;;
  *) echo "error: --env must be x4pro, sticky, gh_release_x4pro or gh_release_sticky" >&2
     echo "       (got '$ENV_NAME_FW')" >&2
     exit 2 ;;
esac

FW="$REPO/.pio/build/$ENV_NAME_FW/firmware.bin"

if [ "$DO_BUILD" -eq 1 ]; then
  echo "building $ENV_NAME_FW ..."
  # Same lock every other build in this workspace takes: concurrent device
  # builds race the shared ~/.platformio and fail inside the espressif32
  # builder with an error that names no file of ours.
  ( cd "$REPO" && pio run -e "$ENV_NAME_FW" ) || { echo "error: build failed" >&2; exit 1; }
fi

# --disable needs no image at all, so skip the build and the validation.
if [ "$DO_DISABLE" -eq 1 ]; then
  FW=""
  SIZE=0
  DO_BUILD=0
fi

# -- the image ---------------------------------------------------------------
[ "$DO_DISABLE" -eq 1 ] || [ -f "$FW" ] || {
  echo "error: no build at $FW" >&2
  echo "       run with --build, or: cd $REPO && pio run -e $ENV_NAME_FW" >&2
  exit 1
}

# An ESP32 app image starts 0xE9. Catching a truncated or wrong-kind file here
# costs nothing; catching it after a 6MB upload costs a minute of Wi-Fi.
if [ "$DO_DISABLE" -eq 0 ]; then
MAGIC="$(head -c1 "$FW" | xxd -p)"
[ "$MAGIC" = "e9" ] || {
  echo "error: $FW does not start with 0xE9, so it is not an app image (got 0x$MAGIC)." >&2
  echo "       A -full.bin starts with the bootloader and must never be sent to the updater." >&2
  exit 1
}
SIZE="$(wc -c < "$FW" | tr -d ' ')"
echo "image: $FW ($SIZE bytes, $ENV_NAME_FW, built $(date -r "$FW" '+%H:%M:%S'))"
fi

# -- find the device ---------------------------------------------------------
# The web server answers a UDP "hello" on 8134 with its name; that reply's
# source address is the only thing we need. Broadcast rather than mDNS because
# the firmware answers this unconditionally while its mDNS name varies.
if [ -z "$IP" ]; then
  echo "discovering (UDP 8134, 3s) ..."
  IP="$(python3 - <<'PY'
import socket, time
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
s.settimeout(0.4)
found = {}
deadline = time.time() + 3
while time.time() < deadline:
    try:
        s.sendto(b"hello", ("255.255.255.255", 8134))
    except OSError:
        pass
    try:
        while True:
            data, addr = s.recvfrom(256)
            if b"crosspoint" in data.lower():
                found[addr[0]] = data.decode("utf-8", "replace").strip()
    except (socket.timeout, OSError):
        pass
for ip, name in found.items():
    print(f"{ip}\t{name}")
PY
)"
  COUNT="$(printf '%s' "$IP" | grep -c . || true)"
  if [ "$COUNT" -eq 0 ]; then
    echo "error: found no device." >&2
    echo "       In order of likelihood:" >&2
    echo "         - it is ASLEEP. Deep sleep is a chip reset, so the device" >&2
    echo "           leaves the network entirely and cannot be woken remotely." >&2
    echo "           Press a button on it." >&2
    echo "         - Developer Mode is off (Settings > System > Developer Mode)." >&2
    echo "         - it has never joined a network, so there is nothing to rejoin." >&2
    echo "       Or pass --ip <addr> directly." >&2
    exit 1
  fi
  if [ "$COUNT" -gt 1 ]; then
    echo "error: found $COUNT devices; name the one you mean with --ip:" >&2
    printf '%s\n' "$IP" | sed 's/^/       /' >&2
    exit 1
  fi
  echo "found: $IP"
  IP="$(printf '%s' "$IP" | cut -f1)"
fi

# -- who is it? --------------------------------------------------------------
# Print the device's own identity before writing to it. The desk units' USB port
# names swap across sleep/wake and have already caused one session to overwrite
# another's build; over Wi-Fi the equivalent mistake is flashing the wrong unit,
# so say out loud which one is about to be replaced.
STATUS="$(curl -fsS --max-time 5 "http://$IP/api/status" 2>/dev/null)" || {
  echo "error: no CrossPlay web server answering at http://$IP/" >&2
  exit 1
}
describe() {  # reads an /api/status body on stdin, prints one human line
  python3 -c 'import json,sys
try:
    d = json.load(sys.stdin)
except Exception:
    sys.exit(1)
print("{} v{} at {}, up {}s, heap {}".format(
    d.get("device","?"), d.get("version","?"), d.get("ip","?"),
    d.get("uptime","?"), d.get("freeHeap","?")))'
}
uptime_of() {  # reads an /api/status body on stdin, prints uptime seconds
  python3 -c 'import json,sys
try:
    print(int(json.load(sys.stdin).get("uptime", 99999)))
except Exception:
    print(99999)'
}

echo "device: $(printf '%s' "$STATUS" | describe || printf '%s' "$STATUS")"

UPTIME_BEFORE="$(printf '%s' "$STATUS" | uptime_of)"

# -- pair -------------------------------------------------------------------
# The token lives only in the device's RAM, so it dies with every reboot --
# including the one this script causes. Cache it anyway: it survives the many
# runs between reboots, and a stale one costs a single 401 and a clear message
# rather than a wrong-looking failure.
if [ -n "$PAIR_CODE" ]; then
  # Body and status from ONE request. An earlier version asked again just to
  # read the status code, which on a genuine typo spent a SECOND wrong guess and
  # doubled the backoff the user was about to be told to wait out.
  PAIR_RAW="$(curl -s --max-time 10 -w '\n%{http_code}' -X POST "http://$IP/api/dev/pair" \
    --data-urlencode "code=$PAIR_CODE" 2>/dev/null || true)"
  PAIR_STATUS="$(printf '%s' "$PAIR_RAW" | tail -n1)"
  TOKEN="$(printf '%s' "$PAIR_RAW" | sed '$d' | python3 -c 'import json,sys
try:
    print(json.load(sys.stdin)["token"])
except Exception:
    pass')"
  if [ -z "$TOKEN" ]; then
    # 429 means the code was never looked at: a run of wrong guesses closed the
    # window. Saying "check the six digits" there sends you to re-read a code
    # that is already correct.
    if [ "$PAIR_STATUS" = "429" ]; then
      echo "error: pairing is rate-limited right now, and the code you have is fine." >&2
      echo "       Wait up to 30s and run the same command again." >&2
    else
      echo "error: pairing refused. Check the six digits on the device screen." >&2
      echo "       They change every time Developer Mode is switched on." >&2
    fi
    exit 1
  fi
  printf '%s' "$TOKEN" > "$TOKEN_FILE"
  chmod 600 "$TOKEN_FILE"
  # Cache the CODE as well as the token. The token dies with every reboot, and
  # every flash causes one -- so without this each flash ended by sending you to
  # read a new code off the device panel, which is most of "no cable" handed
  # back. The device keeps its code across a reboot for the same reason.
  printf '%s' "$PAIR_CODE" > "$CODE_FILE"
  chmod 600 "$CODE_FILE"
  echo "paired; token cached in $TOKEN_FILE"
else
  TOKEN="$(cat "$TOKEN_FILE" 2>/dev/null || true)"
  if [ -z "$TOKEN" ]; then
    echo "error: not paired with this device." >&2
    echo "       On the device: Settings > System > Developer Mode." >&2
    echo "       Then: $0 --pair <the six digits it shows>" >&2
    exit 1
  fi
fi

# Fail early on a dead token rather than after a 6MB upload.
PROBE="$(curl -s -o /dev/null -w '%{http_code}' --max-time 8 \
  -H "X-Dev-Token: $TOKEN" "http://$IP/api/dev/log")"
if [ "$PROBE" = "401" ]; then
  # Expected after any reboot. Re-pair silently with the cached code before
  # bothering the user: the device kept the same code across the reset.
  CACHED_CODE="$(cat "$CODE_FILE" 2>/dev/null || true)"
  TOKEN=""
  if [ -n "$CACHED_CODE" ]; then
    TOKEN="$(curl -s --max-time 10 -X POST "http://$IP/api/dev/pair" \
      --data-urlencode "code=$CACHED_CODE" | python3 -c 'import json,sys
try:
    print(json.load(sys.stdin)["token"])
except Exception:
    pass')"
  fi
  if [ -n "$TOKEN" ]; then
    printf '%s' "$TOKEN" > "$TOKEN_FILE"
    chmod 600 "$TOKEN_FILE"
    echo "token had expired; re-paired automatically"
  else
    echo "error: the cached token is no longer valid and the cached code did not work." >&2
    echo "       The code changes when Developer Mode is switched off and on again," >&2
    echo "       and after five wrong guesses. Read the current one off the device:" >&2
    echo "       $0 --pair <the six digits on the device>" >&2
    exit 1
  fi
elif [ "$PROBE" = "404" ]; then
  echo "error: Developer Mode is off on this device." >&2
  echo "       Turn it on: Settings > System > Developer Mode." >&2
  exit 1
elif [ "$PROBE" != "200" ]; then
  echo "error: unexpected reply from the device (HTTP $PROBE)" >&2
  exit 1
fi

if [ "$FAREWELL" -eq 1 ]; then
  echo
  echo "NOTE: '$ENV_NAME_FW' is a RELEASE image. It does NOT turn Developer Mode"
  echo "      off. The setting lives on the SD card, not in the firmware, and"
  echo "      every build carries the dev routes -- so this device will still"
  echo "      join Wi-Fi and still answer /api/dev/flash afterwards."
  echo "      To actually close it:  $0 --disable"
  echo
fi

if [ "$DO_DISABLE" -eq 1 ]; then
  OUT="$(curl -s -w '\n%{http_code}' --max-time 15 -X POST "http://$IP/api/dev/disable" \
    -H "X-Dev-Token: $TOKEN")"
  if [ "$(printf '%s' "$OUT" | tail -1)" = "200" ]; then
    echo "Developer Mode is now OFF on this device."
    echo "It has left the network. Turn it back on at the device if you need it again."
    rm -f "$TOKEN_FILE"
    exit 0
  fi
  echo "error: could not switch Developer Mode off: $(printf '%s' "$OUT" | sed '$d')" >&2
  exit 1
fi

# -- upload ------------------------------------------------------------------
echo "uploading $SIZE bytes ..."
# PUT, not POST, and raw rather than multipart. This core hands one callback to
# both the upload and raw paths with no way to tell them apart, and taking the
# upload path here dereferences null and resets the device; PUT makes that path
# structurally unreachable. See docs/developer-mode.md.
curl -fsS --max-time 600 -X PUT "http://$IP/api/dev/upload" \
  -H "X-Dev-Token: $TOKEN" -H "Content-Type: application/octet-stream" \
  --data-binary "@$FW" >/dev/null || { echo "error: upload failed" >&2; exit 1; }
echo "uploaded."

# -- flash -------------------------------------------------------------------
# The device validates magic, segments, checksum, SHA, chip id and board tag
# before it writes anything, and only switches otadata at the very end, so a
# failure here leaves the running firmware untouched.
echo "flashing (about a minute; the device's UI is blocked meanwhile) ..."
RESULT="$(curl -sS --max-time 600 -o /dev/stdout -w '\n%{http_code}' \
  -X POST "http://$IP/api/dev/flash" -H "X-Dev-Token: $TOKEN" 2>&1)"
CODE="$(printf '%s' "$RESULT" | tail -1)"
BODY="$(printf '%s' "$RESULT" | sed '$d')"

case "$CODE" in
  200) echo "device says: $BODY" ;;
  404) echo "error: Developer Mode turned off mid-flash." >&2
       exit 1 ;;
  401) echo "error: token rejected mid-flash; the device probably rebooted." >&2
       echo "       Re-pair: $0 --pair <the six digits on the device>" >&2
       exit 1 ;;
  422) echo "error: the device rejected the image: $BODY" >&2
       echo "       TOO_LARGE means its partition table predates the repartition;" >&2
       echo "       WRONG_BOARD means this is the other device's image;" >&2
       echo "       OPEN_FAIL means the upload did not land." >&2
       exit 1 ;;
  *)   echo "error: flash failed (HTTP $CODE): $BODY" >&2; exit 1 ;;
esac

# -- wait for it to come back ------------------------------------------------
# Proof, not optimism: the flash reported success, but only a reboot into the
# new image proves it. uptime going backwards is the signal.
echo -n "waiting for reboot "
for _ in $(seq 1 60); do
  sleep 2
  echo -n "."
  NEW="$(curl -fsS --max-time 3 "http://$IP/api/status" 2>/dev/null)" || continue
  UP="$(printf '%s' "$NEW" | uptime_of)"
  # A reboot is uptime going BACKWARDS. Comparing against a fixed ceiling would
  # also match the device we never knocked over, so remember what it was.
  [ "$UP" -lt "$UPTIME_BEFORE" ] || continue
  echo
  echo "back up: $(printf '%s' "$NEW" | describe || printf '%s' "$NEW")"
  if [ "$FAREWELL" -eq 1 ]; then
    echo "Developer Mode is still ON -- the setting is on the card, not in the image."
  fi
  exit 0
done
echo
echo "warning: the device did not answer within two minutes." >&2
echo "         The flash reported success, so the image is almost certainly on it;" >&2
echo "         what is unproven is that it booted. Likely causes: it rejoined a" >&2
echo "         different network, the AP was slow enough to miss the window, or it" >&2
echo "         went to sleep. Look at the device before assuming the flash failed." >&2
exit 1

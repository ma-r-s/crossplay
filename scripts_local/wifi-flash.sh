#!/bin/bash
# Flash this tree's build to a desk device over Wi-Fi, with no cable.
#
#   ./scripts_local/wifi-flash.sh                 # x4pro build, discover the device
#   ./scripts_local/wifi-flash.sh --env sticky
#   ./scripts_local/wifi-flash.sh --ip 192.168.1.42
#   ./scripts_local/wifi-flash.sh --build         # build first, then flash
#
# Requires a DEV build already on the device: everything this drives exists only
# under -DCROSSPOINT_DEV_WIFI_FLASH, set in [env:x4pro] and [env:sticky] and
# never in a gh_release env. A device running a release neither joins a network
# by itself nor answers this route -- that is the design, not a bug. The first
# flash onto a release device is still USB; every one after it is wireless.
#
# A dev build joins the last-connected network at boot and keeps its web server
# up, so after the first setup there is nothing to press. A sleeping device is
# not reachable until something wakes it.
#

# Boot-time auto-start is not in this version; see docs/wireless-flashing.md.
#
# Two ordinary requests, no custom protocol: POST /upload puts firmware.bin on
# the SD card using the same route that uploads books, then POST /api/dev/flash
# validates and installs it through the same firmware_flash path the SD-card and
# OTA updates use.
set -uo pipefail
source "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/lib-sim.sh"
require_same_tree

ENV_NAME_FW="x4pro"
IP=""
DO_BUILD=0
while [ $# -gt 0 ]; do
  case "$1" in
    --env) ENV_NAME_FW="${2:?--env needs a value}"; shift 2 ;;
    --ip)  IP="${2:?--ip needs a value}"; shift 2 ;;
    --build) DO_BUILD=1; shift ;;
    -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
    *) echo "error: unknown option: $1" >&2; exit 2 ;;
  esac
done

case "$ENV_NAME_FW" in
  x4pro|sticky) ;;
  *) echo "error: --env must be x4pro or sticky (got '$ENV_NAME_FW')" >&2
     echo "       release envs have no dev flash route by construction." >&2
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

# -- the image ---------------------------------------------------------------
[ -f "$FW" ] || {
  echo "error: no build at $FW" >&2
  echo "       run with --build, or: cd $REPO && pio run -e $ENV_NAME_FW" >&2
  exit 1
}

# An ESP32 app image starts 0xE9. Catching a truncated or wrong-kind file here
# costs nothing; catching it after a 6MB upload costs a minute of Wi-Fi.
MAGIC="$(head -c1 "$FW" | xxd -p)"
[ "$MAGIC" = "e9" ] || {
  echo "error: $FW does not start with 0xE9, so it is not an app image (got 0x$MAGIC)." >&2
  echo "       A -full.bin starts with the bootloader and must never be sent to the updater." >&2
  exit 1
}
SIZE="$(wc -c < "$FW" | tr -d ' ')"
echo "image: $FW ($SIZE bytes, $ENV_NAME_FW, built $(date -r "$FW" '+%H:%M:%S'))"

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
    echo "       A dev build joins the last-connected network at boot and keeps" >&2
    echo "       the server up, so the usual causes are: the device is asleep" >&2
    echo "       (wake it), it has never been given a network (pick one once in" >&2
    echo "       Settings -> Network), or it is running a release build." >&2
    echo "       You can also pass --ip <addr> directly." >&2
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

# -- upload ------------------------------------------------------------------
echo "uploading $SIZE bytes ..."
curl -fsS --max-time 300 -F "file=@$FW;filename=firmware.bin" "http://$IP/upload?path=/" >/dev/null || {
  echo "error: upload failed" >&2; exit 1; }
echo "uploaded."

# -- flash -------------------------------------------------------------------
# The device validates magic, segments, checksum, SHA, chip id and board tag
# before it writes anything, and only switches otadata at the very end, so a
# failure here leaves the running firmware untouched.
echo "flashing (about a minute; the device's UI is blocked meanwhile) ..."
RESULT="$(curl -sS --max-time 300 -o /dev/stdout -w '\n%{http_code}' \
  -X POST "http://$IP/api/dev/flash" --data-urlencode "path=/firmware.bin" 2>&1)"
CODE="$(printf '%s' "$RESULT" | tail -1)"
BODY="$(printf '%s' "$RESULT" | sed '$d')"

case "$CODE" in
  200) echo "device says: $BODY" ;;
  404) echo "error: no /api/dev/flash on this device." >&2
       echo "       It is running a release build. The route only exists under" >&2
       echo "       -DCROSSPOINT_DEV_WIFI_FLASH, which release envs never set." >&2
       echo "       Flash a dev build over USB once, then this works." >&2
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
  exit 0
done
echo
echo "warning: the device did not answer within two minutes." >&2
echo "         The flash reported success, so the image is almost certainly on it;" >&2
echo "         what is unproven is that it booted. Likely causes: it rejoined a" >&2
echo "         different network, the AP was slow enough to miss the window, or it" >&2
echo "         went to sleep. Look at the device before assuming the flash failed." >&2
exit 1

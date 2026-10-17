#!/bin/bash
# Flash this tree's build to a device over the USB CABLE. No pairing code, no
# Developer Mode, no button on the device, nothing for a person to read off a
# screen.
#
#   ./scripts_local/usb-flash.sh                      # the only device on USB
#   ./scripts_local/usb-flash.sh --mac B8:1F:3F:D4:83:24
#   ./scripts_local/usb-flash.sh --list               # what is plugged in
#   ./scripts_local/usb-flash.sh --env sticky --build
#
# WHY THIS EXISTS. Every flash path in this tree went over Wi-Fi, and Wi-Fi
# needs Developer Mode on and a six-digit code that REGENERATES every time the
# toggle is switched or the device restarts. So a device sitting on a cable,
# fully under this machine's control, still could not be flashed without asking
# Mario to walk over, find it, read six digits and type them into a chat. He
# has now been asked three times. Over the cable none of that exists: esptool
# talks to the bootloader directly, and the bootloader does not care what the
# firmware thinks about Developer Mode.
#
# IDENTIFY BY MAC, NEVER BY PORT NAME. /dev/cu.usbmodem* numbers track USB port
# POSITION and they swap across sleep/wake -- this workspace has already had one
# session overwrite another's build that way. The MAC is burned into the chip
# and is what `--mac` matches; the port is only ever looked up from it, at the
# moment of writing.
set -uo pipefail
source "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/lib-sim.sh"
require_same_tree

ENV_NAME_FW="x4pro"
WANT_MAC=""
DO_BUILD=0
DO_LIST=0
while [ $# -gt 0 ]; do
  case "$1" in
    --env) ENV_NAME_FW="${2:?--env needs a name}"; shift 2 ;;
    --mac) WANT_MAC="${2:?--mac needs an address}"; shift 2 ;;
    --build) DO_BUILD=1; shift ;;
    --list) DO_LIST=1; shift ;;
    -h|--help) sed -n '2,30p' "$0"; exit 0 ;;
    *) echo "usb-flash: unknown flag $1" >&2; exit 2 ;;
  esac
done

# macOS maps a USB serial device to a port name; this is the only place the two
# are connected, and it is read fresh every run.
inventory() {
  ioreg -r -c IOUSBHostDevice -l 2>/dev/null |
    awk '/USB Serial Number/{gsub(/"/,"",$NF); s=$NF} /IODialinDevice/{gsub(/"/,"",$NF); print s "\t" $NF}' |
    grep -E 'usbmodem' || true
}

INV="$(inventory)"
if [ -z "$INV" ]; then
  echo "error: no device on USB." >&2
  echo "       Nothing is plugged in, or the cable is charge-only. A charge-only" >&2
  echo "       cable enumerates no serial port at all, which looks exactly like" >&2
  echo "       an unplugged device." >&2
  exit 1
fi

if [ "$DO_LIST" = "1" ]; then
  echo "on USB now:"
  printf '%s\n' "$INV" | while IFS=$'\t' read -r mac port; do
    echo "  $mac  $port"
  done
  exit 0
fi

COUNT="$(printf '%s\n' "$INV" | wc -l | tr -d ' ')"
if [ -n "$WANT_MAC" ]; then
  # Case-insensitive: ioreg prints upper, people type lower.
  LINE="$(printf '%s\n' "$INV" | awk -v m="$(printf '%s' "$WANT_MAC" | tr 'a-f' 'A-F')" 'toupper($1)==m')"
  if [ -z "$LINE" ]; then
    echo "error: no device with MAC $WANT_MAC is on USB. Plugged in now:" >&2
    printf '%s\n' "$INV" | while IFS=$'\t' read -r mac port; do echo "  $mac  $port" >&2; done
    exit 1
  fi
elif [ "$COUNT" -gt 1 ]; then
  # Two units on one desk is the normal case here, and picking for the caller is
  # how the wrong one gets overwritten.
  echo "error: $COUNT devices on USB; name the one you mean with --mac:" >&2
  printf '%s\n' "$INV" | while IFS=$'\t' read -r mac port; do echo "  $mac  $port" >&2; done
  exit 1
else
  LINE="$INV"
fi

MAC="$(printf '%s' "$LINE" | cut -f1)"
PORT="$(printf '%s' "$LINE" | cut -f2 | sed 's|/dev/tty|/dev/cu|')"

# Both the build and the upload run UNDER lib-sim.sh's build lock: PlatformIO
# does not tolerate two concurrent runs of one env, and an upload is a pio run
# like any other.
locked_pio() {
  local waited=0
  while ! mkdir "$BUILD_LOCK" 2>/dev/null; do
    sleep 1
    waited=$((waited + 1))
    if [ "$waited" -gt 300 ]; then
      echo "build lock held for 5 minutes; removing stale $BUILD_LOCK" >&2
      rm -rf "$BUILD_LOCK"
    fi
  done
  ( cd "$REPO" && pio run -e "$ENV_NAME_FW" "$@" )
  local status=$?
  rmdir "$BUILD_LOCK" 2>/dev/null
  return $status
}

if [ "$DO_BUILD" = "1" ]; then
  echo "building $ENV_NAME_FW ..."
  # KEPT, not discarded. This used to be >/dev/null, so a compile error printed
  # "build failed" and nothing else, and finding out WHICH line meant running
  # the build again by hand through another script.
  FLASH_LOG="${TMPDIR:-/tmp}/xteink-usbflash-$ENV_NAME_FW.log"
  if ! locked_pio >"$FLASH_LOG" 2>&1; then
    echo "build failed; the last errors follow, and the whole log is $FLASH_LOG" >&2
    grep -E "error:|Error " "$FLASH_LOG" | head -20 >&2
    exit 1
  fi
fi

IMAGE="$REPO/.pio/build/$ENV_NAME_FW/firmware.bin"
[ -f "$IMAGE" ] || { echo "error: no build at $IMAGE -- run with --build" >&2; exit 1; }

echo "device: $MAC on $PORT"
echo "image:  $IMAGE ($(wc -c < "$IMAGE" | tr -d ' ') bytes, $ENV_NAME_FW)"
# ESPTOOL DIRECTLY, NOT `pio run -t upload`. Two reasons, both learned the hard
# way here:
#
#   * RESET MODE IS THE WHOLE TRICK ON THIS CHIP. The X4 Pro is an ESP32-S3
#     whose serial port IS its own native USB, so esptool's default DTR/RTS
#     wiggle reaches nothing and it gives up with "No serial data received",
#     which reads exactly like an unplugged cable. `--before usb_reset` uses the
#     USB control transfer the S3's ROM listens for and needs nobody to hold
#     BOOT.
#   * PlatformIO SILENTLY IGNORED the flag. PLATFORMIO_UPLOAD_FLAGS="--before
#     usb_reset" produced an upload whose output never contains the string
#     usb_reset at all, so the setting looked applied and was not.
#
# The app partition AND boot_app0 at 0xe000. Writing app0 alone is not enough and fails
# SILENTLY in the worst way: every previous flash on these units went over the
# air, and an OTA writes the INACTIVE slot and then points otadata at it. A
# device that last took an OTA is therefore running app1, so an esptool write to
# app0 lands perfectly, verifies its hash, reboots -- and comes back running the
# OLD firmware out of the other slot. It cost an hour here: the app was missing
# from the shelf, from the chooser, from everything, while the image on disk
# plainly contained it, and the version string was the old build's all along.
# Erasing otadata makes the bootloader fall back to app0, which is what was just
# written.
ESPTOOL="$HOME/.platformio/packages/tool-esptoolpy/esptool.py"
PIO_PY="$HOME/.platformio/penv/bin/python"
[ -f "$ESPTOOL" ] || { echo "error: no esptool at $ESPTOOL" >&2; exit 1; }
echo "writing over the cable (no pairing code, no Developer Mode) ..."
# ONE esptool invocation, not two. A first run that leaves the chip in the
# bootloader (--after no_reset) makes the second run's --before usb_reset fail:
# the ROM is already there and does not answer a reset request the same way.
# So otadata is written as blank bytes in the same write_flash as the app.
OTABLANK="${TMPDIR:-/tmp}/xteink-otadata-blank.bin"
python3 -c "import sys; open(sys.argv[1],'wb').write(b'\xff' * 0x2000)" "$OTABLANK"
if "$PIO_PY" "$ESPTOOL" --chip esp32s3 --port "$PORT" --baud 921600 \
     --before usb_reset --after hard_reset write_flash 0xe000 "$OTABLANK" 0x10000 "$IMAGE"; then
  echo "flashed $MAC"
else
  echo "error: the write failed. The device holds its previous firmware." >&2
  echo "       \"No serial data received\" with the cable plugged in means the" >&2
  echo "       chip did not enter its bootloader. A device in a multiplayer" >&2
  echo "       match holds the radio and the port; otherwise try again, and" >&2
  echo "       ./scripts_local/usb-flash.sh --list shows what is really there." >&2
  exit 1
fi

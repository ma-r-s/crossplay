#!/bin/bash
# usb-flash.sh never writes a BOOX Picco, and its Picco check does not cost the
# other boards their flash.
#
# The script writes otadata at 0xe000 and the app at 0x10000: this fork's
# partition table. On a Picco, 0xe000 is inside the vendor's NVS and 0x10000
# runs over its otadata and its `safeboot` recovery partition, so it reads the
# device's table first and refuses one that names safeboot. That read is a
# second esptool session, and it has to reach the chip the way the write does:
# the S3's own USB answers `--before usb_reset`, a USB-UART bridge (the
# Sticky's CH343) ignores it and needs `default_reset` at 460800. #320's first
# version read every board with usb_reset, which on a Sticky fails the read
# and refuses every flash with "could not read the device's partition table".
#
# Driven end to end against stand-ins: ioreg lists one device, and the
# esptool interpreter records every call and answers read_flash with a table
# that does or does not name safeboot. Nothing here opens a port.
#
#   host-tests/usbflash/run.sh
#   USBFLASH_UNDER_TEST=/path/to/usb-flash.sh host-tests/usbflash/run.sh
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SCRIPT="${USBFLASH_UNDER_TEST:-$ROOT/scripts_local/usb-flash.sh}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/usbflash-test.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

checks=0
failed=0
ok()  { checks=$((checks + 1)); }
bad() { checks=$((checks + 1)); failed=$((failed + 1)); echo "FAIL usbflash  $1"; }

# A tree of its own, so lib-sim.sh's require_same_tree and REPO resolve here.
REPO="$WORK/repo"
mkdir -p "$REPO/scripts_local" "$REPO/.pio/build/x4pro" "$WORK/bin" "$WORK/dev" \
         "$WORK/home/.platformio/packages/tool-esptoolpy" "$WORK/home/.platformio/penv/bin"
cp "$SCRIPT" "$REPO/scripts_local/usb-flash.sh"
cp "$ROOT/scripts_local/lib-sim.sh" "$REPO/scripts_local/lib-sim.sh"
printf '\xe9fake-app' > "$REPO/.pio/build/x4pro/firmware.bin"
: > "$WORK/home/.platformio/packages/tool-esptoolpy/esptool.py"

cat > "$WORK/bin/ioreg" <<'IOREG'
#!/bin/bash
printf '    "USB Serial Number" = "%s"\n' "$FAKE_SERIAL"
printf '    "IODialinDevice" = "%s"\n' "$FAKE_PORT"
IOREG
cat > "$WORK/home/.platformio/penv/bin/python" <<'PY'
#!/bin/bash
# argv: esptool.py --chip ... <command> <args>
shift
echo "$*" >> "$FAKE_CALLS"
case " $* " in
  *" read_flash "*)
    [ "${FAKE_READ_FAILS:-0}" = 1 ] && exit 1
    out="${@: -1}"
    if [ "$FAKE_TABLE" = picco ]; then
      printf '\xaa\x50\x01\x02nvs\0safeboot\0app\0' > "$out"
    else
      printf '\xaa\x50\x01\x02nvs\0app0\0app1\0' > "$out"
    fi ;;
esac
exit 0
PY
chmod +x "$WORK/bin/ioreg" "$WORK/home/.platformio/penv/bin/python"

# run <serial> <table> [usb-flash args...]: sets RC and CALLS.
run() {
  local serial="$1" table="$2"; shift 2
  : > "$WORK/calls"
  local port="$WORK/dev/cu.usbmodem$RANDOM"
  : > "$port"
  ( cd "$REPO" && \
    PATH="$WORK/bin:$PATH" HOME="$WORK/home" TMPDIR="$WORK" \
    FAKE_SERIAL="$serial" FAKE_PORT="$port" FAKE_TABLE="$table" FAKE_CALLS="$WORK/calls" \
    FAKE_READ_FAILS="${READ_FAILS:-0}" \
    bash "$REPO/scripts_local/usb-flash.sh" "$@" > "$WORK/out" 2>&1 )
  RC=$?
  CALLS="$(cat "$WORK/calls")"
}

MAC="B8:1F:3F:D4:83:24"   # an S3 on its own USB reports its MAC as the serial
BRIDGE="5A7B012345"       # a CH343 bridge reports its own serial

# 1. An X4 Pro: the table is read and the write happens, both over usb_reset.
run "$MAC" stock
if [ "$RC" = 0 ] && printf '%s\n' "$CALLS" | grep -q 'usb_reset .*read_flash 0x8000' \
   && printf '%s\n' "$CALLS" | grep -q 'usb_reset .*write_flash 0xe000 .* 0x10000 '; then ok
else bad "an X4 Pro should be read and then written over usb_reset (rc=$RC): $CALLS $(cat "$WORK/out")"; fi

# 2. A Sticky: the READ needs the bridge's reset and baud as much as the write.
run "$BRIDGE" stock
if [ "$RC" = 0 ] && printf '%s\n' "$CALLS" | grep -q -- '--baud 460800 --before default_reset .*read_flash' \
   && printf '%s\n' "$CALLS" | grep -q -- '--before default_reset .*write_flash'; then ok
else bad "a Sticky's table read must use default_reset at 460800 like its write, or every Sticky flash is refused (rc=$RC): $CALLS"; fi

# 3. A Picco: refused after the read, and nothing is written.
run "$MAC" picco
if [ "$RC" = 2 ] && ! printf '%s\n' "$CALLS" | grep -q write_flash; then ok
else bad "a device whose table names safeboot must be refused with nothing written (rc=$RC): $CALLS"; fi
if grep -q "safeboot" "$WORK/out"; then ok; else bad "the Picco refusal should say why: $(cat "$WORK/out")"; fi

# 4. A Picco build is refused before the cable is touched at all.
run "$MAC" stock --env gh_release_picco
if [ "$RC" = 2 ] && [ -z "$CALLS" ]; then ok
else bad "--env gh_release_picco should be refused before any esptool run (rc=$RC): $CALLS"; fi

# 5. A table that cannot be read is not a table without safeboot.
READ_FAILS=1 run "$MAC" stock
if [ "$RC" != 0 ] && ! printf '%s\n' "$CALLS" | grep -q write_flash; then ok
else bad "a failed table read must stop the flash (rc=$RC): $CALLS"; fi

echo "usbflash: $((checks - failed))/$checks passed"
[ "$failed" -eq 0 ]

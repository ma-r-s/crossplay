# Refuses any PlatformIO upload of a Picco env that would write a byte outside
# the app slot. Runs AFTER upstream's scripts/app_only_upload.py, which does the
# work; this only checks it.
#
# The BOOX Picco keeps its maker's partition table: one `app` (ota_0) slot at
# 0xE0000 beside a `safeboot` recovery partition, with the vendor's NVS and
# otadata below it (partitions_picco.csv). A default ESP32 upload writes the
# Arduino bootloader at 0x0, a partition table at 0x8000 and boot_app0 at
# 0xE000, and 0xE000 is inside that NVS. Upstream's script strips those three
# images (crosspoint-reader #4248, fixed in #4510 after its first version only
# cleared FLASH_EXTRA_IMAGES, too late to matter). The check below is the one
# Miguel Schulz's #320 added in his own copy of that script: read the command
# esptool will actually run, and refuse it if anything but the app is in it. A
# platform update that appends images some other way, or a sync that loses
# upstream's fix, then fails here loudly instead of writing over the vendor's
# partitions.
#
# Four other targets write flash without going through the upload command at
# all, and are refused outright for these envs:
#   erase, erase_upload  erase the WHOLE chip: bootloader, NVS and safeboot
#   uploadfs, uploadfsota  write a filesystem image into the `spiffs` partition
#
# host-tests/appslot runs upstream's script and this one against the flag
# layout platform-espressif32's builder/main.py produces.
import os

Import("env")  # noqa: F821  -- injected by SCons/PlatformIO

from SCons.Script import COMMAND_LINE_TARGETS  # noqa: E402

REFUSED_TARGETS = ("erase", "erase_upload", "uploadfs", "uploadfsota")
# The upload command platform-espressif32 builds for esptool. The app goes at
# $ESP32_APP_OFFSET, which the platform takes from the table's ota_0 row; every
# other image rides in $UPLOADERFLAGS as an "<offset> <path>" pair.
APP_WRITE = "$ESP32_APP_OFFSET $SOURCE"


def _refuse(why):
    raise SystemExit("app_slot_guard: refusing to write this board. " + why)


def _number(text):
    try:
        return int(str(text).strip(), 0)
    except ValueError:
        return None


def app_slots(csv_path):
    """The (offset, size) of every ota_N app partition in a partition CSV."""
    slots = []
    with open(csv_path) as fp:
        for line in fp:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            cols = [c.strip() for c in line.split(",")]
            if len(cols) < 5 or cols[1] != "app" or not cols[2].startswith("ota_"):
                continue
            offset, size = _number(cols[3]), _number(cols[4])
            if offset is not None and size is not None:
                slots.append((offset, size))
    return slots


def check_upload(source, target, env):  # noqa: ARG001 -- SCons action signature
    if APP_WRITE not in env.get("UPLOADCMD", ""):
        _refuse(
            "The upload is not the esptool write of $SOURCE at $ESP32_APP_OFFSET "
            "(UPLOADCMD is %r), so nothing here can say what it writes."
            % env.get("UPLOADCMD", "")
        )
    flags = [env.subst(str(f)) for f in env.get("UPLOADERFLAGS", [])]
    stray = [f for f in flags if f.lower().startswith("0x")]
    if stray:
        _refuse(
            "esptool would also write at "
            + ", ".join(stray)
            + ", outside the app slot. "
            "Nothing was written."
        )
    table = env.subst("$PARTITIONS_TABLE_CSV")
    slots = app_slots(table)
    if len(slots) != 1:
        _refuse(
            "%s has %d OTA app slots; this guard expects the vendor's one."
            % (table, len(slots))
        )
    slot_offset, slot_size = slots[0]
    app = _number(env.subst("$ESP32_APP_OFFSET"))
    if app != slot_offset:
        _refuse(
            "The app would go to %s, and the table's app slot starts at %s."
            % (env.subst("$ESP32_APP_OFFSET"), hex(slot_offset))
        )
    image = str(source[0])
    size = os.path.getsize(image) if os.path.isfile(image) else None
    if size is None:
        _refuse("There is no image at %s to measure." % image)
    if size > slot_size:
        _refuse("%s is %d bytes and the app slot holds %d." % (image, size, slot_size))
    print(
        "app_slot_guard: writing only %s at %s (%d of %d bytes in the slot)"
        % (os.path.basename(image), hex(slot_offset), size, slot_size)
    )


wanted = [t for t in COMMAND_LINE_TARGETS if t in REFUSED_TARGETS]
if wanted:
    _refuse(
        "`-t %s` writes outside the app slot at 0xE0000 (an erase takes the vendor's "
        "bootloader, NVS and safeboot with it; a filesystem upload writes the spiffs "
        "partition). Only `-t upload` is allowed for this env." % wanted[0]
    )

env.AddPreAction("upload", check_upload)  # noqa: F821

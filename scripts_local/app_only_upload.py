# Upload ONLY the application image, at the ota_0 offset the partition table
# gives (0xE0000 on the Picco's stock table). For boards that keep their vendor
# bootloader, partition table, otadata, NVS and recovery partition.
#
# Clearing FLASH_EXTRA_IMAGES alone is not enough, and upstream's
# scripts/app_only_upload.py does only that (crosspoint-reader issue #4248):
# the platform builder has already appended every extra image to UPLOADERFLAGS
# as `<offset> <path>` pairs by the time a post: script runs, so the upload
# still wrote the Arduino bootloader at 0x0, the partition table at 0x8000 and
# boot_app0.bin at 0xE000 -- inside the Picco's NVS.
#
# So the pairs are removed from UPLOADERFLAGS as well, and then the result is
# CHECKED: any flash offset left in the flags means an image other than the app
# would be written, and the upload is refused before esptool runs. A platform
# update that appends images some other way fails loudly here rather than
# quietly writing over the vendor's partitions.
import re

Import("env")  # noqa: F821  -- injected by SCons/PlatformIO

extra = {(str(offset), env.subst(path)) for offset, path in env.get("FLASH_EXTRA_IMAGES", [])}  # noqa: F821
flags = list(env.get("UPLOADERFLAGS", []))  # noqa: F821
kept = []
i = 0
while i < len(flags):
    if i + 1 < len(flags) and (str(flags[i]), env.subst(flags[i + 1])) in extra:  # noqa: F821
        i += 2
        continue
    kept.append(flags[i])
    i += 1

env.Replace(FLASH_EXTRA_IMAGES=[], UPLOADERFLAGS=kept)  # noqa: F821

_OFFSET = re.compile(r"^0x[0-9a-fA-F]+$")


def _refuse_extra_images(source, target, env):
    leftover = [str(f) for f in env.get("UPLOADERFLAGS", []) if _OFFSET.match(env.subst(str(f)))]
    if leftover:
        raise SystemExit(
            "app_only_upload: refusing to upload; the esptool command would also write at "
            + ", ".join(leftover)
            + ". Only the app at $ESP32_APP_OFFSET may be written on this board."
        )
    print("app_only_upload: writing only the app at " + env.subst("$ESP32_APP_OFFSET"))


env.AddPreAction("upload", _refuse_extra_images)  # noqa: F821

#!/usr/bin/env python3
"""The pack's order IS its only index, so this is the test that keeps it true.

wallpapers.dat is a bare concatenation of 48062-byte images. Nothing in it
carries a name: the device calls image i builtInStem(i). If sorted filename
order and that table ever diverge, every wallpaper gets someone else's caption,
the pack still unpacks, every file is still a valid BMP, and nothing anywhere
reports an error. There is no runtime check that could catch it.
"""
import re
import sys
from pathlib import Path

BYTES_PER_IMAGE = 48062

def main() -> int:
    root = Path(__file__).resolve().parents[2]
    starter = root / "site" / "wallpapers" / "starter"
    core = root / "src" / "apps_local" / "wallpapers" / "WallpapersCore.cpp"
    header = root / "src" / "apps_local" / "wallpapers" / "WallpapersCore.h"
    failures = []
    checks = 0

    table = core.read_text()
    block = re.search(r"constexpr Entry kBuiltIns\[\] = \{(.*?)\n\};", table, re.S)
    if not block:
        print("FAIL: could not find kBuiltIns in WallpapersCore.cpp")
        return 1
    stems = re.findall(r'\{"([^"]+)",', block.group(1))

    files = sorted(p.stem for p in starter.glob("*.bmp"))

    checks += 1
    if stems != files:
        only_table = [s for s in stems if s not in files]
        only_disk = [f for f in files if f not in stems]
        failures.append(
            "pack order and builtInStem() disagree.\n"
            f"    in the table but not on disk: {only_table}\n"
            f"    on disk but not in the table: {only_disk}\n"
            f"    first mismatch at index "
            f"{next((i for i, (a, b) in enumerate(zip(stems, files)) if a != b), 'n/a')}"
        )

    # The stride the whole format rests on.
    for p in sorted(starter.glob("*.bmp")):
        checks += 1
        n = p.stat().st_size
        if n != BYTES_PER_IMAGE:
            failures.append(f"{p.name} is {n} bytes, not {BYTES_PER_IMAGE} -- the pack stride would be wrong")

    # The constant the firmware sizes its download estimate from.
    checks += 1
    m = re.search(r"kBuiltInCount = (\d+);", header.read_text())
    if not m:
        failures.append("kBuiltInCount not found in WallpapersCore.h")
    elif int(m.group(1)) != len(files):
        failures.append(f"kBuiltInCount is {m.group(1)} but there are {len(files)} wallpapers on disk")

    checks += 1
    m = re.search(r"kWallpaperFileBytes = (\d+);", header.read_text())
    if not m or int(m.group(1)) != BYTES_PER_IMAGE:
        failures.append(f"kWallpaperFileBytes disagrees with the {BYTES_PER_IMAGE}-byte images on disk")

    print(f"wallpack: {checks} checks, {len(failures)} failed")
    for f in failures:
        print(f"  FAIL: {f}")
    return 1 if failures else 0

if __name__ == "__main__":
    sys.exit(main())

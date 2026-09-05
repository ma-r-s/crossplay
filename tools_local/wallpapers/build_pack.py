#!/usr/bin/env python3
"""Build wallpapers.dat: the built-in set as ONE GitHub release asset.

  tools_local/wallpapers/build_pack.py [out.dat]
    default: site/wallpapers/starter/wallpapers.dat

There is no format. Every device wallpaper is exactly 48062 bytes (480x800,
1-bit, uncompressed, 62-byte header), so the pack is a bare concatenation:
image i lives at i * 48062 and the count is the file size divided by it. That
is why the device unpacker is a fixed-stride read loop with no parser, and why
a corrupt pack is caught by one modulo rather than by a checksum nobody has.

THE ORDER IS THE CONTRACT. The device names image i with builtInStem(i) from
WallpapersCore.cpp; nothing in the pack carries a name. Sorted filename order
is that order, and host-tests/wallpack asserts the two still agree -- if they
ever diverge, every wallpaper gets the wrong caption and nothing crashes.
"""
import sys
from pathlib import Path

BYTES_PER_IMAGE = 48062

def main() -> int:
    root = Path(__file__).resolve().parents[2]
    starter = root / "site" / "wallpapers" / "starter"
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else starter / "wallpapers.dat"

    files = sorted(starter.glob("*.bmp"))
    if not files:
        print(f"no .bmp files in {starter}", file=sys.stderr)
        return 1

    blob = bytearray()
    for f in files:
        data = f.read_bytes()
        if len(data) != BYTES_PER_IMAGE:
            # Refuse rather than ship a pack whose stride is wrong: the device
            # would slice every image after this one at the wrong offset and
            # show 20 bands of noise, with no error anywhere.
            print(f"{f.name} is {len(data)} bytes, expected {BYTES_PER_IMAGE}", file=sys.stderr)
            return 1
        blob += data

    out.write_bytes(blob)
    print(f"wrote {out} -- {len(files)} wallpapers, {len(blob)} bytes")
    print("order (this must match builtInStem() in WallpapersCore.cpp):")
    for i, f in enumerate(files):
        print(f"  {i:2d}  {f.stem}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())

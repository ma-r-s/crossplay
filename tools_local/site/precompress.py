#!/usr/bin/env python3
"""Ship the site's big binaries already brotli-compressed.

Vercel compresses static files at the edge on every cache MISS, and for a
multi-megabyte binary that is catastrophically slow: measured on the live
site, crossplay.data took 34.3s compressed and 1.2s uncompressed, streaming
its brotli output at about 35 KB/s. Edge caches are per region, so the
person in Sao Paulo meets a cold one far more often than a check from
Washington does, and every deploy empties all of them. That is the whole
reason the emulator "used to load in under a second" and then did not.

Serving bytes that are ALREADY brotli, with Content-Encoding set by us in
vercel.json, makes Vercel pass them straight through: the same file went
from 34.3s to 0.99s, decoding byte-identically in the browser.

    python3 tools_local/site/precompress.py           # compress in place
    python3 tools_local/site/precompress.py --check   # verify, exit 1 if not

THE FOOTGUN, and why --check exists: these files are committed in their
compressed form. Anything that regenerates one (tools_local/wasm/build.py,
fetch_pyodide.py) leaves raw bytes behind a header promising brotli, and the
site serves garbage. Those scripts call this one at the end, and check.sh
runs --check, so a forgotten step is a failed check rather than a broken
page. Keep this list and the vercel.json headers in step.
"""

import pathlib
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
SITE = REPO / "site"

# Whole directories, so a new file cannot be missed by a list nobody updated.
# Every path served from these must carry Content-Encoding: br in vercel.json.
DIRS = ["emulator", "pyodide"]
# Individual files elsewhere, big enough to be worth it (> ~1MB).
FILES = ["study/NotoSansCJK.otf"]


def targets():
    for name in DIRS:
        base = SITE / name
        if base.is_dir():
            for path in sorted(base.rglob("*")):
                if path.is_file():
                    yield path
    for name in FILES:
        path = SITE / name
        if path.is_file():
            yield path


def is_brotli(data):
    """Brotli has no magic number, so the only honest test is to decode it."""
    try:
        import brotli
    except ImportError:
        sys.exit(
            "the brotli module is missing: uv pip install --python"
            " .venv-study/bin/python brotli"
        )
    try:
        brotli.decompress(data)
        return True
    except Exception:
        return False


def main():
    try:
        import brotli
    except ImportError:
        sys.exit(
            "the brotli module is missing: uv pip install --python"
            " .venv-study/bin/python brotli"
        )

    check = "--check" in sys.argv
    raw = []
    total_before = total_after = 0
    for path in targets():
        data = path.read_bytes()
        if is_brotli(data):
            total_before += len(brotli.decompress(data))
            total_after += len(data)
            continue
        if check:
            raw.append(path.relative_to(SITE))
            continue
        # quality 9: within a few percent of 11 on these files and many times
        # faster to produce, which matters because this runs in every build.
        packed = brotli.compress(data, quality=9)
        path.write_bytes(packed)
        total_before += len(data)
        total_after += len(packed)
        print(
            f"  {path.relative_to(SITE)}  {len(data) // 1024} -> {len(packed) // 1024} KB"
        )

    if check:
        if raw:
            print(
                "these files are served with Content-Encoding: br but are NOT brotli:"
            )
            for path in raw:
                print(f"  {path}")
            print("run: python3 tools_local/site/precompress.py")
            return 1
        print(f"all pre-compressed ({total_after // 1024} KB on the wire)")
        return 0

    saved = total_before - total_after
    print(
        f"\n{total_before // 1024} KB -> {total_after // 1024} KB"
        f" ({saved // 1024} KB less over the wire, and no edge compression)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())

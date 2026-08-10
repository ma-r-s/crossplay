#!/usr/bin/env python3
"""Build FreeType 2.13.2 + ftshim.c into site/study/ft.{js,wasm}.

Why this exact version: the CLI rasterises through freetype-py, whose wheel
bundles FreeType 2.13.2, and the acceptance test for the installer's font
step is byte-identical .cpfont output against the CLI. A different FreeType
means a different hinter, which means different bitmaps, silently. For the
same reason the module list below is INSTALL.ANY's full default set with
default options: that is what a stock build (and so the wheel) contains.

    source ../.emsdk/emsdk_env.sh   # or let this script find emcc itself
    python3 tools_local/wasm-ft/build.py

The FreeType source tarball is fetched once into .pio/build/ft-src (already
gitignored); the output artifacts are committed, like site/emulator/'s,
because Vercel has no Emscripten.
"""

import os
import pathlib
import subprocess
import sys
import tarfile
import urllib.request

FT_VERSION = "2.13.2"
FT_TAG = "VER-" + FT_VERSION.replace(".", "-")
FT_URL = f"https://github.com/freetype/freetype/archive/refs/tags/{FT_TAG}.tar.gz"

REPO = pathlib.Path(__file__).resolve().parents[2]
SRC_CACHE = REPO / ".pio" / "build" / "ft-src"
OBJ = REPO / ".pio" / "build" / "ft-wasm"
OUT = REPO / "site" / "study"
SHIM = pathlib.Path(__file__).resolve().parent / "ftshim.c"

# docs/INSTALL.ANY's list: the base pieces plus every default module. bzip2
# is the one omission -- it needs the system library, and stock builds
# without it also omit it (FT_CONFIG_OPTION_USE_BZIP2 is off by default).
FT_SOURCES = [
    "src/autofit/autofit.c",
    "src/base/ftbase.c",
    "src/base/ftbbox.c",
    "src/base/ftbdf.c",
    "src/base/ftbitmap.c",
    "src/base/ftcid.c",
    "src/base/ftfstype.c",
    "src/base/ftgasp.c",
    "src/base/ftglyph.c",
    "src/base/ftgxval.c",
    "src/base/ftinit.c",
    "src/base/ftmm.c",
    "src/base/ftotval.c",
    "src/base/ftpatent.c",
    "src/base/ftpfr.c",
    "src/base/ftstroke.c",
    "src/base/ftsynth.c",
    "src/base/fttype1.c",
    "src/base/ftwinfnt.c",
    "src/base/ftsystem.c",
    "src/base/ftdebug.c",
    "src/bdf/bdf.c",
    "src/cache/ftcache.c",
    "src/cff/cff.c",
    "src/cid/type1cid.c",
    "src/gzip/ftgzip.c",
    "src/lzw/ftlzw.c",
    "src/pcf/pcf.c",
    "src/pfr/pfr.c",
    "src/psaux/psaux.c",
    "src/pshinter/pshinter.c",
    "src/psnames/psnames.c",
    "src/raster/raster.c",
    "src/sdf/sdf.c",
    "src/sfnt/sfnt.c",
    "src/smooth/smooth.c",
    "src/svg/svg.c",
    "src/truetype/truetype.c",
    "src/type1/type1.c",
    "src/type42/type42.c",
    "src/winfonts/winfnt.c",
]

EXPORTED = [
    "_ft_init",
    "_ft_version",
    "_ft_new_memory_face",
    "_ft_done_face",
    "_ft_set_char_size",
    "_ft_get_char_index",
    "_ft_load_glyph",
    "_ft_bitmap_buffer",
    "_ft_bitmap_pitch",
    "_ft_bitmap_width",
    "_ft_bitmap_rows",
    "_ft_bitmap_left",
    "_ft_bitmap_top",
    "_ft_linear_hori_advance",
    "_ft_size_height",
    "_ft_size_ascender",
    "_ft_size_descender",
    "_malloc",
    "_free",
]


def find_emcc():
    import shutil

    if shutil.which("emcc"):
        return "emcc"
    # The emsdk sits at the workspace root: one level above firmware-next,
    # two above a worktree under wt/.
    for base in (REPO.parent, REPO.parent.parent):
        candidate = base / ".emsdk" / "upstream" / "emscripten" / "emcc"
        if candidate.exists():
            return str(candidate)
    sys.exit("emcc not found -- source .emsdk/emsdk_env.sh")


def fetch_source():
    root = SRC_CACHE / f"freetype-{FT_TAG}"
    if (root / "include" / "ft2build.h").exists():
        return root
    SRC_CACHE.mkdir(parents=True, exist_ok=True)
    tarball = SRC_CACHE / "freetype.tar.gz"
    print(f"fetching FreeType {FT_VERSION} ...")
    with urllib.request.urlopen(FT_URL, timeout=120) as response:
        tarball.write_bytes(response.read())
    with tarfile.open(tarball) as tf:
        tf.extractall(SRC_CACHE, filter="data")
    if not (root / "include" / "ft2build.h").exists():
        sys.exit(f"unexpected tarball layout under {SRC_CACHE}")
    return root


def main():
    emcc = find_emcc()
    ft = fetch_source()
    OBJ.mkdir(parents=True, exist_ok=True)

    common = [
        emcc,
        "-DFT2_BUILD_LIBRARY",
        f"-I{ft / 'include'}",
        "-Oz",
        "-fno-signed-char",  # match the emulator build's char signedness
        "-Wno-implicit-function-declaration",
    ]

    objs = []
    for rel in FT_SOURCES + [str(SHIM)]:
        src = SHIM if rel == str(SHIM) else ft / rel
        obj = OBJ / (pathlib.Path(rel).name + ".o")
        objs.append(obj)
        if obj.exists() and obj.stat().st_mtime > src.stat().st_mtime:
            continue
        print(f"  cc {pathlib.Path(rel).name}")
        result = subprocess.run(
            common + ["-c", str(src), "-o", str(obj)],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            print(result.stderr[-3000:])
            sys.exit(f"compile failed: {rel}")

    print("linking ...")
    result = subprocess.run(
        [
            emcc,
            *[str(o) for o in objs],
            "-o",
            str(OUT / "ft.js"),
            "-Oz",
            "-sMODULARIZE=1",
            "-sEXPORT_NAME=createFtModule",
            "-sENVIRONMENT=web,worker",
            "-sALLOW_MEMORY_GROWTH=1",
            "-sEXPORTED_RUNTIME_METHODS=HEAPU8",
            "-sEXPORTED_FUNCTIONS=" + ",".join(EXPORTED),
            "-sASSERTIONS=0",
        ],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        print(result.stderr[-3000:])
        sys.exit("link failed")
    for name in ("ft.js", "ft.wasm"):
        size = (OUT / name).stat().st_size
        print(f"  {name}  {size / 1024:.0f} KB")


if __name__ == "__main__":
    sys.exit(main())

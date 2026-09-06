#!/usr/bin/env python3
"""Pack the study tools into site/study/tools.zip, deterministically.

The installer page runs the repository's own Python tools through Pyodide;
this script is how they get to the site. One zip, byte-stable for identical
inputs (fixed timestamps, sorted names), so `git status` only moves when a
tool really changed.

    python3 tools_local/study/sync_site.py            # write the zip
    python3 tools_local/study/sync_site.py --check    # exit 1 if stale

--check exists for check.sh: a page serving last week's converter while the
CLI has this week's is exactly the drift the Pyodide design is supposed to
make impossible, so staleness has to be loud.
"""

import io
import pathlib
import sys
import zipfile

REPO = pathlib.Path(__file__).resolve().parents[2]
OUT = REPO / "site" / "study" / "tools.zip"

# Paths are kept repo-relative inside the zip so every tool's own
# "repo_root = parents[2]" arithmetic works unchanged under /tools.
MEMBERS = [
    "tools_local/study/apkg.py",
    "tools_local/study/anki_to_deck.py",
    "tools_local/study/fsrs.py",
    # anki_to_deck, check_deck, make_fonts and measure_layout all import this.
    # Leaving it out is a page that dies on import with a traceback the user
    # cannot read, which is exactly what happened to deck_to_anki.py once.
    "tools_local/study/scripts.py",
    "tools_local/study/check_deck.py",
    # Forgetting this one shipped a page whose sync step was dead: the host
    # test ran beside the real files and could not notice. test_web_glue.py
    # --from-zip now runs the pipeline from inside this zip, so an absent
    # member fails a test instead of a user.
    "tools_local/study/deck_to_anki.py",
    "tools_local/study/inspect_deck.py",
    "tools_local/study/make_images.py",
    "tools_local/study/make_fonts.py",
    "tools_local/study/mono_cpfont.py",
    "tools_local/study/study.py",
    "tools_local/study/web_glue.py",
    # The wasm-FreeType stand-in for freetype-py; only the page ever sees it.
    "tools_local/study/web_shims/freetype.py",
    # check_deck reads the built-in serif's coverage from this header.
    "lib/EpdFont/builtinFonts/notoserif_16_regular.h",
    # The stock converter mono_cpfont patches over, and its version module.
    "lib/EpdFont/scripts/fontconvert_sdcard.py",
    "lib/EpdFont/scripts/cpfont_version.py",
]


def build():
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as zf:
        for member in sorted(MEMBERS):
            src = REPO / member
            if not src.is_file():
                sys.exit(f"missing {member}")
            info = zipfile.ZipInfo(member, date_time=(2020, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            zf.writestr(info, src.read_bytes())
    return buf.getvalue()


def main():
    data = build()
    if "--check" in sys.argv:
        if not OUT.exists() or OUT.read_bytes() != data:
            print(
                f"{OUT.relative_to(REPO)} is stale --"
                " run tools_local/study/sync_site.py"
            )
            return 1
        print("tools.zip is current")
        return 0
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(data)
    print(f"wrote {OUT.relative_to(REPO)} ({len(data) / 1024:.0f} KB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

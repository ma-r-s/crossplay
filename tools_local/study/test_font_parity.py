#!/usr/bin/env python3
"""The font acceptance test: the CLI's side of byte-identical.

    .venv-study/bin/python tools_local/study/test_font_parity.py

Converts the committed sample deck (site/study/demo/sat-vocabulary.apkg)
and builds its faces from the committed serif (site/study/DejaVuSerif.ttf)
through the REAL freetype-py, then prints the sha256 of every .cpfont.

The page's side of the same test: load /study/?deck=demo/sat-vocabulary.apkg,
pick the bundled serif, and read StudyInstaller.state().fonts.hashes. The
two sets must match byte for byte; if they ever stop matching, the wasm
FreeType (tools_local/wasm-ft/) or the pinned fontTools version has drifted
from what the CLI uses, and the installer is quietly building different
fonts than the documentation promises.

Preconditions this asserts rather than assumes: freetype-py wraps 2.13.2
(what wasm-ft builds) and fontTools is 4.56.0 (what Pyodide 0.29.1 ships).
"""

import hashlib
import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))

import apkg  # noqa: E402


def main():
    import fontTools
    import freetype

    ft = freetype.version()
    if ft != (2, 13, 2):
        sys.exit(f"freetype-py wraps {ft}, wasm-ft builds 2.13.2: not comparable")
    if fontTools.version != "4.56.0":
        sys.exit(
            f"fontTools is {fontTools.version}, Pyodide ships 4.56.0:"
            f" pin it (uv pip install fonttools==4.56.0) or the subsets differ"
        )

    demo = REPO / "site" / "study" / "demo" / "sat-vocabulary.apkg"
    serif = REPO / "site" / "study" / "DejaVuSerif.ttf"

    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        info = apkg.extract(demo, tmp / "unpacked")
        deck_name = next(
            name for name, _ in apkg.list_decks(info["collection"]) if "SAT" in name
        )
        deck = tmp / "deck"
        for argv in (
            [
                sys.executable,
                str(HERE / "anki_to_deck.py"),
                str(info["collection"]),
                "--deck",
                deck_name,
                "--out",
                str(deck),
            ],
            [
                sys.executable,
                str(HERE / "make_fonts.py"),
                "--font",
                str(serif),
                "--deck",
                str(deck),
                "--out",
                str(deck / "fonts"),
            ],
        ):
            result = subprocess.run(argv, capture_output=True, text=True)
            if result.returncode != 0:
                sys.exit(f"{argv[1]} failed:\n{result.stdout}\n{result.stderr}")

        fonts = sorted((deck / "fonts").rglob("*.cpfont"))
        if not fonts:
            sys.exit("no .cpfont produced")
        for path in fonts:
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            print(f"{digest}  fonts/{path.relative_to(deck / 'fonts')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""What the installer page calls, and nothing else does.

This is the boundary between the website and the study tools: the worker
imports this one module and calls its three functions, each returning JSON
because that is what crosses the Pyodide bridge cleanly. Everything real
happens in the tools themselves, driven exactly as the CLI drives them
(argv + runpy), so the page cannot grow behavior the CLI does not have.

Paths are fixed: the package lands at /work/deck.apkg, unpacks under
/work/unpacked, and the converted deck is built in /work/deck. One deck at a
time; opening a new package clears the lot.
"""

import contextlib
import io
import json
import pathlib
import runpy
import shutil
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
# The wasm-FreeType stand-in for freetype-py. First on the path, and present
# only in the site's tools.zip, so the CLI keeps the real binding.
sys.path.insert(0, str(HERE / "web_shims"))

import apkg  # noqa: E402
import study as study_cli  # noqa: E402

WORK = pathlib.Path("/work")


class _Tee(io.StringIO):
    """A capture buffer that also forwards whole lines to a callback, so the
    page can show a slow tool's progress while it runs."""

    def __init__(self, callback):
        super().__init__()
        self._callback = callback
        self._pending = ""

    def write(self, text):
        if self._callback:
            self._pending += text
            while "\n" in self._pending:
                line, self._pending = self._pending.split("\n", 1)
                if line.strip():
                    self._callback(line)
        return super().write(text)


def _run_tool(script_name, argv, tee=None):
    """Run a tool the way its own command line would, capturing everything.

    runpy with run_name __main__ so the tool's argparse, prints and
    sys.exit all behave exactly as they do in a terminal. The exit code and
    the combined output come back; nothing escapes to the real stdout.
    """
    buf = _Tee(tee)
    code = 0
    saved_argv = sys.argv
    sys.argv = [script_name] + [str(a) for a in argv]
    try:
        with contextlib.redirect_stdout(buf), contextlib.redirect_stderr(buf):
            runpy.run_path(str(HERE / script_name), run_name="__main__")
    except SystemExit as exc:
        if isinstance(exc.code, int) or exc.code is None:
            code = exc.code or 0
        else:
            # sys.exit("message"): the message is the point, the code is 1.
            buf.write(str(exc.code) + "\n")
            code = 1
    finally:
        sys.argv = saved_argv
    return code, buf.getvalue()


def _deck_files():
    """Every converted file, fonts included, as deck-relative paths."""
    out = WORK / "deck"
    return sorted(str(p.relative_to(out)) for p in out.rglob("*") if p.is_file())


def open_apkg():
    """Unwrap /work/deck.apkg and describe what is inside, for the page."""
    unpacked = WORK / "unpacked"
    shutil.rmtree(unpacked, ignore_errors=True)
    shutil.rmtree(WORK / "deck", ignore_errors=True)
    try:
        info = apkg.extract(WORK / "deck.apkg", unpacked)
    except apkg.ApkgError as exc:
        return json.dumps({"error": str(exc)})

    decks = apkg.list_decks(info["collection"])
    if not decks:
        return json.dumps({"error": "this package contains no cards"})
    sched = apkg.scheduling_summary(info["collection"])
    media = sorted(info["media_dir"].iterdir())
    fonts = [p.name for p in media if p.suffix.lower() in apkg.FONT_SUFFIXES]
    return json.dumps(
        {
            "decks": [{"name": name, "cards": cards} for name, cards in decks],
            "cards": sched["cards"],
            "cardsWithState": sched["cards_with_state"],
            "reviews": sched["reviews"],
            "fonts": fonts,
            "images": len(media) - len(fonts),
            "mediaSkipped": info["media_skipped"],
        }
    )


def convert(deck_name, mapping):
    """Convert one deck and immediately check it, like setup does.

    mapping is a list of "slot=Field" strings, or None; it becomes --map
    flags verbatim. Returns the converter's own words, the checker's own
    words, and what the page needs to know to draw the next step.
    """
    out = WORK / "deck"
    shutil.rmtree(out, ignore_errors=True)
    collection = WORK / "unpacked" / "collection.anki2"

    argv = [collection, "--deck", deck_name, "--out", out]
    for pair in mapping or []:
        argv += ["--map", pair]
    code, log = _run_tool("anki_to_deck.py", argv)
    if code != 0:
        return json.dumps({"error": log.strip() or "conversion failed"})

    media = WORK / "unpacked" / "media"
    images_log = ""
    if any(p.suffix.lower() in apkg.IMAGE_SUFFIXES for p in media.iterdir()):
        icode, images_log = _run_tool(
            "make_images.py",
            ["--collection", collection, "--out", out, "--media", media],
        )
        if icode != 0:
            images_log += "\n(image packing failed; the deck works without it)"

    check_code, check_log = _run_tool("check_deck.py", [out])

    return json.dumps(
        {
            "log": log,
            "imagesLog": images_log,
            "checkLog": check_log,
            "checkFailed": check_code != 0,
            "slug": study_cli.slug(deck_name),
            "hasCjk": study_cli.deck_has_cjk(out),
            "files": _deck_files(),
        }
    )


def build_fonts(mode, tee=None):
    """Build the deck's faces the way make_fonts.py always has.

    mode "cjk" uses the TTFs the package's media carried; mode "custom" uses
    whatever TTF the worker put at /work/custom.ttf (the bundled serif or the
    user's own). Then check_deck runs again, against the real faces this
    time, and its verdict replaces the built-in-only one.
    """
    out = WORK / "deck"
    fonts_dir = out / "fonts"
    shutil.rmtree(fonts_dir, ignore_errors=True)

    if mode == "cjk":
        argv = [
            "--media",
            WORK / "unpacked" / "media",
            "--deck",
            out,
            "--out",
            fonts_dir,
        ]
    elif mode == "custom":
        argv = ["--font", WORK / "custom.ttf", "--deck", out, "--out", fonts_dir]
    else:
        return json.dumps({"error": f"unknown font mode {mode!r}"})

    code, log = _run_tool("make_fonts.py", argv, tee=tee)
    if code != 0:
        shutil.rmtree(fonts_dir, ignore_errors=True)
        return json.dumps({"error": log.strip() or "font build failed"})

    check_code, check_log = _run_tool("check_deck.py", [out, "--fonts", fonts_dir])

    import hashlib

    hashes = {}
    for rel in _deck_files():
        if rel.endswith(".cpfont"):
            digest = hashlib.sha256((out / rel).read_bytes()).hexdigest()
            hashes[rel] = digest

    return json.dumps(
        {
            "log": log,
            "checkLog": check_log,
            "checkFailed": check_code != 0,
            "files": _deck_files(),
            "hashes": hashes,
        }
    )


def deck_file(name):
    """One converted file's bytes, for injection or writing to the card."""
    root = (WORK / "deck").resolve()
    path = (root / name).resolve()
    if root not in path.parents:
        raise ValueError(f"not a deck file: {name}")
    return path.read_bytes()

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

import apkg  # noqa: E402
import study as study_cli  # noqa: E402

WORK = pathlib.Path("/work")


def _run_tool(script_name, argv):
    """Run a tool the way its own command line would, capturing everything.

    runpy with run_name __main__ so the tool's argparse, prints and
    sys.exit all behave exactly as they do in a terminal. The exit code and
    the combined output come back; nothing escapes to the real stdout.
    """
    buf = io.StringIO()
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
            "files": sorted(p.name for p in out.iterdir()),
        }
    )


def deck_file(name):
    """One converted file's bytes, for injection or writing to the card."""
    path = (WORK / "deck" / name).resolve()
    if path.parent != (WORK / "deck").resolve():
        raise ValueError(f"not a deck file: {name}")
    return path.read_bytes()

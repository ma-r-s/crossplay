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

import anki_to_deck  # noqa: E402
import apkg  # noqa: E402
import check_deck as check_deck_mod  # noqa: E402
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
    namespace = {}
    saved_argv = sys.argv
    sys.argv = [script_name] + [str(a) for a in argv]
    try:
        with contextlib.redirect_stdout(buf), contextlib.redirect_stderr(buf):
            namespace = runpy.run_path(str(HERE / script_name), run_name="__main__")
    except SystemExit as exc:
        if isinstance(exc.code, int) or exc.code is None:
            code = exc.code or 0
        else:
            # sys.exit("message"): the message is the point, the code is 1.
            buf.write(str(exc.code) + "\n")
            code = 1
    finally:
        sys.argv = saved_argv
    return code, buf.getvalue(), namespace


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
            "audio": info["audio"],
            "pictures": info["pictures"],
        }
    )


def mapping_dict(mapping):
    """"slot=Field" strings to a dict. An empty value is a real instruction --
    "show nothing here" -- not an absent one, so it is kept."""
    out = {}
    for pair in mapping or []:
        key, sep, value = pair.partition("=")
        if sep:
            out[key] = value
    return out


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
    code, log, used = _run_tool("anki_to_deck.py", argv)
    if code != 0:
        return json.dumps({"error": log.strip() or "conversion failed"})

    media = WORK / "unpacked" / "media"
    images_log = ""
    if any(p.suffix.lower() in apkg.IMAGE_SUFFIXES for p in media.iterdir()):
        icode, images_log, _ = _run_tool(
            "make_images.py",
            ["--collection", collection, "--out", out, "--media", media],
        )
        if icode != 0:
            images_log += "\n(image packing failed; the deck works without it)"

    check_code, check_log, _ = _run_tool("check_deck.py", [out])

    # The page's headline numbers come from here, parsed from the converter's
    # own summary line, so they can never disagree with the log below them.
    import re

    # What the user's question needs: the fields of the deck's dominant note
    # type, the mapping the converter just used for it, and the first card
    # exactly as converted -- so the page can show 'this is how a card will
    # read' and let the user move fields around with real content in view.
    import sqlite3 as _sql

    db = _sql.connect(f"file:{collection}?mode=ro", uri=True)
    db.create_collation(
        "unicase", lambda a, b: (a.lower() > b.lower()) - (a.lower() < b.lower())
    )
    like = deck_name.replace("::", "\x1f")
    type_rows = db.execute(
        """select nt.name, nt.id, count(*) as n from cards c
           join notes nn on nn.id = c.nid
           join notetypes nt on nt.id = nn.mid
           join decks d on d.id = c.did
           where d.name = ? or d.name like ?
           group by nt.id order by n desc""",
        (like, like + "\x1f%"),
    ).fetchall()
    fields = []
    guess = {}
    if type_rows:
        dominant_id = type_rows[0][1]
        fields = [
            row[0]
            for row in db.execute(
                "select name from fields where ntid = ? order by ord", (dominant_id,)
            )
        ]
        # What really ran, straight out of the converter's namespace. Deriving
        # it again here is how the page came to show "type" as the word while
        # the deck was built from "german".
        used_profiles = used.get("USED_PROFILES") or {}
        guess = dict(used_profiles.get(type_rows[0][0]) or {})
    db.close()

    # Cards for the page to show: the first, then the ones the checker
    # complained about. A deck is judged on the screen that shows a card, and
    # judging 5742 of them by card #1 is how a wrong mapping ships -- and how
    # a dropdown that only affects later cards looks broken.
    samples = []
    flagged = []
    reasons = {}
    for line in check_log.splitlines():
        hit = re.search(r"note (\d+)", line)
        if not hit:
            continue
        index = int(hit.group(1))
        if index not in flagged:
            flagged.append(index)
        # Why this card was flagged, so the page can say it beside the card
        # instead of leaving the user to guess at a normal-looking one.
        if index not in reasons:
            tail = line.split(":", 1)[-1].strip()
            reasons[index] = tail[:120] if tail else ""
    wanted = [0] + flagged[:6]
    try:
        for index, note_fields in check_deck_mod.read_deck(out / "deck.dat"):
            if index in wanted:
                card = dict(zip(check_deck_mod.FIELD_NAMES, note_fields))
                card["index"] = index
                card["flagged"] = index in flagged
                card["reason"] = reasons.get(index, "")
                samples.append(card)
            if len(samples) >= len(wanted):
                break
    except (OSError, SystemExit):
        pass
    samples.sort(key=lambda c: wanted.index(c["index"]))
    sample = samples[0] if samples else None

    problems = {}
    for line in check_log.splitlines():
        found = re.match(r"\s+(?:ok|FAIL)\s+(.*?): (\d+)$", line)
        if found and int(found.group(2)):
            problems[found.group(1)] = int(found.group(2))

    counts = re.search(
        r"deck '.*': (\d+) cards \((\d+) with scheduling state, (\d+) skipped\)",
        log,
    )
    # The deck's own Anki limit, read back from meta.dat, so the page can say
    # why a 1467-card deck offers twenty a day. Anki's setting, not ours.
    # magic(8) + version/flags(4) + params(19*4) + retention(4) + maxInterval(4)
    per_day = None
    try:
        import struct as _struct

        meta = (out / "meta.dat").read_bytes()
        (value,) = _struct.unpack_from("<i", meta, 8 + 4 + 19 * 4 + 4 + 4)
        if 0 < value < 100000:
            per_day = value
    except Exception:
        per_day = None
    images_packed = re.search(r"images: (\d+) packed", images_log)
    # Cloze decks convert now. What is still dropped is the EMPTY cloze card
    # -- one whose hole was edited out of the text and which Anki itself
    # deletes under Tools > "Empty Cards" -- and the page says so in those
    # words rather than in "cloze".
    cloze_line = re.search(r"(\d+) empty cloze card\(s\) skipped", log)

    return json.dumps(
        {
            "log": log,
            "imagesLog": images_log,
            "checkLog": check_log,
            "checkFailed": check_code != 0,
            "slug": study_cli.slug(deck_name),
            "hasCjk": study_cli.deck_has_cjk(out),
            "files": _deck_files(),
            "cards": int(counts.group(1)) if counts else None,
            "withState": int(counts.group(2)) if counts else None,
            "skipped": int(counts.group(3)) if counts else None,
            "clozeEmpty": int(cloze_line.group(1)) if cloze_line else 0,
            "imagesPacked": int(images_packed.group(1)) if images_packed else 0,
            "sample": sample,
            "samples": samples,
            "fields": fields,
            "guess": {k: v for k, v in guess.items() if v},
            "noteTypes": len(type_rows),
            "problems": problems,
            "newPerDay": per_day,
        }
    )


def build_fonts(mode, tee=None):
    import re

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

    code, log, used = _run_tool("make_fonts.py", argv, tee=tee)
    if code != 0:
        shutil.rmtree(fonts_dir, ignore_errors=True)
        return json.dumps({"error": log.strip() or "font build failed"})

    check_code, check_log, _ = _run_tool("check_deck.py", [out, "--fonts", fonts_dir])

    problems = {}
    for line in check_log.splitlines():
        found = re.match(r"\s+(?:ok|FAIL)\s+(.*?): (\d+)$", line)
        if found and int(found.group(2)):
            problems[found.group(1)] = int(found.group(2))

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
            "problems": problems,
        }
    )


def deck_file(name):
    """One converted file's bytes, for injection or writing to the card."""
    root = (WORK / "deck").resolve()
    path = (root / name).resolve()
    if root not in path.parents:
        raise ValueError(f"not a deck file: {name}")
    return path.read_bytes()


def make_zip(slug):
    """The converted deck as a zip, laid out the way the card wants it.

    The fallback for browsers without the File System Access API: the user
    unpacks this at the root of the SD card and gets exactly what the write
    step would have written.
    """
    import zipfile

    out = io.BytesIO()
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zf:
        for rel in _deck_files():
            zf.write(WORK / "deck" / rel, f"study/{slug}/{rel}")
    return out.getvalue()


def sync_local():
    """Replay every deck the worker staged under /work/sync into the
    collection copy beside them, exactly as `study.py sync` would.

    The page put the card's deck directories at /work/sync/decks/<name> and
    the user's collection at /work/sync/collection.anki2. deck_to_anki.py
    does the replay per deck, idempotently (rows keyed by timestamp), and
    the page writes the updated collection back to the real file, after its
    own backup. Nothing here talks to AnkiWeb; Anki's own Sync button does
    that part better than we could.
    """
    base = WORK / "sync"
    collection = base / "collection.anki2"
    decks = sorted(d for d in (base / "decks").iterdir() if d.is_dir())
    if not decks:
        return json.dumps({"error": "no decks with reviews were staged"})

    logs = []
    failed = False
    for deck_dir in decks:
        code, log, used = _run_tool("deck_to_anki.py", [deck_dir, collection])
        logs.append(f"== {deck_dir.name}\n{log.rstrip()}")
        failed = failed or code != 0
    return json.dumps({"log": "\n\n".join(logs), "failed": failed})


def sync_file():
    """The replayed collection, for the page to write back."""
    return (WORK / "sync" / "collection.anki2").read_bytes()

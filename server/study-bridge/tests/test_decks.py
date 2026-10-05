#!/usr/bin/env python3
"""Which decks get faces built, from what, and what a failed build says.

Run: .venv/bin/python tests/test_decks.py

On 2026-10-05, five of the eighteen people who had chosen decks on the sync
bridge had a deck that never built. Three of them had decks the converter
handled perfectly -- two of those pure Latin -- and lost them to the face step,
which ran whenever the media folder held ANY font file while make_fonts.py only
ever looks for five particular ones. This builds real collections with Anki's
own library and runs the real converter; only make_fonts is stood in for,
because what is under test is the decision to call it and with which source,
not fontTools.
"""

import pathlib
import subprocess
import sys
import tempfile
import types

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
REPO = ROOT.parents[1]
sys.path.insert(0, str(ROOT))

from bridge import decks  # noqa: E402

checks = 0
failures = 0


def ok(condition, what):
    global checks, failures
    checks += 1
    if not condition:
        failures += 1
        print(f"FAIL: {what}")


def collection(base, notes, media_files=()):
    """A user dir the way the bridge lays one out: collection.anki2 beside
    collection.media."""
    from anki.collection import Collection

    base.mkdir(parents=True)
    col = Collection(str(base / "collection.anki2"))
    basic = col.models.by_name("Basic")
    did = col.decks.id("Deck")
    for front, back in notes:
        note = col.new_note(basic)
        note.fields[0], note.fields[1] = front, back
        col.add_note(note, did)
    col.close()
    media = base / "collection.media"
    media.mkdir(exist_ok=True)
    for name in media_files:
        (media / name).write_bytes(
            b"not a real font; the gate must not need to open it"
        )
    return types.SimpleNamespace(root=base, collection_path=base / "collection.anki2")


LATIN = [("ubiquitous", "found everywhere"), ("lucid", "clear")]
HANZI = [("你好", "hello"), ("谢谢", "thank you")]


def main():
    decks.TOOLS = REPO / "tools_local" / "study"
    decks.BUNDLED_CJK = pathlib.Path("/bundled/NotoSansCJK.otf")

    # --- The reasons are short enough for the reader to show whole. The
    # verdict's body is four lines at the large face; the first sentence for a
    # picture deck ran 70 characters and was cut off at "its cards have a
    # pi...", which is the reason, which is the point.
    for code, clause in decks.REASONS.items():
        ok(len(clause) <= 36, f"{code}: '{clause}' is {len(clause)} chars, over 36")
        ok(
            clause == clause.strip() and not clause.endswith("."),
            f"{code}: the reader adds the full stop",
        )
        ok(clause[:1].islower(), f"{code}: it follows a colon mid-sentence")

    # --- The five faces are read out of make_fonts.py, not retyped here.
    faces = decks.face_files()
    ok(
        "_simsun.ttf" in faces and len(faces) == 5,
        f"the face list should be make_fonts' five, got {faces}",
    )

    real_run = decks._run
    calls = []

    def run(args, timeout=900):
        if args[0].endswith("make_fonts.py"):
            calls.append(args)
            out = pathlib.Path(args[args.index("--out") + 1])
            out.mkdir(parents=True, exist_ok=True)
            (out / "face.cpfont").write_bytes(b"x")
            return subprocess.CompletedProcess(args, 0, "", "")
        return real_run(args, timeout)

    decks._run = run
    try:
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)

            def build(name, notes, media=()):
                calls.clear()
                store = collection(tmp / name, notes, media)
                try:
                    return decks.build_deck(store, "Deck"), None
                except Exception as exc:  # noqa: BLE001 -- the exception IS the result
                    return None, exc

            entry, exc = build("latin-stray-font", LATIN, ["_inter-regular.ttf"])
            ok(
                exc is None,
                f"a Latin deck with an unrelated font in media must build, got {exc}",
            )
            ok(not calls, "and must not run the face step at all")

            entry, exc = build("latin-with-simsun", LATIN, ["_simsun.ttf"])
            ok(
                exc is None and not calls,
                "a Latin deck needs no faces even when the media has SimSun",
            )

            entry, exc = build("hanzi-own-faces", HANZI, ["_simsun.ttf"])
            ok(
                exc is None,
                f"a Chinese deck with its template's faces builds, got {exc}",
            )
            ok(
                len(calls) == 1 and "--media" in calls[0],
                f"from the media folder, got {calls}",
            )

            entry, exc = build("hanzi-stray-font", HANZI, ["_inter-regular.ttf"])
            ok(exc is None, f"a Chinese deck with no template faces builds, got {exc}")
            ok(
                len(calls) == 1
                and "--font" in calls[0]
                and str(decks.BUNDLED_CJK) in calls[0],
                f"from the bundled face, as the installer page does, got {calls}",
            )

            entry, exc = build(
                "pictures", [('<img src="lincoln.jpg">', "Abraham Lincoln")]
            )
            ok(
                isinstance(exc, decks.BuildFailed),
                f"a deck of picture fronts fails to build, got {exc!r}",
            )
            ok(
                getattr(exc, "reason", None) == "picture-front",
                f"and says why, got {getattr(exc, 'reason', None)}",
            )
            ok(
                decks.reason_sentence(exc) == decks.REASONS["picture-front"],
                "as the reader's clause",
            )
            ok(
                decks.reason_sentence(RuntimeError("anything")) == "",
                "an unknown failure sends no clause",
            )
    finally:
        decks._run = real_run

    print(f"test_decks: {checks} checks, {failures} failed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()

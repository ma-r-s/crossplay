#!/usr/bin/env python3
"""Run one .apkg through the whole installer pipeline and say what happened.

    .venv-study/bin/python tools_local/study/try_apkg.py DECK.apkg [DECK2.apkg ...]

This is the page's flow without the page: extract, list decks, convert the
largest, pack images, run check_deck. One line of verdict per package, the
tools' own words underneath when something fails. It exists so a pile of
real-world decks (AnkiWeb shared decks, friends' exports) can be thrown at
the pipeline after any change, and so a user's broken package can be
diagnosed without a browser in the loop.
"""

import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import apkg  # noqa: E402


def run_tool(name, argv):
    result = subprocess.run(
        [sys.executable, str(HERE / name), *[str(a) for a in argv]],
        capture_output=True,
        text=True,
    )
    return result.returncode, result.stdout + result.stderr


def try_one(path):
    print(f"\n=== {path.name} ({path.stat().st_size / 1024 / 1024:.1f} MB)")
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        try:
            info = apkg.extract(path, tmp / "unpacked")
        except apkg.ApkgError as exc:
            print(f"  REFUSED: {exc}")
            return False

        decks = apkg.list_decks(info["collection"])
        sched = apkg.scheduling_summary(info["collection"])
        print(
            f"  v{info['version']} package, {len(decks)} deck(s),"
            f" {sched['cards']} cards ({sched['cards_with_state']} with state),"
            f" media kept {info['media_kept']} / skipped {info['media_skipped']}"
        )
        if not decks:
            print("  FAILED: no decks with cards")
            return False

        name, cards = max(decks, key=lambda d: d[1])
        print(f"  converting largest: {name!r} ({cards} cards)")
        code, log = run_tool(
            "anki_to_deck.py",
            [info["collection"], "--deck", name, "--out", tmp / "deck"],
        )
        if code != 0:
            print("  CONVERT FAILED:")
            print("    " + "\n    ".join(log.strip().splitlines()[-10:]))
            return False
        summary = [
            line
            for line in log.splitlines()
            if line.strip().startswith(("deck ", "glyphs", "FSRS", "steps"))
        ]
        print("    " + "\n    ".join(s.strip() for s in summary))

        media = info["media_dir"]
        if any(p.suffix.lower() in apkg.IMAGE_SUFFIXES for p in media.iterdir()):
            code, log = run_tool(
                "make_images.py",
                [
                    "--collection",
                    info["collection"],
                    "--out",
                    tmp / "deck",
                    "--media",
                    media,
                ],
            )
            line = next(
                (l.strip() for l in log.splitlines() if l.startswith("images:")),
                "images: tool failed" if code else "",
            )
            print(f"    {line}")

        code, log = run_tool("check_deck.py", [tmp / "deck"])
        verdict = log.strip().splitlines()[-1] if log.strip() else "no output"
        marker = "ok " if code == 0 else "CHECK FAILED"
        print(f"  {marker}: {verdict}")
        if code != 0:
            fails = [l for l in log.splitlines() if "FAIL" in l]
            print("    " + "\n    ".join(f.strip() for f in fails[:6]))
        return True


def main():
    paths = [pathlib.Path(p) for p in sys.argv[1:]]
    if not paths:
        sys.exit(__doc__)
    good = 0
    for path in paths:
        good += bool(try_one(path))
    print(f"\n{good} of {len(paths)} packages made it through")
    return 0 if good == len(paths) else 1


if __name__ == "__main__":
    sys.exit(main())

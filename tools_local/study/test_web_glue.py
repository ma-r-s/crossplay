#!/usr/bin/env python3
"""Exercise web_glue.py, the page's whole Python boundary, on the host.

    .venv-study/bin/python tools_local/study/test_web_glue.py
    .venv-study/bin/python tools_local/study/test_web_glue.py --from-zip

The page cannot be unit-tested from a shell, but its Python side can: the
same open -> convert -> sync_local path the worker drives, against the
committed sample deck, with a device review fabricated in the on-card
format. What stays browser-only after this is the File System Access
plumbing (pickers, backup, write-back), which is thin and hand-tested.

--from-zip runs the identical suite from INSIDE a freshly built tools.zip,
which is the code the browser actually gets. The plain run once passed
while the page's sync step was dead, because deck_to_anki.py sat beside
the test on disk and was missing from the zip's member list. Never again.

Counted checks, printed, because "PASS (0 checks)" has bitten before.
"""

import json
import pathlib
import shutil
import sqlite3
import struct
import sys
import tempfile
import time
import zipfile

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]

FROM_ZIP = "--from-zip" in sys.argv
if FROM_ZIP:
    sys.path.insert(0, str(HERE))
    import io

    import sync_site

    _zip_root = pathlib.Path(tempfile.mkdtemp(prefix="toolszip-"))
    with zipfile.ZipFile(io.BytesIO(sync_site.build())) as zf:
        zf.extractall(_zip_root)
    sys.path.insert(0, str(_zip_root / "tools_local" / "study"))
else:
    sys.path.insert(0, str(HERE))

import web_glue  # noqa: E402

if FROM_ZIP:
    got = pathlib.Path(web_glue.__file__).resolve()
    assert str(_zip_root) in str(got), f"imported {got}, not the zip's copy"

CHECKS = 0


def ok(condition, what):
    global CHECKS
    CHECKS += 1
    if not condition:
        sys.exit(f"FAIL check {CHECKS}: {what}")


def main():
    demo = REPO / "site" / "study" / "demo" / "sat-vocabulary.apkg"
    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp) / "work"
        work.mkdir()
        web_glue.WORK = work

        shutil.copy2(demo, work / "deck.apkg")
        opened = json.loads(web_glue.open_apkg())
        ok("error" not in opened, f"open_apkg: {opened.get('error')}")
        ok(opened["decks"], "no decks listed")

        converted = json.loads(web_glue.convert(opened["decks"][0]["name"], None))
        ok("error" not in converted, f"convert: {converted.get('error')}")
        ok(converted["slug"] == "sat-vocabulary", f"slug: {converted['slug']}")
        ok(not converted["hasCjk"], "sample deck flagged as CJK")
        ok("cards.dat" in converted["files"], "cards.dat missing from files")
        ok(not converted["checkFailed"], "check_deck failed on the sample deck")

        zipped = web_glue.make_zip(converted["slug"])
        import io
        import zipfile

        names = zipfile.ZipFile(io.BytesIO(zipped)).namelist()
        ok(
            "study/sat-vocabulary/deck.dat" in names,
            f"zip lays out wrong: {names[:4]}",
        )

        # A review the device would have written: first card, GOOD, now.
        cards = (work / "deck" / "cards.dat").read_bytes()
        card_id = struct.unpack_from("<q", cards, 0)[0]
        review = struct.pack(
            "<qqBBhiIB3x",
            card_id,
            int(time.time() * 1000),
            3,  # GOOD
            0,  # was new
            0,
            1,
            4200,
            0,
        )
        staging = work / "sync"
        (staging / "decks" / "sat").mkdir(parents=True)
        (staging / "decks" / "sat" / "revlog.dat").write_bytes(review)
        shutil.copy2(
            work / "deck" / "cards.dat", staging / "decks" / "sat" / "cards.dat"
        )
        shutil.copy2(
            work / "unpacked" / "collection.anki2", staging / "collection.anki2"
        )

        result = json.loads(web_glue.sync_local())
        ok(not result.get("failed"), f"sync_local failed:\n{result.get('log')}")
        ok("applied   1 reviews" in result["log"], f"replay log:\n{result['log']}")

        db = sqlite3.connect(staging / "collection.anki2")
        (count,) = db.execute(
            "select count(*) from revlog where cid = ?", (card_id,)
        ).fetchone()
        db.close()
        # The fixture gave the first ten cards one review each already.
        ok(count == 2, f"expected 2 revlog rows for card {card_id}, got {count}")

        # Idempotency: the same staging replayed again changes nothing.
        again = json.loads(web_glue.sync_local())
        ok(
            "skipped   1 already present" in again["log"],
            f"second replay was not idempotent:\n{again['log']}",
        )

        updated = web_glue.sync_file()
        ok(len(updated) > 10000, "sync_file returned something too small")

    print(f"PASS ({CHECKS} checks)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

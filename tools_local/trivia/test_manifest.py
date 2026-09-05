#!/usr/bin/env python3
"""The pack id, the manifest, and the one thing pack.meta must never do: guess.

    python3 tools_local/trivia/test_manifest.py

Standard library only, builds its own packs, never skips.

The expensive failure this guards is not a crash. It is a device that reports
(pack id, index) against a pack.dat it no longer holds: the service resolves
those indices through the WRONG build's index map, and questions nobody
reported are deleted, silently, with the pack simply coming out smaller.
"""

import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import manifest as M  # noqa: E402
import pack_format as PF  # noqa: E402

FAILED = []


def check(name, ok, detail=""):
    print(f"  [{'ok  ' if ok else 'FAIL'}] {name}{('  -- ' + detail) if detail and not ok else ''}")
    if not ok:
        FAILED.append(name)


def items(n, salt=""):
    return [
        {"id": f"id{i:06d}{salt}"[:12], "d": (i % 5) + 1, "y": 2000,
         "q": f"Question {i}{salt}", "a": f"Answer {i}", "alt": [], "w": []}
        for i in range(n)
    ]


def make(tmp, n, salt="", name="pack.dat"):
    p = os.path.join(tmp, name)
    PF.write(items(n, salt), p)
    return p


def test_id_is_derived():
    with tempfile.TemporaryDirectory() as tmp:
        a = make(tmp, 8, name="a.dat")
        b = make(tmp, 8, name="b.dat")
        c = make(tmp, 8, salt="x", name="c.dat")
        check("the same bytes give the same id", M.pack_id(a) == M.pack_id(b))
        check("different bytes give a different id", M.pack_id(a) != M.pack_id(c))
        check("count comes out of the header", M.pack_count(a) == 8, f"got {M.pack_count(a)}")

        foreign = os.path.join(tmp, "not-a-pack.dat")
        open(foreign, "wb").write(b"NOPE" * 8)
        try:
            M.pack_count(foreign)
            check("a foreign file is rejected", False, "did not raise")
        except ValueError:
            check("a foreign file is rejected", True)


def test_manifest_matches_the_pack():
    with tempfile.TemporaryDirectory() as tmp:
        p = make(tmp, 12)
        man, man_path = M.write(p, built="2026-09-05T00:00:00+00:00")
        check("manifest names the pack's own byte count",
              man["bytes"] == os.path.getsize(p), f"{man['bytes']} vs {os.path.getsize(p)}")
        check("manifest names the pack's own question count", man["count"] == 12)
        check("manifest id equals the pack's id", man["id"] == M.pack_id(p))
        check("manifest is written beside the pack", os.path.basename(man_path) == "pack.json")


def test_index_map():
    with tempfile.TemporaryDirectory() as tmp:
        rows = items(5)
        out = M.write_index_map(rows, os.path.join(tmp, "pack.idx.tsv"))
        got = {}
        for line in open(out, encoding="utf-8"):
            if not line.startswith("#"):
                i, qid = line.strip().split("\t")
                got[int(i)] = qid
        check("one entry per question, in pack order",
              got == {i: r["id"] for i, r in enumerate(rows)}, f"got {got}")

        try:
            M.write_index_map([{"q": "no id here"}], os.path.join(tmp, "bad.tsv"))
            check("an item with no id refuses", False, "did not raise")
        except ValueError:
            check("an item with no id refuses", True)


# --- the one that matters ----------------------------------------------------
def test_meta_never_guesses():
    with tempfile.TemporaryDirectory() as tmp:
        p = make(tmp, 10)
        man, _ = M.write(p)
        M.write_meta(p, man)

        got = M.read_meta(p)
        check("a meta matching its pack reads back", got and got["id"] == man["id"], f"got {got}")

        # THE HAND-COPY. A different build of the SAME question count replaces
        # pack.dat and pack.meta is left behind. This is the case the device
        # cannot otherwise see, and the one that deletes good questions.
        PF.write(items(10, salt="different"), p)
        check("a same-count hand-copy is caught by bytes",
              M.read_meta(p) is None, f"got {M.read_meta(p)}")

        # A different count, which is the common case.
        M.write_meta(p, M.build(p))
        PF.write(items(11), p)
        check("a different-count replacement is caught", M.read_meta(p) is None)

        # Missing or damaged meta is "I do not know", not a crash and not a guess.
        M.write_meta(p, M.build(p))
        check("a good meta round-trips again", M.read_meta(p) is not None)
        open(M.meta_path(p), "w", encoding="utf-8").write("id\tonly\n")
        check("a meta missing fields is discarded", M.read_meta(p) is None)
        os.remove(M.meta_path(p))
        check("no meta at all is None, not an error", M.read_meta(p) is None)


def test_meta_count_check_is_not_dead_code():
    """The bytes check catches almost everything, which is why this one needs
    its own case: without it the count guard is unfalsifiable, and an
    unfalsifiable guard is indistinguishable from a deleted one.

    Constructed rather than hoped for: the same file LENGTH with a different
    count in the header. Nothing stops a rebuilt pack landing on the same size
    by coincidence, and if it does, bytes alone says the meta is fine while
    every index in it names a different question.
    """
    with tempfile.TemporaryDirectory() as tmp:
        p = make(tmp, 10)
        M.write_meta(p, M.build(p))
        check("baseline meta is valid", M.read_meta(p) is not None)

        blob = bytearray(open(p, "rb").read())
        before = len(blob)
        blob[12:16] = (9).to_bytes(4, "little")  # count says 9, file unchanged
        open(p, "wb").write(bytes(blob))
        check("the mutation kept the file length", os.path.getsize(p) == before)
        check("same size, different count is caught by count",
              M.read_meta(p) is None, f"got {M.read_meta(p)}")


def test_meta_survives_a_state_rewrite():
    # pack.state changing must NOT invalidate the meta: they describe different
    # things, and conflating them would make every hidden question look like a
    # pack replacement.
    with tempfile.TemporaryDirectory() as tmp:
        p = make(tmp, 6)
        M.write_meta(p, M.build(p))
        st = PF.state_path(p)
        PF.write_state(st, 6)
        PF.set_flag(st, 2, PF.FLAGGED)
        check("flagging a question leaves the meta valid", M.read_meta(p) is not None)


if __name__ == "__main__":
    for fn in (test_id_is_derived, test_manifest_matches_the_pack, test_index_map,
               test_meta_never_guesses, test_meta_count_check_is_not_dead_code,
               test_meta_survives_a_state_rewrite):
        print(fn.__name__)
        fn()
    print()
    if FAILED:
        print(f"FAILED: {len(FAILED)}")
        for n in FAILED:
            print(f"  - {n}")
        sys.exit(1)
    print("all ok")

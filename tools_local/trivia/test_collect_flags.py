#!/usr/bin/env python3
"""What collect_flags.py must REFUSE, written as the corpus damage it prevents.

    python3 tools_local/trivia/test_collect_flags.py

Standard library only, builds its own packs, needs no corpus, never skips.

Every case here is a way to delete a question nobody reported. That is the
whole risk surface of this tool: a verdict is applied by build_pack.py and
assemble_pack.py without further review, and a question dropped for a bad
verdict comes out as a pack one row smaller with no error anywhere. So the
tests are almost all refusals, and each was written by reverting the check and
watching it go red.
"""

import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import collect_flags as CF  # noqa: E402
import pack_format as PF  # noqa: E402
import reports as RP  # noqa: E402

FAILED = []


def check(name, ok, detail=""):
    print(f"  [{'ok  ' if ok else 'FAIL'}] {name}{('  -- ' + detail) if detail and not ok else ''}")
    if not ok:
        FAILED.append(name)


def refuses(name, fn, expect=None):
    try:
        fn()
    except CF.Refused as err:
        if expect and expect not in str(err):
            check(name, False, f"refused, but for {str(err)[:70]!r}")
        else:
            check(name, True)
        return
    except Exception as err:  # noqa: BLE001
        check(name, False, f"raised {type(err).__name__}: {err}")
        return
    check(name, False, "did NOT refuse")


def items(n):
    return [
        {"d": (i % 5) + 1, "y": 2000, "q": f"Question number {i}", "a": f"Answer {i}", "alt": [], "w": []}
        for i in range(n)
    ]


def build(tmp, n=6, flags=(), name="pack.dat"):
    pack = os.path.join(tmp, name)
    PF.write(items(n), pack)
    state = PF.state_path(pack)
    PF.write_state(state, n)
    for i in flags:
        PF.set_flag(state, i, PF.FLAGGED)
    return pack, state


# --- the staleness refusal, which is the one that matters ---------------------
def test_stale_state():
    with tempfile.TemporaryDirectory() as tmp:
        pack, state = build(tmp, 6, flags=(1, 4))

        rows, _ = CF.collect(pack)
        check("a matching state file collects its flags", sorted(r[3] for r in rows) == [1, 4],
              f"got {[r[3] for r in rows]}")

        # SHORTER: the pack grew under the state file.
        with open(state, "r+b") as f:
            f.truncate(5)
        refuses("a SHORTER state file is refused", lambda: CF.collect(pack), "replaced under")

        # LONGER is the case that actually happens: a rated pack is smaller than
        # the 50,000 it replaced. A tool that only checked `<` would sail past it.
        PF.write_state(state, 6)
        PF.set_flag(state, 1, PF.FLAGGED)
        with open(state, "ab") as f:
            f.write(b"\0" * 3)
        refuses("a LONGER state file is refused too", lambda: CF.collect(pack), "replaced under")


# --- ids ---------------------------------------------------------------------
def test_ids():
    with tempfile.TemporaryDirectory() as tmp:
        pack, _ = build(tmp, 4, flags=(2,))
        rows, notes = CF.collect(pack)
        check("re-derived id matches build_pack's key",
              rows[0][0] == CF.rederive("Question number 2"), f"got {rows[0][0]}")
        check("re-deriving is reported, not silent",
              any("RE-DERIVED" in n for n in notes), f"notes={notes}")

        # A manifest is the whole point: it pins the id as it was on build day,
        # so a later clue repair cannot move it.
        man = os.path.join(tmp, "pack.idx.tsv")
        with open(man, "w", encoding="utf-8") as f:
            f.write("# index\tid\n")
            for i in range(4):
                f.write(f"{i}\tstickyid{i:04d}\n")
        rows, notes = CF.collect(pack, manifest_path=man)
        check("a manifest id wins over re-derivation", rows[0][0] == "stickyid0002", f"got {rows[0][0]}")
        check("with a manifest nothing is re-derived", not any("RE-DERIVED" in n for n in notes))

        short = os.path.join(tmp, "short.tsv")
        with open(short, "w", encoding="utf-8") as f:
            f.write("0\taaa\n1\tbbb\n")
        refuses("a manifest for a different build is refused",
                lambda: CF.collect(pack, manifest_path=short), "different build")


# --- the reasons queue -------------------------------------------------------
def test_queue():
    with tempfile.TemporaryDirectory() as tmp:
        pack, _ = build(tmp, 6, flags=(1, 3))
        q = os.path.join(tmp, "reports.dat")

        RP.write(q, "2026-09-05a", 6, [(1, RP.CODES["wrong"]), (3, RP.CODES["giveaway"])])
        rows, _ = CF.collect(pack, queue_path=q)
        got = {r[3]: r[2] for r in rows}
        check("reasons join to their flags by index", got == {1: "wrong", 3: "giveaway"}, f"got {got}")

        # A flag with no queue entry is still a verdict. Mario's rule: a report
        # with no reason is still a report.
        RP.write(q, "2026-09-05a", 6, [(1, RP.CODES["wrong"])])
        rows, _ = CF.collect(pack, queue_path=q)
        got = {r[3]: r[2] for r in rows}
        check("a flag with no reason still collects", got == {1: "wrong", 3: "none"}, f"got {got}")

        # THE ONE THE DEVICE CANNOT SEE FOR ITSELF: a same-count replacement keeps
        # pack.state, so only the queue's own header can catch it.
        RP.write(q, "2026-09-05a", 99, [(1, RP.CODES["wrong"])])
        refuses("a queue filed against another pack size is refused",
                lambda: CF.collect(pack, queue_path=q), "Refusing rather than re-labelling")

        RP.write(q, "2026-09-05a", 6, [(1, RP.CODES["wrong"])])
        with open(q, "ab") as f:
            f.write(b"\x01\x02\x03")  # a torn tail
        refuses("a torn queue is refused, not truncated to the last whole entry",
                lambda: CF.collect(pack, queue_path=q), "not a multiple")


def test_queue_format():
    with tempfile.TemporaryDirectory() as tmp:
        q = os.path.join(tmp, "r.dat")
        RP.write(q, "abc", 10)
        RP.append(q, 4, RP.CODES["broken"])
        pack_id, count, entries = RP.read(q)
        check("header round-trips", (pack_id, count) == ("abc", 10), f"got {pack_id!r},{count}")
        check("append lands one entry", entries == [(4, "broken")], f"got {entries}")

        RP.write(q, "abc", 10, [(11, RP.CODES["wrong"])])
        refuses_rp("an index past the pack is refused", lambda: RP.read(q), "past the pack")

        with open(q, "r+b") as f:
            f.write(b"XXXXXXXX")
        refuses_rp("a foreign file is refused", lambda: RP.read(q), "not a report queue")

        # 0 is a real code, not a hole: it is "reported, no reason given".
        check("code 0 is a defined reason", RP.REASONS[0] == "none")
        check("every code is distinct", len(set(RP.REASONS.values())) == len(RP.REASONS))


def refuses_rp(name, fn, expect):
    try:
        fn()
    except RP.Refused as err:
        check(name, expect in str(err), f"refused for {str(err)[:70]!r}")
        return
    except Exception as err:  # noqa: BLE001
        check(name, False, f"raised {type(err).__name__}: {err}")
        return
    check(name, False, "did NOT refuse")


# --- idempotency -------------------------------------------------------------
def test_apply_is_idempotent():
    with tempfile.TemporaryDirectory() as tmp:
        pack, _ = build(tmp, 5, flags=(0, 2))
        verd = os.path.join(tmp, "verdicts.tsv")
        with open(verd, "w", encoding="utf-8") as f:
            f.write("# header\n")

        argv = [pack, "--verdicts", verd, "--apply"]
        CF.main(argv)
        first = open(verd, encoding="utf-8").read().count("\n")
        CF.main(argv)
        second = open(verd, encoding="utf-8").read().count("\n")
        check("a second run appends nothing", first == second, f"{first} then {second}")
        check("the first run appended both flags", first == 3, f"{first} lines")

        # A verdicts file with no trailing newline must not have its last row
        # joined to the first new one.
        with open(verd, "w", encoding="utf-8") as f:
            f.write("# header\nffffffffffff\tbad\thand-written")
        CF.main(argv)
        lines = [ln for ln in open(verd, encoding="utf-8").read().split("\n") if ln.strip()]
        check("a missing trailing newline does not fuse two rows",
              all(ln.startswith("#") or ln.count("\t") >= 2 for ln in lines) and len(lines) == 4,
              f"lines={lines}")


def test_refusal_exit_code():
    with tempfile.TemporaryDirectory() as tmp:
        pack, state = build(tmp, 4, flags=(1,))
        with open(state, "r+b") as f:
            f.truncate(2)
        check("a refusal exits non-zero", CF.main([pack]) == 2)


if __name__ == "__main__":
    for fn in (test_stale_state, test_ids, test_queue, test_queue_format,
               test_apply_is_idempotent, test_refusal_exit_code):
        print(fn.__name__)
        fn()
    print()
    if FAILED:
        print(f"FAILED: {len(FAILED)}")
        for name in FAILED:
            print(f"  - {name}")
        sys.exit(1)
    print("all ok")

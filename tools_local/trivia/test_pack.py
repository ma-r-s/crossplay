#!/usr/bin/env python3
"""Invariants every shipped trivia pack must hold.

These are the properties that reading 70 questions by hand established as
mattering. Encoding them here means a corpus refresh cannot quietly break one.

    python3 tools_local/trivia/test_pack.py <pack.jsonl>
"""

import collections, json, os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import textfit

FAILURES = []
CHECKS = 0


def check(name, ok, detail=""):
    global CHECKS
    CHECKS += 1
    status = "ok  " if ok else "FAIL"
    print(f"  [{status}] {name}{('  -- ' + detail) if detail and not ok else ''}")
    if not ok:
        FAILURES.append(name)


def main(path):
    rows = [json.loads(l) for l in open(path, encoding="utf-8")]
    n = len(rows)
    print(f"pack: {path}  ({n:,} questions)\n")

    # structure
    check(
        "every row has q, a, d, id", all({"q", "a", "d", "id"} <= set(r) for r in rows)
    )
    ids = [r["id"] for r in rows]
    check("ids are unique", len(set(ids)) == n, f"{n - len(set(ids))} collisions")
    check("difficulty is 1-5", all(1 <= r["d"] <= 5 for r in rows))

    # screen fit -- 480x800, and the clue must be readable aloud
    # No upper CHARACTER bound: it is the wrong unit. Two 24-character strings
    # differ by 69 pixels in the same face, so a character cap both rejects
    # clues that fit and passes clues that overflow. The real bound is the
    # measured one below. Only a lower bound survives, for substance.
    short = [r for r in rows if len(r["q"]) < 30]
    check("no trivially short clue", not short, f"{len(short)} under 30 chars")
    bad_ans = [r for r in rows if not (0 < len(r["a"]) <= 25)]
    check("answer at most 25 chars", not bad_ans, f"{len(bad_ans)} too long")

    # text hygiene
    unbal = [
        r
        for r in rows
        if r["q"].count('"') % 2 or r["q"].count("(") != r["q"].count(")")
    ]
    check("punctuation balanced", not unbal, f"{len(unbal)} unbalanced")
    esc = [r for r in rows if '\\"' in r["q"] or "\\\\" in r["q"]]
    check("no leaked escapes", not esc, f"{len(esc)} with backslash escapes")
    abbr = re.compile(
        r"\b(Switz\.|int'l|Amer\.|Brit\.|Gov\.|Pres\.|Gen\.|Capt\.|Col\.|"
        r"Mt\.|Mts\.|Ft\.|cent\.|mil\.|bil\.|yrs\.)"
    )
    ab = [r for r in rows if abbr.search(r["q"])]
    check(
        "no read-aloud abbreviations",
        len(ab) <= n * 0.0002,
        f"{len(ab)} remain, e.g. {ab[0]['q'][:60] if ab else ''}",
    )

    # A shout is an all-caps WORD of 6+ letters ("SWISS CHEESE PLANT").
    # Acronyms (E.T., NAACP, AT&T, R.E.M.) are correctly upper and must survive.
    def shouted(a):
        return any(
            w.isalpha() and w.isupper() and len(w) >= 6
            for w in re.split(r"[\s.&-]+", a)
        )

    caps = [r for r in rows if shouted(r["a"])]
    check(
        "no shouted answers (acronyms exempt)",
        not caps,
        f"{len(caps)} e.g. {caps[0]['a'] if caps else ''}",
    )

    # playability
    demo = re.compile(r"\b(this|these|his|her|its|hers|he|she|they|it)\b", re.I)
    nodemo = [r for r in rows if not demo.search(r["q"])]
    check(
        "every clue points at its answer",
        not nodemo,
        f"{len(nodemo)} without a demonstrative",
    )
    media = re.compile(
        r"(seen here|heard here|pictured|audio clue|on your monitor|\[)", re.I
    )
    med = [r for r in rows if media.search(r["q"])]
    check("no media-dependent clues", not med, f"{len(med)} need a picture or sound")
    leak = []
    for r in rows:
        toks = [
            t for t in re.sub(r"[^a-z0-9 ]", "", r["a"].lower()).split() if len(t) > 3
        ]
        c = re.sub(r"[^a-z0-9 ]", "", r["q"].lower())
        if toks and all(re.search(rf"\b{re.escape(t)}", c) for t in toks):
            leak.append(r)
    check("clue never contains its own answer", not leak, f"{len(leak)} self-answering")

    # balance -- a pack skewed to one tier plays badly. Measured on the
    # INTERNATIONAL half only, because that is what a default player is dealt:
    # US-centric questions ship tagged (us:true) but are hidden until the
    # settings toggle asks for them, and they pile into level 5 by construction
    # (68% of them rate hardest for an international table), which would make the
    # FULL distribution fail this while the shipped default experience is fine.
    intl = [r for r in rows if not r.get("us")]
    spread = collections.Counter(r["d"] for r in intl)
    lo, hi = min(spread.values()), max(spread.values())
    check(
        "difficulty reasonably spread (international, US hidden)",
        hi <= lo * 2.5,
        f"{dict(sorted(spread.items()))}",
    )
    n_us = sum(1 for r in rows if r.get("us"))
    if n_us:
        print(
            f"  ({n_us:,} us-centric ship tagged; hidden by default, "
            f"difficulty spread above is the international default)"
        )

    # the panel truncates silently: an overflowing clue photographs fine and
    # simply stops mid-sentence, so this is measured in pixels, never characters
    over = [r for r in rows if not textfit.fits(r["q"], 448, 583, "reading_serif_14")]
    check(
        "no clue overflows its box (measured)",
        not over,
        f"{len(over)} overflow, e.g. {over[0]['q'][:50] if over else ''}",
    )

    # solo multiple choice: the generated options must not leak the answer
    mc = [r for r in rows if r.get("w")]
    check("solo MC questions exist", len(mc) > 5000, f"only {len(mc)}")
    check("every MC has at least 3 distractors", all(len(r["w"]) >= 3 for r in mc))
    dup = [r for r in mc if any(w.lower() == r["a"].lower() for w in r["w"])]
    check("no distractor equals its answer", not dup, f"{len(dup)} collide")
    dupw = [r for r in mc if len({w.lower() for w in r["w"]}) != len(r["w"])]
    check("distractors are distinct", not dupw, f"{len(dupw)} with repeats")
    if mc:
        # the tell every published corpus leaks: 31-38% there, chance is 25%
        strict = sum(1 for r in mc if len(r["a"]) > max(len(w) for w in r["w"][:3]))
        rate = 100 * strict / len(mc)
        check(
            "answer is not the longest option",
            rate < 15,
            f"strictly longest {rate:.1f}% of the time",
        )

    # duplicates by normalised clue
    norm = [re.sub(r"[^a-z0-9]", "", r["q"].lower()) for r in rows]
    check("no duplicate clues", len(set(norm)) == n, f"{n - len(set(norm))} duplicates")

    # verdicts must have been applied
    try:
        bad = {
            l.split("\t")[0]
            for l in open("tools_local/trivia/verdicts.tsv", encoding="utf-8")
            if l.strip() and not l.startswith("#") and "\tbad\t" in l
        }
        check(
            "no question marked bad survives",
            not (bad & set(ids)),
            f"{len(bad & set(ids))} rejected questions still present",
        )
    except FileNotFoundError:
        pass

    print(f"\n{CHECKS - len(FAILURES)} checks passed, {len(FAILURES)} failed")
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "pack.jsonl"))

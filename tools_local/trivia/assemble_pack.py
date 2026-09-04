#!/usr/bin/env python3
"""Assemble a shippable trivia pack from the decoded corpus plus a local rating run.

build_pack.py cannot do this, and that is why this file exists. build_pack.py
rebuilds from the Jeopardy season TSV (544k rows, not in this repo and not on
this machine) and derives difficulty from the clue's DOLLAR VALUE. A local
rating run produces something the dollar value cannot express -- how hard the
question actually is for a group of people in a bar -- and there was no path
from that number to a pack at all. Adding a source-less second mode to
build_pack.py would have made --src conditionally required and left its whole
filter body dead in that mode, so the two builds are two files:

    build_pack.py     Jeopardy TSV     -> pack   (difficulty from dollar value)
    assemble_pack.py  corpus + ratings -> pack   (difficulty from the rater)

Inputs:

  --corpus    the decoded pack, one JSON object per line, carrying the fields
              the device already ships: d, y, q, a, alt, w, id
  --enriched  enrich_pack.py's output: id, r, w, bad, us, topic, n_gen, n_kept
              r is 0-10 on the international scale, HIGH r meaning EASY.

    python3 tools_local/trivia/assemble_pack.py \\
        --corpus .rate/corpus_repaired.jsonl \\
        --enriched .rate/enriched.jsonl \\
        --out pack.jsonl --dat pack.dat
    python3 tools_local/trivia/test_pack.py pack.jsonl

US-centric questions are DROPPED BY DEFAULT (rule 4). `--keep-us` builds the
inclusive pack from the same inputs; nothing is deleted from the corpus or from
the ratings, so a future in-app toggle needs no re-rating.

Four rules here were each paid for. Do not quietly undo any of them; the tests
in test_assemble.py exist to make undoing one loud.
"""

import argparse
import collections
import hashlib
import json
import os
import random
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_pack
import distractors


# --- rule 1: the corpus's stored id is the join key --------------------------
# Pack ids are a sha1 of normalised clue text, and build_pack.py re-derives them
# from the text it is holding. That is right when the text IS the text the id
# was made from, and wrong here: corpus_repaired.jsonl deliberately carries
# PRE-repair ids beside POST-repair clue text, because the pack is addressed by
# index and moving an id would move a question out from under pack.state. 349
# rows are in that state. Re-deriving drops their ratings, and an unrated
# question is simply excluded from the pack, so the loss shows up as nothing at
# all -- no error, no warning, a slightly smaller pack. Board card #146.
#
# So: join on the stored id, and REPORT what re-deriving would have cost, which
# is the only way that number is ever visible.
def rederive(clue):
    return hashlib.sha1(re.sub(r"[^a-z0-9]", "", clue.lower()).encode()).hexdigest()[
        :12
    ]


# --- rule 3: r -> d by fixed absolute thresholds, never by quantile ----------
# Each level is a band on the rater's own 0-10 scale. Absolute, so "level 3"
# means the same question difficulty in a pack built from 20,000 rated rows and
# in one built from 40,000. Quantile bands would be balanced by construction
# and would silently redefine every level each time the run is re-cut, which
# makes a difficulty setting meaningless across builds.
#
# r is "out of ten groups, how many get it right", so HIGH r is EASY and level
# 1 is the easiest tier -- the same direction as build_pack.py's dollar tiers.
#
# CALIBRATED ON: the INTERNATIONAL half of the rating run, 2026-09-04, 20,063
# surviving rows (25,968 rated, minus the 5,905 flagged us_centric that no
# longer ship). That population is the whole reason these numbers changed. The
# previous floors (9, 7, 5, 3) were chosen when US-centric questions still
# shipped, and for an international table those questions genuinely ARE the
# hard ones: 68% of them rate r<=1, against 2% of the rest. Dropping them took
# level 5 from 5,926 questions to 845 and the pack failed test_pack.py's
# difficulty spread. These floors are the same fixed-constant scheme measured
# against the population that actually ships.
#
# Bands are narrow at the easy end and wide at the hard end because that is
# where the international mass sits, not as a balancing trick: level 1 still
# means "9 or 10 groups in 10 get it" and level 5 still means "at most 4 do".
#
# Re-derive them in one command when the rating run finishes:
#     python3 tools_local/trivia/calibrate_levels.py \
#         --corpus .rate/corpus_repaired.jsonl --enriched .rate/enriched.jsonl
# It prints how these constants stand on the current data and what it would
# choose instead. See docs/trivia-curation.md.
LEVELS = ((9, 1), (8, 2), (7, 3), (5, 4))


def level(r):
    for floor, d in LEVELS:
        if r >= floor:
            return d
    return 5


# --- the longest-option defect ----------------------------------------------
# test_pack.py fails a pack whose answer is strictly the longest of the four
# options more than 15% of the time, because that is a tell a player uses
# without knowing anything about the subject. Model-generated options fail it
# at 20.7%. The rule-based picker, head to head on the same 7,768 questions,
# is at 4.2%.
#
# The cause is SPREAD, not bias. Mean option length is 7.5 characters against a
# 7.7-character answer in both, so the model is not writing short options; it
# is writing options of scattered lengths (stdev 1.63 against the picker's
# 0.49), and scatter is what leaves the answer alone at the top.
#
# distractors.length_ok is the band the picker already uses, and applying it
# here moves 20.7% -> 15.7%: better, still failing. Banding cannot finish the
# job because "the answer is the longest" is a property of the SET, not of any
# one option -- three individually in-band options can all still be shorter.
#
# So the guarantee is constructed rather than sampled for: every option set
# carries at least one option AT LEAST AS LONG as the answer. The answer is
# then never strictly longest, by construction, on every draw the device can
# make -- not merely on average. A question whose candidates cannot supply one
# keeps its clue and loses its options; it is still playable read-aloud.
def pick_options(answer, alts, model_w, rule_w, want=3):
    """Choose `want` distractors, or [] if no sound set exists.

    model_w comes from the rating run and is topically the better material, so
    it is offered first; rule_w is distractors.choose()'s output for the same
    question and is used to top up. Every returned option is inside the length
    band, and at least one is at least as long as the answer.
    """
    banned = {answer.lower()} | {a.lower() for a in alts}
    pool = []
    for c in list(model_w) + list(rule_w):
        c = distractors.display_case(c)
        if c.lower() in banned:
            continue
        if any(c.lower() == p.lower() for p in pool):
            continue
        if not distractors.length_ok(answer, c):
            continue
        if distractors.odd_case(c) or c[:1].isalpha() != answer[:1].isalpha():
            continue
        if distractors.twins(c, answer) or any(distractors.twins(c, a) for a in alts):
            continue
        if any(distractors.twins(c, p) for p in pool):
            continue
        pool.append(c)

    if len(pool) < want:
        return []
    covers = [c for c in pool if len(c) >= len(answer)]
    if not covers:
        return []
    # The tightest cover, so the set reads as a tie rather than making the
    # longest option the new tell. Then fill by closeness in length, which is
    # what pulls the spread down toward the picker's.
    lead = min(covers, key=lambda c: (len(c) - len(answer), pool.index(c)))
    rest = sorted(
        (c for c in pool if c is not lead),
        key=lambda c: (abs(len(c) - len(answer)), pool.index(c)),
    )
    return ([lead] + rest)[:want]


# Exactly three are stored, where build_pack.py stores six and lets the device
# draw three. Two reasons, and the trade is deliberate: half the rated rows
# (8,582 of 16,680) have only three sound candidates at all, so six is not
# available for them; and with three stored the set on the panel IS the set
# checked here, which makes the cover guarantee exact instead of a property of
# one sampled draw. The cost is that replaying a question shows the same three
# options. See docs/trivia-curation.md.
STORED = 3


# --- rule 4: US-centric questions do not ship, and are not deleted either -----
# Mario's call, 2026-09-04: "US centric trivia needs to go. At least for now.
# Not removed from our data, but for now and until we decide to write a toggle.
# They shouldn't show up."
#
# So the filter lives HERE, at the pack build, and nowhere upstream of it.
# enrich_pack.py still writes `us` as a field rather than a deletion, the
# ratings file still carries every US-centric row with its rating intact, and
# --keep-us rebuilds the inclusive pack from exactly the same inputs. When the
# toggle is written, nothing needs re-rating.
KICK_US_BY_DEFAULT = True


def survivors(corpus, enriched, kick_us=KICK_US_BY_DEFAULT, stats=None):
    """Yield (corpus_row, rating_row) for every question that reaches the pack.

    Split out of assemble() so that calibrate_levels.py measures the population
    that actually SHIPS rather than a second, hand-copied idea of it. A
    calibration run against a different filter would choose thresholds for a
    pack nobody builds, and would look exactly like a correct one.
    """
    if stats is None:
        stats = collections.Counter()
    for x in corpus:
        e = enriched.get(x["id"])
        if e is None:
            stats["unrated (not yet reached by the run)"] += 1
            continue
        if e.get("r") is None:
            stats["rated, but the rater returned no number"] += 1
            continue
        if e.get("bad"):
            stats["rejected: unanswerable"] += 1
            continue
        if kick_us and e.get("us"):
            stats["rejected: us_centric (default; --keep-us to include)"] += 1
            continue
        yield x, e


def assemble(corpus, enriched, kick_us=KICK_US_BY_DEFAULT, seed=20260904):
    """corpus and enriched are lists/dicts of already-parsed rows."""
    stats = collections.Counter()

    # Pass 1: which questions survive, on ratings alone. The rule-based picker
    # draws from the SHIPPED slice, so it has to be indexed over the survivors
    # rather than over the corpus -- an option must be an answer the player
    # could otherwise have met.
    keep = []
    for x, e in survivors(corpus, enriched, kick_us, stats):
        item = {
            "id": x["id"],
            "q": x["q"],
            "a": x["a"],
            "d": level(e["r"]),
            "y": x["y"],
        }
        if x.get("alt"):
            item["alt"] = list(x["alt"])
        item["_model_w"] = list(e.get("w") or [])
        item["_corpus_w"] = list(x.get("w") or [])
        keep.append(item)

    if not keep:
        return [], stats

    # Pass 2: options. The rule-based picker supplies candidates the model did
    # not, which is what lets a set reach three in band.
    rng = random.Random(seed)
    index = distractors.TypeIndex(keep)
    used = collections.Counter()
    order = sorted(range(len(keep)), key=lambda i: rng.random())
    rule = {}
    for i in order:
        picks = distractors.choose(index, keep[i], index.keys[i], used, rng)
        if picks:
            rule[keep[i]["id"]] = picks

    for x in keep:
        model_w = x.pop("_model_w")
        corpus_w = x.pop("_corpus_w")
        # rule 2: fewer than three sound options means NO `w` key at all, not a
        # short one. A two-option set fails test_pack.py's "every MC has at
        # least 3 distractors" and would put two choices on a four-option
        # screen; without the key the question is read-aloud and correct.
        # Topping these up is board card #172.
        w = pick_options(
            x["a"], x.get("alt", ()), model_w, rule.get(x["id"], []) + corpus_w, STORED
        )
        if len(w) >= 3:
            x["w"] = w
            stats["_mc"] += 1
        else:
            stats["_readaloud"] += 1
        x["a"] = distractors.display_case(x["a"])
        if x.get("alt"):
            x["alt"] = [distractors.display_case(a) for a in x["alt"]]

    build_pack.dedash(keep)
    return keep, stats


def load_enriched(path):
    """Last write wins, and duplicates are counted rather than swallowed: the
    file is append-only and resumable, so a duplicate id is a resume artefact
    worth seeing. Same rule as export_enriched.py."""
    out, dupes = {}, 0
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if not line:
            continue
        try:
            r = json.loads(line)
        except ValueError:
            continue
        if "id" not in r:
            continue
        if r["id"] in out:
            dupes += 1
        out[r["id"]] = r
    return out, dupes


def apply_verdicts(corpus, path):
    """Drop every id a person marked `bad`, whatever the rater said about it.

    Hand verdicts outrank the rater in both directions of trust: a person
    looked at the question. Applied before assembly so a `bad` id can never
    reach the pack through a rating that liked it. Shared with
    calibrate_levels.py so both see the same corpus.
    """
    if not path or not os.path.exists(path):
        return list(corpus)
    bad = set()
    for line in open(path, encoding="utf-8"):
        parts = line.strip().split("\t")
        if len(parts) >= 2 and not line.startswith("#") and parts[1].strip() == "bad":
            bad.add(parts[0].strip())
    return [x for x in corpus if x["id"] not in bad]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument(
        "--corpus", required=True, help="decoded pack jsonl: d,y,q,a,alt,w,id"
    )
    ap.add_argument("--enriched", required=True, help="enrich_pack.py output jsonl")
    ap.add_argument("--out", required=True)
    ap.add_argument("--dat", default="", help="also write the on-card binary pack")
    ap.add_argument(
        "--keep-us",
        action="store_true",
        help="include us_centric questions (default: they are dropped)",
    )
    ap.add_argument(
        "--verdicts",
        default="tools_local/trivia/verdicts.tsv",
        help="hand verdicts; a `bad` id is dropped whatever the rater said",
    )
    a = ap.parse_args()

    corpus = [json.loads(l) for l in open(a.corpus, encoding="utf-8") if l.strip()]
    enriched, dupes = load_enriched(a.enriched)
    if not enriched:
        sys.exit(f"{a.enriched}: no usable records")

    before = len(corpus)
    corpus = apply_verdicts(corpus, a.verdicts)
    n_verdict = before - len(corpus)

    pack, stats = assemble(corpus, enriched, kick_us=not a.keep_us)

    # What re-deriving the ids would have cost, printed whether or not it is
    # zero. It is the number card #146 is about and it is invisible everywhere
    # else: a dropped rating just means a question quietly absent from the pack.
    moved = sum(1 for x in corpus if rederive(x["q"]) != x["id"])
    lost = sum(
        1 for x in corpus if x["id"] in enriched and rederive(x["q"]) not in enriched
    )

    with open(a.out, "w", encoding="utf-8") as f:
        for x in pack:
            f.write(json.dumps(x, ensure_ascii=False, separators=(",", ":")) + "\n")

    print(
        f"corpus            : {len(corpus):,}"
        + (f"  ({n_verdict} dropped by verdicts.tsv)" if n_verdict else "")
    )
    print(
        f"ratings           : {len(enriched):,}"
        + (f"  ({dupes} duplicate ids, last kept)" if dupes else "")
    )
    print(f"  ids that moved  : {moved:,} rows carry a pre-repair id")
    print(f"  re-derive would : lose {lost:,} ratings SILENTLY (card #146)")
    for k, v in stats.most_common():
        if not k.startswith("_"):
            print(f"  dropped {v:>7,}  {k}")
    print(f"\npack              : {len(pack):,}")
    print(f"  solo MC ready   : {stats['_mc']:,} ({STORED} stored options each)")
    print(
        f"  read-aloud only : {stats['_readaloud']:,} (no sound option set; card #172)"
    )
    print(f"  with alternates : {sum(1 for x in pack if x.get('alt')):,}")
    print(
        f"  difficulty      : {dict(sorted(collections.Counter(x['d'] for x in pack).items()))}"
    )
    print(f"  size            : {os.path.getsize(a.out) / 1e6:.1f} MB")

    if a.dat:
        import pack_format

        n = pack_format.write(pack, a.dat)
        pack_format.write_state(pack_format.state_path(a.dat), len(pack))
        print(f"  on-card pack    : {a.dat} ({n / 1e6:.2f} MB)")


if __name__ == "__main__":
    main()

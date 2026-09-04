#!/usr/bin/env python3
"""The blind check: a sheet Claude can rate without being able to see, infer or
be told what the local model said about the same question.

WHAT MAKES IT BLIND, and each of these is a way it could have been faked:

  * THE SAMPLE IS DRAWN FROM QUESTIONS CLAUDE HAS NEVER RATED. Re-rating the
    9,617 in difficulty.tsv would measure Claude against Claude's own earlier
    answer, which is a different (and much easier) question than the one asked.
  * THE SAMPLE IS UNIFORM RANDOM over those questions, not stratified by the
    local score. Stratifying to "get the extremes in" widens the x-variance and
    inflates Spearman, so the number would be a property of the sampling and
    not of the rater. This is the correlation the pack would actually get.
  * THE SHEET CARRIES NO SCORE. It is the clue and the answer, numbered, in the
    format rate_pack.py's sheets use. Nothing on it is derived from the local
    rating -- not the order, not the numbering.
  * THE ORDER IS A SEEDED SHUFFLE, INDEPENDENT OF THE LOCAL SCORE, and each
    judge gets a DIFFERENT shuffle of the same items. A rater cannot infer a
    ranking from position, and two judges cannot be compared on position.
  * THE KEY IS NOT IN THE SHEET DIRECTORY. `--key` is written wherever it is
    told to, and the intended use is a directory the rating session is never
    given. The sheet names items 1..N; without the key those numbers join to
    nothing, so a session that read the whole local ratings file still could
    not line it up.

Ratings come back as "<item> <score>" lines; `score` joins them to ids.

    python3 tools_local/trivia/blind_sheet.py make --corpus .rate/corpus.jsonl \\
        --exclude .../difficulty.tsv --pool .rate/local.tsv \\
        --n 150 --judges 2 --out .rate/blind --key .rate/blind-key/key.json
    python3 tools_local/trivia/blind_sheet.py score --key .rate/blind-key/key.json \\
        --raw .rate/blind-key/raw --out .rate/blind-key/claude.tsv
"""

import argparse, collections, json, os, random, re, statistics, sys

PROMPT_HEAD = """Rate trivia questions for a bar quiz. Read the file {sheet} with the Read tool. \
It has {n} numbered items, each a clue and its ANSWER.
"""


def cmd_make(a):
    corpus = [json.loads(l) for l in open(a.corpus, encoding="utf-8")]
    by_id = {x["id"]: x for x in corpus}

    def ids_of(path):
        out = set()
        if not path or not os.path.exists(path):
            return out
        for line in open(path, encoding="utf-8"):
            line = line.strip()
            if line and not line.startswith("#"):
                out.add(line.split("\t")[0].strip())
        return out

    excluded = ids_of(a.exclude)
    # WITHOUT --pool the eligible set is every corpus question the cloud rater
    # has not seen, and the sample is drawn BEFORE any local model has rated
    # it. That is strictly more blind than drawing from local ratings that
    # already exist: at the moment the sheet is written there is no local score
    # for these questions anywhere on disk, so no ordering, no filtering and no
    # sampling decision can have been informed by one. It also decouples the
    # sample from the choice of model, so three candidates are all measured on
    # the same 150 questions rather than on one draw each.
    pool_ids = ids_of(a.pool) if a.pool else set(by_id)
    pool = sorted((pool_ids & set(by_id)) - excluded)
    if len(pool) < a.n:
        sys.exit(f"pool is {len(pool):,}, asked for {a.n}")
    rng = random.Random(a.seed)
    picked = rng.sample(pool, a.n)  # uniform, NOT stratified. See the docstring.

    os.makedirs(a.out, exist_ok=True)
    os.makedirs(os.path.dirname(os.path.abspath(a.key)), exist_ok=True)
    key = {}
    for j in range(a.judges):
        items = list(picked)
        # A per-judge shuffle: same questions, different positions, so neither
        # judge's sheet order carries information about the other's or about
        # the local score.
        random.Random(a.seed * 1000 + j).shuffle(items)
        lines = []
        for i, qid in enumerate(items, 1):
            x = by_id[qid]
            lines.append(f"{i}. {x['q']}\n   ANSWER: {x['a']}\n")
            key[f"{j}:{i}"] = qid
        open(os.path.join(a.out, f"sheet{j}.txt"), "w", encoding="utf-8").write(
            "\n".join(lines)
        )
    json.dump(key, open(a.key, "w"))
    print(
        f"pool {len(pool):,} ("
        f"{'local-rated, ' if a.pool else ''}not in {os.path.basename(a.exclude)})  "
        f"sample {a.n}  judges {a.judges}"
    )
    print(f"  sheets -> {a.out}/sheet0..{a.judges - 1}.txt")
    print(f"  key    -> {a.key}   (keep this away from whoever rates)")


def cmd_score(a):
    key = json.load(open(a.key))
    per = collections.defaultdict(dict)
    for name in sorted(os.listdir(a.raw)):
        m = re.match(r"s(\d+)\.txt$", name)
        if not m:
            continue
        j = int(m.group(1))
        for line in open(os.path.join(a.raw, name), encoding="utf-8"):
            p = line.split()
            if len(p) == 2 and p[0].isdigit() and p[1].lstrip("-").isdigit():
                qid = key.get(f"{j}:{int(p[0])}")
                if qid:
                    per[j][qid] = max(0, min(10, int(p[1])))
    if not per:
        sys.exit(f"no rated sheets under {a.raw} (expected s0.txt, s1.txt ...)")
    for j in sorted(per):
        print(f"  judge {j}: {len(per[j]):,} items")
    merged = collections.defaultdict(list)
    for j in per:
        for qid, v in per[j].items():
            merged[qid].append(v)
    with open(a.out, "w", encoding="utf-8") as f:
        f.write("# id\trating\tjudges\n")
        for qid in sorted(merged):
            f.write(f"{qid}\t{statistics.mean(merged[qid]):.2f}\t{len(merged[qid])}\n")
    print(f"{len(merged):,} questions -> {a.out}")
    if len(per) > 1:
        for j in sorted(per):
            with open(f"{a.out}.judge{j}", "w", encoding="utf-8") as f:
                f.write("# id\trating\tjudges\n")
                for qid in sorted(per[j]):
                    f.write(f"{qid}\t{per[j][qid]:.2f}\t1\n")
            print(f"  judge {j} alone -> {a.out}.judge{j}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("make")
    m.add_argument("--corpus", required=True)
    m.add_argument(
        "--pool",
        default="",
        help="local ratings TSV to draw from. Omit to draw from the whole corpus, "
        "which is what you want when the cloud judge rates FIRST -- see cmd_make.",
    )
    m.add_argument(
        "--exclude", required=True, help="difficulty.tsv; these are NOT eligible"
    )
    m.add_argument("--out", required=True)
    m.add_argument("--key", required=True)
    m.add_argument("--n", type=int, default=150)
    m.add_argument("--judges", type=int, default=2)
    m.add_argument("--seed", type=int, default=20260904)
    m.set_defaults(fn=cmd_make)
    s = sub.add_parser("score")
    s.add_argument("--key", required=True)
    s.add_argument(
        "--raw", required=True, help="directory of s0.txt, s1.txt ... rated sheets"
    )
    s.add_argument("--out", required=True)
    s.set_defaults(fn=cmd_score)
    a = ap.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()

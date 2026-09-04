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


# A rated file is s<sheet>.txt, or s<sheet>-<judge>.txt when more than one
# judge rated the same sheet. Two judges on one sheet is the measurement that
# says how much of the disagreement is the rater rather than the questions, and
# without the second form the second judge's item numbers resolve against a
# key entry that does not exist: every line drops, and the file scores zero
# items without saying so.
RAW_RE = re.compile(r"s(\d+)(?:-([A-Za-z0-9]+))?\.txt$")


def cmd_score(a):
    key = json.load(open(a.key))
    per = collections.defaultdict(dict)
    for name in sorted(os.listdir(a.raw)):
        m = RAW_RE.match(name)
        if not m:
            continue
        sheet = int(m.group(1))
        judge = f"{sheet}-{m.group(2)}" if m.group(2) else str(sheet)
        got = 0
        for line in open(os.path.join(a.raw, name), encoding="utf-8"):
            p = line.split()
            if len(p) == 2 and p[0].isdigit() and p[1].lstrip("-").isdigit():
                qid = key.get(f"{sheet}:{int(p[0])}")
                if qid:
                    per[judge][qid] = max(0, min(10, int(p[1])))
                    got += 1
        if not got:
            sys.exit(
                f"{name}: not one of its item numbers is in the key under "
                f"sheet {sheet}. Either it rates a sheet this key does not "
                f"describe, or it is a second judge on another sheet and "
                f"should be named s<that sheet>-<judge>.txt."
            )
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
    o = sub.add_parser("options")
    o.add_argument("--corpus", required=True)
    o.add_argument("--enriched", required=True, help="enrich_pack.py jsonl")
    o.add_argument("--out", required=True)
    o.add_argument("--key", required=True)
    o.add_argument("--n", type=int, default=80)
    o.add_argument("--seed", type=int, default=20260904)
    o.set_defaults(fn=cmd_options)
    os_ = sub.add_parser("options-score")
    os_.add_argument("--key", required=True)
    os_.add_argument("--raw", required=True)
    os_.set_defaults(fn=cmd_options_score)
    a = ap.parse_args()
    a.fn(a)




# --- option quality, generated against rule-based -----------------------------
def cmd_options(a):
    """A sheet of four-option questions, half written by the model and half by
    the existing rule-based picker, shuffled together and unlabelled.

    A correlation cannot tell you an option is absurd, and neither can the twin
    check: "Oliver!, Grease, Camelot" beside a RIVER passes every automated
    gate in the pack, because each is a real musical, none is the answer and
    none repeats another. The only instrument that catches it is a person
    reading the four options and asking which they would pick. Mixing the two
    sources into one unlabelled sheet means the reader grades the OPTIONS
    rather than grading a source they were told to be suspicious of, and it
    gives the rule-based picker a fair score on the same scale rather than an
    assumed one.
    """
    corpus = {json.loads(l)["id"]: json.loads(l)
              for l in open(a.corpus, encoding="utf-8")}
    gen = {}
    for line in open(a.enriched, encoding="utf-8"):
        r = json.loads(line)
        if len(r.get("w") or []) == 3:
            gen[r["id"]] = r["w"]
    rng = random.Random(a.seed)
    gen_ids = sorted(gen)
    rng.shuffle(gen_ids)
    gen_ids = gen_ids[: a.n // 2]
    # The rule-based half comes from questions that ALREADY ship three or more
    # options, which is the live comparison: what a player sees today.
    rule_ids = sorted(i for i, x in corpus.items()
                      if len(x.get("w") or []) >= 3 and i not in gen)
    rng.shuffle(rule_ids)
    rule_ids = rule_ids[: a.n - len(gen_ids)]

    rows = ([(i, gen[i], "gen") for i in gen_ids]
            + [(i, corpus[i]["w"][:3], "rule") for i in rule_ids])
    rng.shuffle(rows)
    lines, key = [], {}
    for n, (qid, wrong, src) in enumerate(rows, 1):
        x = corpus[qid]
        opts = list(wrong) + [x["a"]]
        rng.shuffle(opts)                      # the answer is not always last
        lines.append(f"{n}. {x['q']}\n" + "".join(f"   - {o}\n" for o in opts))
        key[str(n)] = {"id": qid, "src": src, "answer": x["a"]}
    os.makedirs(os.path.dirname(os.path.abspath(a.out)) or ".", exist_ok=True)
    os.makedirs(os.path.dirname(os.path.abspath(a.key)) or ".", exist_ok=True)
    open(a.out, "w", encoding="utf-8").write("\n".join(lines))
    json.dump(key, open(a.key, "w"))
    print(f"{len(rows)} questions ({len(gen_ids)} generated, {len(rule_ids)} rule-based), "
          f"sources shuffled together and NOT marked")
    print(f"  sheet -> {a.out}\n  key   -> {a.key}")


def cmd_options_score(a):
    key = json.load(open(a.key))
    verdicts = {}
    for line in open(a.raw, encoding="utf-8"):
        p = line.split()
        if len(p) >= 2 and p[0].rstrip(".").isdigit():
            v = p[1].lower().strip(".,")
            if v in ("good", "weak", "broken"):
                verdicts[p[0].rstrip(".")] = v
    tally = collections.defaultdict(collections.Counter)
    for n, v in verdicts.items():
        if n in key:
            tally[key[n]["src"]][v] += 1
    print(f"{len(verdicts)} graded")
    for src in sorted(tally):
        t = tally[src]
        n = sum(t.values())
        print(f"  {src:<5} n={n:3d}  "
              + "  ".join(f"{k} {t[k]:3d} ({100*t[k]/n:4.1f}%)"
                          for k in ("good", "weak", "broken")))


if __name__ == "__main__":
    main()

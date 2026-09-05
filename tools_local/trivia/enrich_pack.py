#!/usr/bin/env python3
"""One local pass over the pack: rating, three wrong options, two verdicts, a topic.

WHY ONE PASS. Getting the question into a 14B's context is the expensive part;
asking it for five things instead of one costs output tokens, not a second
fourteen-hour run. Only 18,386 of the 50,000 carry three wrong options, so a
third of the corpus cannot be played in solo mode, and a rating-only pass walks
past every one of them.

WHY A MODEL WRITES THE OPTIONS AND A REGEX DOES NOT. The shipped distractors
are rule-generated, and the rule types the answer by taking the first lowercase
word after "this" -- so "this musical river" was typed as a musical and offered
Oliver!, Grease and Camelot beside it. Plausible-but-wrong is the one thing a
language model is better at than a regex.

THE HARD CONSTRAINT, and it is the whole reason this file is careful: A WRONG
OPTION THAT IS ALSO CORRECT IS THE WORST DEFECT IN THE PACK. It is worse than
having no options at all, because the question becomes unwinnable and still
looks fine. Hindu/Hinduism, Eye/Eyes and Bicycle/Bicycles are caught by
stemming; Holland/Netherlands shares no letters with its twin and is caught
only by an alias table. Both live in distractors.twins(), which is the SAME
function the rule-based picker is gated on, so a generated option clears
exactly the bar a rule-generated one clears. Anything that fails is DROPPED.
A question that ends the pass without three clean options KEEPS WHAT IT HAD;
this file never ships a set it could not validate, and never pads one.

`bad` and `us_centric` are written as FIELDS, not applied as deletions. The
pack build filters on them, so the decision is one switch and reversible, and
a US-inclusive pack for an American player remains possible from the same data.

    python3 tools_local/trivia/enrich_pack.py --corpus .rate/corpus.jsonl \\
        --out .rate/enriched.jsonl --model qwen2.5:14b-instruct-q4_K_M
"""

import argparse, json, os, queue, sys, threading, time
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import distractors
from rate_local import SCALE_INTL, build_shots, expected_value, load_corpus

TASK = """You are preparing questions for an international bar quiz.

{scale}

You will also do four other things for the same question.

WRONG OPTIONS. Give exactly three wrong answers to sit beside the real one in a
four-option multiple choice. Rules, and the first one is absolute:

 1. A wrong option must be WRONG. Never give another name for the right answer,
    never a plural or singular of it, never a broader or narrower form of it.
    If the answer is Netherlands, "Holland" is the same country and is banned.
    If the answer is Eye, "Eyes" is banned.
 2. Each must be the SAME KIND OF THING as the answer. A country for a country,
    a colour for a colour, a composer for a composer, a year for a year. An
    option of the wrong kind gives the answer away without any knowledge.
 3. Each must be roughly the same length and written the same way as the
    answer. If the answer is two words, prefer two words.
 4. They must be plausible: things a person could believe, not jokes, and not
    so obscure that only the right answer is recognisable.
 5. Do not repeat each other.

BAD. true when the clue cannot be answered as written: it needs a category or
board the player never sees ("3 of the 4 people honored in 1901 & 1902 were
from this neutral country" only works if you are told the subject is the Nobel
prizes), or it is self-referential, or the text is corrupted ("U.South Army"
for "U.S. Army"), or the stated answer simply does not follow from the clue.
Otherwise false.

US_CENTRIC. true when producing the answer requires knowing something specific
to the United States: its states, cities, politics, civics, wars, sports,
brands, television, or an American turn of phrase. False for anything a person
anywhere could know.

TOPIC. One or two words naming the subject, lowercase, from an ordinary quiz
vocabulary: history, geography, science, literature, music, film, sport, art,
religion, food, language, technology, politics, mythology, television, business.

Answer with JSON only."""

SCHEMA = {
    "type": "object",
    "properties": {
        "rating": {"type": "integer", "minimum": 0, "maximum": 10},
        "wrong": {
            "type": "array",
            "items": {"type": "string"},
            "minItems": 3,
            "maxItems": 3,
        },
        "bad": {"type": "boolean"},
        "us_centric": {"type": "boolean"},
        "topic": {"type": "string"},
    },
    "required": ["rating", "wrong", "bad", "us_centric", "topic"],
}

USER = "{q}\nANSWER: {a}"


def validate(item, options):
    """Keep only options that are wrong, distinct, and not the answer again.

    Returns (kept, dropped) where dropped says WHY, because "the survival rate
    was 71%" is not actionable and "61% of the losses were twins of the answer"
    is. Gated on distractors.twins(), the same function the rule-based picker
    uses, against the answer AND every alternate the pack already accepts --
    an item's `alt` list is the set of strings the device marks correct, so an
    option matching one of those is a correct answer wearing another name.
    """
    answer = item["a"]
    accepted = [answer] + list(item.get("alt") or [])
    kept, dropped = [], []
    for o in options:
        o = (o or "").strip()
        if not o:
            dropped.append(("empty", o))
            continue
        hit = next((a for a in accepted if distractors.twins(o, a)), None)
        if hit is not None:
            dropped.append((f"twin of accepted answer {hit!r}", o))
            continue
        if any(distractors.twins(o, k) for k in kept):
            dropped.append(("duplicate of another option", o))
            continue
        kept.append(o)
    return kept, dropped


class Client:
    def __init__(self, host, model, shots=(), num_ctx=4096, timeout=300):
        self.url = host.rstrip("/") + "/api/chat"
        self.model = model
        self.shots = list(shots)
        self.num_ctx = num_ctx
        self.timeout = timeout

    def ask(self, q, a):
        body = {
            "model": self.model,
            "messages": (
                [{"role": "system", "content": TASK.format(scale=SCALE_INTL)}]
                + self.shots
                + [{"role": "user", "content": USER.format(q=q, a=a)}]
            ),
            "stream": False,
            "think": False,
            "format": SCHEMA,
            "options": {
                "temperature": 0.0,
                "top_p": 1.0,
                "num_predict": 200,
                "num_ctx": self.num_ctx,
                "seed": 7,
            },
        }
        req = urllib.request.Request(
            self.url,
            data=json.dumps(body).encode(),
            headers={"Content-Type": "application/json"},
        )
        with urllib.request.urlopen(req, timeout=self.timeout) as r:
            out = json.loads(r.read())
        txt = (out.get("message") or {}).get("content", "")
        return json.loads(txt), out.get("eval_count") or 0


def run(a):
    corpus = load_corpus(a.corpus)
    by_id = {x["id"]: x for x in corpus}
    done = set()
    if os.path.exists(a.out):
        for line in open(a.out, encoding="utf-8"):
            try:
                done.add(json.loads(line)["id"])
            except (ValueError, KeyError):
                pass
    only = None
    if a.only:
        only = {l.strip() for l in open(a.only, encoding="utf-8") if l.strip()}
    todo = [
        x for x in corpus if x["id"] not in done and (only is None or x["id"] in only)
    ]
    if a.limit:
        todo = todo[: a.limit]
    shots = ()
    if a.shots:
        excl = {x["id"] for x in todo}
        for f in a.shots_exclude:
            excl |= {l.strip() for l in open(f, encoding="utf-8") if l.strip()}
        shots, ids = build_shots(a.shots_from, a.shots, excl, corpus)
        # The rating examples are single integers; this task answers JSON. A
        # mismatched example teaches the wrong output shape, so the assistant
        # turns are rewritten as JSON with only the rating filled in.
        shots = [
            m
            if m["role"] == "user"
            else {
                "role": "assistant",
                "content": json.dumps({"rating": int(m["content"])}),
            }
            for m in shots
        ]
        print(f"  {len(ids)} rating examples, rewritten as JSON", flush=True)
    print(
        f"corpus {len(corpus):,}  already done {len(done):,}  to do {len(todo):,}  "
        f"model {a.model}  workers {a.workers}",
        flush=True,
    )
    if not todo:
        return 0

    client = Client(a.host, a.model, shots, a.num_ctx)
    work = queue.Queue()
    for x in todo:
        work.put(x)
    lock = threading.Lock()
    out = open(a.out, "a", encoding="utf-8")
    bad = open(a.out + ".bad", "a", encoding="utf-8")
    st = {
        "n": 0,
        "err": 0,
        "tok": 0,
        "kept3": 0,
        "opts": 0,
        "drop": 0,
        "isbad": 0,
        "isus": 0,
        "t0": time.time(),
    }
    reasons = {}

    def worker():
        while True:
            try:
                x = work.get_nowait()
            except queue.Empty:
                return
            try:
                d, ntok = client.ask(x["q"], x["a"])
            except (urllib.error.URLError, OSError, ValueError) as e:
                with lock:
                    st["err"] += 1
                    bad.write(f"{x['id']}\tERROR\t{e}\n")
                    bad.flush()
                continue
            kept, dropped = validate(x, d.get("wrong") or [])
            rec = {
                "id": x["id"],
                "r": d.get("rating"),
                "w": kept,
                "bad": bool(d.get("bad")),
                "us": bool(d.get("us_centric")),
                "topic": (d.get("topic") or "").strip().lower(),
                "n_gen": len(d.get("wrong") or []),
                "n_kept": len(kept),
            }
            with lock:
                st["n"] += 1
                st["tok"] += ntok
                st["opts"] += len(kept)
                st["drop"] += len(dropped)
                st["kept3"] += 1 if len(kept) == 3 else 0
                st["isbad"] += 1 if rec["bad"] else 0
                st["isus"] += 1 if rec["us"] else 0
                for why, o in dropped:
                    key = why.split(" of ")[0]
                    reasons[key] = reasons.get(key, 0) + 1
                    bad.write(f"{x['id']}\tDROPPED\t{why}\t{o!r}\n")
                out.write(json.dumps(rec, ensure_ascii=False) + "\n")
                out.flush()
                bad.flush()
                if st["n"] % a.report == 0:
                    el = time.time() - st["t0"]
                    print(
                        f"  {st['n']:,}/{len(todo):,}  {st['n'] / el:.2f}/s  "
                        f"{st['tok'] / st['n']:.0f} out-tok/q  "
                        f"3 clean options {100 * st['kept3'] / st['n']:.0f}%  "
                        f"bad {100 * st['isbad'] / st['n']:.0f}%  "
                        f"us {100 * st['isus'] / st['n']:.0f}%  "
                        f"err {st['err']}  "
                        f"eta {(len(todo) - st['n']) / (st['n'] / el) / 3600:.1f}h",
                        flush=True,
                    )

    threads = [threading.Thread(target=worker, daemon=True) for _ in range(a.workers)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    out.close()
    bad.close()
    el = time.time() - st["t0"]
    n = max(1, st["n"])
    print(
        f"\n{st['n']:,} questions in {el / 60:.1f} min ({st['n'] / el:.2f}/s), "
        f"{st['err']} errors",
        flush=True,
    )
    print(f"  output tokens per question : {st['tok'] / n:.0f}")
    print(f"  options generated          : {st['opts'] + st['drop']:,}")
    print(
        f"  survived validation        : {st['opts']:,} "
        f"({100 * st['opts'] / max(1, st['opts'] + st['drop']):.1f}%)"
    )
    print(
        f"  questions with 3 clean     : {st['kept3']:,} ({100 * st['kept3'] / n:.1f}%)"
    )
    for why, c in sorted(reasons.items(), key=lambda kv: -kv[1]):
        print(f"    dropped, {why}: {c:,}")
    print(
        f"  kicked bad                 : {st['isbad']:,} ({100 * st['isbad'] / n:.1f}%)"
    )
    print(
        f"  kicked us_centric          : {st['isus']:,} ({100 * st['isus'] / n:.1f}%)"
    )
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--corpus", required=True)
    ap.add_argument(
        "--out", required=True, help="append-only JSONL; also the checkpoint"
    )
    ap.add_argument("--model", default="qwen2.5:14b-instruct-q4_K_M")
    ap.add_argument("--host", default="http://127.0.0.1:11434")
    ap.add_argument("--workers", type=int, default=8)
    ap.add_argument("--num-ctx", type=int, default=4096)
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--report", type=int, default=100)
    ap.add_argument("--only", default="")
    ap.add_argument("--shots", type=int, default=0)
    ap.add_argument("--shots-from", default="")
    ap.add_argument("--shots-exclude", action="append", default=[])
    sys.exit(run(ap.parse_args()))


if __name__ == "__main__":
    main()

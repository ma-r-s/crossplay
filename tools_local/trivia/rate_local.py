#!/usr/bin/env python3
"""Rate trivia difficulty with a LOCAL model, on the scale rate_pack.py defines.

WHY THIS EXISTS. `rate_pack.py` writes sheets a cloud model reads. That is the
right instrument for a few thousand questions and the wrong one for fifty
thousand: the pack is 50,000 questions and only 9,617 of them are rated, so the
other 40,383 have no level and do not ship. This runs the SAME scale through a
model on the laptop, where the only cost is wall time.

THE SCALE IS NOT REDEFINED HERE. The paragraphs below -- the bar, the ten
groups of four, the twenty seconds, the seven anchors, "be harsh", "use the
whole range" -- are `rate_pack.py`'s SCALE_PROMPT, copied word for word. A
local rating and a Claude rating are the same number or they are not
comparable, and the merge in `rate_pack.py` would then average two scales.
The ONE difference is the delivery: a sheet asks for 400 numbered lines from a
file, this asks for one integer about one question, because a 14B model asked
for 400 numbered lines loses the numbering and silently returns 380 answers
aligned to the wrong items.

WHAT IT WRITES. `--out` is an append-only TSV, `id<TAB>score`, flushed per
line. A run that dies at 38,000 resumes by reading it back; nothing is
recomputed. Refusals and unparseable replies go to `<out>.bad` with the raw
text, and are NOT scored -- a rating this file cannot read is missing data, not
a 5.

    python3 tools_local/trivia/rate_local.py \\
        --corpus .rate/corpus.jsonl --out .rate/local.tsv \\
        --model qwen2.5:14b-instruct-q4_K_M --workers 4

The corpus is a jsonl of {id, q, a}. `pack_format.py <pack.dat>` writes one
from the shipped pack; the ids are content-addressed off the clue text, so they
join to `difficulty.tsv` across a corpus refresh.
"""

import argparse, json, math, os, queue, random, re, sys, threading, time
import urllib.error
import urllib.request

# Verbatim from rate_pack.py SCALE_PROMPT: the scale, the anchors and the
# instruction not to cluster. Changing a word here forks the scale.
SCALE = """THE SCALE. Picture ten separate groups of four ordinary adults sitting in a bar. Not trivia \
specialists. Mixed ages, mixed backgrounds, general education, no phones. Someone reads the clue \
aloud. The group has 20 seconds and must produce the answer out loud. There are no multiple choice \
options.

Give ONE integer 0 to 10: how many of those ten groups get it right.

CALIBRATION ANCHORS. Use these to fix the scale:

10  This canal opened in 1914 links the Atlantic and Pacific across Central America -> Panama Canal
 9  The Greek historian Herodotus called this country "The Gift of the Nile" -> Egypt
 7  Missing cowboys looking to start over were often described as "G.T.T.": Gone to this state -> Texas
 5  In February 1861 he was chosen provisional president of the Confederacy -> Jefferson Davis
 3  Her "Death Comes for the Archbishop" was inspired by the letters of the real-life Father Machebeuf -> Willa Cather
 1  His discovery of energy quanta earned him the 1918 Nobel Prize for Physics -> Max Planck
 0  A perjury charge growing out of this evangelist's 1926 disappearance was later dismissed -> Aimee Semple McPherson

Be harsh. Most people know far less than a well-read person assumes. A clue whose only handhold is \
a name the group has never heard is a 0 or 1 even if the answer itself is famous. A clue that names \
something everyone associates with the answer is high even if the topic sounds academic. American \
state capitals, 19th century US politicians, opera, classical composers beyond the three or four \
famous ones, poets, and pre-1960 film and television all score low with a mixed-age general audience.

Use the whole range. Do not cluster everything in the middle.

Answer with the integer alone. No words, no punctuation, no explanation."""

USER = "{q}\nANSWER: {a}"


def build_shots(path, k, exclude, corpus, seed=99):
    """K already-rated questions as prior turns, so the model sees the scale
    applied to THIS corpus rather than only described.

    The seven anchors in SCALE are prose examples chosen by hand; measured on
    600 questions they left qwen2.5-14b compressed into the middle and 0.8
    softer than the ratings they were meant to reproduce. Real items with real
    ratings are the same scale stated in the only form a small model reliably
    copies. They are drawn ONLY from ids the caller excludes from evaluation,
    so nothing being scored has ever been shown as an example, and the draw is
    seeded so every candidate model sees the identical examples in the
    identical order -- a model must not win by getting a friendlier draw.

    The shots are the same on every call and sit directly after the system
    prompt, so they extend the cached prefix instead of costing per question.
    """
    rated = load_ratings_tsv(path)
    by_id = {x["id"]: x for x in corpus}
    pool = sorted((set(rated) & set(by_id)) - set(exclude))
    rng = random.Random(seed)
    # Spread over the scale rather than sampled from it: a uniform draw is
    # mostly 2s and 3s (the corpus is hard), and a model shown eight examples
    # of "2" learns to say 2.
    picked, per = [], max(1, k // 11)
    for band in range(11):
        band_ids = [i for i in pool if round(rated[i]) == band]
        rng.shuffle(band_ids)
        picked += band_ids[:per]
    rng.shuffle(picked)
    msgs = []
    for qid in picked[:k]:
        x = by_id[qid]
        msgs.append({"role": "user", "content": USER.format(q=x["q"], a=x["a"])})
        msgs.append({"role": "assistant", "content": str(int(round(rated[qid])))})
    return msgs, picked[:k]


def load_corpus(path):
    return [json.loads(l) for l in open(path, encoding="utf-8")]


def load_done(path):
    """id -> score for everything already written. The file is the checkpoint."""
    done = {}
    if not os.path.exists(path):
        return done
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        p = line.split("\t")
        if len(p) >= 2:
            try:
                done[p[0]] = float(p[1])
            except ValueError:
                pass
    return done


def load_ratings_tsv(path):
    """id -> float, for difficulty.tsv (id, rating, judges) and for our own out."""
    out = {}
    if not path or not os.path.exists(path):
        return out
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        p = line.split("\t")
        if len(p) >= 2:
            try:
                out[p[0].strip()] = float(p[1])
            except ValueError:
                pass
    return out


SCORE_RE = re.compile(r"-?\d+")


def parse_score(text):
    """First integer in the reply, clamped. None when there is no integer at
    all -- a refusal or a sentence of prose must not become a number."""
    m = SCORE_RE.search(text or "")
    if not m:
        return None
    try:
        v = int(m.group(0))
    except ValueError:
        return None
    if v < 0 or v > 10:
        return None
    return v


def expected_value(logprobs, content):
    """The rating as the mean of the model's own digit distribution.

    WHY NOT JUST TAKE THE DIGIT IT SAID. Greedy decoding returns one of eleven
    integers, and a 14B model does not spread over eleven evenly: measured on
    600 questions it put 30% on "2" and 30% on "5" and NOTHING on "6", so
    thousands of questions the model ranks differently inside itself come out
    with the same number and cannot be ordered at all. The distribution over
    the first token holds that ordering; collapsing it to argmax throws it
    away. This reads the distribution instead: p over the digit tokens,
    renormalised (mass on "Assuming", "If" and the rest is dropped, not scored)
    and averaged.

    THE TWO-DIGIT CASE. 10 arrives as the tokens "1" then "0", so "1" as a
    first token is ambiguous. It is resolved by what the model actually
    generated: only when the whole reply is "10" does the "1" bucket mean ten.
    Ten is rare enough on this scale to make the residual error irrelevant
    (Claude used it 9 times in 9,617), and guessing the other way would put
    a tenth of the mass of every "1" at the top of the scale.
    """
    if not logprobs:
        return None
    top = (logprobs[0] or {}).get("top_logprobs") or []
    ten = (content or "").strip() == "10"
    ps, vs = [], []
    for t in top:
        tok = (t.get("token") or "").strip()
        # `str.isdigit()` is TRUE for the subscript "\u2081" and for every other
        # unicode digit form, and int() then raises on it. The candidate list is
        # ASCII "0".."9" and nothing else.
        if tok in "0123456789" and len(tok) == 1:
            ps.append(math.exp(t["logprob"]))
            vs.append(10 if (tok == "1" and ten) else int(tok))
    tot = sum(ps)
    if tot <= 0:
        return None
    return sum(p * v for p, v in zip(ps, vs)) / tot


class Ollama:
    def __init__(self, host, model, mode="ev", shots=(), timeout=180):
        self.url = host.rstrip("/") + "/api/chat"
        self.model = model
        self.mode = mode
        self.shots = list(shots)
        self.timeout = timeout

    def score(self, q, a):
        """(rating or None, raw text). None means the reply carried no digit at
        all; the caller records it as missing rather than as a number."""
        body = {
            "model": self.model,
            "messages": (
                [{"role": "system", "content": SCALE}]
                + self.shots
                + [{"role": "user", "content": USER.format(q=q, a=a)}]
            ),
            "stream": False,
            "think": False,
            "options": {
                "temperature": 0.0,
                "top_p": 1.0,
                "num_predict": 8,
                "num_ctx": 1024,
                "seed": 7,
            },
        }
        if self.mode == "ev":
            body["logprobs"] = True
            body["top_logprobs"] = 20
        req = urllib.request.Request(
            self.url,
            data=json.dumps(body).encode(),
            headers={"Content-Type": "application/json"},
        )
        with urllib.request.urlopen(req, timeout=self.timeout) as r:
            out = json.loads(r.read())
        txt = (out.get("message") or {}).get("content", "")
        if self.mode == "ev":
            v = expected_value(out.get("logprobs"), txt)
            if v is not None:
                return v, txt
            # No usable distribution (a server that dropped logprobs, or a
            # reply with no digit in its top-20). Fall through to the integer
            # rather than silently returning nothing.
        v = parse_score(txt)
        return (float(v) if v is not None else None), txt


def run(a):
    corpus = load_corpus(a.corpus)
    done = load_done(a.out)
    skip = set(done)
    if a.skip_rated:
        skip |= set(load_ratings_tsv(a.skip_rated))
    only = None
    if a.only:
        only = {l.strip() for l in open(a.only, encoding="utf-8") if l.strip()}
    todo = [
        x for x in corpus if x["id"] not in skip and (only is None or x["id"] in only)
    ]
    if a.limit:
        todo = todo[: a.limit]
    print(
        f"corpus {len(corpus):,}  already in {a.out} {len(done):,}  "
        f"to rate {len(todo):,}  model {a.model}  workers {a.workers}",
        flush=True,
    )
    if not todo:
        return 0

    shots, shot_ids = ((), [])
    if a.shots:
        if not a.shots_from:
            sys.exit("--shots needs --shots-from (a rated TSV to draw examples from)")
        # Excluded from the draw: everything being rated in this run, plus any
        # id file the caller names. An example that is also a question under
        # test is the model reading the answer off its own prompt.
        excl = {x["id"] for x in todo}
        for f in a.shots_exclude:
            excl |= {l.strip() for l in open(f, encoding="utf-8") if l.strip()}
        shots, shot_ids = build_shots(a.shots_from, a.shots, excl, corpus)
        print(f"  {len(shot_ids)} example turns from {os.path.basename(a.shots_from)}, "
              f"none of them under test", flush=True)
    client = Ollama(a.host, a.model, a.mode, shots)
    work = queue.Queue()
    for x in todo:
        work.put(x)
    lock = threading.Lock()
    out = open(a.out, "a", encoding="utf-8")
    bad = open(a.out + ".bad", "a", encoding="utf-8")
    state = {"n": 0, "bad": 0, "err": 0, "t0": time.time()}
    stop = threading.Event()

    def worker():
        while not stop.is_set():
            try:
                x = work.get_nowait()
            except queue.Empty:
                return
            try:
                s, txt = client.score(x["q"], x["a"])
            except (urllib.error.URLError, OSError, json.JSONDecodeError) as e:
                with lock:
                    state["err"] += 1
                    bad.write(f"{x['id']}\tERROR\t{e}\n")
                    bad.flush()
                continue
            with lock:
                if s is None:
                    state["bad"] += 1
                    bad.write(f"{x['id']}\tUNPARSED\t{(txt or '')[:200]!r}\n")
                    bad.flush()
                else:
                    out.write(f"{x['id']}\t{s:.3f}\n")
                    out.flush()
                    state["n"] += 1
                n = state["n"] + state["bad"] + state["err"]
                if n % a.report == 0:
                    el = time.time() - state["t0"]
                    rate = n / el if el else 0
                    left = (len(todo) - n) / rate if rate else 0
                    print(
                        f"  {n:,}/{len(todo):,}  {rate:.2f}/s  "
                        f"bad {state['bad']} err {state['err']}  "
                        f"eta {left / 3600:.1f}h",
                        flush=True,
                    )

    threads = [threading.Thread(target=worker, daemon=True) for _ in range(a.workers)]
    for t in threads:
        t.start()
    try:
        for t in threads:
            t.join()
    except KeyboardInterrupt:
        stop.set()
        print("\ninterrupted; what is written is kept", flush=True)
    out.close()
    bad.close()
    el = time.time() - state["t0"]
    n = state["n"] + state["bad"] + state["err"]
    print(
        f"rated {state['n']:,}  unparsed {state['bad']}  errors {state['err']}  "
        f"in {el / 60:.1f} min  ({n / el if el else 0:.2f}/s)",
        flush=True,
    )
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--corpus", required=True, help="jsonl of {id,q,a}")
    ap.add_argument(
        "--out", required=True, help="append-only id<TAB>score TSV; also the checkpoint"
    )
    ap.add_argument("--model", default="qwen2.5:14b-instruct-q4_K_M")
    ap.add_argument("--host", default="http://127.0.0.1:11434")
    ap.add_argument("--workers", type=int, default=4)
    ap.add_argument("--mode", choices=("ev", "greedy"), default="ev",
                    help="ev: mean of the model's digit distribution (continuous). "
                         "greedy: the single integer it says.")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--report", type=int, default=200)
    ap.add_argument("--shots", type=int, default=0,
                    help="prepend N already-rated questions as example turns")
    ap.add_argument("--shots-from", default="", help="rated TSV to draw the examples from")
    ap.add_argument("--shots-exclude", action="append", default=[],
                    help="id file whose questions must never be used as an example")
    ap.add_argument("--only", default="", help="file of ids; rate only these")
    ap.add_argument(
        "--skip-rated", default="", help="a ratings TSV whose ids to leave alone"
    )
    sys.exit(run(ap.parse_args()))


if __name__ == "__main__":
    main()

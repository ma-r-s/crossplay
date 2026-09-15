#!/usr/bin/env python3
"""Rank the catalog and say what fits on a card.

    rank.py --catalog catalog.jsonl --report library-data.md [--langs en,es]

The method from docs/apps/library-plan.md: every book carries a value (the
probability someone will ever want it; v0 uses Gutenberg's 30-day download
count as the only signal that covers every title) and a weight (the size of
the edition shipped, the text-only EPUB); the card is filled greedily by
value per byte, which is within one book of the optimal knapsack here.

Before ranking, a census: what the catalog contains and what each filter
rule would remove, counted in titles AND in demand, so a rule is judged by
what it costs. The rules are applied after the census, in this order:
Text only, public domain only, has a text-only EPUB, language, form (not a
book that reads on a one-bit panel), one edition per work.

Writes a markdown report: the census, the fit table (card size x slider),
the coverage curve, per-language totals, the top 100, and samples down the
list. Nothing here touches the network.
"""

import argparse
import collections
import json
import re
import unicodedata

CARDS_GB = [16, 32, 64, 128, 256]
SLIDER = [30, 50, 75, 95]
CURVE_N = [100, 500, 1000, 2000, 5000, 10000, 20000, 30000, 40000, 50000]
SAMPLE_RANKS = [500, 1000, 2000, 5000, 10000, 20000, 30000, 40000]
VOLUME_RE = re.compile(r"\b(vol(ume)?|part|book|tome)\.?\s*([0-9]+|[ivxlc]+)\b|\(of \d+\)", re.I)

# Form rules: what does not read on a 480x800 one-bit panel. Each rule is
# named so the census can say what it removes. LC classes: AP periodicals,
# M music, N fine arts (pictures, which the text-only edition strips), QA
# mathematics, Z bibliography and library science.
LOCC_OUT = {"AP", "M", "ML", "MT", "N", "NA", "NB", "NC", "ND", "NE", "NK", "NX", "QA", "Z"}
SUBJECT_OUT = re.compile(
    r"pictorial works|caricatures and cartoons|comic books|photograph|"
    r"periodicals|catalogs|dictionaries|sheet music|atlases|directories",
    re.I,
)
TITLE_OUT = re.compile(
    r"\b(index|catalog(ue)?|bibliography|dictionary|almanac|timetable|"
    r"directory|proceedings|transactions|glossary|concordance|gazetteer)\b|"
    r"\b(vol(ume)?\.? ?\d+|no\.? ?\d+|number \d+|issue \d+)\b.*\b(magazine|journal|review|gazette|weekly|monthly|quarterly|bulletin|punch)\b|"
    r"^(punch|the (atlantic monthly|mirror of literature|nursery|strand magazine|american missionary|continental monthly))",
    re.I,
)


def fold(s):
    s = unicodedata.normalize("NFKD", s or "")
    s = "".join(c for c in s if not unicodedata.combining(c)).lower()
    s = re.sub(r"[^a-z0-9 ]+", " ", s)
    s = re.sub(r"^(the|a|an|le|la|les|el|los|las|der|die|das|il|lo|un|una|une) ", "", s.strip())
    return re.sub(r"\s+", " ", s).strip()


def work_key(row):
    title = row["title"] or ""
    # A subtitle after a colon or a newline is edition dressing, not the work.
    title = re.split(r"[:\n;]", title, maxsplit=1)[0]
    # "Volume 1", "Part 2", "Complete" and the like distinguish editions, not works.
    title = re.sub(r"\b(vol(ume)?|part|book|tome)\.?\s*([0-9]+|[ivxlc]+)\b.*$", "", title, flags=re.I)
    title = re.sub(r",?\s*\b(complete|unabridged|illustrated|annotated)\b\.?\s*$", "", title, flags=re.I)
    creators = [c["name"] or "" for c in row["creators"]]
    surname = fold(creators[0].split(",")[0]) if creators else ""
    lang = row["languages"][0] if row["languages"] else "?"
    return (lang, fold(title)[:60], surname)


def form_rule(row):
    """Return the name of the first form rule the row trips, or None."""
    for code in row["locc"]:
        if code in LOCC_OUT:
            return "locc:" + code
    for s in row["subjects"]:
        if SUBJECT_OUT.search(s):
            return "subject"
    if TITLE_OUT.search(row["title"] or ""):
        return "title"
    return None


def blend(pool, ol_path, wiki_path):
    """Give every work a value: its share of demand, up to scale.

    v0 (no signal files): Gutenberg's 30-day download count alone.
    v1: a mixture of three demand distributions. Each signal is turned into
    a share of its own total over the pool (downloads, Open Library
    shelvings = want-to-read + reading + read + ratings, Wikipedia page views
    by humans over a year), and the value is 0.4 x downloads share + 0.4 x
    shelvings share + 0.2 x views share: shelvings are the most direct "want
    to read", and an article's traffic can belong to the subject rather than
    the book (Magna Carta the charter, not the text). A work with no shelvings and no
    article keeps only its downloads term, so a crawler-inflated title
    sinks below any work real people shelve or look up, and the tail stays
    ordered by downloads. A mixture, not a product: multiplying three
    heavy-tailed signals put 74% of all value in the top 1,000 works, where
    every real demand curve measured for this project puts 15 to 44%.
    Returns the paragraph the report prints about it.
    """
    if not ol_path and not wiki_path:
        for r in pool:
            r["value"] = float(r["downloads"])
        return ("v0: the value is Gutenberg's 30-day download count, the only signal that "
                "covers every title. Its head is polluted by crawlers.")
    ol = {}
    if ol_path:
        for line in open(ol_path):
            d = json.loads(line)
            ol[d["id"]] = d["want"] + d["reading"] + d["read"] + d["ratings"]
    pv = {}
    if wiki_path:
        for line in open(wiki_path):
            d = json.loads(line)
            pv[d["id"]] = d["views"]
    for r in pool:
        r["ol"] = ol.get(r["id"], 0)
        r["pv"] = pv.get(r["id"], 0)
    tot_dl = sum(r["downloads"] for r in pool) or 1
    tot_ol = sum(r["ol"] for r in pool) or 1
    tot_pv = sum(r["pv"] for r in pool) or 1
    w_dl, w_ol, w_pv = 0.4, 0.4, 0.2
    for r in pool:
        # An article's traffic counts only when reading intent corroborates it:
        # a text nobody shelves and few download (Magna Carta, the Rosary) is
        # looked up as a subject, not reached for as a book.
        pv = r["pv"] if (r["ol"] > 0 or r["downloads"] >= 5000) else 0
        r["value"] = 1e6 * (w_dl * r["downloads"] / tot_dl + w_ol * r["ol"] / tot_ol + w_pv * pv / tot_pv)
    n_ol = sum(1 for r in pool if r["ol"] > 0)
    n_pv = sum(1 for r in pool if r["pv"] > 0)
    top = sorted(pool, key=lambda r: -r["downloads"])[:1000]
    return (f"v1: value = 0.4 x share of downloads + 0.4 x share of Open Library shelvings "
            f"+ 0.2 x share of Wikipedia page views, each share taken over the pool "
            f"({tot_dl:,} downloads in 30 days, {tot_ol:,} shelvings, {tot_pv:,} views in a year). "
            f"Matched: {n_ol:,} works carry shelvings and {n_pv:,} carry views; of the 1,000 most "
            f"downloaded, {sum(1 for r in top if r['ol'] > 0):,} have shelvings and "
            f"{sum(1 for r in top if r['pv'] > 0):,} have an article. A work with neither keeps only "
            "its downloads term, which is what demotes crawler-inflated titles; views count only "
            "for a work with some shelvings or at least 5,000 downloads, so a subject looked up "
            "but not read (Magna Carta) does not ride its article.")


def gb(n):
    return n / 1e9


def fmt_int(n):
    return f"{n:,}"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--catalog", required=True)
    ap.add_argument("--report", required=True)
    ap.add_argument("--langs", default="", help="comma-separated; empty = every language")
    ap.add_argument("--list-json", help="write the ranked list as JSONL here")
    ap.add_argument("--ol", help="ol.jsonl from olsignal.py: want-to-read, reads, ratings per id")
    ap.add_argument("--wiki", help="wiki.jsonl from wikisignal.py: page views per id")
    args = ap.parse_args()
    langs = {l for l in args.langs.split(",") if l}

    rows = [json.loads(line) for line in open(args.catalog)]
    for r in rows:
        # MARC subfield markers and stray carriage returns leak into titles.
        r["title"] = re.sub(r"\s*:\s*\$b\s*", ": ", (r["title"] or "").replace("\r", "")).strip()
    total_demand = sum(r["downloads"] for r in rows)
    out = []
    p = out.append

    p("# Library data: what is in the catalog, what fits, what the list looks like\n")
    p("Produced by `tools_local/library/rank.py` from Project Gutenberg's catalog "
      "(`catalog.py`), v0: value = Gutenberg's 30-day download count, weight = the "
      "text-only EPUB's size. Every count below carries its share of demand "
      "(downloads), because a filter that removes 10% of titles and 0.1% of demand "
      "costs nothing.\n")

    # ---- census
    p("## Census\n")
    p(f"Records: {fmt_int(len(rows))}. Demand mass (30-day downloads): {fmt_int(total_demand)}.\n")

    def share_table(title, counter, demand, top=None, note=None):
        p(f"### {title}\n")
        if note:
            p(note + "\n")
        p("| | titles | share of demand |\n| --- | --- | --- |")
        items = counter.most_common(top) if top else sorted(counter.items(), key=lambda kv: -kv[1])
        for k, n in items:
            p(f"| {k} | {fmt_int(n)} | {100 * demand[k] / total_demand:.1f}% |")
        p("")

    def census(keyfn, rows_):
        c, d = collections.Counter(), collections.Counter()
        for r in rows_:
            k = keyfn(r)
            c[k] += 1
            d[k] += r["downloads"]
        return c, d

    share_table("By type", *census(lambda r: r["type"] or "?", rows))
    share_table("By rights", *census(lambda r: (r["rights"] or "?")[:40], rows))
    share_table("By language (first)", *census(lambda r: r["languages"][0] if r["languages"] else "?", rows), top=15)
    share_table("By LC class (first letter of the first code)",
                *census(lambda r: r["locc"][0][0] if r["locc"] else "none", rows), top=25,
                note="A: general works, B: philosophy/religion, D-F: history, G: geography/recreation, "
                     "H: social sciences, J: politics, K: law, L: education, M: music, N: arts, "
                     "P: literature, Q: science, R: medicine, S: agriculture, T: technology, "
                     "U/V: military, Z: bibliography.")

    # ---- filters, each counted
    p("## Filters, in order, each counted\n")
    p("| step | removed | demand removed | remaining | demand remaining |\n| --- | --- | --- | --- | --- |")
    kept = rows

    def step(name, pred):
        nonlocal kept
        before = kept
        kept = [r for r in before if pred(r)]
        removed = len(before) - len(kept)
        d_removed = sum(r["downloads"] for r in before) - sum(r["downloads"] for r in kept)
        d_kept = sum(r["downloads"] for r in kept)
        p(f"| {name} | {fmt_int(removed)} | {100 * d_removed / total_demand:.2f}% | "
          f"{fmt_int(len(kept))} | {100 * d_kept / total_demand:.1f}% |")

    step("Text only", lambda r: r["type"] == "Text")
    step("Public domain only", lambda r: (r["rights"] or "").startswith("Public domain"))
    step("Has a text-only EPUB", lambda r: "epub_noimages" in r["sizes"])
    if langs:
        step("Language in " + ",".join(sorted(langs)), lambda r: bool(set(r["languages"]) & langs))

    # form rules: census first, then apply
    rule_c, rule_d = collections.Counter(), collections.Counter()
    for r in kept:
        rule = form_rule(r)
        if rule:
            rule_c[rule] += 1
            rule_d[rule] += r["downloads"]
    step("Form (reads on the panel)", lambda r: form_rule(r) is None)

    # dedup: one edition per work, the most downloaded
    by_work, work_demand = {}, collections.Counter()

    def edition_rank(r):
        # A whole novel beats "Vol. 2 (of 3)"; among equals the most downloaded wins.
        partial = bool(VOLUME_RE.search(r["title"] or ""))
        return (partial, -r["downloads"])

    for r in kept:
        k = work_key(r)
        work_demand[k] += r["downloads"]
        if k not in by_work or edition_rank(r) < edition_rank(by_work[k]):
            by_work[k] = r
    merged = len(kept) - len(by_work)
    for k, r in by_work.items():
        r["downloads"] = work_demand[k]  # a work's demand is the sum of its editions
    step("One edition per work", lambda r, s=set(id(v) for v in by_work.values()): id(r) in s)
    p("")
    p(f"The work merge folded {fmt_int(merged)} editions into their most downloaded one; "
      "the survivor carries the sum of their demand, so the step removes titles, not demand.\n")

    p("### What each form rule removed\n")
    p("| rule | titles | share of demand |\n| --- | --- | --- |")
    for k, n in rule_c.most_common():
        p(f"| {k} | {fmt_int(n)} | {100 * rule_d[k] / total_demand:.2f}% |")
    p("")

    # ---- ranking
    pool = kept
    pool_bytes = sum(r["sizes"]["epub_noimages"] for r in pool)
    signal_note = blend(pool, args.ol, args.wiki)
    pool_demand = sum(r["value"] for r in pool)
    for r in pool:
        r["density"] = r["value"] / max(r["sizes"]["epub_noimages"], 1)
    by_density = sorted(pool, key=lambda r: -r["density"])
    by_value = sorted(pool, key=lambda r: -r["value"])

    p("## The value each work carries\n")
    p(signal_note + "\n")
    p("## The pool after the filters\n")
    p(f"{fmt_int(len(pool))} works, {gb(pool_bytes):.1f} GB as text-only EPUBs "
      f"(median {sorted(r['sizes']['epub_noimages'] for r in pool)[len(pool)//2] // 1000} KB), "
      "Coverage below is the share of the pool's value (defined above).\n")

    # per-language totals
    lc, ld, lb = collections.Counter(), collections.Counter(), collections.Counter()
    for r in pool:
        l = r["languages"][0]
        lc[l] += 1
        ld[l] += r["value"]
        lb[l] += r["sizes"]["epub_noimages"]
    p("### Per language\n")
    p("| language | works | GB | share of pool demand |\n| --- | --- | --- | --- |")
    for l, n in lc.most_common(12):
        p(f"| {l} | {fmt_int(n)} | {gb(lb[l]):.2f} | {100 * ld[l] / pool_demand:.1f}% |")
    p("")

    # coverage curve
    def curve(order):
        cum = 0
        marks = {}
        for i, r in enumerate(order, 1):
            cum += r["value"]
            if i in CURVE_N:
                marks[i] = cum
        return marks

    cd, cv = curve(by_density), curve(by_value)
    p("## How concentrated demand is\n")
    p("Share of the pool's value held by the top N works, in the order the card is filled "
      "(value per byte) and in raw value order.\n")
    p("| top N | by value per byte | by popularity |\n| --- | --- | --- |")
    for n in CURVE_N:
        if n <= len(pool):
            p(f"| {fmt_int(n)} | {100 * cd[n] / pool_demand:.1f}% | {100 * cv[n] / pool_demand:.1f}% |")
    p("")

    # fit table
    def fill(budget):
        used = cum = count = 0
        for r in by_density:
            s = r["sizes"]["epub_noimages"]
            if used + s > budget:
                break  # first non-fit: the greedy prefix (within one book of optimal)
            used += s
            cum += r["value"]
            count += 1
        return count, used, cum

    p("## What fits\n")
    p("Card sizes as printed (decimal gigabytes); the budget is the slider's share of the "
      "card, with nothing else on it. A real card loses a few percent to the file system "
      "and to whatever is already there. 'The whole pool' means every book in this pool "
      "fits with room over; the pool is not every book, see universe.py for that.\n")
    p("| card | " + " | ".join(f"fill to {s}%" for s in SLIDER) + " |")
    p("| --- | " + " | ".join("---" for _ in SLIDER) + " |")
    for card in CARDS_GB:
        cells = []
        for s in SLIDER:
            budget = card * 1e9 * s / 100
            if budget >= pool_bytes:
                cells.append(f"the whole pool ({fmt_int(len(pool))} books, {gb(pool_bytes):.1f} GB)")
            else:
                count, used, cum = fill(budget)
                cells.append(f"{fmt_int(count)} books, {gb(used):.1f} GB, {100 * cum / pool_demand:.1f}% of demand")
        p(f"| {card} GB | " + " | ".join(cells) + " |")
    p("")

    # the list
    def line(i, r):
        who = ", ".join(c["name"] for c in r["creators"] if c["name"]) or "?"
        title = (r["title"] or "?").split("\n")[0]
        return (f"| {i} | {title[:70]} | {who[:40]} | {r['languages'][0]} | "
                f"{fmt_int(r['downloads'])} | {fmt_int(r.get('ol', 0))} | {fmt_int(r.get('pv', 0))} | "
                f"{r['sizes']['epub_noimages'] // 1000} |")

    p("## The top 100, in fill order\n")
    p("| # | title | author | lang | downloads/30d | OL shelvings | Wikipedia views/yr | KB |\n| --- | --- | --- | --- | --- | --- | --- | --- |")
    for i, r in enumerate(by_density[:100], 1):
        p(line(i, r))
    p("")
    p("## Samples down the list\n")
    p("Five works at and after each rank, to see what the tail looks like.\n")
    p("| # | title | author | lang | downloads/30d | OL shelvings | Wikipedia views/yr | KB |\n| --- | --- | --- | --- | --- | --- | --- | --- |")
    for n in SAMPLE_RANKS:
        if n < len(by_density):
            for i in range(n, min(n + 5, len(by_density))):
                p(line(i + 1, by_density[i]))
    p("")

    open(args.report, "w").write("\n".join(out) + "\n")
    if args.list_json:
        with open(args.list_json, "w") as f:
            for i, r in enumerate(by_density, 1):
                f.write(json.dumps({"rank": i, "id": r["id"], "title": r["title"],
                                    "creators": [c["name"] for c in r["creators"]],
                                    "lang": r["languages"][0], "downloads": r["downloads"],
                                    "ol": r.get("ol", 0), "pv": r.get("pv", 0), "value": round(r["value"], 2),
                                    "bytes": r["sizes"]["epub_noimages"]}, ensure_ascii=False) + "\n")
    print(f"pool {len(pool)} works, {gb(pool_bytes):.1f} GB; report {args.report}")


if __name__ == "__main__":
    main()

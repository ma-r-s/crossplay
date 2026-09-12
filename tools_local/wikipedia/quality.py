#!/usr/bin/env python3
"""What a pack reads like, measured over every article, before it ships.

    quality.py <pack-dir> [--sample N --plain out.md] [--json out.json]

Walks every article of a built pack and counts the marks a cut leaves
behind: empty parentheses, a space before a comma, a sentence that starts
lowercase, a bare "( )", doubled spaces, a letter followed by a bare number
where a symbol stood, near-empty bodies, omitted tables. Each signature gets
a count, a rate per thousand articles and a few examples with their
context, so a rule is written from evidence and its effect is a number on
the next run. --sample writes N random articles as plain text for a
reviewer to read as a reader would; that review is the part no regex does.
"""

import argparse
import collections
import html
import json
import random
import re
import sys

sys.path.insert(0, __import__("os").path.dirname(__import__("os").path.abspath(__file__)))
import pack_format as pf  # noqa: E402

SIGNATURES = [
    ("empty_parens", re.compile(r"\(\s*[,;:\s]*\)")),
    ("space_before_punct", re.compile(r"\w [,.;:!?](?=\s|$)")),
    ("double_space", re.compile(r"\S  +\S")),
    ("lowercase_sentence", re.compile(r"(?<![A-Z])[.!?] [a-z]{3,}\b")),
    ("padded_dash", re.compile(r"\w [\u2013\u2014]\w|\w[\u2013\u2014] \w")),
    ("bare_number_after_letter", re.compile(r"\b(?:where|let|if|when) [b-hj-z] \d")),
    ("formula_hole", re.compile(r"= =|\b(?:to|of|is|where|equals) [.,;]")),
    ("dangling_conjunction", re.compile(r"\b(?:and|or|of|the|in|by|with|to|from) [.,;)]")),
    ("hyphen_gap", re.compile(r"\w -\w|\w- (?!and\b|or\b)\w")),
    ("orphan_quote", re.compile(r"\"\s*\"|\(\s*\"\s*\)")),
    ("html_entity", re.compile(r"&[a-z]+;|&#\d+;")),
    ("bracket_leftover", re.compile(r"\[\s*\]|\[\d+\]")),
    ("omitted_table", re.compile(r"\(a table was omitted\)")),
]
NEAR_EMPTY = 300


_BLOCK = re.compile(r"</?(?:p|h[1-6]|li|ul|table|tr|td|th|body|html)\b[^>]*>")
_INLINE = re.compile(r"</?(?:a|b|i)\b[^>]*>")


def plain(xhtml):
    """The text as the reader lays it out: inline tags vanish (a link's
    closing tag is not a space before the period), block tags break lines,
    entities are characters."""
    t = xhtml.decode("utf-8", "replace")
    t = _INLINE.sub("", t)
    t = _BLOCK.sub("\n", t)
    t = re.sub(r"<[^>]+>", " ", t)
    t = html.unescape(t)
    t = re.sub(r"[ \t]+", " ", t)
    return re.sub(r"\n\s*\n+", "\n", t).strip()


def scan(pack_dir, sample_n=0, seed=20260911):
    p = pf.Pack(pack_dir)
    rng = random.Random(seed)
    counts = collections.Counter()
    articles_hit = collections.Counter()
    examples = collections.defaultdict(list)
    sizes = []
    near_empty = []
    sample = []
    n = 0
    try:
        for e in p.iter_entries():
            if e.redirect:
                continue
            n += 1
            a = p.article(e.locator)
            t = plain(a.xhtml)
            body = t[len(a.title):].strip() if t.startswith(a.title) else t
            sizes.append(len(body))
            if len(body) < NEAR_EMPTY:
                near_empty.append((a.title, len(body)))
            for name, rx in SIGNATURES:
                hits = list(rx.finditer(t))
                if not hits:
                    continue
                counts[name] += len(hits)
                articles_hit[name] += 1
                if len(examples[name]) < 6:
                    m = hits[0]
                    examples[name].append((a.title, t[max(0, m.start() - 50):m.end() + 40]))
            if sample_n:
                if len(sample) < sample_n:
                    sample.append((a.title, t))
                else:
                    j = rng.randrange(n)
                    if j < sample_n:
                        sample[j] = (a.title, t)
    finally:
        p.close()
    sizes.sort()
    report = {
        "articles": n,
        "body_chars": {"median": sizes[n // 2] if n else 0, "p10": sizes[n // 10] if n else 0, "p1": sizes[n // 100] if n else 0},
        "near_empty": near_empty[:20],
        "near_empty_count": len(near_empty),
        "signatures": {
            name: {
                "hits": counts[name],
                "articles": articles_hit[name],
                "per_1000_articles": round(1000.0 * articles_hit[name] / n, 2) if n else 0,
                "examples": examples[name],
            }
            for name, _ in SIGNATURES
        },
    }
    return report, sample


# The census, judged. A removed character is acceptable only when the panel
# truly cannot hold its script and the text keeps a romanisation beside it;
# everything else (Greek, accented Latin, maths, arrows, numbers, punctuation)
# carries meaning in English prose and must be drawn or spelled, never
# dropped. A build whose census has any of the latter fails this gate.
ACCEPTED_BLOCKS = (
    ("Han", 0x4E00, 0x9FFF), ("Han ext A", 0x3400, 0x4DBF), ("Han ext B+", 0x20000, 0x2FA1F),
    ("CJK symbols", 0x3000, 0x303F), ("Hiragana", 0x3040, 0x309F), ("Katakana", 0x30A0, 0x30FF),
    ("Hangul", 0xAC00, 0xD7AF), ("Hangul jamo", 0x1100, 0x11FF), ("Bopomofo", 0x3100, 0x312F),
    ("Arabic", 0x0600, 0x06FF), ("Arabic supplement", 0x0750, 0x077F), ("Arabic forms", 0xFB50, 0xFDFF),
    ("Arabic forms B", 0xFE70, 0xFEFF), ("Hebrew", 0x0590, 0x05FF), ("Syriac", 0x0700, 0x074F),
    ("Thaana", 0x0780, 0x07BF), ("Devanagari", 0x0900, 0x097F), ("Bengali", 0x0980, 0x09FF),
    ("Gurmukhi", 0x0A00, 0x0A7F), ("Gujarati", 0x0A80, 0x0AFF), ("Oriya", 0x0B00, 0x0B7F),
    ("Tamil", 0x0B80, 0x0BFF), ("Telugu", 0x0C00, 0x0C7F), ("Kannada", 0x0C80, 0x0CFF),
    ("Malayalam", 0x0D00, 0x0D7F), ("Sinhala", 0x0D80, 0x0DFF), ("Thai", 0x0E00, 0x0E7F),
    ("Lao", 0x0E80, 0x0EFF), ("Tibetan", 0x0F00, 0x0FFF), ("Myanmar", 0x1000, 0x109F),
    ("Georgian", 0x10A0, 0x10FF), ("Ethiopic", 0x1200, 0x137F), ("Khmer", 0x1780, 0x17FF),
    ("Mongolian", 0x1800, 0x18AF), ("Armenian", 0x0530, 0x058F), ("IPA", 0x0250, 0x02AF),
    ("Spacing modifiers", 0x02B0, 0x02FF), ("Combining marks", 0x0300, 0x036F),
    ("Yi", 0xA000, 0xA4CF), ("Cherokee", 0x13A0, 0x13FF), ("Canadian syllabics", 0x1400, 0x167F),
    ("Emoji and pictographs", 0x1F300, 0x1FAFF), ("Misc symbols", 0x2600, 0x26FF), ("Dingbats", 0x2700, 0x27BF),
    ("Private use", 0xE000, 0xF8FF), ("Specials", 0xFFF0, 0xFFFF), ("Variation selectors", 0xFE00, 0xFE0F),
    ("Enclosed alphanumerics", 0x2460, 0x24FF), ("Box drawing", 0x2500, 0x257F), ("Geometric shapes", 0x25A0, 0x25FF),
)
WATCH_BLOCKS = (
    ("Greek", 0x0370, 0x03FF), ("Greek extended", 0x1F00, 0x1FFF), ("Latin extended additional", 0x1E00, 0x1EFF),
    ("Latin extended B", 0x0180, 0x024F), ("Latin extended C/D", 0x2C60, 0x2C7F), ("Cyrillic", 0x0400, 0x04FF),
    ("Cyrillic supplement", 0x0500, 0x052F), ("General punctuation", 0x2000, 0x206F),
    ("Super and subscripts", 0x2070, 0x209F), ("Currency", 0x20A0, 0x20CF), ("Letterlike", 0x2100, 0x214F),
    ("Number forms", 0x2150, 0x218F), ("Arrows", 0x2190, 0x21FF), ("Mathematical operators", 0x2200, 0x22FF),
    ("Misc technical", 0x2300, 0x23FF), ("Supplemental math", 0x2A00, 0x2AFF), ("Misc math A/B", 0x27C0, 0x27EF),
    ("Latin-1", 0x0080, 0x00FF), ("Latin extended A", 0x0100, 0x017F), ("ASCII", 0x0000, 0x007F),
)


def block_of(ch):
    cp = ord(ch)
    for name, lo, hi in ACCEPTED_BLOCKS:
        if lo <= cp <= hi:
            return name, True
    for name, lo, hi in WATCH_BLOCKS:
        if lo <= cp <= hi:
            return name, False
    return "other U+%04X" % cp, False


def judge_census(census):
    """census: [[char, count], ...]. Returns (accepted, refused) as
    {block: (count, [chars])}."""
    accepted, refused = {}, {}
    for ch, c in census:
        name, ok = block_of(ch)
        bucket = accepted if ok else refused
        cnt, chars = bucket.get(name, (0, []))
        if len(chars) < 12:
            chars.append(ch)
        bucket[name] = (cnt + c, chars)
    return accepted, refused


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("pack")
    ap.add_argument("--sample", type=int, default=0, help="write N random articles as plain text")
    ap.add_argument("--plain", help="where the sample goes (markdown)")
    ap.add_argument("--chars", type=int, default=1500, help="characters of each sampled article")
    ap.add_argument("--json", help="write the report here too")
    ap.add_argument("--seed", type=int, default=20260911)
    ap.add_argument("--summary", help="a build's summary.json: judge its census of removed characters")
    args = ap.parse_args(argv)
    gate_failed = False
    if args.summary:
        with open(args.summary, encoding="utf-8") as f:
            summary = json.load(f)
        accepted, refused = judge_census(summary.get("removed_chars", []))
        print("removed characters, by block:")
        for name, (cnt, chars) in sorted(accepted.items(), key=lambda kv: -kv[1][0]):
            print(f"  ok       {name:28s} {cnt:10,}  {' '.join(chars)}")
        for name, (cnt, chars) in sorted(refused.items(), key=lambda kv: -kv[1][0]):
            print(f"  REFUSED  {name:28s} {cnt:10,}  {' '.join(chars)}   (must be drawn or spelled, not dropped)")
        if refused:
            gate_failed = True
        print(f"symbols spelled: {summary.get('symbols_translated', 0):,}; diacritics reduced to base letters: {summary.get('diacritics_dropped', 0):,}")
    report, sample = scan(args.pack, args.sample, args.seed)
    print(f"{report['articles']:,} articles; body chars median {report['body_chars']['median']:,}, "
          f"p10 {report['body_chars']['p10']:,}, p1 {report['body_chars']['p1']:,}; "
          f"near-empty (<{NEAR_EMPTY}): {report['near_empty_count']}")
    for name, r in report["signatures"].items():
        print(f"  {name:26s} {r['hits']:8,} hits in {r['articles']:7,} articles ({r['per_1000_articles']:6.1f} per 1000)")
        for title, ctx in r["examples"][:3]:
            print(f"      {title}: ...{ctx.replace(chr(10), ' ')}...")
    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(report, f, ensure_ascii=False, indent=1)
    if args.plain and sample:
        with open(args.plain, "w", encoding="utf-8") as f:
            f.write("# %d random articles, as the reader shows them (first %d characters each)\n\n" % (len(sample), args.chars))
            for title, t in sample:
                f.write("## " + title + "\n\n" + t[:args.chars] + ("\n\n[...]\n\n" if len(t) > args.chars else "\n\n"))
        print(f"sample of {len(sample)} articles -> {args.plain}")
    if gate_failed:
        print("QUALITY GATE: FAILED, characters that carry meaning were dropped")
        return 1
    print("QUALITY GATE: passed (every removed character is in an accepted script)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

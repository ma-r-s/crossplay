#!/usr/bin/env python3
"""Every character the source contains, and what the pipeline does to it.

    census.py --rows <rows.jsonl.gz> [--limit N] [--workers N]
              --json <census.json> --md <census.md>

A rule written from the head covers the cases its author thought of. This
counts every code point above ASCII in the text the converter feeds to the
strip step (each paragraph, heading, fact and link), across every article,
and records for each one: occurrences, articles affected, whether it sits
inside a parenthetical, what the pipeline does to it today (drawn, spelled,
folded to its base letter, or dropped), and real sentences showing the text
before and after. It also measures the collateral: drawable letters that
went with an undrawable run (a whole parenthetical, a labelled name), and
lists the articles that lost the most.

The outcome column is decided by the same tables the converter uses
(glyphs.py, symbols.py, article_html._fold_chars), so the census cannot
drift from the pipeline: change a table and the next census shows it.
"""

import argparse
import difflib
import gzip
import json
import multiprocessing as mp
import os
import re
import sys
import time
import unicodedata

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import article_html  # noqa: E402
import glyphs  # noqa: E402
import symbols  # noqa: E402
from build_pack import read_rows  # noqa: E402

WINDOW = 90
EXAMPLES_PER_CHAR = 3
EXAMPLES_PER_ARTICLE = 3

# Unicode blocks that matter for English Wikipedia text. Anything outside
# is named by the first word of the code point's Unicode name.
BLOCKS = [
    (0x0080, 0x00FF, "Latin-1 Supplement"),
    (0x0100, 0x017F, "Latin Extended-A"),
    (0x0180, 0x024F, "Latin Extended-B"),
    (0x0250, 0x02AF, "IPA Extensions"),
    (0x02B0, 0x02FF, "Spacing Modifier Letters"),
    (0x0300, 0x036F, "Combining Diacritical Marks"),
    (0x0370, 0x03FF, "Greek and Coptic"),
    (0x0400, 0x04FF, "Cyrillic"),
    (0x0500, 0x052F, "Cyrillic Supplement"),
    (0x0530, 0x058F, "Armenian"),
    (0x0590, 0x05FF, "Hebrew"),
    (0x0600, 0x06FF, "Arabic"),
    (0x0700, 0x074F, "Syriac"),
    (0x0750, 0x077F, "Arabic Supplement"),
    (0x0780, 0x07BF, "Thaana"),
    (0x07C0, 0x07FF, "NKo"),
    (0x0900, 0x097F, "Devanagari"),
    (0x0980, 0x09FF, "Bengali"),
    (0x0A00, 0x0A7F, "Gurmukhi"),
    (0x0A80, 0x0AFF, "Gujarati"),
    (0x0B00, 0x0B7F, "Oriya"),
    (0x0B80, 0x0BFF, "Tamil"),
    (0x0C00, 0x0C7F, "Telugu"),
    (0x0C80, 0x0CFF, "Kannada"),
    (0x0D00, 0x0D7F, "Malayalam"),
    (0x0D80, 0x0DFF, "Sinhala"),
    (0x0E00, 0x0E7F, "Thai"),
    (0x0E80, 0x0EFF, "Lao"),
    (0x0F00, 0x0FFF, "Tibetan"),
    (0x1000, 0x109F, "Myanmar"),
    (0x10A0, 0x10FF, "Georgian"),
    (0x1100, 0x11FF, "Hangul Jamo"),
    (0x1200, 0x137F, "Ethiopic"),
    (0x13A0, 0x13FF, "Cherokee"),
    (0x1400, 0x167F, "Unified Canadian Aboriginal Syllabics"),
    (0x1780, 0x17FF, "Khmer"),
    (0x1800, 0x18AF, "Mongolian"),
    (0x1B00, 0x1B7F, "Balinese"),
    (0x1D00, 0x1D7F, "Phonetic Extensions"),
    (0x1D80, 0x1DBF, "Phonetic Extensions Supplement"),
    (0x1DC0, 0x1DFF, "Combining Diacritical Marks Supplement"),
    (0x1E00, 0x1EFF, "Latin Extended Additional"),
    (0x1F00, 0x1FFF, "Greek Extended"),
    (0x2000, 0x206F, "General Punctuation"),
    (0x2070, 0x209F, "Superscripts and Subscripts"),
    (0x20A0, 0x20CF, "Currency Symbols"),
    (0x20D0, 0x20FF, "Combining Marks for Symbols"),
    (0x2100, 0x214F, "Letterlike Symbols"),
    (0x2150, 0x218F, "Number Forms"),
    (0x2190, 0x21FF, "Arrows"),
    (0x2200, 0x22FF, "Mathematical Operators"),
    (0x2300, 0x23FF, "Miscellaneous Technical"),
    (0x2400, 0x243F, "Control Pictures"),
    (0x2460, 0x24FF, "Enclosed Alphanumerics"),
    (0x2500, 0x257F, "Box Drawing"),
    (0x2580, 0x259F, "Block Elements"),
    (0x25A0, 0x25FF, "Geometric Shapes"),
    (0x2600, 0x26FF, "Miscellaneous Symbols"),
    (0x2700, 0x27BF, "Dingbats"),
    (0x27C0, 0x27EF, "Miscellaneous Mathematical Symbols-A"),
    (0x27F0, 0x27FF, "Supplemental Arrows-A"),
    (0x2800, 0x28FF, "Braille Patterns"),
    (0x2900, 0x297F, "Supplemental Arrows-B"),
    (0x2980, 0x29FF, "Miscellaneous Mathematical Symbols-B"),
    (0x2A00, 0x2AFF, "Supplemental Mathematical Operators"),
    (0x2B00, 0x2BFF, "Miscellaneous Symbols and Arrows"),
    (0x2C60, 0x2C7F, "Latin Extended-C"),
    (0x2C80, 0x2CFF, "Coptic"),
    (0x2E00, 0x2E7F, "Supplemental Punctuation"),
    (0x2E80, 0x2FDF, "CJK Radicals"),
    (0x3000, 0x303F, "CJK Symbols and Punctuation"),
    (0x3040, 0x309F, "Hiragana"),
    (0x30A0, 0x30FF, "Katakana"),
    (0x3100, 0x312F, "Bopomofo"),
    (0x3130, 0x318F, "Hangul Compatibility Jamo"),
    (0x3200, 0x32FF, "Enclosed CJK Letters and Months"),
    (0x3300, 0x33FF, "CJK Compatibility"),
    (0x3400, 0x4DBF, "CJK Unified Ideographs Extension A"),
    (0x4E00, 0x9FFF, "CJK Unified Ideographs"),
    (0xA000, 0xA4CF, "Yi"),
    (0xA640, 0xA69F, "Cyrillic Extended-B"),
    (0xA720, 0xA7FF, "Latin Extended-D"),
    (0xAB30, 0xAB6F, "Latin Extended-E"),
    (0xAC00, 0xD7AF, "Hangul Syllables"),
    (0xE000, 0xF8FF, "Private Use Area"),
    (0xF900, 0xFAFF, "CJK Compatibility Ideographs"),
    (0xFB00, 0xFB4F, "Alphabetic Presentation Forms"),
    (0xFB50, 0xFDFF, "Arabic Presentation Forms-A"),
    (0xFE00, 0xFE0F, "Variation Selectors"),
    (0xFE20, 0xFE2F, "Combining Half Marks"),
    (0xFE30, 0xFE4F, "CJK Compatibility Forms"),
    (0xFE70, 0xFEFF, "Arabic Presentation Forms-B"),
    (0xFF00, 0xFFEF, "Halfwidth and Fullwidth Forms"),
    (0xFFF0, 0xFFFF, "Specials"),
    (0x10000, 0x1007F, "Linear B Syllabary"),
    (0x10300, 0x1032F, "Old Italic"),
    (0x10330, 0x1034F, "Gothic"),
    (0x10400, 0x1044F, "Deseret"),
    (0x10900, 0x1091F, "Phoenician"),
    (0x12000, 0x123FF, "Cuneiform"),
    (0x13000, 0x1342F, "Egyptian Hieroglyphs"),
    (0x16800, 0x16A3F, "Bamum Supplement"),
    (0x1D000, 0x1D0FF, "Byzantine Musical Symbols"),
    (0x1D100, 0x1D1FF, "Musical Symbols"),
    (0x1D400, 0x1D7FF, "Mathematical Alphanumeric Symbols"),
    (0x1F000, 0x1F02F, "Mahjong Tiles"),
    (0x1F0A0, 0x1F0FF, "Playing Cards"),
    (0x1F100, 0x1F1FF, "Enclosed Alphanumeric Supplement"),
    (0x1F300, 0x1F5FF, "Miscellaneous Symbols and Pictographs"),
    (0x1F600, 0x1F64F, "Emoticons"),
    (0x1F680, 0x1F6FF, "Transport and Map Symbols"),
    (0x1F900, 0x1F9FF, "Supplemental Symbols and Pictographs"),
    (0x20000, 0x2A6DF, "CJK Unified Ideographs Extension B"),
    (0xE0100, 0xE01EF, "Variation Selectors Supplement"),
]


def block_of(cp):
    for lo, hi, name in BLOCKS:
        if lo <= cp <= hi:
            return name
    try:
        return "Other: " + unicodedata.name(chr(cp)).split()[0].title()
    except ValueError:
        return "Other: unnamed U+%04X" % cp


def char_name(cp):
    try:
        return unicodedata.name(chr(cp))
    except ValueError:
        return "U+%04X" % cp


def outcome(cp):
    """What the pipeline does with this code point, decided by its tables."""
    ch = chr(cp)
    # the same order as strip_undrawable: the symbol table runs first
    if ch in symbols.SYMBOLS:
        return "spelled"
    if ch in symbols.GREEK:
        return "spelled when alone, dropped in a word"
    if 0x0400 <= cp <= 0x052F:
        return "romanised in prose, removed with its label (Cyrillic)"
    if 0x0370 <= cp <= 0x03FF or 0x1F00 <= cp <= 0x1FFF:
        return "romanised in prose, removed with its label (Greek)"
    if glyphs.is_drawable(cp):
        return "drawn"
    # the same order as article_html._fold_chars
    compat = unicodedata.normalize("NFKC", ch)
    if compat != ch and all(glyphs.is_drawable(ord(c)) for c in compat):
        return "compat to " + compat
    decomposed = unicodedata.normalize("NFD", ch)
    if article_html.DECOMPOSE and len(decomposed) > 1 and all(glyphs.is_drawable(ord(c)) for c in decomposed):
        return "decomposed"
    base = "".join(c for c in decomposed if not unicodedata.combining(c))
    if ch.isalpha() and base != ch and base and all(glyphs.is_drawable(ord(c)) for c in base):
        return "folded to " + base
    if ch in symbols.LOOKALIKES:
        return "lookalike inside a word (" + symbols.LOOKALIKES[ch] + "), else dropped"
    return "dropped"


# ---------------------------------------------------------------- worker

_orig_strip = article_html.strip_undrawable
_ok = None
_row = None  # the record for the row being converted
_examples_given = {}  # cp -> how many examples this worker has already sent


def _letters(s):
    return sum(1 for c in s if c.isalpha() and _ok.match(c))


def _window(text, i):
    lo = max(0, i - WINDOW)
    hi = min(len(text), i + WINDOW)
    # widen to the enclosing parenthetical, so the paren rule can run on the
    # example as it runs on the paragraph, then to word boundaries
    depth = 0
    for j in range(i, -1, -1):
        if text[j] == ")":
            depth += 1
        elif text[j] == "(":
            if depth == 0:
                lo = min(lo, j)
                break
            depth -= 1
    depth = 0
    for j in range(i, len(text)):
        if text[j] == "(":
            depth += 1
        elif text[j] == ")":
            if depth == 0:
                hi = max(hi, j + 1)
                break
            depth -= 1
    while lo > 0 and not text[lo - 1].isspace():
        lo -= 1
    while hi < len(text) and not text[hi].isspace():
        hi += 1
    return text[lo:hi]


def _strip_recording(text, stats, lead=False):
    rec = _row
    if rec is None or not text:
        return _orig_strip(text, stats, lead)
    depth = 0
    chars = rec["chars"]
    for i, ch in enumerate(text):
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth = max(0, depth - 1)
        cp = ord(ch)
        if cp <= 0x7E:
            continue
        e = chars.get(cp)
        if e is None:
            e = chars[cp] = [0, 0]
        e[0] += 1
        if depth:
            e[1] += 1
        rec["blocks"].add(block_of(cp))
        if _examples_given.get(cp, 0) < EXAMPLES_PER_CHAR:
            _examples_given[cp] = _examples_given.get(cp, 0) + 1
            before = _window(text, i)
            after = _orig_strip(before, {}, lead)
            rec["examples"].append((cp, before, after))
    after = _orig_strip(text, stats, lead)
    if not glyphs_run_re().search(text):
        rec["letters"] += _letters(after)
        return after
    t1, _ = symbols.translate(text)
    t2, _ = article_html._fold_chars(t1)
    a = _letters(t2)
    b = _letters(after)
    rec["letters"] += b
    if a > b:
        rec["collateral"] += a - b
        if len(rec["collateral_examples"]) < EXAMPLES_PER_ARTICLE:
            aw = t2.split()
            bw = after.split()
            sm = difflib.SequenceMatcher(None, aw, bw, autojunk=False)
            best = None
            for op, i1, i2, j1, j2 in sm.get_opcodes():
                if op not in ("delete", "replace"):
                    continue
                lost = sum(_letters(w) for w in aw[i1:i2]) - sum(_letters(w) for w in bw[j1:j2])
                if lost > 0 and (best is None or lost > best[0]):
                    best = (lost, i1, i2)
            if best:
                _, i1, i2 = best
                lo = max(0, i1 - 6)
                hi = min(len(aw), i2 + 6)
                rec["collateral_examples"].append(
                    {"context": " ".join(aw[lo:hi]), "gone": " ".join(aw[i1:i2])}
                )
    return after


_run_re = None


def glyphs_run_re():
    global _run_re
    if _run_re is None:
        _run_re, _ = article_html._runs()
    return _run_re


def _init():
    global _ok
    _ok = re.compile("[" + glyphs.drawable_class() + "]")
    article_html.strip_undrawable = _strip_recording


def convert(row):
    global _row
    _row = {
        "chars": {},
        "blocks": set(),
        "examples": [],
        "letters": 0,
        "collateral": 0,
        "collateral_examples": [],
    }
    try:
        title, _, _ = article_html.article_xhtml(row, {})
    except (ValueError, TypeError) as e:
        _row = None
        return None, str(e)
    rec = _row
    _row = None
    rec["title"] = title
    return rec, None


# ---------------------------------------------------------------- parent


def run(args):
    t0 = time.time()
    chars = {}  # cp -> dict
    articles = []  # (collateral, letters, title, examples)
    rows = 0
    failed = 0
    total_letters = 0
    total_collateral = 0
    articles_with_collateral = 0
    articles_per_block = {}

    def rows_iter():
        n = 0
        for row in read_rows(args.rows):
            if args.limit and n >= args.limit:
                return
            n += 1
            yield row

    ctx = mp.get_context("fork")
    with ctx.Pool(processes=args.workers, initializer=_init) as pool:
        for rec, why in pool.imap_unordered(convert, rows_iter(), chunksize=8):
            rows += 1
            if rec is None:
                failed += 1
                continue
            for cp, (n, inparen) in rec["chars"].items():
                e = chars.get(cp)
                if e is None:
                    e = chars[cp] = {
                        "count": 0,
                        "articles": 0,
                        "in_paren": 0,
                        "examples": [],
                    }
                e["count"] += n
                e["articles"] += 1
                e["in_paren"] += inparen
            for b in rec["blocks"]:
                articles_per_block[b] = articles_per_block.get(b, 0) + 1
            for cp, before, after in rec["examples"]:
                e = chars[cp]
                if len(e["examples"]) < EXAMPLES_PER_CHAR:
                    e["examples"].append(
                        {"title": rec["title"], "before": before, "after": after}
                    )
            total_letters += rec["letters"]
            if rec["collateral"]:
                total_collateral += rec["collateral"]
                articles_with_collateral += 1
                articles.append(
                    (
                        rec["collateral"],
                        rec["letters"],
                        rec["title"],
                        rec["collateral_examples"],
                    )
                )
            if rows % 5000 == 0:
                print(
                    "%d rows, %d code points, %ds"
                    % (rows, len(chars), time.time() - t0),
                    file=sys.stderr,
                    flush=True,
                )

    articles.sort(key=lambda a: (-a[0], a[2]))
    out = {
        "rows": rows,
        "failed": failed,
        "seconds": round(time.time() - t0),
        "letters_kept": total_letters,
        "collateral_letters": total_collateral,
        "articles_with_collateral": articles_with_collateral,
        "articles_per_block": articles_per_block,
        "chars": {
            "%04X" % cp: {
                "char": chr(cp),
                "name": char_name(cp),
                "block": block_of(cp),
                "category": unicodedata.category(chr(cp)),
                "outcome": outcome(cp),
                **e,
            }
            for cp, e in chars.items()
        },
        "damaged_articles": [
            {"title": t, "collateral": c, "letters": l, "examples": ex}
            for c, l, t, ex in articles[:400]
        ],
    }
    return out


# ---------------------------------------------------------------- report


def esc_md(s):
    return s.replace("|", "\\|").replace("\n", " ")


def fmt_example(ex):
    return "%s: \u201c%s\u201d \u2192 \u201c%s\u201d" % (
        esc_md(ex["title"]),
        esc_md(ex["before"]),
        esc_md(ex["after"]),
    )


def report(census, path, top_chars=200):
    chars = census["chars"]
    rows = census["rows"]
    lines = []
    w = lines.append
    w("# Character census\n")
    w(
        "%d articles scanned, %d failed, %ds. %d distinct code points above ASCII.\n"
        % (rows, census["failed"], census["seconds"], len(chars))
    )
    w(
        "Letters kept in the output: %d. Drawable letters lost with an undrawable run "
        "(collateral): %d, in %d articles.\n"
        % (
            census["letters_kept"],
            census["collateral_letters"],
            census["articles_with_collateral"],
        )
    )

    # by outcome
    by_out = {}
    for e in chars.values():
        key = e["outcome"].split(" ")[0]
        d = by_out.setdefault(
            key, {"code points": 0, "occurrences": 0, "article hits": 0}
        )
        d["code points"] += 1
        d["occurrences"] += e["count"]
        d["article hits"] += e["articles"]
    w("\n## By outcome\n")
    w("| outcome | code points | occurrences | article hits |")
    w("|---|---:|---:|---:|")
    for k in ("drawn", "spelled", "compat", "decomposed", "folded", "lookalike", "romanised", "dropped"):
        d = by_out.get(k, {"code points": 0, "occurrences": 0, "article hits": 0})
        w(
            "| %s | %d | %d | %d |"
            % (k, d["code points"], d["occurrences"], d["article hits"])
        )
    w(
        "\n(article hits: one per article per code point, so an article counts once per distinct character.)\n"
    )

    # by block
    by_block = {}
    for cp, e in chars.items():
        b = by_block.setdefault(
            e["block"], {"cps": [], "count": 0, "articles": set(), "outs": {}}
        )
        b["cps"].append((cp, e))
        b["count"] += e["count"]
        k = e["outcome"].split(" ")[0]
        b["outs"][k] = b["outs"].get(k, 0) + e["count"]
    w("\n## By block\n")
    w(
        "Sorted by occurrences. Outcome mix counts occurrences. Top characters by articles affected.\n"
    )
    w(
        "| block | code points | occurrences | articles | outcome mix | top characters |"
    )
    w("|---|---:|---:|---:|---|---|")
    for name, b in sorted(by_block.items(), key=lambda kv: -kv[1]["count"]):
        top = sorted(b["cps"], key=lambda kv: -kv[1]["articles"])[:10]
        mix = ", ".join(
            "%s %d" % (k, v)
            for k, v in sorted(b["outs"].items(), key=lambda kv: -kv[1])
        )
        tops = " ".join("%s(%d)" % (esc_md(e["char"]), e["articles"]) for _, e in top)
        w(
            "| %s | %d | %d | %d | %s | %s |"
            % (
                name,
                len(b["cps"]),
                b["count"],
                census.get("articles_per_block", {}).get(name, 0),
                mix,
                tops,
            )
        )

    def section(title, pred, n, note):
        w("\n## %s\n" % title)
        w(note + "\n")
        items = sorted(
            ((cp, e) for cp, e in chars.items() if pred(e)),
            key=lambda kv: (-kv[1]["articles"], -kv[1]["count"]),
        )
        w("%d code points; showing %d.\n" % (len(items), min(n, len(items))))
        w(
            "| char | U+ | name | block | occurrences | articles | in () | outcome | example |"
        )
        w("|---|---|---|---|---:|---:|---:|---|---|")
        for cp, e in items[:n]:
            ex = fmt_example(e["examples"][0]) if e["examples"] else ""
            inp = "%d%%" % (100 * e["in_paren"] // max(1, e["count"]))
            w(
                "| %s | %s | %s | %s | %d | %d | %s | %s | %s |"
                % (
                    esc_md(e["char"]),
                    cp,
                    e["name"],
                    e["block"],
                    e["count"],
                    e["articles"],
                    inp,
                    e["outcome"],
                    ex,
                )
            )

    section(
        "Dropped",
        lambda e: e["outcome"] == "dropped",
        top_chars,
        "Nothing in the pipeline knows these; the run rules remove them with whatever "
        "touches them. Ranked by articles affected. `in ()` is the share of occurrences "
        "inside a parenthetical, where the lead rule removes the whole labelled aside.",
    )
    section(
        "Greek",
        lambda e: e["outcome"].startswith("spelled when"),
        60,
        "A lone letter is spelled by name; a letter inside a Greek word is dropped with the word.",
    )
    section(
        "Compatibility forms",
        lambda e: e["outcome"].startswith("compat"),
        60,
        "Drawn as the plain form Unicode names as equivalent: a circled digit as the digit, a script capital as the capital, a fullwidth comma as a comma.",
    )
    section(
        "Decomposed",
        lambda e: e["outcome"] == "decomposed",
        60,
        "Drawn as the base letter plus its combining mark, which the renderer overlays: nothing lost.",
    )
    section(
        "Look-alike letters",
        lambda e: e["outcome"].startswith("lookalike"),
        60,
        "Inside a word (a token with letters and no hyphen) the letter becomes the plain letter it stands in for; elsewhere it is a pronunciation symbol and goes with its span.",
    )
    section(
        "Folded to the base letter",
        lambda e: e["outcome"].startswith("folded"),
        120,
        "The accent goes, the letter stays. Every one of these is a loss of information "
        "the reader cannot see; the question per row is whether the base letter misleads.",
    )
    section(
        "Spelled",
        lambda e: e["outcome"] == "spelled",
        80,
        "Replaced by the spelling in symbols.py.",
    )

    w("\n## Most damaged articles\n")
    w(
        "Collateral is drawable letters removed because they touched an undrawable run "
        "(a whole parenthetical, a labelled name, a slashed pronunciation). Ranked by "
        "letters lost; the share is against the letters the article kept.\n"
    )
    w("| article | letters lost | share | what went (one example) |")
    w("|---|---:|---:|---|")
    for a in census["damaged_articles"][:150]:
        share = 100.0 * a["collateral"] / max(1, a["letters"] + a["collateral"])
        ex = a["examples"][0] if a["examples"] else {"context": "", "gone": ""}
        w(
            "| %s | %d | %.1f%% | gone: \u201c%s\u201d in \u201c%s\u201d |"
            % (
                esc_md(a["title"]),
                a["collateral"],
                share,
                esc_md(ex["gone"]),
                esc_md(ex["context"]),
            )
        )
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


def main(argv=None):
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--rows", nargs="+", help="rows files (jsonl or jsonl.gz)")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--workers", type=int, default=max(1, (os.cpu_count() or 2) - 2))
    ap.add_argument("--json", help="write the full census here")
    ap.add_argument("--md", help="write the report here")
    ap.add_argument("--from-json", help="skip the scan; report from an existing census")
    args = ap.parse_args(argv)
    if args.from_json:
        with open(args.from_json, encoding="utf-8") as f:
            census = json.load(f)
        for cp, e in census["chars"].items():  # the tables may have moved since the scan
            e["outcome"] = outcome(int(cp, 16))
    else:
        if not args.rows:
            ap.error("--rows or --from-json")
        census = run(args)
        if args.json:
            with open(args.json, "w", encoding="utf-8") as f:
                json.dump(census, f, ensure_ascii=False, indent=1)
    if args.md:
        report(census, args.md)
    print(
        "%d rows; %d code points; %d collateral letters in %d articles"
        % (
            census["rows"],
            len(census["chars"]),
            census["collateral_letters"],
            census["articles_with_collateral"],
        )
    )


if __name__ == "__main__":
    main()

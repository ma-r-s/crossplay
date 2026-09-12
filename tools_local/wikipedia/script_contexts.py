#!/usr/bin/env python3
"""Where the scripts the panel should not show actually occur.

    script_contexts.py --rows <rows.jsonl.gz> [--limit N] [--workers N] --md <report>

Mario, 2026-09-11: "If I can't even read them why would I want them here?
Think about how to get rid of them in a smart way that doesn't butcher the
articles. See where they are used." This counts every run of Greek, Cyrillic,
IPA and every other non-Latin script in the text the converter feeds its strip
step, by the place it stands in:

  pronunciation   inside a slashed or bracketed span: /.../ or [...]
  labelled        inside a parenthetical, after a "Script:" or "romanized:"
                  label in the same segment
  parenthetical   inside a parenthetical with no label ("(\u041c\u043e\u0441\u043a\u0432\u0430)")
  prose           in running text outside any parenthetical
  heading, fact, table, list   by the block it sits in

and says, per script and place, how many runs and articles, whether a
romanisation stands beside the run, and shows real examples. The removal
rule for each cell of that table is then a decision made on evidence.
"""

import argparse
import json
import multiprocessing as mp
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import article_html  # noqa: E402
import symbols  # noqa: E402
from build_pack import read_rows  # noqa: E402
from glyphs import drawable_class  # noqa: E402

EXAMPLES = 4

SCRIPTS = [
    ("Greek", 0x0370, 0x03FF),
    ("Greek", 0x1F00, 0x1FFF),
    ("Cyrillic", 0x0400, 0x052F),
    ("IPA", 0x0250, 0x02AF),
    ("IPA", 0x02B0, 0x02FF),
    ("IPA", 0x1D00, 0x1DBF),
    ("Arabic", 0x0600, 0x06FF),
    ("Arabic", 0x0750, 0x077F),
    ("Arabic", 0xFB50, 0xFDFF),
    ("Arabic", 0xFE70, 0xFEFF),
    ("Hebrew", 0x0590, 0x05FF),
    ("Devanagari", 0x0900, 0x097F),
    ("Bengali", 0x0980, 0x09FF),
    ("Tamil", 0x0B80, 0x0BFF),
    ("Thai", 0x0E00, 0x0E7F),
    ("Tibetan", 0x0F00, 0x0FFF),
    ("Armenian", 0x0530, 0x058F),
    ("Georgian", 0x10A0, 0x10FF),
    ("Hangul", 0xAC00, 0xD7AF),
    ("Hangul", 0x1100, 0x11FF),
    ("Hangul", 0x3130, 0x318F),
    ("Kana", 0x3040, 0x30FF),
    ("Han", 0x4E00, 0x9FFF),
    ("Han", 0x3400, 0x4DBF),
    ("Han", 0x20000, 0x2FA1F),
    ("Han", 0x3000, 0x303F),
    ("Han", 0xFF00, 0xFFEF),
]


def script_of(ch):
    cp = ord(ch)
    for name, lo, hi in SCRIPTS:
        if lo <= cp <= hi:
            return name
    return "other"


_run_re = None
_slashed = re.compile(r"/[^/]{1,80}/|\[[^\[\]]{1,80}\]")
_label = re.compile(
    r"(?:(?<![A-Za-z])[A-Z][A-Za-z]*(?:[ -][A-Za-z]+){0,2}|romanized|romanised|lit\.|literally|pinyin|IPA|born)\s*:\s*$"
)
_romanised = re.compile(
    r"\b(?:romani[sz]ed|translit(?:eration|erated)?|pinyin|lit\.|IPA)\s*:", re.I
)


def _runs():
    """Runs of code points the panel should not show: what the serif lacks,
    plus Cyrillic, which it draws and Mario does not want."""
    global _run_re
    if _run_re is None:
        bad = "[^" + drawable_class() + "]|[\u0400-\u052f]"
        _run_re = re.compile("(?:%s)(?:\\s*(?:%s))*" % (bad, bad))
    return _run_re


def classify(text, start, end):
    """(place, romanised_nearby) for the run text[start:end]."""
    for m in _slashed.finditer(text):
        if m.start() <= start and end <= m.end():
            return "pronunciation", False
    depth = 0
    open_at = -1
    for j in range(start - 1, -1, -1):
        c = text[j]
        if c == ")":
            depth += 1
        elif c == "(":
            if depth == 0:
                open_at = j
                break
            depth -= 1
    if open_at < 0:
        return "prose", bool(_romanised.search(text[end : end + 60]))
    close_at = text.find(")", end)
    segment = text[open_at + 1 : close_at if close_at > 0 else len(text)]
    seg_start = open_at + 1
    before = text[seg_start:start]
    last_sep = max(before.rfind(";"), before.rfind(","))
    head = before[last_sep + 1 :] if last_sep >= 0 else before
    romanised = bool(_romanised.search(segment)) or bool(
        re.search(r",\s*[A-Z][a-z]", text[end : end + 40])
    )
    if _label.search(head.strip() + ":") and head.strip():
        return "labelled", romanised
    if _label.search(before[-40:].rstrip() + ":") and re.search(
        r"[A-Za-z]:\s*$", before.rstrip() + ":"
    ):
        return "labelled", romanised
    if re.search(r"[A-Za-z]:\s*$", before):
        return "labelled", romanised
    return "parenthetical", romanised


_kind = "paragraph"
_rec = None
_orig_strip = article_html.strip_undrawable


def _strip_recording(text, stats, lead=False):
    rec = _rec
    if rec is not None and text:
        # what the converter spells or folds is not a script run: count only
        # what would still stand after those two steps
        counted, _ = symbols.translate(text)
        counted, _ = article_html._fold_chars(counted)
        run_re = _runs()
        for m in run_re.finditer(counted):
            text_was, text = text, counted
            run = m.group(0)
            letters = [c for c in run if not c.isspace()]
            if not letters:
                continue
            script = script_of(letters[0])
            place, romanised = classify(text, m.start(), m.end())
            if _kind != "paragraph":
                place = _kind
            key = (script, place, romanised)
            e = rec["runs"].get(key)
            if e is None:
                e = rec["runs"][key] = [0, []]
            e[0] += 1
            if len(e[1]) < 1 and rec["examples_left"] > 0:
                lo = max(0, m.start() - 70)
                hi = min(len(text), m.end() + 70)
                while lo > 0 and not text[lo - 1].isspace():
                    lo -= 1
                while hi < len(text) and not text[hi].isspace():
                    hi += 1
                e[1].append(text[lo:hi])
                rec["examples_left"] -= 1
            text = text_was
    return _orig_strip(text, stats, lead)


def _wrap(method_name, kind):
    original = getattr(article_html._Doc, method_name)

    def wrapped(self, *a, **kw):
        global _kind
        saved = _kind
        _kind = kind
        try:
            return original(self, *a, **kw)
        finally:
            _kind = saved

    setattr(article_html._Doc, method_name, wrapped)


def _init():
    article_html.strip_undrawable = _strip_recording
    _wrap("heading", "heading")
    _wrap("facts", "fact")
    _wrap("table_html", "table")
    _wrap("list_items", "list")


def convert(row):
    global _rec
    _rec = {"runs": {}, "examples_left": 40}
    try:
        title, _, _ = article_html.article_xhtml(row, {})
    except (ValueError, TypeError):
        _rec = None
        return None
    rec = _rec
    _rec = None
    rec["title"] = title
    return rec


def main(argv=None):
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--rows", nargs="+", required=True)
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--workers", type=int, default=max(1, (os.cpu_count() or 2) - 2))
    ap.add_argument("--md", required=True)
    ap.add_argument("--json")
    args = ap.parse_args(argv)
    t0 = time.time()

    def rows_iter():
        n = 0
        for row in read_rows(args.rows):
            if args.limit and n >= args.limit:
                return
            n += 1
            yield row

    table = {}  # (script, place, romanised) -> {runs, articles, examples}
    rows = 0
    ctx = mp.get_context("fork")
    with ctx.Pool(processes=args.workers, initializer=_init) as pool:
        for rec in pool.imap_unordered(convert, rows_iter(), chunksize=8):
            rows += 1
            if rec is None:
                continue
            for key, (n, exs) in rec["runs"].items():
                e = table.get(key)
                if e is None:
                    e = table[key] = {"runs": 0, "articles": 0, "examples": []}
                e["runs"] += n
                e["articles"] += 1
                for ex in exs:
                    if len(e["examples"]) < EXAMPLES:
                        e["examples"].append((rec["title"], ex))
            if rows % 10000 == 0:
                print(
                    "%d rows, %ds" % (rows, time.time() - t0),
                    file=sys.stderr,
                    flush=True,
                )

    lines = [
        "# Where the scripts the panel should not show stand\n",
        "%d articles, %ds.\n" % (rows, time.time() - t0),
    ]
    lines.append(
        "A run is one stretch of a script the serif lacks (or Cyrillic, which it draws and Mario does not want). "
        "`romanised` means a romanisation, transliteration or pinyin stands beside it in the same segment, "
        "or a capitalised word follows it after a comma.\n"
    )
    lines.append("| script | place | romanised beside | runs | articles |")
    lines.append("|---|---|---|---:|---:|")
    for (script, place, rom), e in sorted(
        table.items(), key=lambda kv: (-kv[1]["articles"], kv[0])
    ):
        lines.append(
            "| %s | %s | %s | %d | %d |"
            % (script, place, "yes" if rom else "no", e["runs"], e["articles"])
        )
    lines.append("\n## Examples\n")
    for (script, place, rom), e in sorted(
        table.items(), key=lambda kv: (-kv[1]["articles"], kv[0])
    ):
        lines.append(
            "\n### %s, %s, romanised beside: %s (%d articles)\n"
            % (script, place, "yes" if rom else "no", e["articles"])
        )
        for title, ex in e["examples"]:
            lines.append(
                "- %s: \u201c%s\u201d"
                % (title.replace("|", "/"), ex.replace("|", "/").replace("\n", " "))
            )
    with open(args.md, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(
                {"%s|%s|%s" % k: v for k, v in table.items()},
                f,
                ensure_ascii=False,
                indent=1,
            )
    print("%d rows; %d script/place cells -> %s" % (rows, len(table), args.md))


if __name__ == "__main__":
    main()

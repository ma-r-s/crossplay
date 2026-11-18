#!/usr/bin/env python3
"""Cut the German test articles out of a Kiwix ZIM.

    tests/make_zim_fixtures.py wikipedia_de_top_nopic_2026-10.zim

Each fixture is one real article as the ZIM holds it, with its pictures and
style sheets taken out (zim_article.parse takes them out too) and cut at a
section boundary after about 40 KB, gzipped: the lead, the infobox and the
first sections, which is where the rules under test act. The articles are
Wikipedia's, CC BY-SA 4.0.
"""

import gzip
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

import zim  # noqa: E402
from zim_article import _IMG, _STYLE  # noqa: E402

ARTICLES = {
    # ß in the title, umlauts throughout, apparatus sections at the end
    "Straße": "strasse",
    # a taxobox: the infobox as a grid
    "Rotfuchs": "rotfuchs",
    # a film infobox whose values are lines (companies, cast)
    "Titanic_(1997)": "titanic",
    # a linked IPA transcription in brackets
    "Bordeaux": "bordeaux",
    # Cyrillic inside an audio button
    "Moskau": "moskau",
    # Greek with the source's own transliteration beside it
    "Athen": "athen",
    # Chinese with Pinyin beside it
    "Peking": "peking",
    # the longest title in the set, 100 bytes
    "Borat_–_Kulturelle_Lernung_von_Amerika,_um_Benefiz_für_glorreiche_Nation_von_Kasachstan_zu_machen": "borat",
    # formulas
    "Nullstelle": "nullstelle",
    # a country's box: flag and arms over their captions, a value of lines
    # with dashed sub-items and labels ("- regionale Amtssprachen:")
    "\u00d6sterreich": "oesterreich",
}
BUDGET = 40_000


# kept whole: the sections the converter drops by name are at the end
WHOLE = {"strasse"}


def cut(html, whole=False):
    start = html.find("mw-parser-output")
    start = html.rfind("<", 0, start)
    end = html.find("<!--htdig_noindex-->")
    body = _STYLE.sub("", _IMG.sub("", html[start:end]))
    if not whole and len(body) > BUDGET:
        at = body.find("<section", BUDGET)
        if at > 0:
            body = body[:at]
    return body


def main(path):
    z = zim.Zim(path)
    for title, name in ARTICLES.items():
        e = z.find(title)
        if e is None:
            sys.exit(f"{title} is not in {path}")
        html = z.content(e).decode("utf-8")
        out = os.path.join(HERE, "fixtures", "zim_de", name + ".html.gz")
        with gzip.open(out, "wt", encoding="utf-8", compresslevel=9) as f:
            f.write(cut(html, name in WHOLE))
        print(f"{name}: {os.path.getsize(out):,} bytes")
    # a redirect to a section, as the ZIM writes it
    e = z.find("Schwarze_Blattern")
    with open(os.path.join(HERE, "fixtures", "zim_de", "section_redirect.html"), "wb") as f:
        f.write(z.content(e))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

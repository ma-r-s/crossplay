#!/usr/bin/env python3
"""The chrome: the pages a person sees before they are anybody here.

These run without a network and without the fake service, because what they
check is what the HTML says, not what the bridge does with it.

The bug they exist for: an SVG attribute written unquoted swallows the tag's
own self-closing slash (`height="40/"`, tag never closed), so the whole figure
renders as an empty box with a caption under it. Nothing else notices. Every
suite stayed green, the login page still worked, and the only signal was
looking at it. So one of these tests parses the drawings and counts what is in
them, and another refuses an unquoted attribute anywhere in the chrome.

Run: .venv/bin/python tests/test_pages.py
"""

import pathlib
import re
import sys
import xml.etree.ElementTree as ET

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(ROOT))
# bridge.app pulls in the deck engine, which imports the converter by flat name
# the way the image's PYTHONPATH provides it. Same two lines as test_api.py.
REPO = ROOT.parent.parent
sys.path.insert(0, str(REPO / "tools_local" / "study"))

from bridge import chrome  # noqa: E402

FAILED = 0


def ok(cond, what):
    global FAILED
    print(("  ok   " if cond else "  FAIL ") + what)
    if not cond:
        FAILED += 1


# --------------------------------------------------------------- the drawings
FIGURES = {
    "service_flow": chrome.service_flow(),
    "reader_with_code": chrome.reader_with_code(),
    "confirm_on_reader": chrome.confirm_on_reader(),
}
GLYPHS = {
    "MARK": chrome.MARK,
    "TICK": chrome.TICK,
    "BAR": chrome.BAR,
    "READER": chrome.READER,
}

# An attribute whose value is not quoted. Legal HTML until the tag self-closes,
# and then it is the bug at the top of this file.
UNQUOTED = re.compile(r"""\s[A-Za-z-]+=(?!["'])""")

# A figure is a diagram and a glyph is one mark, so they are not held to the
# same count -- but an empty one is the failure either way.
for name, html in {**FIGURES, **GLYPHS}.items():
    ok(not UNQUOTED.search(html), f"{name} quotes every attribute")

    least = 8 if name in FIGURES else 1
    svg = html[html.index("<svg") : html.rindex("</svg>") + 6]
    try:
        root = ET.fromstring(svg)
        drawn = [
            e
            for e in root.iter()
            if e.tag.split("}")[-1] in ("rect", "path", "circle", "line", "text")
        ]
        ok(len(drawn) >= least, f"{name} draws {len(drawn)} shapes (needs {least})")
    except ET.ParseError as e:
        ok(False, f"{name} is well-formed SVG ({e})")

# The pairing code is eight characters (bridge/pairing.py) from an alphabet
# with no 0/O/1/I, and the figure teaches people what to look for. A sample of
# a different length, or one using a character a real code never contains,
# teaches them to expect the wrong thing.
fig = chrome.reader_with_code()
sample = re.search(r">([A-Z0-9]{4,})</text>", fig)
ok(sample is not None, "the pairing figure shows a sample code")
if sample:
    from bridge import pairing

    code = sample.group(1)
    ok(len(code) == 8, f"the sample code is 8 characters, like a real one ({code})")
    ok(
        all(c in pairing.CODE_ALPHABET for c in code),
        f"every character in it can occur in a real code ({code})",
    )
ok(fig.count("h10M") + fig.count('h10"') >= 8, "the empty field shows eight places")

# ------------------------------------------------------------------- the rail
for step in (1, 2, 3):
    body = chrome.page("t", "<h1>h</h1>", step=step).body.decode()
    ok(body.count("aria-current=step") == 1, f"step {step} marks exactly one place")
    ok(body.count("class='now'") == 1, f"step {step} fills exactly one dot")
    ok(
        body.count("class='done'") == step - 1,
        f"step {step} ticks the {step - 1} behind it",
    )

ok("class=dots" in chrome.waiting(), "the waiting page marks itself as waiting")
ok("will not change by itself" in chrome.waiting(),
   "and says so, because a silent screen reads as a broken one")
ok("a.btn{" in chrome.CSS, "a link that is the only way on is set as a button")

plain = chrome.page("t", "<h1>h</h1>").body.decode()
ok("aria-current" not in plain, "a page off the path claims no position in it")
ok("<title>" in plain and "CrossPlay" in plain, "every page is titled and branded")
ok("class=band" in plain and "class=rule" in plain, "every page carries the band")

# ------------------------------------------------------------------ the fonts
static = ROOT / "bridge" / "static"
for f in ("jersey25.woff2", "instrumentserif.woff2"):
    ok((static / f).is_file(), f"{f} is vendored beside the app")
    ok(f"url(/assets/{f})" in chrome.CSS, f"the CSS asks for {f} where it is served")
for lic in ("OFL-Jersey25.txt", "OFL-InstrumentSerif.txt"):
    ok((static / lic).is_file(), f"{lic} ships with the face it licenses")

from bridge import app as appmod  # noqa: E402

ok(
    appmod._ASSETS == {"jersey25.woff2", "instrumentserif.woff2"},
    "the asset route is an allowlist, so no name can traverse out of it",
)

# -------------------------------------------------------------- the two twins
# The other service's chrome is this one with three strings changed. Drift
# between them is how one bridge quietly stops looking like the product.
twin = (
    ROOT.parent
    / ("study-bridge" if ROOT.name == "read-bridge" else "read-bridge")
    / "bridge"
    / "chrome.py"
)
if twin.is_file():
    mine = (ROOT / "bridge" / "chrome.py").read_text()
    theirs = twin.read_text()
    norm = lambda t: (
        t.replace("Instapaper", "@")
        .replace("AnkiWeb", "@")  # noqa: E731
        .replace("Read later", "%")
        .replace("Anki sync", "%")
        .replace("read.ma-r-s.com", "#")
        .replace("sync.ma-r-s.com", "#")
        .replace("articles", "&")
        .replace("cards", "&")
        .replace("study-bridge/bridge/chrome.py", "~")
        .replace("read-bridge/bridge/chrome.py", "~")
    )
    ok(norm(mine) == norm(theirs), "both bridges' chrome is the same file")

print(f"\n{'FAILED' if FAILED else 'ok'}: {FAILED} failed")
sys.exit(1 if FAILED else 0)

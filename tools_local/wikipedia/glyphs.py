"""Which code points the reader's serif can draw.

The builder reads the glyph intervals straight out of the font header the
device compiles in (lib/EpdFont/builtinFonts/notoserif_14_regular.h), so
"drawable" means exactly what the panel can show and moves when the font
does. A code point outside every interval would render as a box.
"""

import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
DEFAULT_FONT_HEADER = os.path.join(
    REPO, "lib", "EpdFont", "builtinFonts", "notoserif_14_regular.h"
)

_INTERVALS_RE = re.compile(
    r"EpdUnicodeInterval\s+\w+Intervals\[\]\s*=\s*\{(.*?)\};", re.S
)
_ENTRY_RE = re.compile(
    r"\{\s*0x([0-9A-Fa-f]+)\s*,\s*0x([0-9A-Fa-f]+)\s*,\s*0x[0-9A-Fa-f]+\s*\}"
)

_cache = {}


def font_header():
    """The header "drawable" is read from: the device's serif, unless
    CROSSPLAY_WIKI_FONT_HEADER names a pinned copy. The tests pin the table
    their expected outputs were written against, so a font that gains a glyph
    upstream moves the builder's output and not their verdict."""
    return os.environ.get("CROSSPLAY_WIKI_FONT_HEADER") or DEFAULT_FONT_HEADER


def load_intervals(path=None):
    """[(first, last), ...] as written in the header, in file order."""
    path = path or font_header()
    if path in _cache:
        return _cache[path]
    with open(path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    m = _INTERVALS_RE.search(text)
    if not m:
        raise ValueError(f"no EpdUnicodeInterval table in {path}")
    intervals = [(int(a, 16), int(b, 16)) for a, b in _ENTRY_RE.findall(m.group(1))]
    if not intervals:
        raise ValueError(f"empty interval table in {path}")
    _cache[path] = intervals
    return intervals


def drawable_class(path=None):
    """A regex character class body matching every drawable code point,
    with the whitespace the text pipeline keeps (space) always included."""
    parts = []
    for lo, hi in load_intervals(path):
        if lo < 0x20:
            continue  # controls: the text pipeline removes them anyway
        if lo == hi:
            parts.append("\\u%04x" % lo if lo < 0x10000 else "\\U%08x" % lo)
        else:
            parts.append(
                ("\\u%04x-\\u%04x" % (lo, hi))
                if hi < 0x10000
                else ("\\U%08x-\\U%08x" % (lo, hi))
            )
    return "".join(parts)


def is_drawable(cp, path=None):
    for lo, hi in load_intervals(path):
        if lo <= cp <= hi:
            return True
    return False

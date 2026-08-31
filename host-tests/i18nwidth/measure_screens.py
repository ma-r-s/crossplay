#!/usr/bin/env python3
"""Measure every unwrapped UI string against the panel width."""

import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(REPO / "tools_local" / "i18n"))
from measure_string import load_font, measure  # noqa: E402

PANEL_WIDTH = 480
FONTS = REPO / "lib/EpdFont/builtinFonts"

# Slot -> face files. From src/main.cpp, where UI_10_FONT_ID binds to
# ui10FontFamily and UI_12_FONT_ID to ui12FontFamily. Verified, not assumed.
FACES = {
    ("10", False): FONTS / "ubuntu_10_regular.h",
    ("10", True): FONTS / "ubuntu_10_bold.h",
    ("12", False): FONTS / "ubuntu_12_regular.h",
    ("12", True): FONTS / "ubuntu_12_bold.h",
}

# drawCenteredText(<font id>, ..., tr(STR_X) [, ..., EpdFontFamily::BOLD])
# The style argument is captured because BOLD is about 5% wider: a gate that
# measured only the regular face would under-measure twelve settings call sites,
# which is exactly enough to license a string that then overflows.
CALL = re.compile(
    r"drawCenteredText\((?P<args>[^;]*?UI_(?P<slot>\d+)_FONT_ID[^;]*?tr\((?P<key>STR_[A-Z0-9_]+)\)[^;]*?)\)",
    re.S,
)


def translations(path=None):
    out = {}
    path = path or REPO / "lib/I18n/translations/english.yaml"
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        m = re.match(r'^(STR_[A-Z0-9_]+):\s*"(.*)"\s*$', line)
        if m:
            out[m.group(1)] = m.group(2)
    return out


def sites():
    found = []
    for path in sorted((REPO / "src").rglob("*")):
        if path.suffix not in (".cpp", ".h"):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for m in CALL.finditer(text):
            bold = "EpdFontFamily::BOLD" in m.group("args")
            line = text[: m.start()].count("\n") + 1
            found.append((m.group("slot"), bold, m.group("key"),
                          "%s:%d" % (path.relative_to(REPO), line)))
    return found


def survey():
    """Every language, reported and NOT gated. 26 of 32 languages had at least
    one overflowing unwrapped string when this was written -- a live shipping
    defect on the recovery and sync screens, in Catalan, Czech, German, Kazakh
    and 22 others. English is one of the six that fit, which is exactly why a
    green English gate must not be read as "it fits everywhere".

    Fixing it is a translator pass, not a release task, so this reports rather
    than fails. docs/translators.md carries the constraint.
    """
    faces = {k: load_font(v) for k, v in FACES.items() if v.exists()}
    keys = {(slot, bold, key) for slot, bold, key, _ in sites()}
    rows = []
    for path in sorted((REPO / "lib/I18n/translations").glob("*.yaml")):
        strings = translations(path)
        worst = None
        for slot, bold, key in keys:
            face = faces.get((slot, bold))
            if face is None or key not in strings:
                continue
            width, _ = measure(strings[key], *face)
            if width > PANEL_WIDTH and (worst is None or width > worst[1]):
                worst = (key, width)
        rows.append((path.stem, worst))
    over = [r for r in rows if r[1]]
    print("all-language survey (reported, not gated)")
    for name, worst in sorted(over, key=lambda r: -r[1][1]):
        print("  %-12s worst %7.1fpx > %dpx  %s" % (name, worst[1], PANEL_WIDTH, worst[0]))
    print("  %d of %d languages have at least one overflowing unwrapped string"
          % (len(over), len(rows)))
    return 0


def main():
    if "--all-languages" in sys.argv:
        return survey()
    strings = translations()
    faces = {k: load_font(v) for k, v in FACES.items() if v.exists()}
    if not faces:
        print("FAIL no font headers found under %s" % FONTS)
        return 1

    seen, failures, missing = set(), [], []
    print("unwrapped screen strings")
    for slot, bold, key, where in sites():
        if (slot, bold, key) in seen:
            continue
        seen.add((slot, bold, key))
        face = faces.get((slot, bold))
        if face is None or key not in strings:
            continue
        glyphs, intervals = face
        # measure() reports characters the face has no glyph for. They draw as
        # NOTHING AT ALL, so without this they would cost zero width and a
        # broken string would measure as a comfortable fit.
        width, gaps = measure(strings[key], glyphs, intervals)
        if gaps:
            missing.append((key, where, "".join(sorted(set(gaps)))))
        if width > PANEL_WIDTH:
            failures.append((key, where, width, slot, bold))

    for key, where, width, slot, bold in sorted(failures, key=lambda r: -r[2]):
        print("  FAIL %-32s %7.1fpx > %dpx  UI_%s%s  %s"
              % (key, width, PANEL_WIDTH, slot, " BOLD" if bold else "", where))
    for key, where, chars in missing:
        print("  FAIL %-32s has no glyph for %r  %s" % (key, chars, where))

    total = len(seen)
    bad = len(failures) + len(missing)
    if not bad:
        # Say what this does NOT cover. The gate reads english.yaml, and 26 of
        # the 32 translations have an overflowing unwrapped string today, so a
        # bare "all fit" would certify a defect rather than catch it.
        print("  ok   %d unwrapped strings all fit %dpx -- ENGLISH ONLY" % (total, PANEL_WIDTH))
        print("       other languages are NOT gated: run with --all-languages")
    print("%d checks, %d failed" % (total, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

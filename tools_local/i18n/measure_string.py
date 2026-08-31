#!/usr/bin/env python3
"""Measure how wide a UI string will actually be, in pixels, before shipping it.

    tools_local/i18n/measure_string.py "Firmware too large. Reinstall over USB."
    tools_local/i18n/measure_string.py --key STR_FIRMWARE_TOO_LARGE

Why this exists: several screens draw a string through
`renderer.drawCenteredText`, which is ONE LINE and does not wrap. Nothing gates
i18n string widths -- host-tests/brand reads the translation files for brand
names and key parity and never measures anything -- so a string that is one word
too long runs off the panel with every suite green. On 2026-08-31 that was
nearly shipped into STR_FIRMWARE_TOO_LARGE, and the same family of bug
(something drawn wider than its box, with no glyph for the ellipsis that would
have marked it) accounted for three near-misses in one night.

Counting characters does not answer it: the face is proportional, so "iiiii" and
"WWWWW" differ by a factor of three. This reads the real advance widths out of
the generated font header.

The panel is 480px wide. A safe reference point rather than a rule: the longest
string known to render on the firmware-update screens today is
STR_POWER_ON_HINT at 421px.
"""

import argparse
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
# The face those screens use: main.cpp binds UI_10_FONT_ID to ui10FontFamily.
FONT = REPO / "lib/EpdFont/builtinFonts/ubuntu_10_regular.h"
TRANSLATIONS = REPO / "lib/I18n/translations/english.yaml"
PANEL_WIDTH = 480


def load_font(path: pathlib.Path):
    """-> (glyphs, intervals). advanceX is 12.4 fixed-point (EpdFontData.h)."""
    src = path.read_text()
    name = path.stem
    start = src.index(f"{name}Glyphs[] = {{")
    glyphs = [
        tuple(int(x, 0) for x in m)
        for m in re.findall(
            r"\{\s*(-?\w+),\s*(-?\w+),\s*(-?\w+),\s*(-?\w+),\s*(-?\w+),\s*(-?\w+),\s*(-?\w+)\s*\}",
            src[start : src.index("\n};", start)],
        )
    ]
    start = src.index(f"{name}Intervals[] = {{")
    intervals = [
        tuple(int(x, 0) for x in m)
        for m in re.findall(
            r"\{\s*(0x[0-9A-Fa-f]+|\d+),\s*(0x[0-9A-Fa-f]+|\d+),\s*(0x[0-9A-Fa-f]+|\d+)\s*\}",
            src[start : src.index("\n};", start)],
        )
    ]
    return glyphs, intervals


def measure(text: str, glyphs, intervals):
    """-> (pixels, missing). A character the face has no glyph for draws as
    NOTHING AT ALL, so it is reported rather than silently costing zero."""
    total = 0.0
    missing = []
    for ch in text:
        cp = ord(ch)
        for first, last, offset in intervals:
            if first <= cp <= last:
                total += glyphs[offset + (cp - first)][2] / 16.0
                break
        else:
            missing.append(ch)
    return total, missing


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("text", nargs="*", help="string(s) to measure")
    ap.add_argument("--key", action="append", default=[], help="measure a STR_ key from english.yaml")
    ap.add_argument("--width", type=int, default=PANEL_WIDTH, help=f"box width in px (default {PANEL_WIDTH})")
    args = ap.parse_args()

    glyphs, intervals = load_font(FONT)
    items = [(None, t) for t in args.text]
    if args.key:
        yaml = TRANSLATIONS.read_text()
        for key in args.key:
            m = re.search(rf'^{re.escape(key)}:\s*"(.*)"\s*$', yaml, re.M)
            if not m:
                sys.exit(f"no such key in english.yaml: {key}")
            items.append((key, m.group(1)))
    if not items:
        ap.error("give a string or a --key")

    worst = 0.0
    for key, text in items:
        px, missing = measure(text, glyphs, intervals)
        worst = max(worst, px)
        verdict = "fits" if px <= args.width else "OVERFLOWS"
        print(f"{px:7.1f}px  {verdict:9s}  {key or ''}{' ' if key else ''}{text!r}")
        if missing:
            print(f"           no glyph for {missing!r} -- these draw as nothing at all")
    return 1 if worst > args.width else 0


if __name__ == "__main__":
    sys.exit(main())

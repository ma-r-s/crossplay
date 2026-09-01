#!/usr/bin/env python3
"""Measure every string WAVELENGTH can draw against the box it is drawn in.

Exists because half this app's pipeline was rigorous and the other half was
trusted. gen_pairs.py has always refused a deck word it cannot fit; the screens'
own labels were never measured at all, and the end-call's spectrum buttons were
one 4px margin away from ellipsising UNDERRATED LETTER OF THE ALPHABET into a
different word on the one screen where the word is the question.

TWO THINGS THIS ENCODES THAT ARE EASY TO GET WRONG BY HAND.

A CHARACTER COUNT IS NOT A WIDTH. `LIVED IN` and `GROWN-UP` are both eight
characters and differ by 70px at the display cut. The host ui suite cannot do
this job: its FakeTarget measures ten pixels a character, so an overflow
assertion there is a character count wearing a width's clothes.

A SLOT IS NOT A FACE, AND THE SLOT'S NAME IS NOT ITS SIZE. The three font slots
resolve to whatever `Faces` the ACTIVITY binds, so the face is read from the
call site below rather than assumed from the slot's name. An audit that guesses
measured this app's title slot 45% narrow and reported clean. And note the
activity builds its `Faces` inline: enumerating the helpers in ToyboxTheme.h
would miss it entirely.
"""

import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path.insert(0, str(HERE))

from gen_pairs import glyph_advances, width  # noqa: E402

ACTIVITY = REPO / "src/apps_local/wavelength/WavelengthActivity.cpp"
SCREENS = REPO / "src/apps_local/wavelength/WavelengthScreens.cpp"

# Toybox font ids to the generated cut each one registers, from ToyboxFonts.cpp.
FACE_OF_ID = {
    "kTileFontId": "toybox_10",
    "kButtonFontId": "toybox_14",
    "kUiFontId": "toybox_20",
    "kDisplayFontId": "toybox_30",
    "kLargeFontId": "toybox_44",
    "kHugeFontId": "toybox_64",
}

# The panel, less the margin either side. Anything wider is truncated, and the
# Toybox cuts carry no ellipsis glyph, so it draws as nothing and the line
# simply stops.
CONTENT_WIDTH = 448

# The dial and reveal put text in a narrow right-hand column beside the strip,
# and it is HALF the content width. The first version of this gate measured only
# `inner` boxes and therefore said nothing about that column at all: it passed a
# 271px string into 224px of it within an hour. An instrument whose scope came
# from an assumption is the exact failure this file exists to prevent, so the
# box widths are derived here from the same arithmetic layout() uses.
PANEL_WIDTH = 480
MARGIN = 16
BOARD_W = PANEL_WIDTH * 5 // 16
BOARD_X = MARGIN + 48
RIGHT_X = BOARD_X + BOARD_W + 26
RIGHT_WIDTH = PANEL_WIDTH - MARGIN - RIGHT_X

BOX_WIDTH = {"inner": CONTENT_WIDTH, "g.right.width": RIGHT_WIDTH}

# Draws whose rect is a computed variable rather than an inline makeRect. The
# parser cannot see their width, so they are counted and capped rather than
# ignored: raising this number is a decision, and lowering it is progress.
# The four draws whose rect is a named local (box, numeral, label, capsule).
# A ratchet, not a budget: lowering it is progress, raising it is a decision
# somebody has to justify in a diff.
MAX_UNREACHED = 4
WIDTH_RE = r"((?:static_cast<int16_t>\([^()]*(?:\([^()]*\)[^()]*)*\)|[\w.]+))"


# Symbols a width expression may be written in terms of, all from layout().
SYMBOLS = {
    "inner": CONTENT_WIDTH,
    "g.right.width": RIGHT_WIDTH,
    "g.board.width": BOARD_W,
    "w": PANEL_WIDTH,
    "toybox::kMargin": MARGIN,
    "box.width": CONTENT_WIDTH,
}


def box_width(expr):
    """A rect's width in pixels, or None if this parser cannot read it.

    None is a FAILURE, never a skip: an unreadable width means the string is
    unmeasured, and unmeasured is indistinguishable from safe in the output.
    """
    expr = expr.strip()
    m = re.fullmatch(r"static_cast<int16_t>\((.*)\)", expr)
    if m:
        expr = m.group(1)
    for name, value in sorted(SYMBOLS.items(), key=lambda kv: -len(kv[0])):
        expr = expr.replace(name, str(value))
    if not re.fullmatch(r"[\d\s+\-*/()]+", expr):
        return None
    try:
        return int(eval(expr, {"__builtins__": {}}, {}))  # noqa: S307 -- digits and operators only
    except Exception:
        return None


def resolved_faces():
    """The app's three slots, read from the Faces literal the activity binds."""
    src = ACTIVITY.read_text(encoding="utf-8")
    m = re.search(r"toybox::Faces\s+\w+\{([^}]*)\}", src)
    if not m:
        raise SystemExit(
            "no Faces literal in WavelengthActivity.cpp -- read the call site and fix this script"
        )
    ids = [part.strip().replace("toybox::", "") for part in m.group(1).split(",")]
    if len(ids) != 3:
        raise SystemExit(f"expected three faces, found {ids}")
    slots = ("kSmallFont", "kBodyFont", "kDisplayFont")
    return {slot: FACE_OF_ID[i] for slot, i in zip(slots, ids)}


def strings_with_slots(src):
    """Every literal and format string, paired with the slot and box it uses.

    Returns (measurable, total_caps_calls). The difference between them is the
    important number: a gate that silently narrows its own scope reports clean
    for the strings it stopped looking at. This file's own first version
    measured only full-width boxes and passed a 271px string into a 224px
    column within the hour; its second dropped two draws when the parser got
    stricter, and said nothing.
    """
    flat = re.sub(r"\n\s+", " ", src)
    # A width may be a bare symbol or a static_cast expression with one level of
    # nested parentheses.
    w = r"((?:static_cast<int16_t>\((?:[^()]|\([^()]*\))*\)|[\w.]+))"
    rect = r"caps\(screen,\s*fui::makeRect\([^,]+,\s*[^,]+,\s*" + w + r",[^)]*\),\s*"
    out = []

    for m in re.finditer(rect + r'"([^"]*)",\s*toybox::(k\w+Font)', flat):
        box, text, slot = m.groups()
        wide = box_width(box)
        if text and wide:
            out.append((text, slot, wide))

    slot_of = {}
    box_of = {}
    for m in re.finditer(rect + r"([a-zA-Z]\w*),\s*toybox::(k\w+Font)", flat):
        box, buf, slot = m.groups()
        wide = box_width(box)
        if wide is None:
            continue
        slot_of[buf] = slot
        box_of[buf] = wide

    for m in re.finditer(r'snprintf\((\w+),\s*sizeof\(\1\),\s*"([^"]+)"', flat):
        buf, fmt = m.groups()
        slot = slot_of.get(buf)
        if slot is None:
            continue
        # Worst REACHABLE value, not worst representable. Guess and target are
        # clamped to the strip; rounds and points are uint16_t. Measuring an
        # unreachable 65535 reports a bug that cannot happen.
        wide = "20" if re.search(r"LOCK|TARGET|GUESS", fmt) else "65535"
        # A %s glued to a word is a plural suffix, not a word.
        plural = re.search(r"\w%s", fmt) is not None
        text = fmt.replace("%s", "S" if plural else "ABOVE").replace("%d", wide)
        out.append((text, slot, box_of.get(buf, CONTENT_WIDTH)))

    sites = re.findall(
        r'caps\(screen,\s*[^;]{0,160}?,\s*(?:"[^"]*"|[a-zA-Z]\w*),\s*toybox::k\w+Font', flat
    )
    return out, len(sites)


def main():
    faces = resolved_faces()
    advances = {slot: glyph_advances(face) for slot, face in faces.items()}
    print(
        "  slots resolved from the activity: "
        + ", ".join(f"{s} -> {f}" for s, f in faces.items())
    )

    measured, total_calls = strings_with_slots(SCREENS.read_text(encoding="utf-8"))
    bad = []
    checked = 0
    for text, slot, box in measured:
        checked += 1
        px = width(text, advances[slot])
        if px > box:
            bad.append((px, box, slot, text))

    for px, box, slot, text in sorted(bad, reverse=True):
        print(f"  OVERFLOWS {px:.0f}px of {box} [{slot} = {faces[slot]}]  {text!r}")
    if bad:
        print(
            f"  {len(bad)} of {checked} strings would be truncated with no ellipsis glyph to show for it"
        )
        return 1
    # A draw this parser cannot resolve is invisible to the gate, and invisible
    # reads exactly like safe. Report the shortfall rather than the successes.
    unreached = total_calls - checked
    print(f"  {checked} strings measured against their own box, none overflow")
    if unreached > MAX_UNREACHED:
        print(f"  BUT {unreached} of {total_calls} draw sites were not resolved and are UNMEASURED.")
        print("  Either extend this parser or give the draw an inline fui::makeRect it can read.")
        return 1
    print(f"  {unreached} of {total_calls} draw sites use a named rect this parser cannot read (ratchet {MAX_UNREACHED})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

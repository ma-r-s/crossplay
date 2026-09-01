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

# Draws whose rect is passed as a named Geometry member rather than built
# inline. Resolving them is better than widening the ratchet: the dial's two end
# words are the most-looked-at strings in the app and were invisible to this
# gate the moment they stopped being built inline.
NAMED_RECTS = {
    "g.topWord": CONTENT_WIDTH,  # makeRect(m, m, w - 2m, wordBox)
    "g.bottomWord": CONTENT_WIDTH,
}

# Draws whose rect is a computed variable rather than an inline makeRect. The
# parser cannot see their width, so they are counted and capped rather than
# ignored: raising this number is a decision, and lowering it is progress.
# Every draw is resolved. A ratchet, not a budget: raising it is a decision
# somebody has to justify in a diff, and at zero any new unreadable rect fails
# the gate the moment it is written rather than quietly leaving a string
# unmeasured.
MAX_UNREACHED = 0
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
    """Every measurable string with its slot and box, plus the draws missed.

    Returns (measurable, unresolved_descriptions). Coverage is computed from the
    MATCH OFFSETS of the resolvers themselves rather than from a second, looser
    regex counting "sites": counting two ways gave two answers that disagreed by
    five, and a gate whose own arithmetic is wrong gets ignored long before it
    catches anything.
    """
    flat = re.sub(r"\n\s+", " ", src)
    w = r"((?:static_cast<int16_t>\((?:[^()]|\([^()]*\))*\)|[\w.]+))"
    inline = r"caps\(screen,\s*fui::makeRect\([^,]+,\s*[^,]+,\s*" + w + r",[^)]*\),\s*"
    named = r"caps\(screen,\s*([\w.]+),\s*"
    out = []
    seen = set()
    unresolved_names = []

    for m in re.finditer(inline + r'"([^"]*)",\s*toybox::(k\w+Font)', flat):
        seen.add(m.start())
        box, text, slot = m.groups()
        wide = box_width(box)
        if text and wide:
            out.append((text, slot, wide))

    for m in re.finditer(named + r'"([^"]*)",\s*toybox::(k\w+Font)', flat):
        seen.add(m.start())
        rect_name, text, slot = m.groups()
        if text and rect_name in NAMED_RECTS:
            out.append((text, slot, NAMED_RECTS[rect_name]))

    slot_of = {}
    box_of = {}
    collisions = set()
    for m in re.finditer(inline + r"([a-zA-Z]\w*),\s*toybox::(k\w+Font)", flat):
        seen.add(m.start())
        box, buf, slot = m.groups()
        wide = box_width(box)
        if wide is None:
            continue
        if buf in box_of and box_of[buf] != wide:
            collisions.add(buf)
        slot_of[buf] = slot
        box_of[buf] = wide
    for m in re.finditer(named + r"([a-zA-Z]\w*),\s*toybox::(k\w+Font)", flat):
        seen.add(m.start())
        rect_name, buf, slot = m.groups()
        if rect_name in NAMED_RECTS:
            slot_of[buf] = slot
            box_of[buf] = NAMED_RECTS[rect_name]

    sizes = dict(re.findall(r"char (\w+)\[(\d+)\]", flat))
    for m in re.finditer(r'snprintf\((\w+),\s*sizeof\(\1\),\s*"([^"]+)"', flat):
        buf, fmt = m.groups()
        slot = slot_of.get(buf)
        if slot is None:
            continue
        if buf in collisions:
            unresolved_names.append(f"buffer {buf!r} is drawn into boxes of different widths -- rename one")
            continue
        # Worst REACHABLE value. Guess and target are clamped to the strip;
        # rounds and points are uint16_t; and a "%d.%d" pair is a value and its
        # TENTHS REMAINDER, so the second is always one digit. Every false alarm
        # this gate has raised came from worst-representable standing in for
        # worst-reachable.
        wide = "20" if re.search(r"LOCK|TARGET|GUESS", fmt) else "65535"
        plural = re.search(r"\w%s", fmt) is not None
        text = fmt.replace("%s", "S" if plural else "ABOVE")
        text = text.replace("%d.%d", wide + ".9").replace("%d", wide)
        out.append((text, slot, box_of.get(buf, CONTENT_WIDTH)))
        cap = sizes.get(buf)
        if cap is not None and len(text) + 1 > int(cap):
            unresolved_names.append(
                f"char {buf}[{cap}] cannot hold {len(text) + 1} bytes of {text!r} -- snprintf will truncate it silently"
            )

    # Anything the resolvers did not touch. Deck words are excluded by name:
    # they are runtime content and gen_pairs.py refuses an overlong pair before
    # it can ever reach the device.
    unresolved = list(unresolved_names)
    for m in re.finditer(
        r'caps\(screen,\s*([^;]{0,120}?),\s*(?:"([^"]*)"|([\w.]+)),\s*toybox::k\w+Font', flat
    ):
        if m.start() in seen:
            continue
        text = m.group(2) if m.group(2) is not None else m.group(3)
        if text and text.startswith("model.spectrum."):
            continue
        unresolved.append(f"{text[:30]!r} in rect {m.group(1)[:50]}")
    return out, unresolved


def deck_draw_count(src):
    """Draws whose text is a deck word, measured by gen_pairs.py instead."""
    flat = re.sub(r"\n\s+", " ", src)
    return len(re.findall(r"caps\(screen,[^;]{0,160}?,\s*model\.spectrum\.\w+,", flat))


def main():
    faces = resolved_faces()
    advances = {slot: glyph_advances(face) for slot, face in faces.items()}
    print(
        "  slots resolved from the activity: "
        + ", ".join(f"{s} -> {f}" for s, f in faces.items())
    )

    source = SCREENS.read_text(encoding="utf-8")
    measured, unresolved = strings_with_slots(source)
    deck_draws = deck_draw_count(source)
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
    unreached = len(unresolved)
    print(
        f"  {checked} strings measured against their own box, none overflow"
        f"  (+{deck_draws} deck-word draws, gated by gen_pairs.py)"
    )
    if unreached > MAX_UNREACHED:
        print(f"  BUT {unreached} draw sites are UNMEASURED:")
        for line in unresolved:
            print(f"    {line}")
        print("  Either extend this parser or give the draw an inline fui::makeRect it can read.")
        return 1
    print(f"  {unreached} draw sites use a rect this parser cannot read (ratchet {MAX_UNREACHED})")
    for line in unresolved:
        print(f"    {line}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

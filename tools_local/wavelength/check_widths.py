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
    """Every literal and format string, paired with the slot it is drawn in."""
    flat = re.sub(r"\n\s+", " ", src)
    out = []
    for m in re.finditer(
        r'caps\(screen,\s*fui::makeRect\([^,]+,\s*[^,]+,\s*(\w+),[^)]*\),\s*"([^"]*)",\s*toybox::(k\w+Font)',
        flat,
    ):
        box, text, slot = m.groups()
        if text and box == "inner":
            out.append((text, slot, CONTENT_WIDTH))
    slot_of = {}
    for m in re.finditer(
        r"caps\(screen,[^;]*?,\s*([a-zA-Z]\w*),\s*toybox::(k\w+Font)", flat
    ):
        slot_of[m.group(1)] = m.group(2)
    for m in re.finditer(r'snprintf\((\w+),\s*sizeof\(\1\),\s*"([^"]+)"', flat):
        buf, fmt = m.groups()
        slot = slot_of.get(buf)
        if slot is None:
            continue
        # Worst REACHABLE value, not worst representable. The guess and target
        # are clamped to the strip; rounds and points are uint16_t. Measuring an
        # unreachable 65535 reports a bug that cannot happen, which is its own
        # kind of wrong.
        wide = "20" if re.search(r"LOCK|TARGET|GUESS", fmt) else "65535"
        # A %s glued to the end of a word is a plural suffix, not a word.
        # Substituting a word there invents an overflow that cannot happen,
        # which is the same false alarm as measuring an unreachable 65535.
        plural = re.search(r"\w%s", fmt) is not None
        text = fmt.replace("%s", "S" if plural else "ABOVE").replace("%d", wide)
        out.append((text, slot, CONTENT_WIDTH))
    return out


def main():
    faces = resolved_faces()
    advances = {slot: glyph_advances(face) for slot, face in faces.items()}
    print(
        "  slots resolved from the activity: "
        + ", ".join(f"{s} -> {f}" for s, f in faces.items())
    )

    bad = []
    checked = 0
    for text, slot, box in strings_with_slots(SCREENS.read_text(encoding="utf-8")):
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
    print(f"  {checked} strings measured, none overflow")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

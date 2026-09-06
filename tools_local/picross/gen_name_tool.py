#!/usr/bin/env python3
"""Generate the data the puzzle-naming web tool runs on.

    tools_local/picross/gen_name_tool.py           # writes site/picross-names/data.js
    tools_local/picross/gen_name_tool.py --check   # fails if the file is stale

Two things go into it, and both are DERIVED rather than typed, because a number
copied by hand into a browser is a number that stops being true the next time
somebody regenerates a font or curates the bank:

1. **The 10x10 puzzles**, read out of `src/apps_local/picross/PicrossPuzzles.h`
   -- id, solution bitmap and provenance row. The tool draws the solved picture,
   so it needs the bitmap; it shows the author, so it needs the provenance.
   Filtered to `size == 10`: the bank is 10x10 only from card #390 onward, and
   reading the size field rather than assuming it means this script gives the
   same answer before and after that lands.

2. **The Jersey advance widths**, read out of `src/apps_local/ui/fonts/`. The
   win screen sets the name in `toybox::kDisplayFont` and puts it through
   `toybox::fittedTitle`, which walks TITLE -> BODY -> SMALL (toybox_30 ->
   toybox_20 -> toybox_10) and only ellipsizes when the smallest still
   overflows. So "will this name fit" has three answers, not two, and the tool
   can only give them by measuring the real proportional face: counting
   characters cannot, since "WWWW" and "iiii" differ threefold.

   The coverage matters as much as the width. toybox_20 and toybox_30 carry
   U+0020..U+007E and NOTHING ELSE, and a codepoint the face has no glyph for
   draws as nothing at all -- not a box, a HOLE -- while advancing the pen by
   zero. `cafe' with an acute renders as `caf'. That is why the tool refuses
   non-ASCII at entry time instead of letting him find out on the panel.
"""

import argparse
import json
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
BANK = REPO / "src/apps_local/picross/PicrossPuzzles.h"
FONTS = REPO / "src/apps_local/ui/fonts"
OUT = REPO / "site/picross-names/data.js"

# The rungs toybox::fittedTitle walks for a name set in kDisplayFont, largest
# first, under toybox::toyboxFaces(). Named by the slot they are bound to so a
# reader can check them against ToyboxTheme.h rather than trusting this list.
CUTS = [
    ("title", "toybox_30"),  # FONT_SLOT_TITLE, the cut the win screen asks for
    ("body", "toybox_20"),  # FONT_SLOT_BODY
    ("small", "toybox_10"),  # FONT_SLOT_SMALL
]

# The width buildWin() gives the name, in pixels. NOT a literal here: it is
# measured from the real screen builder by measure_name_band.sh and left in
# name_band.txt, so a change to the win screen's layout is one command away from
# being reflected here instead of a number nobody knows has gone stale.
BAND_FILE = REPO / "tools_local/picross/name_band.txt"


def load_font(name):
    """-> (advance_px_by_codepoint, line_height). advanceX is 12.4 fixed point."""
    src = (FONTS / f"{name}.h").read_text()
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
    # The struct's fields after the arrays: bitmaps, glyphs, intervals, count,
    # then advanceY and ascender. advanceY is the line height.
    tail = src.index(f"static const EpdFontData {name} = {{")
    fields = [f.strip() for f in src[tail:].split("{", 1)[1].split("}", 1)[0].split(",")]
    line_height = int(fields[4])

    advances = {}
    for cp in range(0x20, 0x7F):
        for first, last, offset in intervals:
            if first <= cp <= last:
                advances[cp] = glyphs[offset + (cp - first)][2] / 16.0
                break
    missing = [cp for cp in range(0x20, 0x7F) if cp not in advances]
    if missing:
        raise SystemExit(f"{name}: no glyph for printable ASCII {missing} -- the tool cannot measure with it")
    return advances, line_height


def janko_id(name, prov):
    """The janko.at puzzle number, as `janko-authors.json` keys it: a decimal
    string with no leading zeros. The names file card #390 consumes is keyed by
    this rather than by the bank's index, because a re-import renumbers the bank
    and does not renumber janko.

    Derived from the SOURCE URL, which is where the number actually comes from,
    and cross-checked against the puzzle's own id. They agree today; if a puzzle
    ever arrives whose id and URL disagree, or which has no janko number at all
    (the fork's own CC0 pictures do not), this refuses rather than inventing a
    key that would silently name the wrong picture."""
    from_url = re.search(r"/Nonogramme/0*(\d+)\.", prov.get("source", "") or "")
    from_name = re.fullmatch(r"JANKO0*(\d+)", name)
    if from_url is None or from_name is None:
        raise SystemExit(
            f"{name}: cannot tell what its janko number is (source {prov.get('source')!r}). "
            "The names file is keyed by that number; fix the provenance or teach this function."
        )
    if from_url.group(1) != from_name.group(1):
        raise SystemExit(f"{name}: its id says {from_name.group(1)} and its source URL says {from_url.group(1)}")
    return from_url.group(1)


def load_bank():
    src = BANK.read_text()
    body = src.split("constexpr Puzzle kPuzzles[] = {", 1)[1].split("\n};", 1)[0]
    entries = re.findall(r'\{"([A-Za-z0-9_-]+)",\s*(\d+),\s*(\d+),\s*\{([^}]*)\}\}', body)
    if not entries:
        raise SystemExit(f"{BANK}: no puzzles parsed -- the table's shape changed")

    prov_body = src.split("constexpr Provenance kProvenances[] = {", 1)[1].split("\n};", 1)[0]
    provs = [
        {"author": a, "license": lic, "source": s}
        for a, lic, s in re.findall(r'\{"([^"]*)",\s*"([^"]*)",\s*"([^"]*)"\}', prov_body)
    ]

    puzzles = []
    for name, size, prov, rows in entries:
        size = int(size)
        if size != 10:
            continue  # the bank is 10x10 only from card #390; read it, do not assume it
        bits = [int(x, 0) for x in re.findall(r"0x[0-9A-Fa-f]+", rows)][:size]
        puzzles.append({"id": name, "janko": janko_id(name, provs[int(prov)]), "rows": bits, "prov": int(prov)})
    if not puzzles:
        raise SystemExit(f"{BANK}: no 10x10 puzzles -- nothing for the tool to name")
    return puzzles, provs


def read_band():
    """The measured name-band width. Refuses to guess: a wrong width here is a
    tool that says a name fits when it does not."""
    if not BAND_FILE.exists():
        raise SystemExit(f"{BAND_FILE.relative_to(REPO)} is missing -- run tools_local/picross/measure_name_band.sh")
    text = BAND_FILE.read_text().strip()
    if not text.isdigit() or int(text) <= 0:
        raise SystemExit(f"{BAND_FILE.relative_to(REPO)} does not hold a width: {text!r}")
    return int(text)


def render(band_width):
    puzzles, provs = load_bank()
    fonts = {}
    for slot, cut in CUTS:
        advances, line_height = load_font(cut)
        fonts[slot] = {
            "cut": cut,
            "lineHeight": line_height,
            # Indexed from 0x20 so the browser can look a codepoint up by
            # subtraction rather than carrying 95 keys.
            "advance": [advances[cp] for cp in range(0x20, 0x7F)],
        }

    # Only the provenance rows the kept puzzles actually index, renumbered. A
    # 15x15-only designer is not a designer of anything this tool shows, and
    # shipping their row would credit them for work that is not here.
    used = sorted({p["prov"] for p in puzzles})
    remap = {old: new for new, old in enumerate(used)}
    for p in puzzles:
        p["prov"] = remap[p["prov"]]

    payload = {
        "generatedBy": "tools_local/picross/gen_name_tool.py",
        "size": 10,
        "bandWidth": band_width,
        "puzzles": puzzles,
        "provenances": [provs[old] for old in used],
        "fonts": fonts,
    }
    # A .js rather than a .json so the page works from file:// too: a fetch() of
    # a local JSON is a cross-origin request and every browser refuses it, which
    # would make the tool unopenable in exactly the offline case it is most
    # useful in.
    return (
        "// GENERATED by tools_local/picross/gen_name_tool.py -- do not edit by hand.\n"
        "// The 10x10 bank and the real Jersey advance widths the win screen sets\n"
        "// names in. Regenerate after any change to PicrossPuzzles.h or to a\n"
        "// toybox_* cut; tools_local/picross/gen_name_tool.py --check fails when\n"
        "// this file has gone stale.\n"
        "window.PICROSS_NAME_DATA = " + json.dumps(payload, separators=(",", ":"), sort_keys=True) + ";\n"
    )


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--check", action="store_true", help="fail if the generated file is stale")
    ap.add_argument(
        "--band",
        type=int,
        default=None,
        help="override the band width in px; the default is measure_name_band.sh's recorded answer",
    )
    args = ap.parse_args()

    band = args.band if args.band is not None else read_band()
    text = render(band)
    if args.check:
        current = OUT.read_text() if OUT.exists() else ""
        if current != text:
            print(f"STALE: {OUT.relative_to(REPO)} does not match the bank and the fonts", file=sys.stderr)
            print("Run: tools_local/picross/gen_name_tool.py", file=sys.stderr)
            return 1
        print(f"ok: {OUT.relative_to(REPO)} is current")
        return 0

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(text)
    data = json.loads(text.split("= ", 1)[1].rstrip(";\n"))
    print(f"wrote {OUT.relative_to(REPO)}: {len(data['puzzles'])} puzzles at 10x10, "
          f"{len(data['provenances'])} provenance rows, band {band}px")
    return 0


if __name__ == "__main__":
    sys.exit(main())

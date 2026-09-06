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
   overflows. So "will this name fit" is not a yes/no, and the tool can only
   answer it by measuring the real proportional face: counting characters
   cannot, since "WWWW" and "iiii" differ threefold.

   Three arrays per cut, not one, because the device reports the width of the
   INK BOX and not the sum of the advances -- see measure() in
   site/picross-names/logic.js. The advances are handed over in 12.4 FIXED
   POINT rather than pixels, because EpdFont rounds each one to a whole pixel
   as it accumulates and the browser has to round in the same units at the same
   moments.

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
# Where the designers live now. The attribution left the firmware on card
# #390 -- 137 source URLs were ~34KB of a ~51KB bank -- so the header cannot
# be asked who drew a picture any more, and host-tests/picrossprov fails if a
# later session puts them back.
SOURCE = REPO / "assets_local/picross/janko.txt"
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

    # Kerning would have to be replicated too, and none of these cuts has any
    # ("this cut has no kerning data at all" in every toybox_* header). Refuse
    # rather than measure a cut whose pairs move: EpdFont folds the kern into
    # the same accumulator as the advance, so ignoring it would make the tool's
    # answer quietly wrong by a few pixels per pair.
    if "kernLeftCodepoints" in src and "nullptr,  // kernLeftCodepoints" not in src:
        raise SystemExit(f"{name}: this cut has kerning data and measure() does not replicate kerning")

    metrics = {}
    for cp in range(0x20, 0x7F):
        for first, last, offset in intervals:
            if first <= cp <= last:
                g = glyphs[offset + (cp - first)]
                # advanceX (12.4 fixed point), the bitmap's width, and its left
                # side bearing. All three, because the device measures the INK
                # BOX -- maxX - minX -- and not the sum of the advances.
                metrics[cp] = {"adv": g[2], "w": g[0], "left": g[3]}
                break
    missing = [cp for cp in range(0x20, 0x7F) if cp not in metrics]
    if missing:
        raise SystemExit(f"{name}: no glyph for printable ASCII {missing} -- the tool cannot measure with it")
    return metrics, line_height


def parse_source():
    """Every candidate picture in janko.txt, keyed by its BITMAP.

    Keyed by the bitmap and not by a name, because since card #390 the string a
    shipped puzzle carries is Mario's name for the picture and says nothing
    about where it came from. The bitmap is the puzzle. This is deliberately the
    same mechanism host-tests/picrossprov re-derives the credit with -- one way
    of answering "which janko puzzle is this", not two.

    Parsed with a GRID STATE rather than line by line: a solid row is
    "##########", which starts with a '#' and is indistinguishable from a
    comment unless you already know a grid is open.
    """
    lines = SOURCE.read_text(encoding="utf-8").splitlines()
    by_bitmap = {}
    defaults = {}
    pending = {}
    i = 0
    while i < len(lines):
        line = lines[i].rstrip()
        if not line.strip() or line.lstrip().startswith("#"):
            i += 1
            continue
        if line.lstrip().startswith("@"):
            text = line.strip()
            sticky = text.startswith("@@")
            key, _, value = text.lstrip("@").partition(" ")
            (defaults if sticky else pending)[key.strip().lower()] = value.strip()
            i += 1
            continue
        name = line.strip()
        prov = dict(defaults)
        prov.update(pending)
        pending = {}
        i += 1
        cells = []
        while i < len(lines) and lines[i].strip() and set(lines[i].strip()) <= {"#", "."}:
            cells.append([1 if ch == "#" else 0 for ch in lines[i].strip()])
            i += 1
        if not cells:
            continue
        size = len(cells)
        bits = tuple(sum(1 << c for c in range(size) if cells[r][c]) for r in range(size))
        by_bitmap.setdefault((size, bits), []).append((name, prov))
    return by_bitmap


def janko_key(name):
    """The bare janko number, as janko-names.json keys it.

    janko-names.json accepts either "JANKO222" or "222" and this emits the bare
    form. Refused rather than guessed for anything else: a wrong key here names
    the wrong picture, and nobody would catch that by reading the file.
    """
    m = re.fullmatch(r"JANKO0*(\d+)", name)
    if m is None:
        raise SystemExit(f"{name}: not a janko puzzle name, so the names file has no key for it")
    return m.group(1)


def load_bank():
    """The puzzles that ACTUALLY SHIP, with the origin of each.

    Read from the emitted header rather than from janko.txt filtered to 10x10:
    the bank is whatever gen_picross.py chose to emit, and a tool that re-derives
    that choice for itself is a tool that can disagree with the device about
    which pictures exist.
    """
    src = BANK.read_text()
    body = src.split("constexpr Puzzle kPuzzles[] = {", 1)[1].split("\n};", 1)[0]
    # The struct lost its provenance index when the attribution left the
    # firmware (card #390), and `name` now holds Mario's name for the picture --
    # empty until he writes one. Neither the janko id nor the designer is in
    # this file any more, by design, and host-tests/picrossprov fails if either
    # comes back.
    entries = re.findall(r'\{"((?:[^"\\]|\\.)*)",\s*(\d+),\s*\{([^}]*)\}\}', body)
    if not entries:
        raise SystemExit(f"{BANK}: no puzzles parsed -- the table's shape changed")

    by_bitmap = parse_source()
    puzzles = []
    provs = []
    prov_index = {}
    for _name, size, rows in entries:
        size = int(size)
        bits = tuple(int(x, 0) for x in re.findall(r"0x[0-9A-Fa-f]+", rows))[:size]
        matches = by_bitmap.get((size, bits), [])
        if len(matches) != 1:
            raise SystemExit(
                f"a shipped {size}x{size} bitmap matches {len(matches)} pictures in "
                f"{SOURCE.relative_to(REPO)}; the tool cannot say where it came from"
            )
        src_name, prov = matches[0]
        for key in ("author", "license", "source"):
            if not prov.get(key):
                raise SystemExit(f"{src_name}: no {key} recorded, so the tool would credit nobody")
        row = (prov["author"], prov["license"], prov["source"])
        if row not in prov_index:
            prov_index[row] = len(provs)
            provs.append({"author": row[0], "license": row[1], "source": row[2]})
        puzzles.append(
            {"id": src_name, "janko": janko_key(src_name), "rows": list(bits), "prov": prov_index[row]}
        )
    if not puzzles:
        raise SystemExit(f"{BANK}: no puzzles -- nothing for the tool to name")
    sizes = {len(p["rows"]) for p in puzzles}
    if len(sizes) != 1:
        raise SystemExit(f"{BANK}: the bank mixes sizes {sorted(sizes)} and this tool draws one")
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
        metrics, line_height = load_font(cut)  # noqa: F821 -- see the note on CUTS
        fonts[slot] = {
            "cut": cut,
            "lineHeight": line_height,
            # Three parallel arrays, indexed from 0x20 so the browser looks a
            # codepoint up by subtraction rather than carrying 95 keys.
            #
            # `advance` is 12.4 FIXED POINT, not pixels, and it is handed over
            # that way on purpose: EpdFont rounds each advance to a whole pixel
            # as it accumulates, so the browser has to round in the same units
            # at the same moments. Converting here would throw away the only
            # thing that makes the two agree.
            "advance": [metrics[cp]["adv"] for cp in range(0x20, 0x7F)],
            "width": [metrics[cp]["w"] for cp in range(0x20, 0x7F)],
            "left": [metrics[cp]["left"] for cp in range(0x20, 0x7F)],
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
        # Read off the bank rather than stated: the bank was two size tiers
        # until card #390 dropped 15x15, and a literal here would have been a
        # number that quietly stopped being true.
        "size": len(puzzles[0]["rows"]),
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


def bank_names():
    """Every name string the shipped bank carries, in bank order.

    Read straight out of the emitted header, without parse_source() and without
    any of the janko machinery, because this is the one question that decides
    whether this tool has a job at all.
    """
    src = BANK.read_text()
    body = src.split("constexpr Puzzle kPuzzles[] = {", 1)[1].split("\n};", 1)[0]
    return [m for m, _size, _rows in re.findall(r'\{"((?:[^"\\]|\\.)*)",\s*(\d+),\s*\{([^}]*)\}\}', body)]


def superseded():
    """Is there anything left for this tool to do?

    THIS TOOL EXISTS TO NAME AN UNNAMED BANK. The janko import carried no titles
    at all, so every reveal would have said nothing until Mario typed 137 names,
    and this generated the data the page needed to let him. The bank it was
    built for is gone: the current one arrives titled, every puzzle already has
    the string the win screen reveals, and there is nothing here to name.

    So the question this returns is not "is data.js stale" but "does the tool
    still have a job", and it is asked of the BANK rather than asserted in a
    comment. If a future bank ships unnamed puzzles again, this goes false, the
    staleness check below runs exactly as it always did, and the tool is needed
    again -- which is the property worth keeping and the reason this is a
    function and not a deletion.
    """
    names = bank_names()
    return bool(names) and all(names)


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

    # Asked before anything is rendered, because rendering requires janko.txt
    # and janko.txt is gone with the bank it described.
    if superseded():
        note = (
            f"superseded: every puzzle in {BANK.relative_to(REPO)} already carries its own "
            "name, so there is nothing for the naming tool to name.\n"
            f"{OUT.relative_to(REPO)} is FROZEN, deliberately: it still describes the previous "
            "bank, and it is left untouched so that anything Mario typed into the page still "
            "loads. The page and this tool are proposed for removal together -- his call, "
            "card #393 -- and until then neither is regenerated.\n"
            "This stops being true, and the staleness check below runs again, the moment a "
            "bank ships a puzzle with no name."
        )
        if args.check:
            print(note)
            return 0
        print(note, file=sys.stderr)
        print("Refusing to regenerate. Nothing would be gained and a rewrite would "
              "discard the page's only record of the old bank.", file=sys.stderr)
        return 1

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
    print(f"wrote {OUT.relative_to(REPO)}: {len(data['puzzles'])} puzzles at "
          f"{data['size']}x{data['size']}, {len(data['provenances'])} provenance rows, band {band}px")
    return 0


if __name__ == "__main__":
    sys.exit(main())

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
CORE = REPO / "src/apps_local/wavelength/WavelengthCore.h"

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
# Every OTHER named rect is resolved from its own declaration by
# declared_rects(), not listed here. A width copied into this file is a fact
# about the screens kept somewhere the screens cannot update, and this app has
# already paid for one of those.

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


def local_constants(flat):
    """`const int16_t NAME = <number>;` from the screens themselves.

    Listing them in SYMBOLS would be a fact about the screens kept where the
    screens cannot update it, which is the mistake this file's header warns
    about. Read them instead: without `swatchW` the reveal's two swatch rows
    resolved to no width at all and fell back to the full 448, which is more
    than twice the column they are actually drawn in.
    """
    return {
        name: int(value)
        for name, value in re.findall(r"const int16_t (\w+) = (\d+);", flat)
    }


def box_width(expr, extra=None):
    """A rect's width in pixels, or None if this parser cannot read it.

    None is a FAILURE, never a skip: an unreadable width means the string is
    unmeasured, and unmeasured is indistinguishable from safe in the output.
    """
    expr = expr.strip()
    m = re.fullmatch(r"static_cast<int16_t>\((.*)\)", expr)
    if m:
        expr = m.group(1)
    symbols = dict(SYMBOLS)
    symbols.update(extra or {})
    for name, value in sorted(symbols.items(), key=lambda kv: -len(kv[0])):
        expr = expr.replace(name, str(value))
    if not re.fullmatch(r"[\d\s+\-*/()]+", expr):
        return None
    try:
        return int(eval(expr, {"__builtins__": {}}, {}))  # noqa: S307 -- digits and operators only
    except Exception:
        return None


def max_round_points():
    """The most one round can pay, READ FROM THE RULES rather than typed here.

    The reveal's own figure is "+%d" with no word in it for the reachable-value
    rule below to key on, so it would otherwise be measured at 65535 -- a false
    alarm about a number no round can score. Reading the two constants keeps the
    bound true if the scoring ever changes, which it has: an exact lock paid 6
    until v1.12.3 while every screen in the app said 5.
    """
    src = CORE.read_text(encoding="utf-8")

    def constant(name):
        m = re.search(rf"constexpr int {name}\s*=\s*(\d+)", src)
        if not m:
            raise SystemExit(f"no {name} in WavelengthCore.h -- read the rules and fix this script")
        return int(m.group(1))

    return constant("kPointsExact") + constant("kPointsEndCall")


def split_args(text):
    """Split a call's arguments on top-level commas, ignoring nested parens."""
    args = []
    depth = 0
    current = ""
    for ch in text:
        if ch == "(" or ch == "<":
            depth += 1
        elif ch == ")" or ch == ">":
            depth -= 1
        if ch == "," and depth == 0:
            args.append(current)
            current = ""
        else:
            current += ch
    args.append(current)
    return args


def declared_rects(flat):
    """Named rect -> width in pixels, read from each `fui::Rect NAME = makeRect`.

    A name declared twice with two widths maps to None, which reports the draw
    as unmeasured rather than measuring it against whichever declaration this
    parser happened to see last.
    """
    out = {}
    decls = []
    for m in re.finditer(r"fui::Rect (\w+) =\s*fui::makeRect\((.*?)\);", flat):
        name, args = m.group(1), split_args(m.group(2))
        if len(args) == 4:
            decls.append((name, args[2]))
    # Two passes, because a rect may be sized from one declared above it
    # (`label.width`). Substituting those is what keeps a stacked pair of labels
    # measurable without either width being written down twice.
    for _ in range(2):
        for name, expr in decls:
            resolved = expr
            for other, value in out.items():
                resolved = resolved.replace(f"{other}.width", str(value))
            wide = box_width(resolved)
            if wide is None:
                continue
            if name in out and out[name] != wide:
                out[name] = None
            else:
                out[name] = wide
    return {k: v for k, v in out.items() if v is not None}


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


def literal_tables(flat):
    """Named `const char*` variables and tables, mapped to every string they can be.

    A draw whose text is an EXPRESSION -- a ternary, or an index into a table of
    words -- was invisible to every resolver below: not a literal, not a
    buffer, and not matched by the unresolved sweep either, so it appeared in
    neither column of the report. The reveal's verdict and the two scoring
    tables were all unmeasured that way. Which branch the expression takes at
    runtime does not matter; what has to fit is the widest one it can take.
    """
    decl = r'(?:static\s+)?const char\* (\w+)(?:\[[^\]]*\])* =\s*(.*?);'
    out = {}
    for m in re.finditer(decl, flat):
        name, strings = m.group(1), set(re.findall(r'"([^"]*)"', m.group(2)))
        # A name declared twice with different words maps to None, exactly as a
        # rect declared twice does: measuring against whichever declaration this
        # parser happened to see last is how a 448px line got checked against a
        # 224px column and passed.
        if name in out and out[name] is not None and out[name] != strings:
            out[name] = None
        elif name not in out:
            out[name] = strings
    # A variable built out of another inherits its strings, which is what the
    # reveal's `verdict = practice ? "NO SCORE" : kDistance[...]` needs.
    for _ in range(2):
        for m in re.finditer(decl, flat):
            name, expr = m.group(1), m.group(2)
            if out.get(name) is None:
                continue
            for other, strings in out.items():
                if other != name and strings and re.search(rf"\b{other}\b", expr):
                    out[name] |= strings
    return {k: v for k, v in out.items() if v is not None}


def literals_in(expr, tables):
    """Every string a text argument can evaluate to, or an empty set."""
    found = set(re.findall(r'"([^"]*)"', expr))
    for name in re.findall(r"\b([A-Za-z_]\w*)\b", expr):
        found |= tables.get(name, set())
    return found


def slot_strings():
    """Every string a hand-built slot buffer can hold, as the code builds it.

    Three draws fill a `char buf[4]` a digit at a time rather than through
    snprintf -- the two tick columns and the big numeral -- so there is no
    format string for the resolver below to key on and all three were silently
    unmeasured. What they hold is a SLOT, and the strip has twenty of them,
    right-aligned in two columns with a leading space under ten.
    """
    slots = int(re.search(r"constexpr int kSlots = (\d+)", CORE.read_text(encoding="utf-8")).group(1))
    return [f"{n:2d}" for n in range(1, slots + 1)]


def hand_built_slot_buffers(flat):
    """Buffer names filled with `buf[1] = '0' + <something> % 10`."""
    return {m.group(1) for m in re.finditer(r"(\w+)\[1\] = static_cast<char>\('0' \+ [\w.]+ % 10\)", flat)}


def strings_with_slots(src):
    """Every measurable string with its slot and box, plus the draws missed.

    Returns (measurable, unresolved_descriptions). Coverage is computed from the
    MATCH OFFSETS of the resolvers themselves rather than from a second, looser
    regex counting "sites": counting two ways gave two answers that disagreed by
    five, and a gate whose own arithmetic is wrong gets ignored long before it
    catches anything.

    UNDERSTOOD IS NOT MEASURED, and conflating the two hid the reveal's verdict
    for a whole app. The buffer pass marks a site the moment it recognises the
    SHAPE `caps(rect, identifier, slot)`; if that identifier turns out to have
    no snprintf behind it -- a `const char*` picked out of a table of words --
    nothing ever measures it, and it is missing from the overflow column and
    from the unmeasured column at the same time. So sites go into `measured`
    only when a width was actually checked, and the sweep at the end reports
    everything else.
    """
    flat = re.sub(r"\n\s+", " ", src)
    consts = local_constants(flat)
    named_rects = dict(NAMED_RECTS)
    named_rects.update(declared_rects(flat))
    w = r"((?:static_cast<int16_t>\((?:[^()]|\([^()]*\))*\)|[\w.]+))"
    inline = r"caps\(screen,\s*fui::makeRect\([^,]+,\s*[^,]+,\s*" + w + r",[^)]*\),\s*"
    named = r"caps\(screen,\s*([\w.]+),\s*"
    out = []
    measured = set()
    unresolved_names = []

    for m in re.finditer(inline + r'"([^"]*)",\s*toybox::(k\w+Font)', flat):
        box, text, slot = m.groups()
        wide = box_width(box, consts)
        if text and wide:
            measured.add(m.start())
            out.append((text, slot, wide))

    for m in re.finditer(named + r'"([^"]*)",\s*toybox::(k\w+Font)', flat):
        rect_name, text, slot = m.groups()
        # Only a name this table can resolve counts as covered. Marking every
        # named rect seen was a hole with the shape of the bug this gate exists
        # to catch: the draw vanished from both columns of the report and read
        # as measured.
        if rect_name not in named_rects:
            continue
        if text:
            measured.add(m.start())
            out.append((text, slot, named_rects[rect_name]))

    slot_of = {}
    box_of = {}
    sites_of = {}
    collisions = set()
    for m in re.finditer(inline + r"([a-zA-Z]\w*),\s*toybox::(k\w+Font)", flat):
        box, buf, slot = m.groups()
        wide = box_width(box, consts)
        if wide is None:
            continue
        if buf in box_of and box_of[buf] != wide:
            collisions.add(buf)
        slot_of[buf] = slot
        box_of[buf] = wide
        sites_of.setdefault(buf, []).append(m.start())
    for m in re.finditer(named + r"([a-zA-Z]\w*),\s*toybox::(k\w+Font)", flat):
        rect_name, buf, slot = m.groups()
        if rect_name not in named_rects:
            continue
        slot_of[buf] = slot
        box_of[buf] = named_rects[rect_name]
        sites_of.setdefault(buf, []).append(m.start())

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
        if fmt == "+%d":
            wide = str(max_round_points())
        else:
            wide = "20" if re.search(r"LOCK|TARGET|NUMBER|GUESS", fmt) else "65535"
        plural = re.search(r"\w%s", fmt) is not None
        text = fmt.replace("%s", "S" if plural else "ABOVE")
        text = text.replace("%d.%d", wide + ".9").replace("%d", wide)
        measured.update(sites_of.get(buf, ()))
        out.append((text, slot, box_of.get(buf, CONTENT_WIDTH)))
        cap = sizes.get(buf)
        if cap is not None and len(text) + 1 > int(cap):
            unresolved_names.append(
                f"char {buf}[{cap}] cannot hold {len(text) + 1} bytes of {text!r} -- snprintf will truncate it silently"
            )

    for buf in hand_built_slot_buffers(flat) & set(slot_of):
        measured.update(sites_of.get(buf, ()))
        for text in slot_strings():
            out.append((text, slot_of[buf], box_of.get(buf, CONTENT_WIDTH)))

    # Text arguments that are expressions rather than one literal or one
    # buffer. Every string the expression can evaluate to is measured, because
    # the widest branch is the one that decides whether the line fits.
    tables = literal_tables(flat)
    expr = r"([^,]+),\s*toybox::(k\w+Font)"
    for pattern, resolve_box in (
        (inline + expr, lambda b: box_width(b, consts)),
        (named + expr, named_rects.get),
    ):
        for m in re.finditer(pattern, flat):
            if m.start() in measured:
                continue
            box, text, slot = m.groups()
            wide = resolve_box(box)
            if wide is None:
                continue
            found = literals_in(text, tables)
            if not found:
                continue
            measured.add(m.start())
            for literal in sorted(found):
                if literal:
                    out.append((literal, slot, wide))

    # Anything the resolvers did not touch. Deck words are excluded by name:
    # they are runtime content and gen_pairs.py refuses an overlong pair before
    # it can ever reach the device.
    #
    # The sweep takes the text argument as a whole expression rather than as a
    # literal or a bare identifier. It used to demand one of those two shapes,
    # so a draw it could not resolve did not appear here EITHER -- the exact
    # shape of hole this file exists to close.
    unresolved = list(unresolved_names)
    for m in re.finditer(r"caps\(screen,\s*([^;]{0,160}?),\s*([^,]+),\s*toybox::k\w+Font", flat):
        if m.start() in measured:
            continue
        text = m.group(2).strip()
        # Deck words, however they reach the draw: `endWord()` takes one as a
        # `const char* word` parameter, so the name is all there is to go on.
        if text.startswith("model.spectrum.") or text == "word":
            continue
        unresolved.append(f"{text[:40]!r} in rect {m.group(1)[:50]}")
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

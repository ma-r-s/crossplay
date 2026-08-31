#!/usr/bin/env python3
"""Turn tools_local/forehead/words/*.txt into src/apps_local/forehead/ForeheadWords.h.

    ./tools_local/forehead/gen_forehead_words.py

The output is committed, the way every generated table in this fork is: a
checkout has to build without Python. Edit a word list, run this, commit both.

Three rules are enforced here rather than discovered on the panel, because all
three fail SILENTLY at 220ppi:

  * ASCII only. Toybox's face is subset to ASCII and a glyph the font does not
    have draws as NOTHING -- no box, no fallback, no log line. A single curly
    apostrophe pasted in from a web page would produce a card reading "PHILOSOPHER
    S STONE" and nothing anywhere would say why.
  * A length cap. The card sizes itself by walking the font cuts down, and the
    smallest cut still has a width. kMaxEntryLen is what the layout is tuned
    against, so an entry past it is a build failure rather than a clipped word
    somebody notices in a game.
  * No duplicates within a category. The deck deals without repeating, and one
    answer listed twice defeats that silently: the mask marks one index and the
    other is still in the bag. ACROSS categories a repeat is fine and often
    right -- CLAPPING is both an action and a sound -- so those are reported and
    allowed, since only one category is in play at a time.

The category order is `categories.txt`, not the directory listing: the picker
shows them in that order and the crowd-pleasers go first. Every list file must
be named there and every name must have a file, so a new category cannot be
half-added.
"""

import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
WORDS = REPO / "tools_local/forehead/words"
MANIFEST = REPO / "tools_local/forehead/categories.txt"
OUT = REPO / "src/apps_local/forehead/ForeheadWords.h"
# The icon table is a SECOND generated file, from the same source, because
# ForeheadWords.h is included by the freestanding rules layer and a
# freeink::Icon* would drag the SDK into it. Splitting the output keeps the
# rules host-testable with nothing on the include path, and the icon column is
# still derived rather than hand-kept.
ICONS_OUT = REPO / "src/apps_local/forehead/ForeheadCategoryIcons.h"

# What the card layout is tuned against. Raising it means re-checking the
# smallest cut against the widest card, not just changing this number.
MAX_ENTRY_LEN = 22

# The caps below are in PIXELS, measured against the real font, because the
# character counts they replaced were wrong in a way only a render showed.
#
# A character cap assumes every letter is the same width and none is: "HOLD IT
# ON YOUR FOREHEAD" and "EASY ONES FOR SMALL ONES" are both 24 characters and
# differ by 69 pixels on the panel. Three separate overflows shipped behind
# character caps that all looked satisfied -- a category hint, a set of picker
# titles, and 24 entries clipped in the results list -- and each truncates with
# U+2026, which the cuts at 14 and above do not carry, so the text stops
# mid-word and looks
# deliberate.
#
# The boxes, from ForeheadScreens.cpp:
#   entry -> the results list's label column, drawn at the 14px cut
#   title -> the picker row between its icon and its right-aligned value, 20px
#   hint  -> the READY card across the landscape panel, 30px
MAX_ENTRY_PX = 276
MAX_TITLE_PX = 280
MAX_HINT_PX = 760
FONTS_DIR = REPO / "src/apps_local/ui/fonts"
# The hint appears in three places and the tightest is the READY card, which
# draws it at the 30px cut across 800px of landscape: about 24 characters.
# Past that it is truncated with U+2026, which the 30px cut does not carry
# (only toybox_10 does), so it draws as NOTHING and the sentence just stops.
# Measured, not estimated:
# "HOLD IT ON YOUR FOREHEAD" is 24 and fits exactly.
MAX_HINT_LEN = 24
# The picker row draws the title between a 32px icon and a right-aligned value.
#
# Fifteen fits the ROW. It does not fit the row once the value is "BEST 14"
# instead of "NEW", and that is the cap that matters: measured, a 15-character
# title runs 19px into where a two-digit best begins, and 14 runs 13px in. The
# first renders all looked clean only because those categories were unplayed --
# the collision arrives the day somebody scores in one, and it arrives as a
# silently chopped name, because the 20px cut does not carry U+2026.
# Thirteen clears the widest value the row can ever show.
MAX_TITLE_LEN = 13
# A round records up to kMaxCards cards, and a category with fewer entries than
# that laps inside a single round -- which is the "this device is broken"
# reading the no-repeat deck exists to prevent. Read out of the header rather
# than copied, so the two cannot drift apart.
CORE = REPO / "src/apps_local/forehead/ForeheadCore.h"
ENTRY_RE = re.compile(r"[A-Z0-9 '&.-]+")


def fail(message):
    sys.exit(f"gen_forehead_words: {message}")


_ADVANCES = {}


def pixels(text, size):
    """Width of `text` at a Toybox cut, in real panel pixels.

    Read out of the generated font header rather than estimated: EpdGlyph stores
    advanceX as 12.4 fixed point, so the raw number is sixteenths of a pixel. An
    estimate is exactly what a character count already was.
    """
    if size not in _ADVANCES:
        source = (FONTS_DIR / f"toybox_{size}.h").read_text()
        table = {}
        for m in re.finditer(r"\{\s*\d+,\s*\d+,\s*(\d+),[^}]*\},\s*//\s*(.)", source):
            table[m.group(2)] = int(m.group(1)) / 16.0
        space = re.search(r"\{\s*\d+,\s*\d+,\s*(\d+),[^}]*\},\s*//\s*U\+0020", source)
        if space:
            table[" "] = int(space.group(1)) / 16.0
        if not table:
            fail(f"could not read glyph advances out of toybox_{size}.h")
        _ADVANCES[size] = table
    return sum(_ADVANCES[size].get(c, 0) for c in text)


def near_duplicates(entries):
    """Pairs that are ONE answer written two ways.

    The exact-duplicate check was never the problem. What shipped was a plural
    beside its singular (GRAPE/GRAPES), a word order beside its reverse
    (DOG BARKING/BARKING DOG), a title with and without its article
    (LION KING/THE LION KING), and two spellings of one word
    (DONUT/DOUGHNUT). The deck deals each pair as two cards; the room has one
    answer for both, and the holder who says the other one is marked wrong.

    Every pair of this shape in the shipped lists was MANUFACTURED by the
    curation step, whose "is it already there" test was an exact string match --
    so it added the variant of a word that was already present and this file
    waved it through.

    SUBSETS are deliberately not checked here. TABLE TENNIS and TENNIS are two
    sports, WATER POLO and POLO are two sports, and a rule that refuses them
    refuses the truth. tools_local/forehead/dupes.py reports those for a human
    to judge; only the four classes above are mechanical enough to fail a build.
    """
    stop = {"THE", "A", "AN", "OF", "AND"}
    spellings = [("DONUT", "DOUGHNUT"), ("YOGURT", "YOGHURT"),
                 ("OMELET", "OMELETTE"), ("GRAY", "GREY")]

    def singular(word):
        if word.endswith("IES") and len(word) > 4:
            return word[:-3] + "Y"
        if word.endswith("ES") and len(word) > 4 and word[-3] in "SXZHO":
            return word[:-2]
        if word.endswith("S") and not word.endswith("SS") and len(word) > 3:
            return word[:-1]
        return word

    def bag(entry):
        # Punctuation goes too: MRS DOUBTFIRE and MRS. DOUBTFIRE are one film.
        words = (re.sub(r"[.'-]", "", w) for w in entry.split())
        return frozenset(singular(w) for w in words if w and w not in stop)

    found, seen = [], {}
    for entry in entries:
        key = bag(entry)
        if not key:
            continue
        for other in seen.get(key, []):
            found.append((other, entry))
        seen.setdefault(key, []).append(entry)
    for a, b in spellings:
        if a in entries and b in entries:
            found.append((a, b))
    return found


def max_cards():
    match = re.search(r"inline constexpr int kMaxCards = (\d+);", CORE.read_text())
    if not match:
        fail(f"no kMaxCards in {CORE.name}")
    return int(match.group(1))


def read_list(path):
    title = hint = icon = None
    entries = []
    for lineno, raw in enumerate(path.read_text().splitlines(), 1):
        line = raw.strip()
        if not line:
            continue
        if line.startswith("#"):
            key, _, value = line[1:].partition(":")
            key, value = key.strip(), value.strip()
            if key == "title":
                title = value
            elif key == "hint":
                hint = value
            elif key == "icon":
                icon = value
            continue
        if line != line.upper():
            fail(f"{path.name}:{lineno}: entries are stored upper case ({line!r})")
        if not ENTRY_RE.fullmatch(line):
            fail(
                f"{path.name}:{lineno}: {line!r} has a character the ASCII face cannot draw"
            )
        if len(line) > MAX_ENTRY_LEN:
            fail(
                f"{path.name}:{lineno}: {line!r} is {len(line)} chars, cap is {MAX_ENTRY_LEN}"
            )
        wide = pixels(line, 14)
        if wide > MAX_ENTRY_PX:
            fail(
                f"{path.name}:{lineno}: {line!r} is {wide:.0f}px at the 14px cut and "
                f"the results column is {MAX_ENTRY_PX}px -- it would clip with no ellipsis"
            )
        entries.append(line)

    for field, value in (("title", title), ("hint", hint), ("icon", icon)):
        if not value:
            fail(f"{path.name}: no '# {field}:' line")
    if pixels(hint, 30) > MAX_HINT_PX:
        fail(f"{path.name}: hint is {pixels(hint, 30):.0f}px at the 30px cut, "
             f"and the READY card has {MAX_HINT_PX}px")
    if pixels(title, 20) > MAX_TITLE_PX:
        fail(f"{path.name}: title is {pixels(title, 20):.0f}px at the 20px cut, "
             f"and the picker row has {MAX_TITLE_PX}px beside its value")
    if len(set(entries)) != len(entries):
        duplicate = next(e for e in entries if entries.count(e) > 1)
        fail(f"{path.name}: {duplicate!r} appears twice")
    for a, b in near_duplicates(entries):
        fail(f"{path.name}: {a!r} and {b!r} are one answer written two ways -- "
             f"the deck deals two cards and the room has one word for both")
    if not entries:
        fail(f"{path.name}: no entries")
    return title, hint, icon, entries


def main():
    slugs = [line.strip() for line in MANIFEST.read_text().splitlines()]
    slugs = [s for s in slugs if s and not s.startswith("#")]
    on_disk = {p.stem for p in WORDS.glob("*.txt")}
    if set(slugs) != on_disk:
        fail(f"categories.txt and words/ disagree: {set(slugs) ^ on_disk}")

    floor = max_cards()
    categories, entries, seen, shared = [], [], {}, []
    for slug in slugs:
        title, hint, icon, words = read_list(WORDS / f"{slug}.txt")
        for word in words:
            if word in seen:
                shared.append(f"{word} ({seen[word]} + {slug})")
            seen[word] = slug
        if len(words) <= floor:
            fail(f"{slug}.txt has {len(words)} entries; a category needs more than "
                 f"kMaxCards ({floor}) or a single round can lap it")
        categories.append((slug, title, hint, icon, len(entries), len(words)))
        entries.extend(words)

    longest = max(entries, key=len)
    lines = [
        "#pragma once",
        "",
        "// Generated by tools_local/forehead/gen_forehead_words.py from",
        "// tools_local/forehead/words/*.txt. Do not edit.",
        "//",
        "// constexpr, so the table and every string in it live in flash rather than",
        "// being copied into RAM at startup. Entries are one flat array and a category",
        "// is a contiguous slice of it, which is what lets the no-repeat mask be a",
        "// single bitmap over the whole game rather than one per category.",
        "",
        "#include <cstdint>",
        "",
        "namespace forehead {",
        "",
        f"inline constexpr int kCategoryCount = {len(categories)};",
        f"inline constexpr int kEntryCount = {len(entries)};",
        f"// Longest entry is {longest!r} at {len(longest)} characters, which is what the",
        "// card's font ladder is sized against.",
        f"inline constexpr int kMaxEntryLen = {len(longest)};",
        "",
        "struct CategoryInfo {",
        "  const char* title;",
        "  // One line under the title in the picker, and on the READY card. It says how",
        "  // to CLUE this category, which is the only thing a player needs told.",
        "  const char* hint;",
        "  uint16_t first;",
        "  uint16_t count;",
        "};",
        "",
        "inline constexpr CategoryInfo kCategories[kCategoryCount] = {",
    ]
    for slug, title, hint, _icon, first, count in categories:
        lines.append(f'    {{"{title}", "{hint}", {first}, {count}}},')
    lines += ["};", "", "inline constexpr const char* kEntries[kEntryCount] = {"]
    for slug, _title, _hint, _icon, first, count in categories:
        lines.append(f"    // {slug}: {count}")
        for word in entries[first : first + count]:
            lines.append(f'    "{word}",')
    lines += ["};", "", "}  // namespace forehead", ""]

    OUT.write_text("\n".join(lines))

    # The icon column, keyed by the same slug that named the list file, so one
    # token spells both the category and its bitmap. A hand-kept parallel table
    # is the shape of bug that survived a mutation test in this fork once
    # already: swapping two icon pointers left the word column correct and
    # nothing failed. Here a missing icon does not link.
    icon_lines = [
        "#pragma once",
        "",
        "// Generated by tools_local/forehead/gen_forehead_words.py. Do not edit.",
        "//",
        "// Separate from ForeheadWords.h because that header is included by the",
        "// freestanding rules layer, and a freeink::Icon* would drag the SDK into it.",
        "// Screens include both; ForeheadCore includes only the words.",
        "",
        '#include "../ui/ToyboxIcons.h"',
        '#include "ForeheadWords.h"',
        "",
        "namespace forehead {",
        "",
        "inline const freeink::Icon* categoryIcon(const int category) {",
        "  static const freeink::Icon* const kIcons[kCategoryCount] = {",
    ]
    for slug, _title, _hint, _icon, _first, _count in categories:
        icon_lines.append(f"      &icon_cat_{slug}_32,")
    icon_lines += [
        "  };",
        "  return category >= 0 && category < kCategoryCount ? kIcons[category] : kIcons[0];",
        "}",
        "",
        "}  // namespace forehead",
        "",
    ]
    ICONS_OUT.write_text("\n".join(icon_lines))
    total = sum(c[5] for c in categories)
    print(
        f"wrote {OUT.relative_to(REPO)}: {len(categories)} categories, {total} entries"
    )
    for slug, title, _hint, _icon, _first, count in categories:
        print(f"  {slug:10} {count:4}  {title}")
    if shared:
        print(f"  ({len(shared)} entries appear in more than one category: "
              f"{', '.join(shared[:4])}{' ...' if len(shared) > 4 else ''})")


if __name__ == "__main__":
    main()

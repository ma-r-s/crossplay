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
# The hint appears in three places and the tightest is the READY card, which
# draws it at the 30px cut across 800px of landscape: about 24 characters.
# Past that it is truncated with U+2026 -- which Jersey does not have, so it
# draws as NOTHING and the sentence just stops. Measured, not estimated:
# "HOLD IT ON YOUR FOREHEAD" is 24 and fits exactly.
MAX_HINT_LEN = 24
# The picker row draws the title between a 32px icon and a right-aligned value.
# Measured: sixteen characters clips, fifteen fits -- and the truncation is that
# same missing U+2026, so it reads as a category with a chopped name and nothing
# says why. "AROUND THE HOUSE", "SPACE AND SCIENCE" and "MYTHS AND MONSTERS" all
# shipped that way until somebody rendered page two.
MAX_TITLE_LEN = 15
ENTRY_RE = re.compile(r"[A-Z0-9 '&.-]+")


def fail(message):
    sys.exit(f"gen_forehead_words: {message}")


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
        entries.append(line)

    for field, value in (("title", title), ("hint", hint), ("icon", icon)):
        if not value:
            fail(f"{path.name}: no '# {field}:' line")
    if len(hint) > MAX_HINT_LEN:
        fail(f"{path.name}: hint is {len(hint)} chars, cap is {MAX_HINT_LEN}")
    if len(title) > MAX_TITLE_LEN:
        fail(f"{path.name}: title is {len(title)} chars, cap is {MAX_TITLE_LEN}")
    if len(set(entries)) != len(entries):
        duplicate = next(e for e in entries if entries.count(e) > 1)
        fail(f"{path.name}: {duplicate!r} appears twice")
    if not entries:
        fail(f"{path.name}: no entries")
    return title, hint, icon, entries


def main():
    slugs = [line.strip() for line in MANIFEST.read_text().splitlines()]
    slugs = [s for s in slugs if s and not s.startswith("#")]
    on_disk = {p.stem for p in WORDS.glob("*.txt")}
    if set(slugs) != on_disk:
        fail(f"categories.txt and words/ disagree: {set(slugs) ^ on_disk}")

    categories, entries, seen, shared = [], [], {}, []
    for slug in slugs:
        title, hint, icon, words = read_list(WORDS / f"{slug}.txt")
        for word in words:
            if word in seen:
                shared.append(f"{word} ({seen[word]} + {slug})")
            seen[word] = slug
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

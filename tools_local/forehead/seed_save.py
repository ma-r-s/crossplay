#!/usr/bin/env python3
"""Write a plausible forehead.sav onto a simulator SD card.

    ./tools_local/forehead/seed_save.py [fs_agent]

A screenshot of an empty save file is not a screenshot of the design. The front
door draws sixteen rounds of your record as its ornament, and on a fresh card
that panel is a bracketed rectangle with one line of apology in it -- which is
a real state, but it is not the state anybody judging the layout should be
looking at. See docs/building-apps.md, "Seed the card to render a state
honestly".

This is not faking a result. The bytes are real input to the real loader, and
the loader is part of what is under test: seeding is also how the save format
gets exercised before a device ever writes one.

The layout mirrors SaveState in ForeheadActivity.cpp. It is __attribute__
((packed)) there and '<' here, which is the same thing: no padding, little
endian. If the struct changes, kSaveVersion changes with it and an old file is
discarded rather than misread -- including this one.
"""

import pathlib
import re
import struct
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
WORDS = REPO / "src/apps_local/forehead/ForeheadWords.h"
VERSION = 1
RECENT = 16


def constant(name):
    match = re.search(rf"inline constexpr int {name} = (\d+);", WORDS.read_text())
    if not match:
        sys.exit(
            f"seed_save: no {name} in {WORDS.name}; regenerate the word table first"
        )
    return int(match.group(1))


def main():
    card = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else REPO / "fs_agent"
    categories = constant("kCategoryCount")
    entries = constant("kEntryCount")
    mask_bytes = (entries + 7) // 8

    # An evening and a bit: sixteen rounds with a real spread, so the ornament
    # has a shape rather than a flat row. The peak is 14 and there is a zero in
    # there, because "scored nothing" and "never played" must not look alike and
    # the only way to see that they do not is to render one.
    recent = [7, 9, 5, 11, 8, 12, 6, 10, 0, 13, 9, 8, 14, 7, 11, 9]
    best = max(recent)
    rounds = 24
    words = 187

    best_in = [0] * categories
    played = 0
    # Four lists actually played, the rest untouched, so the picker shows both
    # BEST and NEW side by side rather than one or the other.
    for index, score in ((0, 14), (1, 11), (2, 9), (4, 8)):
        if index < categories:
            best_in[index] = score
            played |= 1 << index

    # A deck a few rounds in: the first forty animals seen, everything else
    # untouched. Enough that the no-repeat mask is doing something real without
    # any category being near a lap.
    mask = bytearray(mask_bytes)
    for entry in range(40):
        mask[entry // 8] |= 1 << (entry % 8)

    blob = struct.pack(
        f"<BBHHBB{RECENT}s{categories}sI{mask_bytes}s",
        0,  # category: ANIMALS
        6,  # roundSeconds / 10 -> 60
        rounds,
        words,
        best,
        len(recent),
        bytes(recent),
        bytes(best_in),
        played,
        bytes(mask),
    )

    out = card / ".crosspoint"
    out.mkdir(parents=True, exist_ok=True)
    (out / "forehead.sav").write_bytes(bytes([VERSION]) + blob)
    print(
        f"seeded {out / 'forehead.sav'}: {rounds} rounds, best {best}, {len(blob)} bytes"
    )


if __name__ == "__main__":
    main()

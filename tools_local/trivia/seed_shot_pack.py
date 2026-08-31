#!/usr/bin/env python3
"""Write the one-question pack the site screenshot is taken against.

The real pack is fifty thousand clues built from the Jeopardy dataset by
build_pack.py, and the dataset is not in this repo. A screenshot does not need
it: it needs ONE clue, always the same one, so the picture and the alt text in
site/index.html can be checked against each other and neither drifts.

The clue below is the one the alt text already describes, so regenerating the
shot reproduces the published image rather than replacing it with whatever the
scheduler happened to deal. That is the property the earlier shot lacked.

    tools_local/trivia/seed_shot_pack.py <sd-card-root>
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pack_format

# d = difficulty 1..5 (drives the pips: one filled, four outlined), y = year.
SHOT_CLUE = {
    "d": 1,
    "y": 1850,
    "q": "The letter in Hawthorne's The Scarlet Letter was this one, "
    "initially standing for the wearer's perceived sin.",
    "a": "A",
    "alt": [],
    "w": ["B", "S", "H"],
}


def main():
    if len(sys.argv) != 2:
        print(__doc__.strip())
        return 1
    root = sys.argv[1]
    out_dir = os.path.join(root, "trivia")
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, "pack.dat")
    written = pack_format.write([SHOT_CLUE], path)
    # The app keeps per-question flags beside the pack; a stale one from a
    # bigger pack would point into records this pack does not have.
    state = pack_format.state_path(path)
    if os.path.exists(state):
        os.remove(state)
    pack_format.write_state(state, 1)
    print(f"wrote {path} ({written} bytes, 1 clue)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

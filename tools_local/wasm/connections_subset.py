#!/usr/bin/env python3
"""Cut a small Connections archive out of the published one, for the browser
build's canned answers.

The real archive is 1.3MB of JSON and the browser build downloads its whole
card before it can boot, so shipping all 1149 puzzles is not worth it. But
neither is inventing puzzles: the page's claim is that this is the firmware,
and a Connections demo playing made-up puzzles would make that a lie. So this
keeps the newest N of the real thing, in the real schema, and the firmware
imports it with the same parser it uses on the device.

Newest rather than oldest, for the same reason pack_subset.py takes the newest
xkcd: an archive that opens on 2023 misrepresents what the app is for.

    python3 tools_local/wasm/connections_subset.py <connections.json> [count]

Writes tools_local/wasm/sdcard/canned/connections.json.

Note on `level`: puzzles from id 832 onward publish `"level": -1` for every
group rather than 0..3. That is the source's own change, not damage from this
script, and the firmware already maps anything outside 0..3 to kLevelUnknown
(ConnectionsImport.cpp:142). Slicing the newest therefore yields puzzles whose
difficulty colours are unknown, which is exactly what the device shows for the
same puzzles.
"""

import json
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
OUT = HERE / "sdcard" / "canned" / "connections.json"


def main() -> None:
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    source = pathlib.Path(sys.argv[1])
    keep = int(sys.argv[2]) if len(sys.argv) > 2 else 40

    puzzles = json.loads(source.read_text())
    if not isinstance(puzzles, list) or not puzzles:
        sys.exit(f"{source} is not a non-empty JSON array")

    # Sorted by id rather than trusting file order: the importer reads in source
    # order and the archive screen counts what it got, so a shuffled slice would
    # still import but would read oddly in the date grid.
    puzzles.sort(key=lambda p: p["id"])
    kept = puzzles[-keep:]

    for p in kept:
        answers = p.get("answers", [])
        if len(answers) != 4 or any(len(a.get("members", [])) != 4 for a in answers):
            sys.exit(
                f"puzzle {p.get('id')} is not four groups of four; refusing to ship it"
            )

    OUT.parent.mkdir(parents=True, exist_ok=True)
    # separators: the device parses bytes off a socket, and every byte of this
    # file is downloaded before the page can boot. No pretty-printing.
    OUT.write_text(json.dumps(kept, separators=(",", ":"), ensure_ascii=True))
    print(
        f"wrote {OUT.relative_to(HERE.parent.parent)}: "
        f"{len(kept)} puzzles, ids {kept[0]['id']}..{kept[-1]['id']}, "
        f"{OUT.stat().st_size / 1024:.0f}KB"
    )


if __name__ == "__main__":
    main()

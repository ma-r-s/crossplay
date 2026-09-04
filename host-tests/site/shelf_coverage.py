"""Every game and app on the shelf is named on the site and in the README.

The shelf is the product and the page is how anyone finds out what is on it,
and nothing connected the two. SUDOKU shipped in v1.3.x and GET BOOKS in
v1.3.6; on 2026-08-29 neither appeared anywhere on the site -- eleven releases
of a game nobody browsing could see. FOREHEAD was on the page and missing from
the README the same day. None of that is visible in a build, in a render or in
a read-through, because what is absent looks like nothing at all.

The list is read out of Shelf.cpp, which is what actually decides what a device
shows, so a new app fails this until somebody writes it up. Prints one line per
gap and nothing when there are none; host-tests/site/run.sh counts the lines.

    python3 host-tests/site/shelf_coverage.py <repo-root>
"""

import pathlib
import re
import sys

if len(sys.argv) < 2:
    sys.exit("usage: shelf_coverage.py <repo-root>")

root = pathlib.Path(sys.argv[1])
shelf = (root / "src/apps_local/Shelf.cpp").read_text()


def titles(table):
    body = re.search(
        r"constexpr shelf::Item " + table + r"\[\] = \{(.*?)\n\};", shelf, re.S
    )
    if not body:
        print(
            f"Shelf.cpp has no {table} table, so this check cannot see the shelf at all"
        )
        return []
    return re.findall(r'\{"([^"]+)"', body.group(1))


def flat(text):
    """Casing, punctuation and &amp; are the page's business, not the shelf's."""
    text = text.replace("&amp;", "&").replace("&#38;", "&")
    return " " + re.sub(r"[^a-z0-9]+", " ", text.lower()) + " "


pages = {
    "the site": flat((root / "site/index.html").read_text()),
    "the README": flat((root / "README.md").read_text()),
}

for table, kind in (("kGames", "game"), ("kApps", "app")):
    for title in titles(table):
        needle = " " + flat(title).strip() + " "
        for where, text in pages.items():
            if needle not in text:
                print(
                    f"the {kind} {title} is on the shelf and named nowhere in {where}"
                )


# ---------------------------------------------------------------------------
# And the PLAY NEARBY list, which is the same failure one level down: a game
# that gained a radio and was never added to the sentence. On 2026-08-29 the
# release notes said "Eight of them" and named eight, months after Toy Battle
# became the ninth. The truth is LinkPlay.h's GameId enum -- an id is what a
# device actually offers to play -- so the sentence is checked against it.
# ---------------------------------------------------------------------------

link = (root / "src/apps_local/link/LinkPlay.h").read_text()
body = re.search(r"enum class GameId[^{]*\{(.*?)\n\};", link, re.S)
if not body:
    print("LinkPlay.h has no GameId enum, so the PLAY NEARBY list cannot be checked")
    ids = []
else:
    ids = [n for n in re.findall(r"^\s*([A-Za-z]+)\s*=", body.group(1), re.M) if n != "Test"]

# ConnectFour -> "Connect Four". flat() then makes the spacing and casing moot.
spaced = [re.sub(r"(?<!^)(?=[A-Z])", " ", name) for name in ids]

prose = {
    "the README": (root / "README.md").read_text(),
    "the release notes": (root / "docs/release-notes.md").read_text(),
}
for where, text in prose.items():
    near = " ".join(par for par in re.split(r"\n\s*\n", text) if "PLAY NEARBY" in par)
    if not near:
        print(f"{where} does not mention PLAY NEARBY at all")
        continue
    near = flat(near)
    for name in spaced:
        if " " + flat(name).strip() + " " not in near:
            print(f"{name} plays over PLAY NEARBY and {where} leaves it out of the list")

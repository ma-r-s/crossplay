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
#
# THE RELEASE PAGE IS NO LONGER ONE OF THE PLACES CHECKED, because it no longer
# enumerates: docs/release-body.md carries one sentence with no names and no
# number in it. The list moved out on 2026-09-04, when the page stopped being
# the archive as well. What this therefore no longer examines is whether the
# release page names every game -- it deliberately names none, and the check
# further down asserts that it stays that way rather than leaving the question
# unasked.
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


# ---------------------------------------------------------------------------
# The names above are checked; the totals beside them were not, and a total is
# the easiest thing in the file to leave behind. The README says "19 games and
# 5 apps" and "Nine of the games play over PLAY NEARBY", and both are facts
# about Shelf.cpp and LinkPlay.h written out as literals. The name checks do
# not catch a stale one: add a twentieth game and every name check still
# passes while the sentence goes on saying nineteen.
# ---------------------------------------------------------------------------

WORDS = {
    "one": 1, "two": 2, "three": 3, "four": 4, "five": 5, "six": 6,
    "seven": 7, "eight": 8, "nine": 9, "ten": 10, "eleven": 11, "twelve": 12,
    "thirteen": 13, "fourteen": 14, "fifteen": 15, "sixteen": 16,
    "seventeen": 17, "eighteen": 18, "nineteen": 19, "twenty": 20,
}


def as_number(token):
    return int(token) if token.isdigit() else WORDS.get(token.lower())


# The release page must state no total of its own. It is rewritten by
# scripts_local/release_notes.py on every release, from the merged pull
# requests, and that generator knows nothing about Shelf.cpp -- so a number
# written into its standing text is a fact about one file maintained by hand in
# another, on the one page a stranger reads first. It said "Eight of them" for
# months. The README is where the enumerated list and the totals live, and both
# are checked against the source above and below.
body_preamble = (root / "docs/release-body.md").read_text().split("### ", 1)[0]
counted = re.search(
    r"\b(\d+|" + "|".join(WORDS) + r")\s+(?:of them\b|of the games\b|games\b|apps\b)",
    body_preamble,
    re.I,
)
if counted:
    print(
        f"docs/release-body.md counts the shelf in its standing text "
        f"({counted.group(0)!r}); that is a fact about Shelf.cpp on a page "
        f"nothing regenerates from it, and it went stale there once already"
    )


readme = (root / "README.md").read_text()

shelf_total = re.search(
    r"\*\*(\d+|[A-Za-z]+) games and (\d+|[A-Za-z]+) apps\*\*", readme
)
if not shelf_total:
    print("the README no longer states an 'N games and M apps' total, so it cannot be checked")
else:
    for stated, table, kind in (
        (shelf_total.group(1), "kGames", "games"),
        (shelf_total.group(2), "kApps", "apps"),
    ):
        want = len(titles(table))
        got = as_number(stated)
        if got != want:
            print(f"the README says {stated} {kind} and {table} in Shelf.cpp has {want}")

nearby = re.search(r"(\d+|[A-Za-z]+) of the games play over \*\*PLAY NEARBY\*\*", readme)
if not nearby:
    print("the README no longer counts the PLAY NEARBY games, so the count cannot be checked")
elif ids and as_number(nearby.group(1)) != len(ids):
    print(
        f"the README says {nearby.group(1)} games play over PLAY NEARBY "
        f"and LinkPlay.h's GameId has {len(ids)}"
    )

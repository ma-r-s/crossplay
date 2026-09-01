"""Two facts about index.html that a browser will hide from you.

A BROWSER NEVER REPORTS EITHER OF THESE. That is the whole reason they are
here: both shipped to the live page and were found by a person looking at it.

1. A card that lost its </article>. On 2026-09-01 the Trivia card was missing
   its closing tag and the Wavelength <article class="card"> that should have
   followed it, so ONE card held two shots, two tags, two headings and two
   paragraphs. .card is a flex column, so Wavelength was stacked under Trivia
   inside a single grid cell and the row went double height with a screen's
   worth of hole beside it.

   Tag counting alone does not see this: the two missing tags are a matched
   pair, so opens still equalled closes (23 = 23) and the file looked balanced.
   What gives it away is the CONTENT -- a card holding two of everything -- so
   that is what is counted here. Error recovery means the page still renders,
   and shelf_coverage.py still finds both names in the text, so nothing else in
   this suite can fail on it.

2. A shot stored at 2x. The simulator captures at twice the panel, and the
   four scripted shots (scripts_local/shoot-*.sh) copy that straight into
   site/assets/shots/. The declared width/height are the 1x numbers, so the
   aspect is right and the card looks perfect -- it is simply four times the
   pixels, on a page that lazy-loads two dozen of them. trivia, wavelength,
   toybattle and forehead were all 2x on 2026-09-01 and nothing said so.

Prints one line per problem and nothing when there are none; run.sh counts the
lines and checks the exit status.

    python3 host-tests/site/page_structure.py <repo-root>
"""

import pathlib
import re
import struct
import sys

if len(sys.argv) < 2:
    sys.exit("usage: page_structure.py <repo-root>")

root = pathlib.Path(sys.argv[1])
page = root / "site/index.html"
html = page.read_text()

# -- 1. one card, one of everything -------------------------------------------

opens = len(re.findall(r"<article\b", html))
closes = len(re.findall(r"</article>", html))
if opens != closes:
    print(f"index.html has {opens} <article> and {closes} </article>")

# Split on the opening tags and read each card up to its own close. Cards are
# never nested, so the first </article> after an open is that card's.
parts = re.split(r"(<article\b[^>]*>)", html)
# What exactly one card holds. Counted rather than named so a card that grows a
# second heading fails here whichever element it duplicated.
PIECES = {
    "shot": r'<div class="shot-wrap"',
    "tag": r'<span class="tag"',
    "heading": r"<h3\b",
}

for i in range(1, len(parts), 2):
    tag, rest = parts[i], parts[i + 1]
    end = rest.find("</article>")
    if end < 0:
        print(f"a {tag} is never closed, so the cards after it are inside it")
        continue
    inner = rest[:end]
    # Name it by its first heading; an unnamed card is not worth guessing at.
    name = re.search(r"<h3[^>]*>(.*?)</h3>", inner, re.S)
    name = re.sub(r"\s+", " ", name.group(1)).strip() if name else "(no heading)"
    for what, pattern in PIECES.items():
        n = len(re.findall(pattern, inner))
        if n != 1:
            others = re.findall(r"<h3[^>]*>(.*?)</h3>", inner, re.S)
            print(
                f"the card starting at {name} holds {n} {what}s, not 1 -- "
                f"a missing </article> stacks cards into one grid cell "
                f"(headings inside it: {', '.join(o.strip() for o in others)})"
            )
            break


# -- 2. every shot stored at the size the page declares ------------------------


def png_size(path):
    """Width and height out of the IHDR chunk. No Pillow: this suite runs on a
    bare python3, and a check that needs installing is a check that gets
    skipped."""
    with open(path, "rb") as fh:
        head = fh.read(24)
    if head[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    return struct.unpack(">II", head[16:24])


for tag in re.findall(r"<img\b[^>]*>", html):
    src = re.search(r'src="([^"]+)"', tag)
    dec_w = re.search(r'\bwidth="(\d+)"', tag)
    dec_h = re.search(r'\bheight="(\d+)"', tag)
    if not src:
        continue
    ref = src.group(1)
    # Nothing on this page loads an image from anywhere else, but a remote or
    # inline one would otherwise be resolved as a relative path and reported as
    # missing, which is a lie about a file that was never meant to be there.
    if ref.startswith(("http://", "https://", "//", "data:")):
        continue
    path = root / "site" / ref.lstrip("/")
    if not path.exists():
        print(f"index.html shows {ref} and no such file exists")
        continue
    if not (dec_w and dec_h):
        print(f"{ref} is shown with no width/height, so it cannot be checked")
        continue
    size = png_size(path)
    if size is None:
        continue
    dec = (int(dec_w.group(1)), int(dec_h.group(1)))
    if size != dec:
        w, h = size
        scale = w / dec[0]
        how = f"{scale:g}x" if scale == h / dec[1] else "a different shape"
        print(
            f"{ref} is stored {w}x{h} and declared {dec[0]}x{dec[1]} "
            f"({how}). The aspect is right so the card looks perfect; it is "
            f"just the bytes. Downscale it to the declared size."
        )

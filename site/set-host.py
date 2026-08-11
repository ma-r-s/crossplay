#!/usr/bin/env python3
"""Stamp the deployed hostname into every page's share tags.

og:image and og:url have to be absolute or no unfurler resolves them, and the
hostname is the one fact this repo cannot know. Rather than leave a comment
nobody reads, this makes it a command:

    python3 site/set-host.py https://crossplay.example

Idempotent: run it again after changing the domain and it rewrites rather than
duplicating. Running it with no argument prints what is currently stamped.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent
# Every page with share tags. A new page joins this list or ships the
# placeholder host in its unfurls.
PAGES = {
    ROOT / "index.html": "/",
    ROOT / "study" / "index.html": "/study/",
}


def main():
    first = next(iter(PAGES))
    current = re.search(r'<meta property="og:url" content="([^"]*)"', first.read_text())
    if len(sys.argv) < 2:
        print(current.group(1) if current else "og:url is not set")
        return

    host = sys.argv[1].rstrip("/")
    if not host.startswith("http"):
        sys.exit("give the full origin, e.g. https://crossplay.example")

    for page, path in PAGES.items():
        html = page.read_text()
        html = re.sub(
            r'<meta property="og:image" content="[^"]*"',
            f'<meta property="og:image" content="{host}/assets/shots/og.png"',
            html,
        )
        if re.search(r'<meta property="og:url" content="', html):
            html = re.sub(
                r'<meta property="og:url" content="[^"]*"',
                f'<meta property="og:url" content="{host}{path}"',
                html,
            )
        else:
            html = html.replace(
                '<meta property="og:type" content="website">',
                f'<meta property="og:type" content="website">\n'
                f'<meta property="og:url" content="{host}{path}">',
            )
        page.write_text(html)
        print(f"stamped {host}{path}")


if __name__ == "__main__":
    main()

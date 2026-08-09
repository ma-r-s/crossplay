#!/usr/bin/env python3
"""Stamp the deployed hostname into index.html's share tags.

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

PAGE = pathlib.Path(__file__).resolve().parent / "index.html"
PLACEHOLDER = "SET-YOUR-HOST"


def main():
    html = PAGE.read_text()
    current = re.search(r'<meta property="og:url" content="([^"]*)"', html)
    if len(sys.argv) < 2:
        print(current.group(1) if current else "og:url is not set")
        return

    host = sys.argv[1].rstrip("/")
    if not host.startswith("http"):
        sys.exit("give the full origin, e.g. https://crossplay.example")

    html = re.sub(
        r'<meta property="og:image" content="[^"]*"',
        f'<meta property="og:image" content="{host}/assets/shots/og.png"',
        html,
    )
    if current:
        html = re.sub(
            r'<meta property="og:url" content="[^"]*"',
            f'<meta property="og:url" content="{host}/"',
            html,
        )
    else:
        html = html.replace(
            '<meta property="og:type" content="website">',
            f'<meta property="og:type" content="website">\n'
            f'<meta property="og:url" content="{host}/">',
        )
    PAGE.write_text(html)
    print(f"stamped {host}")


if __name__ == "__main__":
    main()

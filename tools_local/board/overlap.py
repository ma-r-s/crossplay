#!/usr/bin/env python3
"""Open pull requests that touch the same file, named before either merges.

Two pull requests from different sessions once touched one file: one made
concurrent renders the norm, the other added render-side readers of exactly
the strings that made unsafe. Each was correct and green alone; the bug would
have looked intermittent and bisected to whichever landed second. Nothing in
the tooling said "these two open branches interact". This does, mechanically:
for every pair of open pull requests sharing a file, the pair and the files.

    tools_local/board/overlap.py                 asks gh for the open PRs
    tools_local/board/overlap.py --from-json F   reads gh's JSON from a file

Exit 0 always; the output is the point. Paths under site/emulator/ (CI's own
rebuilt artefact) are ignored.
"""
import argparse
import itertools
import json
import subprocess
import sys

REPO = "ma-r-s/crossplay"
IGNORE_PREFIXES = ("site/emulator/",)


def open_prs(from_json=None):
    if from_json:
        return json.load(open(from_json))
    out = subprocess.run(
        ["gh", "pr", "list", "-R", REPO, "--state", "open", "--limit", "100",
         "--json", "number,title,headRefName,files"],
        capture_output=True, text=True, check=True,
    ).stdout
    return json.loads(out or "[]")


def overlaps(prs):
    """[(a, b, [paths])] for every pair of PRs sharing a file, paths sorted."""
    files = {}
    for pr in prs:
        files[pr["number"]] = {
            f["path"] for f in pr.get("files") or [] if not f["path"].startswith(IGNORE_PREFIXES)
        }
    by_num = {pr["number"]: pr for pr in prs}
    out = []
    for a, b in itertools.combinations(sorted(files), 2):
        shared = sorted(files[a] & files[b])
        if shared:
            out.append((by_num[a], by_num[b], shared))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--from-json", help="gh pr list --json number,title,headRefName,files, saved to a file")
    a = ap.parse_args()
    prs = open_prs(a.from_json)
    pairs = overlaps(prs)
    if not pairs:
        print(f"{len(prs)} open pull request(s), no two touch the same file")
        return 0
    for x, y, shared in pairs:
        print(f"#{x['number']} ({x['headRefName']}) and #{y['number']} ({y['headRefName']}) both touch:")
        for p in shared:
            print(f"    {p}")
    print(f"{len(pairs)} overlapping pair(s) among {len(prs)} open pull request(s); merge one, then re-verify the other on the result")
    return 0


if __name__ == "__main__":
    sys.exit(main())

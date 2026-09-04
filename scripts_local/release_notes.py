#!/usr/bin/env python3
"""Write the next release's notes and version, from what merged since the last tag.

Run by .github/workflows/crossplay-autorelease.yml after every green CI run on
xteink, and by hand for a look:

    scripts_local/release_notes.py --dry-run           # print what it would do
    scripts_local/release_notes.py --write             # bump platformio.ini and the notes

It reads the merges on the first-parent line since the newest v* tag, asks
GitHub for each merge's pull request, takes the pull request's "What is new"
line(s), and falls back to the merge's own subject. The notes replace the
`### What is new in <version>` block in docs/release-notes.md, which is
where host-tests/release insists they live, and `[crossplay] version` in
platformio.ini goes up one patch (one minor if any merged pull request carries
the `release:minor` label).

Every input can be replaced for tests: --repo-dir, --pr-json (a file of pull
requests instead of gh), --last-tag.
"""

import argparse
import json
import pathlib
import re
import subprocess
import sys

TAG = re.compile(r"^v(\d+)\.(\d+)\.(\d+)$")
NEW_LINE = re.compile(
    r"^\s*(?:[-*]\s*)?(?:\*\*)?what is new(?:\*\*)?\s*[:\-]\s*(.+)$", re.I
)
NEW_HEAD = re.compile(r"^#{1,4}\s*what is new\b.*$", re.I)


def run(cmd, cwd):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"release_notes: {' '.join(cmd)}: {r.stderr.strip()[:300]}")
    return r.stdout


def last_tag(repo, override=None):
    if override:
        return override
    tags = [
        t for t in run(["git", "tag", "--list", "v*"], repo).split() if TAG.match(t)
    ]
    if not tags:
        raise SystemExit("release_notes: no v* tag to count from")
    return max(tags, key=lambda t: tuple(int(x) for x in TAG.match(t).groups()))


def merges_since(repo, tag):
    out = run(
        ["git", "log", "--first-parent", "--format=%H%x00%s", f"{tag}..HEAD"], repo
    )
    rows = []
    for line in out.splitlines():
        sha, _, subject = line.partition("\x00")
        if subject.startswith("chore: crossplay ") or subject.startswith(
            "chore: emulator rebuilt"
        ):
            continue
        rows.append((sha, subject))
    return rows


def prs_for(shas, repo_slug, pr_json=None):
    """Pull requests whose merge commit is one of shas: {sha: {number, title, body, labels}}."""
    if pr_json:
        data = json.loads(pathlib.Path(pr_json).read_text())
    else:
        out = subprocess.run(
            [
                "gh",
                "pr",
                "list",
                "-R",
                repo_slug,
                "--state",
                "merged",
                "--limit",
                "100",
                "--json",
                "number,title,body,labels,mergeCommit",
            ],
            capture_output=True,
            text=True,
        )
        data = json.loads(out.stdout or "[]") if out.returncode == 0 else []
    by_sha = {}
    for pr in data:
        mc = pr.get("mergeCommit") or {}
        oid = mc.get("oid") if isinstance(mc, dict) else mc
        if oid and oid in shas:
            by_sha[oid] = pr
    return by_sha


def what_is_new(pr):
    """The pull request's own line(s) for the notes, or None."""
    body = pr.get("body") or ""
    lines = body.splitlines()
    found = []
    for i, line in enumerate(lines):
        m = NEW_LINE.match(line)
        if m:
            found.append(m.group(1).strip())
            continue
        if NEW_HEAD.match(line):
            for nxt in lines[i + 1 :]:
                if nxt.startswith("#"):
                    break
                t = nxt.strip().lstrip("-* ").strip()
                if t:
                    found.append(t)
    return found or None


def branch_subject(repo, sha):
    """For a merge commit, the first real subject on the branch it merged; else None."""
    r = subprocess.run(["git", "log", "--format=%s", f"{sha}^1..{sha}^2"], cwd=repo, capture_output=True, text=True)
    if r.returncode != 0:
        return None
    for line in reversed(r.stdout.splitlines()):
        if line.strip() and not line.startswith(("Merge ", "chore: emulator", "chore: crossplay")):
            return line.strip()
    return None


def humanize(subject):
    s = re.sub(
        r"^Merge (?:branch|pull request) '?#?[^' ]+'? (?:into \S+|from \S+)?",
        "",
        subject,
    ).strip()
    s = re.sub(r"^(feat|fix|chore|docs|refactor|test|perf)(\([^)]*\))?:\s*", "", s)
    return (s[:1].upper() + s[1:]) if s else subject


def bump(version, minor):
    a, b, c = (int(x) for x in version.split("."))
    return f"{a}.{b + 1}.0" if minor else f"{a}.{b}.{c + 1}"


def current_version(ini_text):
    m = re.search(r"^\[crossplay\]\s*\n(?:.*\n)*?version\s*=\s*(\S+)", ini_text, re.M)
    if not m:
        raise SystemExit("release_notes: no [crossplay] version in platformio.ini")
    return m.group(1)


def rewrite_notes(text, version, bullets):
    """Replace the `### What is new in X` block (to the next ### or the end) in docs/release-notes.md."""
    lines = text.splitlines(keepends=True)
    start = next(
        (i for i, l in enumerate(lines) if re.match(r"^\s*### What is new in ", l)),
        None,
    )
    if start is None:
        raise SystemExit(
            "release_notes: docs/release-notes.md has no '### What is new in' heading"
        )
    indent = re.match(r"^(\s*)", lines[start]).group(1)
    end = start + 1
    while end < len(lines):
        l = lines[end]
        if re.match(r"^\s*###\s", l) or (l.strip() and not l.startswith(indent)):
            break
        end += 1
    block = (
        [f"{indent}### What is new in {version}\n", f"{indent}\n"]
        + [f"{indent}- {b}\n" for b in bullets]
        + [f"{indent}\n"]
    )
    return "".join(lines[:start] + block + lines[end:])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo-dir", default=".")
    ap.add_argument("--repo", default="ma-r-s/crossplay")
    ap.add_argument("--pr-json")
    ap.add_argument("--last-tag")
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    repo = pathlib.Path(a.repo_dir).resolve()
    ini = repo / "platformio.ini"
    notes = repo / "docs" / "release-notes.md"

    tag = last_tag(repo, a.last_tag)
    merges = merges_since(repo, tag)
    if not merges:
        print(f"nothing merged since {tag}")
        print("NEXT_VERSION=")
        return
    prs = prs_for({sha for sha, _ in merges}, a.repo, a.pr_json)
    bullets = []
    minor = False
    for sha, subject in merges:
        pr = prs.get(sha)
        if pr:
            if any(
                (l.get("name") if isinstance(l, dict) else l) == "release:minor"
                for l in pr.get("labels") or []
            ):
                minor = True
            lines = what_is_new(pr) or [humanize(pr.get("title") or subject)]
            bullets.extend(lines)
        else:
            bullets.append(humanize(branch_subject(repo, sha) or subject))
    seen = set()
    bullets = [b for b in bullets if not (b in seen or seen.add(b))]

    cur = current_version(ini.read_text())
    nxt = bump(cur, minor)
    print(f"last tag {tag}, {len(merges)} merge(s), {cur} -> {nxt}")
    for b in bullets:
        print(f"  - {b}")
    print(f"NEXT_VERSION={nxt}")
    if a.write:
        ini.write_text(
            re.sub(
                r"(^\[crossplay\]\s*\n(?:.*\n)*?version\s*=\s*)\S+",
                lambda m: m.group(1) + nxt,
                ini.read_text(),
                count=1,
                flags=re.M,
            )
        )
        notes.write_text(rewrite_notes(notes.read_text(), nxt, bullets))
        print("written")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Write the next release: its version, the page it publishes, and the history.

Run by .github/workflows/crossplay-autorelease.yml after every green CI run on
xteink, and by hand for a look:

    scripts_local/release_notes.py --dry-run           # print what it would do
    scripts_local/release_notes.py --write             # bump and write both files

It reads the merges on the first-parent line since the newest v* tag, asks
GitHub for each merge's pull request, takes the pull request's "What is new"
line(s), and falls back to the merge's own subject. `[crossplay] version` in
platformio.ini goes up one patch (one minor if any merged pull request carries
the `release:minor` label), and TWO files are written:

    docs/release-body.md    the page this release publishes. Its
                            `### What is new in <version>` block is replaced;
                            everything else in it (how to install, which file
                            to download) is left exactly as written.
    docs/release-notes.md   the history. The same block is PREPENDED, so every
                            release is kept and no release page carries anybody
                            else's.

That split is the fix for a body of 20,402 characters. docs/release-notes.md
was both the page and the archive, so v1.12.21's release page opened with a
standing catalogue of nineteen games, said what was new in 1.12.21, and then
said what WAS new in 1.12.12, 1.12.11, 1.12.10, 1.12.9, 1.12.8 and 1.12.7.
Every release page carried the entire history. A release page says what changed
in THAT release and links to the rest.

Only landings a person could receive anything different from become notes.
v1.12.17 listed all seven merges since the previous tag, and four of them -- a
board watcher, a server-side bridge and two release-pipeline fixes -- change
nothing anybody receives. Mario got the update prompt on his device, read this
file's output for that version, and asked why a release had happened at all.
The one line that did reach the firmware, an upstream sync, named the operation
and not one thing it changed. Both halves are fixed here: reaches_a_user() asks
the one table that already classifies paths
(scripts_local/device-build-needed.sh --ships), and a sync's body is read for
the upstream subjects its title hides.

The excluded landings are named on stdout, where the autorelease log keeps
them, and NOT in the published body. They used to be a bullet -- "Plus 4
changes nothing on the device can see." -- which is itself a line a player
cannot act on, on a page written for players. The reader who needs to know why
the notes and the merge log differ is a developer reading the job log.

WHERE THIS TEXT IS READ, checked rather than assumed: the GitHub release page
only (crossplay-release.yml passes docs/release-body.md as body_path). The
device shows two version numbers and nothing else -- ReleaseJsonParser.cpp
parses tag_name, the asset name, its url and its size, and OtaUpdateActivity.cpp
draws STR_CURRENT_VERSION and STR_NEW_VERSION. So the prompt a device raises is
"there is a release", and this file answers "and here is what is in it"
somewhere else. That is still the sentence being fixed; it is just not on the
panel, and a check written as though it were would be measuring nothing.

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

# A sync's body carries the upstream commits its title only counts, one
# `- `sha` subject` bullet each, per docs/workflow/upstream-sync.md step 5.
UPSTREAM_COMMIT = re.compile(r"^\s*[-*]\s+`([0-9a-f]{7,40})`\s+(\S.*?)\s*$")
# The sync run's own title, fixed by that runbook's step 5. Anchored, and not
# a substring search for "sync": this branch REPLACES a pull request's title
# with lines lifted out of its body, and `"sync" in title` is true of
# `fix: the deck reopens after sync; review no longer panics` -- a real subject
# from this history, whose notes would then have become whatever sha-shaped
# bullets its body happened to contain. Four more branches here (app/syncard,
# app/upsync, app/syncsdk, app/clocksync) match the substring too.
SYNC_TITLE = re.compile(r"^\s*chore(?:\([^)]*\))?:\s*sync\b", re.I)
# A fence in the body. The commit list is one section of a sync's write-up and
# the rest is prose, so a code block that happens to hold `- `deadbeef` ...`
# lines is reachable, and it must not become the release's notes.
FENCE = re.compile(r"^\s*(?:```|~~~)")
# Upstream's own pull request number, which in these notes reads as one of ours.
UPSTREAM_PR = re.compile(r"\s*\(#\d+\)\s*$")
# The sync's own bookkeeping. Anchored to a WHOLE version, because `^v?\d+\.\d+`
# unanchored also eats `feat: 1.5x zoom on the page view` and `fix: 3.5mm jack
# detection` -- humanize() has already stripped the type prefix by the time this
# runs, so the subject really does start with a digit and a dot. Silently, with
# no count and no log line: exactly the failure this whole file is about.
VERSION_BUMP = re.compile(r"^bump version\b|^v?\d+\.\d+\.\d+\s*$", re.I)
# Upstream writes em-dashes and GitHub truncates long subjects with an ellipsis.
# Both land verbatim on a public page now that a sync yields N of its own lines
# rather than one of ours, so the dash becomes a comma on the way through.
DASH = re.compile(r"\s+[\u2013\u2014]\s+")

# The one classification table in this repository: every path prefix with two
# independent attributes, `builds` and `ships`. This file reads the `ships`
# column. ASKED, never restated: see reaches_a_user().
RULE = pathlib.Path(__file__).resolve().parent / "device-build-needed.sh"


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


def reaches_a_user(repo, sha):
    """Could a person receive anything different because of this landing?

    v1.12.17 announced seven changes. Four of them -- a board watcher, a
    server-side bridge and two release-pipeline fixes -- change nothing anybody
    receives, and they were the lines a reader met first. The release itself
    was right: an upstream sync moved the SDK, the SD font loader and about
    thirty translations. Only the notes were wrong.

    The question is one column of the table in
    scripts_local/device-build-needed.sh, and it is ASKED here rather than
    restated. Two questions read that table -- "does a device build need to
    run" and "could a person notice" -- and for one day they shared a single
    predicate whose unknown-path default was right for the first and wrong for
    the second. That is what cut v1.12.21 for a `.gitignore` edit. A second
    copy of the rule in Python would drift the first time somebody adds a
    top-level directory, silently, since the only symptom would be notes that
    quietly stopped mentioning something.

    `sha^1..sha` is the mainline parent against the merge, which is exactly
    what the landing added to xteink. --range diffs from the merge base, and
    the merge base of a merge and its own first parent is that parent, so this
    is the same range either way.

    Fail-safe is INCLUDE, in every direction the script can fail: exit 1 is its
    only "no". Exit 2 is its refusal -- a changed path is in no row of the
    table -- and everything else is an unreadable range, a missing script or a
    crash. All of them print the bullet, and the refusal prints why. A bullet
    shown needlessly is noise; a bullet hidden wrongly is the bug this function
    exists to fix.
    """
    if not RULE.exists():
        return True
    parent = subprocess.run(
        ["git", "rev-parse", "-q", "--verify", f"{sha}^1"],
        cwd=repo,
        capture_output=True,
        text=True,
    )
    if parent.returncode != 0:
        return True
    r = subprocess.run(
        ["bash", str(RULE), "--range", f"{sha}^1..{sha}", "--ships", "--quiet"],
        cwd=repo,
        capture_output=True,
        text=True,
    )
    if r.returncode not in (0, 1):
        # The refusal, and it must not be swallowed. The bullet goes in either
        # way; what would be lost without this is the only signal that a path
        # nobody has classified just went past.
        print(f"  (unclassified path in {sha[:9]}; listing it) {r.stderr.strip()[:300]}")
        return True
    return r.returncode != 1


def upstream_lines(pr):
    """A sync's body, which lists what came in, instead of its title, which counts it.

    "Sync CrossPoint develop (6 commits) and FreeInk SDK" was the only line in
    v1.12.17 that reached the firmware, and it named the operation rather than
    anything that changed: language-specific fonts, thirty updated
    translations, a new Bulgarian UI and a fix keeping the glyph arena usable
    under heap pressure were all in it and none of them were said.

    docs/workflow/upstream-sync.md step 5 tells the sync run to put the
    upstream commit subjects in the body, one `- `sha` subject` bullet each, so
    they are already there. Two or more of them, or this leaves the title
    alone: one such line is as likely to be a commit mentioned in prose.

    Version bumps are dropped -- they are the sync's own bookkeeping. Nothing
    else is filtered by type: "chore: update translations" is a chore and is
    the most visible thing in that list, so a type filter would be wrong in
    both directions on this very PR.

    Three narrow gates, each of which was wide once and each of which turns a
    pull request's own title into somebody else's text when it is: the title
    must be the sync run's (SYNC_TITLE, not "sync" anywhere in it), the bullets
    must be outside every code fence, and there must be at least two.
    """
    if not SYNC_TITLE.match(pr.get("title") or ""):
        return None
    out = []
    fenced = False
    for line in (pr.get("body") or "").splitlines():
        if FENCE.match(line):
            fenced = not fenced
            continue
        if fenced:
            continue
        m = UPSTREAM_COMMIT.match(line)
        if not m:
            continue
        s = humanize(UPSTREAM_PR.sub("", m.group(2)))
        if VERSION_BUMP.match(s):
            continue
        out.append(DASH.sub(", ", s))
    return out if len(out) >= 2 else None


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
    """Replace the `### What is new in X` block (to the next ### or the end) in the published body."""
    lines = text.splitlines(keepends=True)
    start = next(
        (i for i, l in enumerate(lines) if re.match(r"^\s*### What is new in ", l)),
        None,
    )
    if start is None:
        raise SystemExit(
            "release_notes: docs/release-body.md has no '### What is new in' heading"
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


# The history file's entries begin here. Everything above the marker is the
# file's own preamble and is never touched; everything below is one `### X.Y.Z`
# block per release, newest first.
HISTORY_MARKER = "<!-- releases, newest first -->"


def prepend_history(text, version, bullets):
    """Put this release at the top of the history, keeping every earlier one.

    The old shape had one file being both the published page and the archive,
    which is why a release page carried six previous releases. Here the archive
    only ever grows, and nothing reads it at publish time.

    Idempotent on the version: re-running --write for a version already at the
    top replaces that block rather than stacking a second copy. The autorelease
    commits and tags in one go so this should not happen, but a hand re-run
    after a failed push is the obvious way it would, and a doubled entry is
    silent.
    """
    if HISTORY_MARKER not in text:
        raise SystemExit(
            f"release_notes: docs/release-notes.md has no {HISTORY_MARKER!r} line; "
            "the history has no place to insert at"
        )
    head, _, rest = text.partition(HISTORY_MARKER)
    lines = rest.splitlines(keepends=True)
    # Drop an existing block for this exact version, wherever it sits.
    out, skipping = [], False
    for line in lines:
        if re.match(r"^###\s", line):
            skipping = line.strip() == f"### {version}"
        if not skipping:
            out.append(line)
    body = "".join(out).lstrip("\n")
    block = f"### {version}\n\n" + "".join(f"- {b}\n" for b in bullets) + "\n"
    return head + HISTORY_MARKER + "\n\n" + block + body


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
    body = repo / "docs" / "release-body.md"
    history = repo / "docs" / "release-notes.md"

    tag = last_tag(repo, a.last_tag)
    merges = merges_since(repo, tag)
    if not merges:
        print(f"nothing merged since {tag}")
        print("NEXT_VERSION=")
        return
    prs = prs_for({sha for sha, _ in merges}, a.repo, a.pr_json)
    kept, dropped = [], []
    minor = False
    for sha, subject in merges:
        pr = prs.get(sha)
        if pr:
            if any(
                (l.get("name") if isinstance(l, dict) else l) == "release:minor"
                for l in pr.get("labels") or []
            ):
                # The label is about the version, not about the notes: a
                # tooling pull request that declares itself a minor still is
                # one, whether or not it earns a line.
                minor = True
            title = pr.get("title") or subject
            lines = what_is_new(pr) or upstream_lines(pr) or [humanize(title)]
        else:
            title = branch_subject(repo, sha) or subject
            lines = [humanize(title)]
        (kept if reaches_a_user(repo, sha) else dropped).append(
            (humanize(title), lines)
        )

    def dedupe(seq):
        seen = set()
        return [b for b in seq if not (b in seen or seen.add(b))]

    bullets = dedupe([b for _, lines in kept for b in lines])
    if not bullets:
        # Nothing merged since the tag reaches a user. The automatic path
        # cannot get here -- release-needed.sh gates the release on exactly
        # this question -- but a release cut by hand can, and a "What is new"
        # heading with no bullets under it is worse than a noisy one: it reads
        # as a broken generator and tells nobody anything. So the filter stands
        # down and every line goes in.
        print("nothing since the tag reaches a user: listing every merge")
        bullets = dedupe([b for _, lines in kept + dropped for b in lines])
        dropped = []

    cur = current_version(ini.read_text())
    nxt = bump(cur, minor)
    print(f"last tag {tag}, {len(merges)} merge(s), {cur} -> {nxt}")
    for b in bullets:
        print(f"  - {b}")
    # The excluded landings, by name, HERE and not in the published body. They
    # were a bullet once -- "Plus 4 changes nothing on the device can see." --
    # which is a line a player cannot act on, printed on a page written for
    # players. The reader who needs to know why the notes and the merge log
    # differ is a developer, and this is the autorelease job log they read.
    for title, _ in dropped:
        print(f"  (not a note, reaches no user) {title}")
    if dropped:
        n = len(dropped)
        print(f"{n} landing{'' if n == 1 else 's'} excluded from the notes, named above.")
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
        body.write_text(rewrite_notes(body.read_text(), nxt, bullets))
        history.write_text(prepend_history(history.read_text(), nxt, bullets))
        print("written")


if __name__ == "__main__":
    main()

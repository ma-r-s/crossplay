#!/bin/bash
# The claims the front-door docs make about this repository, checked against the
# repository.
#
# Three separate lies shipped here and every one of them was invisible to every
# other suite, because no suite reads prose:
#
#   1. `docs/contributing/getting-started.md` was upstream's file. It cloned
#      crosspoint-reader, it built `pio run` with no `-e`, and `[env:default]`
#      is FREEINK_DEVICE_X4/X3 -- the ESP32-C3 devices the README's one warning
#      says an S3 image used to brick. The repository's only getting-started
#      guide flashed the wrong chip from the wrong clone.
#   2. `docs/README.md` said upstream's docs stayed "untouched", naming
#      `contributing/` among them, while three files in that directory were
#      almost entirely this fork's and a fourth did not exist upstream at all.
#      That file's whole job is saying who wrote what.
#   3. `LOCAL_SCOPE.md` said "twenty-one apps, seventeen games" long after the
#      shelf passed both.
#
# Every check here DISCOVERS its expected value -- from `Shelf.cpp`, from the
# includes, from `crosspoint/develop` -- rather than holding a second copy of
# the number. A test carrying its own literal would have gone stale beside the
# doc it was guarding.
#
#   host-tests/docsclaims/run.sh
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"

python3 - "$ROOT" <<'PY'
import os
import re
import subprocess
import sys

root = sys.argv[1]
checks = 0
failed = 0


def check(ok, label, detail=""):
    global checks, failed
    checks += 1
    if not ok:
        failed += 1
        print(f"FAIL docsclaims  {label}" + (f": {detail}" if detail else ""))


def read(rel):
    with open(os.path.join(root, rel), encoding="utf-8") as f:
        return f.read()


WORDS = {
    "one": 1, "two": 2, "three": 3, "four": 4, "five": 5, "six": 6,
    "seven": 7, "eight": 8, "nine": 9, "ten": 10, "eleven": 11, "twelve": 12,
    "thirteen": 13, "fourteen": 14, "fifteen": 15, "sixteen": 16,
    "seventeen": 17, "eighteen": 18, "nineteen": 19, "twenty": 20,
}


def number(token):
    token = token.strip().lower()
    if token.isdigit():
        return int(token)
    return WORDS.get(token)


def key(title):
    """Shelf titles shout, README titles do not, and dirs run words together."""
    return re.sub(r"[^A-Z0-9&]", "", title.upper())


# ---------------------------------------------------------------------------
# The shelf is the only list of what is on this device. Everything below is
# measured against it.
# ---------------------------------------------------------------------------
shelf = read("src/apps_local/Shelf.cpp")


def shelf_table(name):
    m = re.search(
        r"constexpr shelf::Item " + name + r"\[\]\s*=\s*\{(.*?)\n\};",
        shelf,
        re.S,
    )
    if not m:
        return None
    return re.findall(r'\{\s*"([^"]+)"', m.group(1))


games = shelf_table("kGames")
apps = shelf_table("kApps")
check(bool(games), "Shelf.cpp kGames table not found -- every count below is unmeasured")
check(bool(apps), "Shelf.cpp kApps table not found -- every count below is unmeasured")
if not games or not apps:
    print(f"{checks} checks, {failed} failed")
    sys.exit(1)

# ---------------------------------------------------------------------------
# README.md: the headline count, and the two tables under it.
# ---------------------------------------------------------------------------
readme = read("README.md")

m = re.search(r"\*\*(\S+) games and (\S+) apps\*\*", readme)
check(bool(m), "README.md has no '**N games and M apps**' claim to check")
if m:
    said_games, said_apps = number(m.group(1)), number(m.group(2))
    check(said_games == len(games), "README.md games count",
          f"says {m.group(1)}, Shelf.cpp kGames has {len(games)}")
    check(said_apps == len(apps), "README.md apps count",
          f"says {m.group(2)}, Shelf.cpp kApps has {len(apps)}")


def readme_section(heading):
    m = re.search(r"^### " + heading + r"\s*$(.*?)(?=^#{2,3} |\Z)",
                  readme, re.S | re.M)
    return m.group(1) if m else ""


def bold_rows(text):
    return [key(t) for t in re.findall(r"^\|\s*\*\*([^*]+)\*\*", text, re.M)]


for heading, table, label in (("Games", games, "kGames"), ("Apps", apps, "kApps")):
    listed = bold_rows(readme_section(heading))
    want = [key(t) for t in table]
    check(bool(listed), f"README.md '### {heading}' table has no rows")
    missing = [t for t in want if t not in listed]
    extra = [t for t in listed if t not in want]
    check(not missing, f"README.md '### {heading}' table is missing shelf entries",
          ", ".join(missing))
    check(not extra, f"README.md '### {heading}' table lists rows {label} does not",
          ", ".join(extra))
    # Length as well as membership. Set comparison alone passes a table that
    # lists one game twice, which is a table with the wrong number of rows
    # sitting under a headline count this same suite checks.
    check(len(listed) == len(want),
          f"README.md '### {heading}' table has the wrong number of rows",
          f"{len(listed)} rows, {label} has {len(want)}")

# ---------------------------------------------------------------------------
# PLAY NEARBY. An app plays nearby exactly when it includes the link activity,
# so the sentence is checked against the includes rather than against itself.
# ---------------------------------------------------------------------------
nearby_dirs = set()
apps_local = os.path.join(root, "src/apps_local")
for entry in sorted(os.listdir(apps_local)):
    d = os.path.join(apps_local, entry)
    if not os.path.isdir(d):
        continue
    for fn in os.listdir(d):
        if not fn.endswith((".cpp", ".h")):
            continue
        with open(os.path.join(d, fn), encoding="utf-8", errors="replace") as f:
            if '"../link/LinkActivity.h"' in f.read():
                nearby_dirs.add(key(entry))
                break

m = re.search(r"(\w+) of the games play over \*\*PLAY NEARBY\*\*:\s*(.+?)\.",
              readme, re.S)
check(bool(m), "README.md has no PLAY NEARBY sentence to check")
if m:
    said = number(m.group(1))
    named = [key(n) for n in re.split(r",\s*|\s+and\s+", m.group(2).strip()) if n.strip()]
    check(said == len(named), "README.md PLAY NEARBY count disagrees with its own list",
          f"says {m.group(1)}, names {len(named)}")
    check(set(named) == nearby_dirs,
          "README.md PLAY NEARBY list disagrees with which apps include link/LinkActivity.h",
          f"named-not-linked={sorted(set(named) - nearby_dirs)} "
          f"linked-not-named={sorted(nearby_dirs - set(named))}")

# ---------------------------------------------------------------------------
# No reader must be able to reach a build or an upload that names no
# environment. `default_envs = default` is upstream's ESP32-C3 target.
# ---------------------------------------------------------------------------
# Every markdown file in the repository, not a chosen few: scoping this to the
# files that were known to be wrong is how the twin in the next directory
# survives. One file is exempt, by name and with a reason.
EXEMPT = {
    # Upstream's README, preserved as a record when the fork took the README.md
    # filename. Rewriting its commands would falsify the record, so it carries a
    # blockquote at the top saying it is upstream's and that its upload flashes
    # a C3 image. That note is asserted below.
    "docs/crosspoint-readme.md",
}
SKIP_DIRS = {".git", ".pio", "freeink-sdk", "node_modules", "fs_agent",
             "fs_mario", "qa-artifacts", "emulator", ".cache", "archive"}
# Deliberately NOT fence-aware. Fence tracking was a toggle that a nested or
# `~~~` block desynced silently, and it made a four-space indented code block
# invisible. The whole tree has exactly one env-less invocation outside a fence
# and it is the exempt file, so requiring a fence bought nothing and hid two
# shapes. Both binary names, because `platformio run` is the same command.
BUILD_CMD = re.compile(r"^(?:\$\s*)?(?:pio|platformio)\s+run\b")
env_less = []
scanned = 0
for dp, dns, fns in os.walk(root):
    dns[:] = [d for d in dns if d not in SKIP_DIRS]
    for fn in sorted(fns):
        if not fn.endswith(".md"):
            continue
        rel = os.path.relpath(os.path.join(dp, fn), root)
        if rel in EXEMPT:
            continue
        scanned += 1
        for n, line in enumerate(read(rel).splitlines(), 1):
            cmd = line.strip()
            if cmd.startswith("```") or cmd.startswith("~~~"):
                continue
            if BUILD_CMD.match(cmd) and " -e " not in cmd and "--environment" not in cmd:
                env_less.append(f"{rel}:{n}  {cmd}")
check(scanned > 50, "the env-less build scan reached almost nothing",
      f"only {scanned} markdown files; a scan that finds no files reports clean")
check(not env_less,
      "a firmware build or upload that names no environment builds [env:default], which is ESP32-C3",
      "; ".join(env_less))

# The exemption is only honest while the file says so itself.
cpr = read("docs/crosspoint-readme.md")
check("upstream's README" in cpr and "ESP32-C3" in cpr and "x4pro" in cpr,
      "docs/crosspoint-readme.md is exempt from the env-less scan but no longer says why",
      "it must state that it is upstream's record and that its commands target C3")

contrib = os.path.join(root, "docs/contributing")

# The two lines that make a stranger's first build work at all. Asserted as
# COMMANDS on a line of their own, not as substrings: a bare `in` test is
# satisfied by any mention anywhere, including a sentence telling you not to.
SUBMODULE_CMD = re.compile(r"^(?:\$\s*)?git\s+submodule\s+update\s+--init\b", re.M)
CLONE_UPSTREAM = re.compile(
    r"^(?:\$\s*)?git\s+clone\b.*crosspoint-reader/crosspoint-reader", re.M)
for rel, label in (("README.md", "README.md"),
                   ("docs/contributing/getting-started.md", "getting-started.md")):
    body = "\n".join(l.strip() for l in read(rel).splitlines())
    check(bool(SUBMODULE_CMD.search(body)),
          f"{label} does not carry `git submodule update --init` as a command",
          ".gitmodules pins freeink-sdk and nothing builds without it")
check("docs/contributing" in readme,
      "README.md never links docs/contributing/",
      "the correct setup path is unreachable from the front door")
# Every doc a contributor follows, not only the one that was wrong. The regex
# catches the SSH form too: `git@github.com:crosspoint-reader/...` matched
# neither of the https-only substring tests this replaces.
for rel in ["README.md", "AGENTS.md"] + [
        f"docs/contributing/{f}" for f in sorted(os.listdir(contrib)) if f.endswith(".md")]:
    body = "\n".join(l.strip() for l in read(rel).splitlines())
    m = CLONE_UPSTREAM.search(body)
    check(m is None, f"{rel} tells the reader to clone upstream's repository",
          m.group(0) if m else "")

# ---------------------------------------------------------------------------
# Which branch a contributor is told to branch from and target. `develop` is
# upstream's; this repository's is `xteink`, and nothing checked that at all --
# a doc could say "Branch from `develop`" and every suite stayed green.
#
# The branch is DISCOVERED, from the remote HEAD git already records, so this
# check does not become the literal it is guarding.
# ---------------------------------------------------------------------------
# origin/HEAD first, but a --single-branch clone and actions/checkout both
# leave that ref unset, so it cannot be the only source: keying on it alone
# turned a fresh checkout red. crossplay-ci.yml's own `branches:` filter is the
# fallback, in the repository, needing no network and no remote.
_r = subprocess.run(["git", "-C", root, "symbolic-ref", "--short",
                     "refs/remotes/origin/HEAD"], capture_output=True, text=True)
default_branch = _r.stdout.strip().split("/")[-1] if _r.returncode == 0 else ""
if not default_branch:
    _w = re.search(r"^\s*branches:\s*\[([A-Za-z0-9._/-]+)\]",
                   read(".github/workflows/crossplay-ci.yml"), re.M)
    default_branch = _w.group(1) if _w else ""
check(bool(default_branch),
      "neither origin/HEAD nor crossplay-ci.yml names a default branch, "
      "so the branch a contributor is sent at is unchecked")
if default_branch:
    BRANCH_INSTRUCTION = re.compile(
        r"^[-*\d.)\s]*(?:Branch from|Target)\s+`([A-Za-z0-9._/-]+)`", re.M | re.I)
    for fn in sorted(os.listdir(contrib)):
        if not fn.endswith(".md"):
            continue
        rel = f"docs/contributing/{fn}"
        for _m2 in BRANCH_INSTRUCTION.finditer(read(rel)):
            check(_m2.group(1) == default_branch,
                  f"{rel} sends a contributor at the wrong branch",
                  f"says `{_m2.group(1)}`, origin's default is `{default_branch}`")

# ---------------------------------------------------------------------------
# docs/buttons.md counts which apps read which logical button. It sat at
# "seventeen apps" and "Up/Down: 3" while ten apps -- seven of them games --
# had grown to read the two real keys, which inverted the finding the document
# exists to record.
# ---------------------------------------------------------------------------
# Read out of docs/buttons.md rather than written twice. This file's header
# says every expected value is discovered; a hand-kept copy of the doc's own
# exclusion list would be the one literal able to disagree with it.
_m = re.search(r"excluding the shared modules that are not apps\s*\n?\(([^)]*)\)",
               read("docs/buttons.md"))
SHARED = set(re.findall(r"`([a-z]+)`", _m.group(1))) if _m else set()
check(bool(SHARED), "docs/buttons.md no longer names the directories its census excludes",
      "the census scope is what makes its numbers mean anything")
button_use = {}
for entry in sorted(os.listdir(apps_local)):
    d = os.path.join(apps_local, entry)
    if not os.path.isdir(d) or entry in SHARED:
        continue
    seen = set()
    for dp, _, fns in os.walk(d):
        for fn in fns:
            if fn.endswith((".cpp", ".h")):
                with open(os.path.join(dp, fn), encoding="utf-8", errors="replace") as f:
                    seen |= set(re.findall(r"Button::(\w+)", f.read()))
    button_use[entry] = seen

buttons = read("docs/buttons.md")
ROWS = (
    ("Back", ("Back",)),
    ("Confirm", ("Confirm",)),
    ("Left / Right", ("Left", "Right")),
    ("Up / Down", ("Up", "Down")),
)
for label, names in ROWS:
    m = re.search(r"^\|\s*" + re.escape(label) + r"\s*\|\s*(\d+)\s*\|", buttons, re.M)
    check(bool(m), "docs/buttons.md has no row for this button", label)
    if m:
        real = sum(1 for s in button_use.values() if s & set(names))
        check(int(m.group(1)) == real, "docs/buttons.md button census is stale",
              f"{label}: says {m.group(1)}, {real} app directories read it")

# ---------------------------------------------------------------------------
# `.github/skills/crosspoint-reader.md` is a SYMLINK to AGENTS.md, and it has to
# stay one.
#
# It reads as a 41KB byte-identical duplicate to anything that resolves it --
# `md5` agrees, and a link checker walking the tree resolves AGENTS.md's
# root-relative links from `.github/skills/` and reports about twenty dead
# links, which is most of the dead links in the repository. Both are artefacts
# of the symlink, not a second copy: the links are correct at AGENTS.md's own
# path. Upstream added this link (a77419be) pointing at `.skills/SKILL.md`,
# then deleted that file and left it dangling; f6d6482d re-pointed it here.
#
# The check exists because "de-duplicate this" is the obvious wrong fix, and
# making it a real file is how the repository would grow a 41KB copy that
# drifts.
# ---------------------------------------------------------------------------
skill_link = os.path.join(root, ".github/skills/crosspoint-reader.md")
check(os.path.islink(skill_link),
      ".github/skills/crosspoint-reader.md is no longer a symlink",
      "it must point at AGENTS.md, never hold a copy of it")
if os.path.islink(skill_link):
    check(os.path.basename(os.readlink(skill_link)) == "AGENTS.md",
          ".github/skills/crosspoint-reader.md points somewhere other than AGENTS.md",
          os.readlink(skill_link))
    check(os.path.exists(skill_link),
          ".github/skills/crosspoint-reader.md dangles, which aborts any tree walk that opens it")

# ---------------------------------------------------------------------------
# docs/contributing/README.md is the index of its own directory. It went stale
# by omission: landing-and-integration.md existed and nothing linked it.
# ---------------------------------------------------------------------------
index = read("docs/contributing/README.md")
for fn in sorted(os.listdir(contrib)):
    if not fn.endswith(".md") or fn == "README.md":
        continue
    check(f"({fn})" in index or f"(./{fn})" in index,
          "docs/contributing/README.md does not link a file in its own directory", fn)

# ---------------------------------------------------------------------------
# docs/README.md says who wrote what. Checked against upstream, not believed.
# ---------------------------------------------------------------------------
docs_readme = read("docs/README.md")


def upstream_ref():
    for ref in ("crosspoint/develop", "upstream/develop"):
        r = subprocess.run(["git", "-C", root, "rev-parse", "--verify", "--quiet", ref],
                           capture_output=True, text=True)
        if r.returncode == 0:
            return ref
    return None


ref = upstream_ref()
if ref is None:
    # In CI this is a FAILURE, not a skip. The workflow fetches the ref in a
    # step of its own precisely so these checks run where a merge is blocked;
    # if that step is dropped, the suite must say so rather than pass with most
    # of itself switched off. Locally it stays a loud skip, so a fresh clone
    # with no upstream remote is not bricked by it.
    if os.environ.get("CI"):
        check(False, "no crosspoint/develop ref in CI",
              "the 'Fetch upstream's tip' step in crossplay-ci.yml is gone, and "
              "docs/README.md's ownership claims are checked nowhere")
    else:
        print("SKIP docsclaims  no crosspoint/develop or upstream/develop ref; "
              "docs/README.md's ownership claims were NOT verified "
              "(git fetch crosspoint develop)")
else:
    def upstream_blob(path):
        r = subprocess.run(["git", "-C", root, "rev-parse", f"{ref}:{path}"],
                           capture_output=True, text=True)
        return r.stdout.strip() if r.returncode == 0 else None

    def local_blob(path):
        # The WORKING TREE, not HEAD. check.sh verifies what you have unless
        # you pass --committed, and a claim checked against HEAD while the file
        # on disk says something else is a check that answers about a tree
        # nobody is looking at.
        full = os.path.join(root, path)
        if not os.path.exists(full):
            return None
        r = subprocess.run(["git", "-C", root, "hash-object", "--", full],
                           capture_output=True, text=True)
        return r.stdout.strip() if r.returncode == 0 else None

    def ownership(path):
        u = upstream_blob(path)
        if u is None:
            return "fork"
        return "verbatim" if u == local_blob(path) else "edited"

    # Every file the first paragraph says keeps upstream's filename must be a
    # path upstream actually has.
    m = re.search(r"\*\*Upstream's reference docs keep upstream's filenames\*\*(.*?)\n\n",
                  docs_readme, re.S)
    check(bool(m), "docs/README.md: the upstream-filenames paragraph was reworded past this check")
    if m:
        named = [n for n in re.findall(r"`([A-Za-z0-9._\-]+\.md)`", m.group(1))]
        check(bool(named), "docs/README.md names no upstream-filename docs")
        for fn in named:
            check(ownership(f"docs/{fn}") != "fork",
                  "docs/README.md calls a file upstream's that upstream does not have", fn)

    # Every file it names as fork-edited must really differ from upstream.
    m = re.search(r"The fork has edited(.*?)at the root", docs_readme, re.S)
    check(bool(m), "docs/README.md: the fork-edited sentence was reworded past this check")
    if m:
        for fn in re.findall(r"`([A-Za-z0-9._\-]+\.md)`", m.group(1)):
            check(ownership(f"docs/{fn}") == "edited",
                  "docs/README.md says the fork edited a file it did not",
                  f"{fn} is {ownership(f'docs/{fn}')}")

    # The contributing/ table, row by row. A row whose wording matches no
    # marker fails rather than passing quietly: a reworded row is a row nothing
    # is checking.
    MARKERS = (
        ("No upstream counterpart", "fork"),
        ("verbatim", "verbatim"),
        ("rewritten for this fork", "edited"),
        ("fork content", "edited"),
        ("fork edits", "edited"),
        ("index extended", "edited"),
        ("re-pointed", "edited"),
    )
    # Scoped to the paragraph that introduces the table, so a future table
    # elsewhere in this file whose first cell is a backticked filename is not
    # silently checked as though it named a file in contributing/.
    tbl = re.search(r"`contributing/` is worth naming file by file(.*?)\n\n(?=[^|])",
                    docs_readme, re.S)
    check(bool(tbl),
          "docs/README.md: the contributing table's introduction was reworded past this check")
    region = tbl.group(1) if tbl else ""
    rows = re.findall(r"^\|\s*`([A-Za-z0-9._\-]+\.md)`\s*\|(.+?)\|\s*$", region, re.M)
    check(len(rows) >= 5, "docs/README.md contributing table not found or truncated",
          f"{len(rows)} rows")
    for fn, desc in rows:
        want = next((v for k, v in MARKERS if k in desc), None)
        check(want is not None,
              "docs/README.md contributing table row matches no known claim, so nothing checks it",
              f"{fn}: {desc.strip()}")
        if want is not None:
            got = ownership(f"docs/contributing/{fn}")
            check(got == want, "docs/README.md contributing table row is wrong",
                  f"{fn}: says {want}, is {got}")

    # Every file in the directory has a row. Omission is how this went wrong.
    listed = {fn for fn, _ in rows}
    for fn in sorted(os.listdir(contrib)):
        if fn.endswith(".md"):
            check(fn in listed,
                  "docs/README.md contributing table omits a file in that directory", fn)

# docs/README.md is the index of docs/. A doc it never names is a doc nobody
# finds: four of them (bezel-insets, build-cache, i18n-overflow,
# trivia-curation) went unmentioned, the same omission that hid
# landing-and-integration.md. Checked without the upstream ref, so CI sees it.
for fn in sorted(os.listdir(os.path.join(root, "docs"))):
    if not fn.endswith(".md") or fn == "README.md":
        continue
    check(f"`{fn}`" in docs_readme or f"({fn})" in docs_readme or f"/{fn}" in docs_readme,
          "docs/README.md never names a doc in its own directory", fn)

print(f"{checks} checks, {failed} failed")
sys.exit(1 if failed else 0)
PY

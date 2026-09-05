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
env_less = []
scan = ["README.md", "AGENTS.md"]
contrib = os.path.join(root, "docs/contributing")
scan += [f"docs/contributing/{f}" for f in sorted(os.listdir(contrib)) if f.endswith(".md")]
for rel in scan:
    fenced = False
    for n, line in enumerate(read(rel).splitlines(), 1):
        if line.lstrip().startswith("```"):
            fenced = not fenced
            continue
        if not fenced:
            continue
        cmd = line.strip().lstrip("$").strip()
        if cmd.startswith("pio run") and " -e " not in cmd:
            env_less.append(f"{rel}:{n}  {cmd}")
check(not env_less,
      "a firmware build or upload that names no environment builds [env:default], which is ESP32-C3",
      "; ".join(env_less))

# The two lines that make a stranger's first build work at all.
check("git submodule update --init" in readme,
      "README.md does not tell a stranger to initialise the submodule",
      ".gitmodules pins freeink-sdk and nothing builds without it")
check("docs/contributing" in readme,
      "README.md never links docs/contributing/",
      "the correct setup path is unreachable from the front door")
gs = read("docs/contributing/getting-started.md")
check("git submodule update --init" in gs,
      "docs/contributing/getting-started.md is missing the submodule step")
check("crosspoint-reader/crosspoint-reader\n" not in gs
      and "clone https://github.com/crosspoint-reader" not in gs,
      "docs/contributing/getting-started.md still clones upstream's repository")

# ---------------------------------------------------------------------------
# docs/buttons.md counts which apps read which logical button. It sat at
# "seventeen apps" and "Up/Down: 3" while ten apps -- seven of them games --
# had grown to read the two real keys, which inverted the finding the document
# exists to record.
# ---------------------------------------------------------------------------
SHARED = {"link", "player", "sample", "ui", "bridge"}
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
    rows = re.findall(r"^\|\s*`([A-Za-z0-9._\-]+\.md)`\s*\|(.+?)\|\s*$", docs_readme, re.M)
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

print(f"{checks} checks, {failed} failed")
sys.exit(1 if failed else 0)
PY

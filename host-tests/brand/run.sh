#!/bin/bash
# The fork's own name, checked where a user can actually read it.
#
# Rebranding a fork fails in two opposite directions and this suite guards both.
#
# It drifts BACK when a merge from upstream, or a new screen copied from an old
# one, reintroduces "CrossPoint" somewhere a person sees it. The 2026-08-28 pass
# found five such places outside src/network/ that a grep of the obvious
# directory had missed: the Wi-Fi hotspot SSID, the mDNS name drawn on screen
# and encoded into a QR code, the hostname routers display, the device name sent
# to a KOReader sync server, and the crash report a user attaches to a bug.
#
# It drifts TOO FAR when someone renames a string that is not branding at all.
# That direction is the dangerous one and it is why PROTECTED exists: permitting
# a literal is not the same as requiring it, and an allowlist alone would let a
# tidy-up rename of /.crosspoint ship a firmware that mounts a different
# directory and makes every user's saves, progress and settings invisible --
# with a green suite. Each protected literal is asserted PRESENT, by count.
#
# For the drift-back direction the test does not hunt a fixed list. It
# DISCOVERS every literal carrying the old name, subtracts the explained parts,
# and fails on whatever is left. A new one is a failure until somebody decides
# which kind it is.
#
#   host-tests/brand/run.sh
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
        print(f"FAIL brand  {label}" + (f": {detail}" if detail else ""))


def read(rel):
    with open(os.path.join(root, rel), encoding="utf-8") as f:
        return f.read()


def walk(subdir, exts):
    base = os.path.join(root, subdir)
    if not os.path.isdir(base):
        return
    skip = {".git", ".pio", "freeink-sdk", "node_modules", "fs_agent", "fs_mario",
            "qa-artifacts", "emulator", "pyodide", "upstream", "libdeps"}
    for dirpath, dirnames, filenames in os.walk(base):
        dirnames[:] = [d for d in dirnames if d not in skip]
        for name in filenames:
            if name.endswith(exts) and not name.endswith(".generated.h") and name not in GENERATED:
                yield os.path.relpath(os.path.join(dirpath, name), root)


GENERATED = ("I18nStrings.cpp", "I18nStrings.h", "I18nKeys.h")

# ---------------------------------------------------------------------------
# PART ONE. Upstream's name where it MUST stay. Renaming any of these does not
# rebrand anything: it loses somebody's data or breaks a handshake. Asserted
# present with a floor, so a partial rename fails too.
# ---------------------------------------------------------------------------
# Each entry also names the "helpfully rebranded" spelling of itself. A floor
# alone cannot catch a PARTIAL rename -- change one of 67 call sites and the
# count still clears the floor while half the firmware reads a different path.
# The twin is the sharp half of this check: it is never legitimate anywhere, so
# one occurrence is one bug.
PROTECTED = [
    (["src", "lib"], "/.crosspoint", 60, r"/\.crossplay(?![\w-])",
     "the SD card directory holding every user's saves, progress, settings and caches"),
    (["src"], "CROSSPOINT-BOARD-V1:", 2, r"CROSSPLAY-BOARD-V1",
     "firmware image magic; upstream's web flasher and our own OTA guard scan for it"),
    (["src"], 'String message = "crosspoint (on "', 1, r'"crossplay \(on "',
     "the discovery reply Calibre's plugin AND scripts_local/wifi-flash.sh match on"),
    (["lib"], "sync.crosspointreader.com", 1, r"crossplayreader\.com",
     "upstream's sync server, a real hostname existing accounts live on"),
    (["src"], "crosspoint-reader/crosspoint-fonts", 1, r"crossplay-fonts",
     "upstream's font repo, a real release URL"),
    (["tools_local"], "crosspoint-study.json", 1, r"crossplay-study\.json",
     "the host-side Study config in a user's ~/.config"),
    (["lib"], "CrossPoint Reader", 25, r"CrossPlay Reader",
     "the Calibre plugin, named in every translation; renaming it sends people looking for nothing"),
]

EVERYWHERE = [("src", (".cpp", ".h", ".html", ".js", ".c")), ("lib", (".cpp", ".h", ".c", ".yaml")),
              ("scripts", (".py", ".sh")), ("tools_local", (".py", ".sh")),
              ("site", (".html", ".js", ".py")), ("host-tests", (".sh",))]

for dirs, literal, floor, twin, reason in PROTECTED:
    n = 0
    for subdir in dirs:
        for rel in walk(subdir, (".cpp", ".h", ".html", ".js", ".py", ".sh", ".yaml")):
            n += read(rel).count(literal)
    check(n >= floor, f"{literal!r} is still present ({reason})",
          f"found {n} in {'/'.join(dirs)}, expected at least {floor} -- renaming this is not a rebrand")

    where = []
    for subdir, exts in EVERYWHERE:
        for rel in walk(subdir, exts):
            if rel.startswith("host-tests/brand/"):
                continue  # this file names every twin on purpose
            if re.search(twin, read(rel)):
                where.append(rel)
    check(not where, f"nothing has been rebranded to match {twin!r} ({reason})",
          f"found in {', '.join(where)} -- a partial rename is worse than none")

tdir = os.path.join(root, "lib", "I18n", "translations")
langs = sorted(n for n in os.listdir(tdir) if n.endswith(".yaml"))

# ---------------------------------------------------------------------------
# PART TWO. Anything else carrying the old name must be explained. The reason
# is SUBTRACTED from the text before the test looks again, so a literal that
# merely mentions an allowed path cannot smuggle branding along beside it.
# ---------------------------------------------------------------------------
ALLOWED = {
    "/.crosspoint": "the SD card directory",
    ".crosspoint": "the SD card directory, with or without its slashes",
    "CROSSPOINT-BOARD-V1": "firmware image magic",
    "crosspoint-reader": "upstream's GitHub org, in repo and release URLs",
    "crosspointreader.com": "upstream's sync server and website",
    "crosspoint-sync": "upstream's sync server project",
    "crosspoint_reader": "upstream's Calibre plugin, by its package name",
    # Narrow on purpose: a bare "CrossPoint Reader" would also excuse
    # <title>CrossPoint Reader</title> on a served page, which is branding.
    "CrossPoint Reader plugin": "upstream's Calibre plugin, by name",
    "CrossPoint Reader device plugin": "the same plugin, as the guide phrases it",
    "CrossPoint Reader Project": "upstream's authorship of the user guide, kept in the EPUB metadata",
    'crosspoint (on ': "the discovery reply Calibre and wifi-flash.sh match on",
    "crosspoint.files.uploadSettings": "read-only localStorage fallback so nobody loses remembered settings",
    "crosspoint-study.json": "the host-side Study config in a user's ~/.config",
    "a fork of CrossPoint": "the fork credit docs/identity.md requires",
    "Inherited from CrossPoint": "the same credit, on the site",
    "CrossPoint</a>": "a link to upstream on the site",
    "every CrossPoint release": "the site describing what it tracks",
    "CrossPointSettings": "a C++ identifier; merge surface, nobody reads it",
    "CrossPointState": "a C++ identifier",
    "CrossPointWebServer": "a C++ identifier and the activity key that restores the home menu",
    "CrossPointPosition": "a C++ identifier",
    "CrossPointOrientation": "a C++ identifier",
    "CrossPointTiltPageTurn": "a C++ identifier",
    "CrossPointSyncServer": "a C++ identifier",
    "CROSSPOINT_": "a build flag or macro; merge surface",
    "crosspoint_tinfl": "vendored miniz symbol prefix",
    "crosspoint_mz": "vendored miniz symbol prefix",
    "[crosspoint]": "upstream's version section in platformio.ini",
    '"crosspoint", "version"': "reading that same section",
    "CrossPoint Test": "test-fixture metadata, never shipped to a device",
    "Crosspoint Test": "test-fixture metadata, never shipped to a device",
    "CrossPoint extension": "upstream's non-standard 2-bit BMP variant, by name",
    "CrossPoint's": "prose about upstream, in a comment or doc",
    "CrossPoint logo this replaces": "the generator explaining what it supersedes",
    "STARTING CROSSPLAY": "the monitor's colour keyword, already renamed",
    "crosspoint-fonts": "upstream's font repo",
    "crosspointVersion": "a local JS identifier in an upstream page; nobody reads it",
    "CrossPoint display": "upstream's panel, in a test-fixture comment",
    "stock CrossPoint": "the site telling people they can flash back",
    "CrossPoint plugin": "upstream's Calibre plugin, in prose or an anchor",
    "crosspoint-plugin": "the anchor for that same heading",
    "CrossPoint Sync Server": "upstream's sync server, by name",
    "CrossPoint sync server": "the same server in prose",
    "the CrossPoint server": "the same server again",
    "Crosspoint theme": "one of upstream's frozen themes, by name",
    "theme for Crosspoint": "the other one",
    'search for "crosspoint"': "the text a user types into Calibre's plugin search",
    "crosspointreadercom": "the same sync-server hostname inside a markdown anchor",
    "CrossPoint upstream": "the site pointing other devices at upstream",
    "CrossPoint first": "the site's flashing instructions",
}

# Literals that are protocol or persisted keys rather than labels. Allowed only
# in the file that owns them, because the same text elsewhere would be branding.
ALLOWED_EXACT = {
    ("src/network/CrossPointWebServer.cpp", '"crosspoint"'):
        "the hostname fallback inside the discovery reply on the next line",
}

# Translation keys naming something upstream publishes, so the words stay
# whatever language they are in.
ALLOWED_KEYS = ("STR_CALIBRE_INSTRUCTION_1",)
SCAN = [
    ("src", (".cpp", ".h", ".html", ".js", ".c")),
    ("lib", (".cpp", ".h", ".c")),
    ("scripts", (".py", ".sh")),
    ("tools_local", (".py", ".sh")),
    ("site", (".html", ".js", ".css", ".svg", ".webmanifest", ".py")),
]
LITERAL = re.compile(r'"([^"\\\n]|\\.)*"')


def unexplained(text):
    """Whatever still says CrossPoint once every allowed reason is subtracted."""
    for phrase in ALLOWED:
        text = re.sub(re.escape(phrase), "", text, flags=re.IGNORECASE)
    return "crosspoint" in text.lower()


found = []
for subdir, exts in SCAN:
    for rel in walk(subdir, exts):
        text = read(rel)
        if rel.endswith((".cpp", ".h", ".c")):
            # Quoted literals only: a comment naming upstream is fine.
            rows = []
            for i, ln in enumerate(text.splitlines()):
                if ln.lstrip().startswith(("//", "*", "/*", "#include")):
                    continue
                rows += [(i + 1, m.group(0)) for m in LITERAL.finditer(ln)]
        else:
            # Blank comments out rather than removing them, so the line
            # numbers this reports still match the file on disk.
            body = re.sub(r"<!--.*?-->|/\*.*?\*/",
                          lambda m: "\n" * m.group(0).count("\n"), text, flags=re.DOTALL)
            body = re.sub(r"^\s*(//|#).*$", "", body, flags=re.MULTILINE)
            rows = list(enumerate(body.splitlines(), 1))
        for lineno, frag in rows:
            if not ("crosspoint" in frag.lower() and unexplained(frag)):
                continue
            if (rel, frag.strip()) in ALLOWED_EXACT:
                continue
            found.append(f"{rel}:{lineno}  {frag.strip()[:100]}")
check(not found, "no unexplained CrossPoint literal reaches a user",
      "\n       " + "\n       ".join(found) if found else "")

# The translated strings are the words drawn on the panel, and USER_GUIDE.md is
# compiled into the EPUB that ships to the device. Both are checked whole, not
# by their first line, because an upstream merge rewrites them wholesale.
for name in langs:
    for i, ln in enumerate(read(f"lib/I18n/translations/{name}").splitlines(), 1):
        if ln.startswith(ALLOWED_KEYS):
            continue
        if "crosspoint" in ln.lower() and unexplained(ln):
            found.append(f"{name}:{i}  {ln.strip()[:90]}")
check(not [f for f in found if f.endswith(".yaml") or ".yaml:" in f],
      "no translation says CrossPoint",
      "; ".join(f for f in found if ".yaml:" in f))

guide_bad = [
    f"USER_GUIDE.md:{i}  {ln.strip()[:90]}"
    for i, ln in enumerate(read("USER_GUIDE.md").splitlines(), 1)
    if "crosspoint" in ln.lower() and unexplained(ln)
]
check(not guide_bad, "USER_GUIDE.md says CrossPlay throughout, not just its heading",
      "\n       " + "\n       ".join(guide_bad) if guide_bad else "")

check('STR_CROSSPLAY: "CrossPlay"' in read("lib/I18n/translations/english.yaml"),
      "the wordmark string is CrossPlay")
missing = [n for n in langs if "STR_CROSSPLAY:" not in read(f"lib/I18n/translations/{n}")]
check(not missing, "every language defines the wordmark", ", ".join(missing))

# ---------------------------------------------------------------------------
# PART THREE. The names a person reads without opening the device. Located by
# grep so a moved constant is still checked, and BOTH User-Agent call sites are
# covered -- the header is set two different ways in one file.
# ---------------------------------------------------------------------------
def literal_of(pattern, label):
    out = subprocess.run(
        ["grep", "-rhoE", pattern, os.path.join(root, "src"), os.path.join(root, "lib")],
        capture_output=True, text=True,
    ).stdout.strip().splitlines()
    check(bool(out), f"{label} is still defined somewhere",
          "grep found nothing -- has it been renamed away?")
    for line in out:
        check("crosspoint" not in line.lower(), f"{label} does not say CrossPoint", line.strip())


literal_of(r'AP_SSID *= *"[^"]*"', "the Wi-Fi hotspot name")
literal_of(r'(AP_)?HOSTNAME *= *"[^"]*"', "the mDNS name shown on screen")
literal_of(r'DEVICE_NAME\[\] *= *"[^"]*"', "the name a sync server shows")
literal_of(r'setUserAgent\("[^"]*"', "the HTTP User-Agent (setUserAgent)")
literal_of(r'"User-Agent", *"[^"]*"', "the HTTP User-Agent (esp_http_client)")
literal_of(r'info \+= "CrossP[^"]*"', "the crash report header")

# ---------------------------------------------------------------------------
# PART FOUR. The mark. Generated files go stale silently: a hand-edit compiles,
# and a build proves only that the tree compiles, never that it holds a change.
# ---------------------------------------------------------------------------
gen = subprocess.run([sys.executable, os.path.join(root, "scripts", "generate_logo.py"), "--check"],
                     capture_output=True, text=True, cwd=root)
check(gen.returncode == 0, "the logo files match their generator", gen.stderr.strip())

logo = read("src/images/Logo120.h")
vals = [v for v in logo[logo.index("{") + 1: logo.rindex("}")].replace("\n", "").split(",") if v.strip()]
check(len(vals) == 120 * 120 // 8, "the mark is a full 120x120 1-bit frame", f"{len(vals)} bytes")

# The generator's output must already be what the repo formatter produces.
# Otherwise the formatter rewrites the file, --check calls it stale, and there
# is no state where both gates are green. That shipped once.
check(not re.search(r"[ \t]+$", logo, re.MULTILINE),
      "the generated header has no trailing whitespace for the formatter to strip")
check(logo.rstrip().endswith("};"), "the generated header closes the way clang-format leaves it")

# The device mark and the site mark are one shape; identity.md tells maintainers
# to change one and change the others, so their geometry must agree.
site = read("site/assets/logo.svg")
sw = float(re.search(r'stroke-width="([\d.]+)"', site).group(1))
mx, my, mrun = re.search(r'd="M([\d.]+) ([\d.]+)V[\d.]+h(-?[\d.]+)"', site).groups()
scale = 120 / float(re.search(r'viewBox="0 0 (\d+)', site).group(1))
arm_units = (float(my) - sw / 2)
site_arm = round((float(my) + sw / 2 - (float(mx) - sw / 2)) * scale)
script = read("scripts/generate_logo.py")
dev_arm = int(re.search(r"^ARM = (\d+)", script, re.MULTILINE).group(1))
check(dev_arm == site_arm, "the device bracket is the same length as the site's",
      f"device ARM={dev_arm}px, site mark scales to {site_arm}px -- the marks would differ")

for rel in ("src/activities/boot_sleep/BootActivity.cpp", "src/activities/boot_sleep/SleepActivity.cpp"):
    src = read(rel)
    check("tr(STR_CROSSPLAY)" in src, f"{os.path.basename(rel)} draws the CrossPlay wordmark")
    check("Logo120" in src, f"{os.path.basename(rel)} draws the mark")

# ---------------------------------------------------------------------------
# PART FIVE. The user guide's H1 is parsed by the EPUB generator. Renaming one
# and not the other leaves a script that silently stops stripping the inline
# table of contents, which is how it shipped broken from upstream.
# ---------------------------------------------------------------------------
guide = read("USER_GUIDE.md")
epub = read("scripts/generate_userguide_epub.py")
h1 = guide.splitlines()[0].lstrip("# ").strip()
check(f"# {h1}\\n" in epub, "the EPUB generator's heading pattern matches USER_GUIDE.md",
      f"the guide's H1 is {h1!r} and the script does not look for it, so it would stop stripping the TOC")
check(r"(?:\A|\n)#" in epub, "the TOC pattern anchors at start-of-file, not only after a newline")
check("add_author('CrossPoint Reader Project')" in epub and "add_author('CrossPlay')" in epub,
      "the EPUB credits upstream for the guide it wrote, alongside the fork")

print(f"{checks} checks, {failed} failed")
sys.exit(1 if failed else 0)
PY

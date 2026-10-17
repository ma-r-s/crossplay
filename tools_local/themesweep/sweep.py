#!/usr/bin/env python3
"""Photograph every Home theme in the X4 Pro simulator, in every state that
changes its layout. Run from inside a worktree after seed.py.

    python3 tools_local/themesweep/sweep.py --prime           # once per card
    python3 tools_local/themesweep/sweep.py --home <outdir>   # 5 themes x 4 states
    python3 tools_local/themesweep/sweep.py --nav <outdir>    # each theme walked
                                                              # through its screens

States: books / empty (a recent book or none) x catalog / no catalog (a
configured OPDS server adds Home's library-slot row: the state the 1.13.28 sync
broke, since every device seeded with Get Books has one). --nav walks each theme,
with a catalog and no recent book, from Home into File Browser, Library,
Settings, Games, Apps and the OPDS browser, coming back to Home in between.

Each run starts from fresh settings and recents, so one theme's saved selection
cannot leak into the next. Thumbnails are deleted first with --fresh-thumbs:
a thumbnail generated during the run takes Home's first-render path, where a
stale cover snapshot once drew a ghost frame (1.13.29).

Then hand the PNGs to a reviewer with no context on the work: review-brief.md
is the brief. See README.md.
"""

import argparse, glob, json, os, shutil, subprocess, sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
TREE = os.path.abspath(os.path.join(HERE, "..", ".."))
CP = os.path.join(TREE, "fs_agent", ".crosspoint")
RECENT = os.path.join(HERE, ".recent.json")
THEMES = {0: "classic", 1: "lyra", 2: "lyra3", 3: "roundedraff", 4: "covergrid"}
CATALOG = {
    "servers": [
        {
            "name": "Project Gutenberg",
            "url": "https://www.gutenberg.org/ebooks.opds/",
            "username": "",
            "password": "",
        }
    ]
}

# Buttons, for the four list themes: Home starts on Browse Files when no book
# is open, and with a catalog the rows run Browse Files, Library, OPDS, File
# Transfer, Settings, Games, Apps. Back returns to the row it left.
NAV_LIST = (
    "9000:ENTER;12000:BACK;13500:DOWN;14000:ENTER;18000:BACK;19500:DOWN;20000:DOWN;20500:DOWN;"
    "21000:ENTER;24000:BACK;25500:DOWN;26000:ENTER;29000:BACK;30500:DOWN;31000:ENTER;34000:BACK;"
    "35500:UP;36000:UP;36500:UP;37000:UP;37500:ENTER;42000:QUIT"
)
# Taps, for Cover Grid's seven bottom tabs (logical px): Files, Library, OPDS,
# File Transfer, Settings, Games, Apps.
NAV_GRID = (
    "9000:TAP:50,755;12000:BACK;13500:TAP:112,755;18000:BACK;19500:TAP:302,755;24000:BACK;"
    "25500:TAP:364,755;29000:BACK;30500:TAP:429,755;34000:BACK;37500:TAP:174,755;42000:QUIT"
)
NAV_SHOTS = (
    "11500:files,13000:home-after-files,17500:library,23500:settings,28500:games,33500:apps,"
    "35000:home-after-apps,41500:opds"
)


def reset(theme, books, catalog, fresh_thumbs):
    os.makedirs(CP, exist_ok=True)
    for name in (
        "settings.json",
        "recent.json",
        "state.json",
        "shelf.cfg",
        "opds.json",
    ):
        path = os.path.join(CP, name)
        if os.path.exists(path):
            os.remove(path)
    if fresh_thumbs:
        for thumb in glob.glob(os.path.join(CP, "epub_*", "thumb_*.bmp")):
            os.remove(thumb)
    json.dump({"uiTheme": theme}, open(os.path.join(CP, "settings.json"), "w"))
    if books:
        if not os.path.exists(RECENT):
            sys.exit("no recents: run seed.py first")
        shutil.copy(RECENT, os.path.join(CP, "recent.json"))
    if catalog:
        json.dump(CATALOG, open(os.path.join(CP, "opds.json"), "w"))


def shoot(outdir, tag, inputs, shots):
    spec = []
    for item in shots.split(","):
        ms, name = item.split(":", 1)
        spec.append(f"{ms}:./qa-artifacts/themesweep/{tag}-{name}.bmp")
    os.makedirs(os.path.join(TREE, "qa-artifacts", "themesweep"), exist_ok=True)
    r = subprocess.run(
        ["./scripts_local/sim-shot.sh", inputs, ";".join(spec)],
        cwd=TREE,
        capture_output=True,
        text=True,
    )
    trail = [
        l.split("Entering activity: ")[-1]
        for l in r.stdout.splitlines()
        if "Entering activity" in l
    ]
    missing = []
    for item in shots.split(","):
        name = item.split(":", 1)[1]
        bmp = os.path.join(TREE, "qa-artifacts", "themesweep", f"{tag}-{name}.bmp")
        if os.path.exists(bmp):
            Image.open(bmp).convert("L").save(os.path.join(outdir, f"{tag}-{name}.png"))
            os.remove(bmp)
        else:
            missing.append(name)
    print(
        f"{tag}: {' > '.join(trail)}" + (f"  MISSING {missing}" if missing else ""),
        flush=True,
    )
    return not missing


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--prime", action="store_true", help="open the first three recent books once"
    )
    ap.add_argument("--home", metavar="OUTDIR")
    ap.add_argument("--nav", metavar="OUTDIR")
    ap.add_argument("--fresh-thumbs", action="store_true")
    args = ap.parse_args()
    ok = True
    if args.prime:
        # Lyra Extended shows three covers: open each once so its cache exists.
        reset(2, True, False, False)
        ok &= shoot(
            os.path.join(TREE, "qa-artifacts", "themesweep"),
            "prime",
            "8000:ENTER;16000:BACK;19000:DOWN;"
            "19500:ENTER;27000:BACK;30000:DOWN;30500:DOWN;31000:ENTER;39000:BACK;45000:QUIT",
            "44000:home",
        )
    if args.home:
        os.makedirs(args.home, exist_ok=True)
        for theme, name in THEMES.items():
            for books in (True, False):
                for catalog in (False, True):
                    reset(theme, books, catalog, args.fresh_thumbs)
                    tag = f"{name}-{'books' if books else 'empty'}{'-opds' if catalog else ''}"
                    ok &= shoot(args.home, tag, "30000:QUIT", "28000:home")
    if args.nav:
        os.makedirs(args.nav, exist_ok=True)
        for theme, name in THEMES.items():
            reset(theme, False, True, False)
            ok &= shoot(
                args.nav,
                f"{name}-empty-opds",
                NAV_GRID if theme == 4 else NAV_LIST,
                NAV_SHOTS,
            )
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()

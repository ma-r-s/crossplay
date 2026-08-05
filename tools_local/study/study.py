#!/usr/bin/env python3
"""Study, in two commands.

    ./tools_local/study/study.py setup     # once, or when you change deck
    ./tools_local/study/study.py sync      # after every session

That is the whole interface. Everything the computer can work out for itself,
it works out: where the Anki collection is, what decks are in it, where the SD
card is mounted, whether the fonts are already built. What it cannot know it
asks once and remembers.

The underlying tools (anki_to_deck.py, deck_to_anki.py, make_fonts.py) still
take explicit paths and still do exactly one thing each -- that is what makes
them testable, and this file is not a replacement for them. It is the layer
that means a person does not have to type a forty-character path with escaped
spaces to revise some flashcards.

Every prompt has a flag, so this is scriptable as well as interactive.
"""

import argparse
import json
import os
import pathlib
import platform
import shutil
import sqlite3
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
CONFIG = (
    pathlib.Path(os.environ.get("XDG_CONFIG_HOME", pathlib.Path.home() / ".config"))
    / "crosspoint-study.json"
)

# Where Anki keeps collections, per platform. Anki2/<profile>/collection.anki2.
ANKI_ROOTS = {
    "Darwin": [pathlib.Path.home() / "Library/Application Support/Anki2"],
    "Linux": [
        pathlib.Path.home() / ".local/share/Anki2",
        pathlib.Path.home() / "Anki2",
    ],
    "Windows": [pathlib.Path(os.environ.get("APPDATA", "")) / "Anki2"],
}


def die(message):
    sys.exit(f"\n{message}")


def load_config():
    if CONFIG.exists():
        try:
            return json.loads(CONFIG.read_text())
        except ValueError:
            return {}
    return {}


def save_config(config):
    CONFIG.parent.mkdir(parents=True, exist_ok=True)
    CONFIG.write_text(json.dumps(config, indent=2) + "\n")


# --- finding things ---------------------------------------------------------


def find_collections():
    """Every Anki profile on this machine, newest first."""
    found = []
    for root in ANKI_ROOTS.get(platform.system(), []):
        if not root.is_dir():
            continue
        for profile in sorted(root.iterdir()):
            candidate = profile / "collection.anki2"
            if candidate.is_file():
                found.append(candidate)
    found.sort(key=lambda p: p.stat().st_mtime, reverse=True)
    return found


# macOS mounts several of its own volumes under /Volumes. Offering the user
# "Recovery" as a place to put their flashcards is the kind of detail that
# makes an auto-detector feel worse than typing the path.
SYSTEM_VOLUMES = {"Recovery", "Preboot", "VM", "Update", "xarts", "iSCPreboot", "Hardware", "Data"}


def find_sd_cards():
    """Mounted volumes that could be the reader's card.

    A volume that already has /study on it wins: that is almost certainly the
    card this was set up with before.
    """
    roots = []
    if platform.system() == "Darwin":
        roots = [pathlib.Path("/Volumes")]
    elif platform.system() == "Linux":
        roots = [
            pathlib.Path("/media") / os.environ.get("USER", ""),
            pathlib.Path("/run/media"),
        ]
    candidates = []
    for root in roots:
        if not root.is_dir():
            continue
        for volume in sorted(root.iterdir()):
            try:
                if not volume.is_dir() or volume.is_symlink():
                    continue
                # The system disk is mounted here on macOS; it is not the card.
                if volume.resolve() == pathlib.Path("/"):
                    continue
                if volume.name in SYSTEM_VOLUMES:
                    continue
                candidates.append(volume)
            except OSError:
                continue
    candidates.sort(key=lambda p: (p / "study").is_dir(), reverse=True)
    return candidates


def list_decks(collection):
    """Deck names with card counts, so the choice is informed."""
    db = sqlite3.connect(f"file:{collection}?mode=ro", uri=True)
    db.create_collation(
        "unicase", lambda a, b: (a.lower() > b.lower()) - (a.lower() < b.lower())
    )
    rows = db.execute(
        "select d.name, count(c.id) from decks d left join cards c on c.did = d.id group by d.id"
    ).fetchall()
    db.close()
    decks = [(name.replace("\x1f", "::"), count) for name, count in rows if count > 0]
    decks.sort(key=lambda x: x[0])
    return decks


def anki_is_running():
    try:
        out = subprocess.run(["pgrep", "-x", "Anki"], capture_output=True, text=True)
        return out.returncode == 0
    except FileNotFoundError:
        return False


# --- asking -----------------------------------------------------------------


def choose(prompt, options, describe=str):
    """One numbered list, one number back. No fuzzy matching to get wrong."""
    if not options:
        return None
    if len(options) == 1:
        print(f"{prompt}\n  {describe(options[0])}")
        return options[0]
    print(f"\n{prompt}")
    for i, option in enumerate(options, 1):
        print(f"  {i:2}. {describe(option)}")
    while True:
        answer = input("  number (or blank to cancel): ").strip()
        if not answer:
            return None
        if answer.isdigit() and 1 <= int(answer) <= len(options):
            return options[int(answer) - 1]


def run(script, *args):
    """Run one of the single-purpose tools, showing what it printed."""
    interpreter = sys.executable
    # make_fonts needs fonttools and freetype-py, make_images needs pillow; the
    # rest are plain Python and run under whatever launched this.
    if script in ("make_fonts.py", "make_images.py"):
        venv = REPO / ".venv-study/bin/python"
        if not venv.exists():
            die(
                "This step needs the tooling venv:\n"
                "    uv venv .venv-study\n"
                "    uv pip install --python .venv-study/bin/python fonttools freetype-py pillow"
            )
        interpreter = str(venv)
    result = subprocess.run([interpreter, str(HERE / script), *[str(a) for a in args]])
    return result.returncode == 0


# --- commands ---------------------------------------------------------------


def cmd_setup(args):
    config = load_config()

    collection = pathlib.Path(args.collection) if args.collection else None
    if collection is None:
        found = find_collections()
        if not found:
            die(
                "No Anki collection found. Pass --collection with the path to collection.anki2."
            )
        collection = choose(
            "Which Anki profile?", found, lambda p: f"{p.parent.name}  ({p})"
        )
        if collection is None:
            die("Cancelled.")

    deck = args.deck
    if deck is None:
        decks = list_decks(collection)
        if not decks:
            die("That collection has no decks with cards in them.")
        picked = choose("Which deck?", decks, lambda d: f"{d[0]}  ({d[1]} cards)")
        if picked is None:
            die("Cancelled.")
        deck = picked[0]

    target = pathlib.Path(args.to) if args.to else None
    if target is None:
        cards = find_sd_cards()
        if cards:
            cards.append("somewhere else")
            picked = choose(
                "Where is the reader's SD card?",
                cards,
                lambda c: (
                    f"{c}  (already has /study)"
                    if not isinstance(c, str) and (c / "study").is_dir()
                    else str(c)
                ),
            )
            if picked is None:
                die("Cancelled.")
            target = None if isinstance(picked, str) else picked
        if target is None:
            answer = input("  path to the SD card: ").strip()
            if not answer:
                die("Cancelled.")
            target = pathlib.Path(answer).expanduser()
    if not target.is_dir():
        die(f"{target} is not a directory.")

    name = args.name or deck.split("::")[-1]
    deck_dir = target / "study" / "mandarin"
    font_dir = target / "study" / "fonts"

    print(f"\nConverting {deck!r} -> {deck_dir}")
    if not run(
        "anki_to_deck.py", collection, "--deck", deck, "--name", name, "--out", deck_dir
    ):
        die("Conversion failed; nothing was changed on the card.")

    # Fonts are 32MB and rarely change, so only build them when they are absent
    # or when the deck has grown characters they do not cover.
    need_fonts = args.rebuild_fonts or not (
        font_dir.is_dir() and any(font_dir.iterdir())
    )
    if not need_fonts:
        print("\nChecking the existing fonts cover this deck...")
        need_fonts = not run("check_deck.py", deck_dir, "--fonts", font_dir)
        if need_fonts:
            print("They do not. Rebuilding.")
    if need_fonts:
        media = collection.parent / "collection.media"
        if not media.is_dir():
            die(
                f"Cannot find {media} -- fonts are built from the ones in your Anki media folder."
            )
        print(f"\nBuilding fonts (a few minutes, ~32MB) -> {font_dir}")
        if not run(
            "make_fonts.py", "--media", media, "--deck", deck_dir, "--out", font_dir
        ):
            die(
                "Font build failed. The deck is on the card but will render in the fallback face."
            )
        run("check_deck.py", deck_dir, "--fonts", font_dir)

    # The photographs. 290 of Mario's 301 cards carry one and the card offers it
    # on a tap, so a deck without them is a quieter deck than the same cards in
    # Anki. Cheap next to the fonts: about 4MB against 32.
    print(f"\nPacking sentence photographs -> {deck_dir / 'images.dat'}")
    if not run("make_images.py", "--collection", collection, "--out", deck_dir):
        print("  (none packed -- the cards still work, without pictures)")

    config.update(
        {"collection": str(collection), "deck": deck, "name": name, "card": str(target)}
    )
    save_config(config)
    print(f"\nReady. On the reader: Apps > STUDY")
    print(f"Settings remembered in {CONFIG}; run 'sync' after each session.")
    return 0


def cmd_sync(args):
    config = load_config()
    collection = pathlib.Path(args.collection or config.get("collection", ""))
    card = pathlib.Path(args.card or config.get("card", ""))
    if not collection.is_file() or not card.is_dir():
        die("Nothing set up yet -- run 'setup' first, or pass --collection and --card.")

    deck_dir = card / "study" / "mandarin"
    if not deck_dir.is_dir():
        die(f"No deck at {deck_dir}. Is the card mounted?")

    if anki_is_running() and not args.force:
        die(
            "Anki is running. Quit it first -- two writers is how a collection gets corrupted.\n"
            "(or pass --force if you are sure)"
        )

    extra = ["--sync"] if args.ankiweb else []
    if args.dry_run:
        extra.append("--dry-run")
    if not run("deck_to_anki.py", deck_dir, collection, *extra):
        die("Sync failed. Your reviews are still on the card; nothing was lost.")

    if args.reconvert and not args.dry_run:
        print(
            f"\nRefreshing the deck from Anki (picks up any re-optimised FSRS parameters)"
        )
        run(
            "anki_to_deck.py",
            collection,
            "--deck",
            config["deck"],
            "--name",
            config.get("name", "Study"),
            "--out",
            deck_dir,
        )
        # The image index is keyed to the deck's own card order, so it has to be
        # rebuilt whenever the deck is.
        run("make_images.py", "--collection", collection, "--out", deck_dir)
    return 0


def cmd_status(args):
    config = load_config()
    if not config:
        print("Nothing set up yet. Run: study.py setup")
        return 0
    print(f"collection  {config.get('collection')}")
    print(f"deck        {config.get('deck')}")
    print(f"card        {config.get('card')}")
    deck_dir = pathlib.Path(config.get("card", "")) / "study" / "mandarin"
    revlog = deck_dir / "revlog.dat"
    if not deck_dir.is_dir():
        print("\nThe card is not mounted.")
    elif revlog.exists():
        pending = revlog.stat().st_size // 32
        print(
            f"\n{pending} review{'' if pending == 1 else 's'} waiting to sync"
            if pending
            else "\nNothing waiting to sync."
        )
    return 0


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    sub = ap.add_subparsers(dest="command", required=True)

    s = sub.add_parser("setup", help="put a deck and its fonts on the SD card")
    s.add_argument(
        "--collection", help="path to collection.anki2 (else found automatically)"
    )
    s.add_argument("--deck", help="deck name (else chosen from a list)")
    s.add_argument("--to", help="SD card path (else found automatically)")
    s.add_argument("--name", help="display name on the device")
    s.add_argument(
        "--rebuild-fonts",
        action="store_true",
        help="rebuild the fonts even if they look fine",
    )
    s.set_defaults(func=cmd_setup)

    s = sub.add_parser("sync", help="apply the device's reviews back to Anki")
    s.add_argument("--ankiweb", action="store_true", help="also push to AnkiWeb")
    s.add_argument(
        "--reconvert",
        action="store_true",
        help="refresh the deck on the card afterwards",
    )
    s.add_argument(
        "--dry-run", action="store_true", help="report what would change, write nothing"
    )
    s.add_argument("--collection")
    s.add_argument("--card")
    s.add_argument(
        "--force", action="store_true", help="sync even if Anki appears to be running"
    )
    s.set_defaults(func=cmd_sync)

    s = sub.add_parser("status", help="what is set up, and what is waiting to sync")
    s.set_defaults(func=cmd_status)

    args = ap.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())

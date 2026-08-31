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
    config = {}
    if CONFIG.exists():
        try:
            config = json.loads(CONFIG.read_text())
        except ValueError:
            config = {}
    # Configs from before multi-deck carried one deck at top level. Fold it
    # into the per-deck map so sync and reconvert treat it like any other.
    if "deck" in config and "decks" not in config:
        name = config.get("name") or config["deck"].split("::")[-1]
        config["decks"] = {slug(name): {"deck": config["deck"], "name": name}}
    config.setdefault("decks", {})
    return config


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
SYSTEM_VOLUMES = {
    "Recovery",
    "Preboot",
    "VM",
    "Update",
    "xarts",
    "iSCPreboot",
    "Hardware",
    "Data",
}


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


def slug(name):
    """A deck path as a directory name: 'Spanish::Food & Drink' -> 'spanish-food-drink'.

    The whole path, not the last component: 'Spanish::Food' and 'Travel::Food'
    are different decks, and giving them the same directory would silently wire
    one deck's reviews to the other's cards.

    MUST match bridge/decks.py slugify(). Both put decks in /study/<slug>/ on
    the same card, and a reader cannot tell which route a folder arrived by, so
    where the two disagree the same deck lands in two directories and is
    studied twice with two separate histories. They had drifted: this capped at
    31 where the service caps at 40, and this reduced every name without Latin
    letters to the single directory "deck", so two Chinese decks shared one
    folder and one review log.
    """
    import hashlib
    import re

    flat = re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")
    if flat and len(flat) <= 40:
        return flat
    stamp = hashlib.sha1(name.encode("utf-8")).hexdigest()[:8]
    return f"{flat[:31]}-{stamp}" if flat else f"deck-{stamp}"


def find_deck_dir(card):
    """The one deck on the card, wherever setup (or an older setup) put it.

    The device scans the same way, so what this returns is what the reader will
    open. More than one match means two setups ran with different names; the
    first alphabetically wins here and on the device, but setup prevents the
    state by offering to remove the old deck first.
    """
    study = card / "study"
    if not study.is_dir():
        return None
    dirs = find_deck_dirs(card)
    return dirs[0] if dirs else None


def find_deck_dirs(card):
    """Every deck on the card, in the same sorted order the device lists them."""
    study = card / "study"
    if not study.is_dir():
        return []
    return sorted(
        d
        for d in study.iterdir()
        if d.is_dir() and d.name != "fonts" and (d / "meta.dat").is_file()
    )


CJK_RANGES = ((0x2E80, 0x9FFF), (0xF900, 0xFAFF), (0xFF00, 0xFFEF))

# Serif faces most machines already have, for the "want a big headword?" offer
# on decks that need no CJK. A student should not need to know what a TTF is
# to get type that looks right.
SYSTEM_FONTS = [
    "/System/Library/Fonts/Supplemental/Georgia.ttf",
    "/System/Library/Fonts/Supplemental/Times New Roman.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf",
]


def deck_has_cjk(deck_dir):
    """Whether this deck needs the CJK font pipeline at all.

    Read from the glyph files the converter just wrote, because they are
    exactly the characters the device will be asked to draw -- not from the
    deck name or a language guess.
    """
    text = ""
    for stem in ("glyphs-headword", "glyphs-sentence"):
        f = deck_dir / f"{stem}.txt"
        if f.is_file():
            text += f.read_text(encoding="utf-8")
    return any(a <= ord(c) <= b for c in text for a, b in [r for r in CJK_RANGES])


def deck_entry(config, deck_dir):
    """The config entry for a deck directory, healing old keys as it goes.

    Entries are keyed by directory name. Configs written before that (or cards
    whose directory predates path-slugs, like Mario's 'mandarin') miss on the
    exact key -- but a config entry whose key matches no directory on the card
    is unclaimed, and when exactly one entry and one directory are unclaimed
    they can only be each other. The key is rewritten to the directory name so
    the guess happens once, not forever.
    """
    entry = config["decks"].get(deck_dir.name)
    if entry is not None:
        return entry
    # Both sides must be singular: one entry with no directory, one directory
    # with no entry. One orphan entry against two unclaimed directories could
    # belong to either, and guessing would hand a reconvert the wrong Anki
    # deck -- which regenerates the directory with another deck's cards.
    orphan_entries = [
        key for key in config["decks"] if not (deck_dir.parent / key).is_dir()
    ]
    unclaimed_dirs = [
        d
        for d in find_deck_dirs(deck_dir.parent.parent)
        if d.name not in config["decks"]
    ]
    if len(orphan_entries) == 1 and unclaimed_dirs == [deck_dir]:
        entry = config["decks"].pop(orphan_entries[0])
        config["decks"][deck_dir.name] = entry
        save_config(config)
        return entry
    return None


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


def ensure_venv(*extra_packages):
    """The tooling venv, created on first use rather than by hand.

    Nothing goes anywhere near the system Python: macOS marks it externally
    managed and `pip install` into it simply fails, which was the first wall a
    fresh user hit. uv when it is there, the stdlib venv when it is not.
    """
    venv = REPO / ".venv-study/bin/python"
    # fonttools is pinned to what Pyodide 0.29.1 ships: the installer page
    # subsets fonts with that exact version, and test_font_parity.py holds
    # the CLI byte-identical to the page. Bump the two together or not at all.
    packages = ["fonttools==4.56.0", "freetype-py", "pillow", *extra_packages]
    if not venv.exists():
        print("Setting up the tooling environment (first run only)...")
        if shutil.which("uv"):
            subprocess.run(["uv", "venv", str(REPO / ".venv-study")], check=True)
            subprocess.run(
                ["uv", "pip", "install", "--python", str(venv), *packages], check=True
            )
        else:
            subprocess.run(
                [sys.executable, "-m", "venv", str(REPO / ".venv-study")], check=True
            )
            subprocess.run(
                [str(venv), "-m", "pip", "install", "-q", *packages], check=True
            )
    for package in extra_packages:
        module = package.replace("-", "_")
        if subprocess.run(
            [str(venv), "-c", f"import {module}"], capture_output=True
        ).returncode:
            print(f"Installing {package} into the tooling environment (once)...")
            if shutil.which("uv"):
                subprocess.run(
                    ["uv", "pip", "install", "--python", str(venv), package], check=True
                )
            else:
                subprocess.run(
                    [str(venv), "-m", "pip", "install", "-q", package], check=True
                )
    return venv


def run(script, *args, venv=False):
    """Run one of the single-purpose tools, showing what it printed."""
    # make_fonts needs fonttools and freetype-py, make_images needs pillow; the
    # rest are plain Python and run under whatever launched this.
    needs_venv = venv or script in ("make_fonts.py", "make_images.py")
    interpreter = str(ensure_venv()) if needs_venv else sys.executable
    # Our own prints are block-buffered when piped; the child's are not. Flush,
    # or every headline appears after the output it was introducing.
    sys.stdout.flush()
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
    deck_dir = target / "study" / slug(deck)
    # The 31-character cap can fold two long deck paths onto one directory, and
    # the recorded entry says whose it is. A collision steps aside rather than
    # silently rewiring one deck's reviews to another's cards.
    recorded = config["decks"].get(deck_dir.name)
    if deck_dir.is_dir() and recorded and recorded.get("deck") != deck:
        base = slug(deck)[:28].rstrip("-")
        n = 2
        while (target / "study" / f"{base}-{n}").is_dir():
            n += 1
        deck_dir = target / "study" / f"{base}-{n}"
        print(
            f"  {slug(deck)} already belongs to {recorded['deck']!r}; using {deck_dir.name}"
        )
    if deck_dir.is_dir():
        pending = deck_dir / "revlog.dat"
        reviews = pending.stat().st_size // 32 if pending.is_file() else 0
        if reviews:
            print(
                f"\nThis deck is already on the card with {reviews} review(s) not yet"
                f" synced to Anki.\nReconverting now resets the card states those reviews"
                f" produced; they return on the next sync.\nCleaner: run 'study.py sync' first."
            )
    font_dir = deck_dir / "fonts"

    # Cards from before per-deck fonts have one shared /study/fonts. When the
    # card holds exactly one deck there is no ambiguity about whose fonts they
    # are; move them in and the card is in the new shape. (The device also
    # falls back to the shared location, so a card that never re-runs setup
    # keeps working -- this just tidies the ones that do.)
    legacy_fonts = target / "study" / "fonts"
    already = find_deck_dirs(target)
    if legacy_fonts.is_dir() and len(already) == 1:
        shutil.move(str(legacy_fonts), str(already[0] / "fonts"))
        print(
            f"Moved the shared fonts into {already[0].name}/fonts (fonts are per-deck now)."
        )

    # Other decks may stay: the reader switches between them from its deck
    # screen. Replacing is the destructive choice, so it is never the default
    # and it names what would be lost.
    others = [d for d in find_deck_dirs(target) if d != deck_dir]
    if others and not args.add:
        print(f"\nThe card already has: {', '.join(d.name for d in others)}.")
        if args.replace:
            answer = "r"
        else:
            answer = (
                input(
                    "  [a]dd this deck alongside (default), [r]eplace them, or [c]ancel: "
                )
                .strip()
                .lower()
                or "a"
            )
        if answer.startswith("c"):
            die("Cancelled; the card is unchanged.")
        if answer.startswith("r"):
            for other in others:
                pending = other / "revlog.dat"
                reviews = pending.stat().st_size // 32 if pending.is_file() else 0
                if reviews:
                    print(
                        f"  {other.name} holds {reviews} review(s) not yet synced to Anki."
                    )
                    confirm = (
                        input(f"  Really remove {other.name}? [y/N] ").strip().lower()
                    )
                    if confirm != "y":
                        die("Cancelled; the card is unchanged.")
                shutil.rmtree(other)
                print(f"  Removed {other}.")

    extra = []
    for pair in args.map or []:
        extra += ["--map", pair]
    print(f"\nConverting {deck!r} -> {deck_dir}")
    if not run(
        "anki_to_deck.py",
        collection,
        "--deck",
        deck,
        "--name",
        name,
        "--out",
        deck_dir,
        *extra,
    ):
        die("Conversion failed; nothing was changed on the card.")

    # Fonts only matter when the deck needs glyphs the built-in serif lacks, or
    # when the user asked for a face of their own. A Latin-script deck without
    # --font skips the whole pipeline: the built-in serif draws it, and the
    # device falls back per card even if stale fonts are lying around.
    needs_cjk = deck_has_cjk(deck_dir)
    if not needs_cjk and not args.font and not args.no_font:
        # Offer the big face rather than requiring the user to know it exists.
        # The built-in serif works, but at 18pt where the fitted face is three
        # times that -- the difference between "works" and "looks right".
        found = next((f for f in SYSTEM_FONTS if pathlib.Path(f).is_file()), None)
        if found:
            answer = (
                input(
                    f"\nBuild a large headword face from {pathlib.Path(found).stem}?"
                    f" (sized to fit this deck's longest word) [Y/n] "
                )
                .strip()
                .lower()
            )
            if answer in ("", "y", "yes"):
                args.font = found
    if not needs_cjk and not args.font:
        print(
            "\nNo CJK in this deck; the built-in face will draw it."
            " (Pass --font YourFont.ttf for a large custom headword face.)"
        )
        # Still verified: a deck can carry the odd glyph the built-in face
        # lacks, and a character that silently does not paint is the kind of
        # gap nobody notices until an exam. Warn, do not block -- it is a few
        # characters in a side field, not a blank deck.
        if not run("check_deck.py", deck_dir):
            print(
                "  warning: the glyphs listed above will not appear on the"
                " device; everything else renders."
            )
    else:
        need_fonts = (
            args.rebuild_fonts
            or args.font
            or not (font_dir.is_dir() and any(font_dir.iterdir()))
        )
        if not need_fonts:
            print("\nChecking the existing fonts cover this deck...")
            need_fonts = not run("check_deck.py", deck_dir, "--fonts", font_dir)
            if need_fonts:
                print("They do not. Rebuilding.")
        if need_fonts:
            font_args = ["--deck", deck_dir, "--out", font_dir]
            if args.font:
                font_args += ["--font", pathlib.Path(args.font).expanduser()]
            media = collection.parent / "collection.media"
            if media.is_dir():
                font_args += ["--media", media]
            elif not args.font:
                die(
                    f"Cannot find {media} -- CJK fonts are built from the ones in"
                    f" your Anki media folder (_simsun.ttf and friends), or pass"
                    f" --font YourFont.ttf to build from any TTF."
                )
            print(f"\nBuilding fonts -> {font_dir}")
            if not run("make_fonts.py", *font_args):
                die(
                    "Font build failed. The deck is on the card and will render"
                    " in the built-in face."
                )
            if not run("check_deck.py", deck_dir, "--fonts", font_dir):
                print(
                    "  warning: some cards have glyphs these fonts cannot draw;"
                    " those cards fall back to the built-in face on the device."
                )

    # The photographs. 290 of Mario's 301 cards carry one and the card offers it
    # on a tap, so a deck without them is a quieter deck than the same cards in
    # Anki. Cheap next to the fonts: about 4MB against 32.
    print(f"\nPacking sentence photographs -> {deck_dir / 'images.dat'}")
    if not run("make_images.py", "--collection", collection, "--out", deck_dir):
        print("  (none packed -- the cards still work, without pictures)")

    config.update({"collection": str(collection), "card": str(target)})
    entry = {"deck": deck, "name": name}
    if args.font:
        # Remembered so a reconvert can rebuild the face when the deck grows
        # words the current subset lacks.
        entry["font"] = str(pathlib.Path(args.font).expanduser())
    config["decks"][deck_dir.name] = entry
    # The old single-deck keys would shadow the map on the next load.
    config.pop("deck", None)
    config.pop("name", None)
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

    deck_dirs = find_deck_dirs(card)
    if not deck_dirs:
        die(f"No deck under {card / 'study'}. Is the card mounted?")

    if anki_is_running() and not args.force:
        die(
            "Anki is running. Quit it first -- two writers is how a collection gets corrupted.\n"
            "(or pass --force if you are sure)"
        )

    # Replay every deck first, then push to AnkiWeb once: each replay is local
    # and cheap, and one push carries them all.
    extra = ["--dry-run"] if args.dry_run else []
    if args.force:
        extra.append("--force")
    for deck_dir in deck_dirs:
        if len(deck_dirs) > 1:
            print(f"\n== {deck_dir.name} ==")
        if not run("deck_to_anki.py", deck_dir, collection, *extra):
            die("Sync failed. Your reviews are still on the card; nothing was lost.")

    if args.ankiweb and not args.dry_run:
        # The AnkiWeb client is Anki's own Python library, which cannot go into
        # the system Python (macOS marks it externally managed), so it lives in
        # the tooling venv and is pulled in the first time anyone asks.
        ensure_venv("anki")
        push = ["--force"] if args.force else []
        if not run("deck_to_anki.py", "--push-only", collection, *push, venv=True):
            die(
                "AnkiWeb push failed. Reviews are applied locally; re-run to retry the push."
            )

    if args.reconvert and not args.dry_run:
        for deck_dir in deck_dirs:
            entry = deck_entry(config, deck_dir)
            if entry is None:
                print(
                    f"\n{deck_dir.name}: not in {CONFIG.name}, cannot reconvert it"
                    f" -- run setup once on this machine to register it"
                )
                continue
            print(f"\nRefreshing {deck_dir.name} from Anki")
            run(
                "anki_to_deck.py",
                collection,
                "--deck",
                entry["deck"],
                "--name",
                entry.get("name", deck_dir.name),
                "--out",
                deck_dir,
            )
            # The image index is keyed to the deck's own card order, so it has
            # to be rebuilt whenever the deck is.
            run("make_images.py", "--collection", collection, "--out", deck_dir)
            # A reconverted deck can have grown glyphs its fonts lack; the
            # device would fall back per card, but quietly shrinking type is
            # worth a rebuild when we know the source.
            fonts = deck_dir / "fonts"
            if fonts.is_dir() and any(fonts.iterdir()):
                if not run("check_deck.py", deck_dir, "--fonts", fonts):
                    font_args = ["--deck", deck_dir, "--out", fonts]
                    if entry.get("font"):
                        font_args += ["--font", entry["font"]]
                    else:
                        media = collection.parent / "collection.media"
                        if not media.is_dir():
                            print(
                                "  fonts no longer cover the deck; re-run setup to rebuild them"
                            )
                            continue
                        font_args += ["--media", media]
                    print("  fonts no longer cover the deck; rebuilding")
                    run("make_fonts.py", *font_args)
    return 0


def cmd_status(args):
    config = load_config()
    if not config:
        print("Nothing set up yet. Run: study.py setup")
        return 0
    print(f"collection  {config.get('collection')}")
    print(f"card        {config.get('card')}")
    deck_dirs = find_deck_dirs(pathlib.Path(config.get("card", "")))
    if not deck_dirs:
        print("\nThe card is not mounted.")
        return 0
    print()
    for deck_dir in deck_dirs:
        revlog = deck_dir / "revlog.dat"
        pending = revlog.stat().st_size // 32 if revlog.is_file() else 0
        entry = deck_entry(config, deck_dir) or {}
        label = entry.get("deck", deck_dir.name)
        print(
            f"{deck_dir.name:20} {label:40} "
            + (f"{pending} review(s) waiting" if pending else "nothing waiting")
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
    s.add_argument(
        "--font",
        help="build the headword/sentence faces from this TTF (any language)",
    )
    s.add_argument(
        "--no-font",
        action="store_true",
        help="use the built-in face without being asked about a bigger one",
    )
    s.add_argument(
        "--map",
        action="append",
        metavar="SLOT=FIELD",
        help="for note types the converter does not know: which Anki field"
        " fills which slot (e.g. --map headword=Word); repeatable",
    )
    s.add_argument(
        "--replace",
        action="store_true",
        help="remove the other decks on the card without asking (add is the default)",
    )
    s.add_argument(
        "--add",
        action="store_true",
        help="add alongside existing decks without asking",
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

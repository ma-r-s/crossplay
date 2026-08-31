"""Deck builds: the shared converter, run as a subprocess, into versioned dirs.

A rebuild never touches an existing build: it writes decks/<slug>/<build>/
fresh and the manifest points there, so a device mid-download of the old
build reads consistent bytes forever. Old builds are GC'd once they are a
few days stale. The subprocess boundary is deliberate (critic A8/C1): the
converter and fontTools parse user-controlled bytes and hold the GIL; a
crash or a hostile file costs one build, not the service.
"""

import hashlib
import json
import logging
import pathlib
import re
import shutil
import subprocess
import sys
import time

log = logging.getLogger("bridge.decks")

TOOLS = None  # set by app startup to the tools_local/study directory
FONT_SUFFIXES = {".ttf", ".otf", ".ttc"}
KEEP_BUILDS = 3


def slugify(deck_name: str) -> str:
    """A directory name, an ack key and a build key, all in one string, so two
    deck names must never reach the same one. An ASCII slug alone does: every
    purely non-Latin name (Chinese, Japanese, Russian, Greek) reduces to the
    empty string, and long subdeck families truncate into each other. Both
    collisions are silent and hand the user one deck's cards under another
    deck's name. Names that survive the reduction intact keep their plain
    slug, so nothing already on a card is renamed."""
    slug = re.sub(r"[^a-z0-9]+", "-", deck_name.lower()).strip("-")
    if slug and len(slug) <= 40:
        return slug
    stamp = hashlib.sha1(deck_name.encode("utf-8")).hexdigest()[:8]
    return f"{slug[:31]}-{stamp}" if slug else f"deck-{stamp}"


def _run(args: list[str], timeout: int = 900) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, *args], capture_output=True, text=True, timeout=timeout
    )


def build_deck(store, deck_name: str) -> dict:
    """Blocking; runs inside the job thread. Returns the manifest entry.
    Raises RuntimeError with the tool's tail on failure."""
    slug = slugify(deck_name)
    build_id = f"{int(time.time())}"
    out = store.root / "decks" / slug / build_id
    out.mkdir(parents=True, exist_ok=True)

    convert = _run(
        [
            str(TOOLS / "anki_to_deck.py"),
            str(store.collection_path),
            "--deck",
            deck_name,
            "--out",
            str(out),
        ]
    )
    if convert.returncode != 0:
        shutil.rmtree(out, ignore_errors=True)
        raise RuntimeError(f"deck convert failed: {convert.stderr.strip()[-400:]}")

    media = store.collection_path.with_suffix(".media")
    has_cjk_fonts = media.is_dir() and any(
        p.suffix.lower() in FONT_SUFFIXES for p in media.iterdir()
    )
    if has_cjk_fonts:
        fonts = _run(
            [
                str(TOOLS / "make_fonts.py"),
                "--media",
                str(media),
                "--deck",
                str(out),
                "--out",
                str(out / "fonts"),
            ]
        )
        if fonts.returncode != 0:
            shutil.rmtree(out, ignore_errors=True)
            raise RuntimeError(f"font build failed: {fonts.stderr.strip()[-400:]}")

    files = {}
    for p in sorted(out.rglob("*")):
        if p.is_file():
            rel = str(p.relative_to(out))
            files[rel] = {
                "size": p.stat().st_size,
                "sha256": hashlib.sha256(p.read_bytes()).hexdigest(),
            }
    entry = {"slug": slug, "deck": deck_name, "buildId": build_id, "files": files}
    (out / ".manifest.json").write_text(json.dumps(entry, indent=1))
    _gc(store.root / "decks" / slug)
    return entry


def deck_fingerprints(store, deck_name: str) -> tuple[str, str]:
    """Two change detectors, read from the mirror: (content, schedule).

    Content covers what deck.dat and the fonts are made of (note text and
    card count); schedule covers what only cards.dat encodes (review state).
    One review changes schedule but not content, and rebuilding cards.dat
    alone is seconds where the full converter plus font subsetting is ~90s
    on the pi -- which is the difference between a review-day sync and a
    first sync, and users do one of those every day."""
    import sqlite3

    like = deck_name.replace("::", "\x1f")
    db = sqlite3.connect(f"file:{store.collection_path}?mode=ro", uri=True)
    try:
        db.create_collation("unicase", lambda a, b: (a.lower() > b.lower()) - (a.lower() < b.lower()))
        row = db.execute(
            """select count(c.id), coalesce(max(n.mod), 0), coalesce(max(c.mod), 0)
               from cards c join notes n on n.id = c.nid
               join decks d on d.id = (case when c.odid = 0 then c.did else c.odid end)
               where d.name = ? or d.name like ?""",
            (like, like + "\x1f%"),
        ).fetchone()
    finally:
        db.close()
    return f"{row[0]}-{row[1]}", f"{row[0]}-{row[2]}"


def rebuild_cards_only(store, deck_name: str, base_build: dict) -> dict:
    """A schedule-only change: re-run the converter into a fresh build dir,
    then reuse the PREVIOUS build's fonts and images wholesale (content
    unchanged means they are byte-identical, and fonts are the expensive
    part). deck.dat is rewritten too -- it is cheap without the fonts."""
    import hashlib
    import shutil as sh

    slug = base_build["slug"]
    build_id = f"{int(time.time())}"
    out = store.root / "decks" / slug / build_id
    prev = store.root / "decks" / slug / base_build["buildId"]
    out.mkdir(parents=True, exist_ok=True)

    convert = _run(
        [str(TOOLS / "anki_to_deck.py"), str(store.collection_path), "--deck", deck_name, "--out", str(out)]
    )
    if convert.returncode != 0:
        sh.rmtree(out, ignore_errors=True)
        raise RuntimeError(f"deck convert failed: {convert.stderr.strip()[-400:]}")
    if (prev / "fonts").is_dir():
        sh.copytree(prev / "fonts", out / "fonts")

    files = {}
    for p in sorted(out.rglob("*")):
        if p.is_file():
            rel = str(p.relative_to(out))
            files[rel] = {"size": p.stat().st_size, "sha256": hashlib.sha256(p.read_bytes()).hexdigest()}
    entry = {"slug": slug, "deck": deck_name, "buildId": build_id, "files": files}
    (out / ".manifest.json").write_text(json.dumps(entry, indent=1))
    _gc(store.root / "decks" / slug)
    return entry


def latest_build(store, slug: str) -> dict | None:
    deck_dir = store.root / "decks" / slug
    if not deck_dir.is_dir():
        return None
    builds = sorted((d for d in deck_dir.iterdir() if d.is_dir()), key=lambda d: d.name)
    for d in reversed(builds):
        manifest = d / ".manifest.json"
        if manifest.exists():
            return json.loads(manifest.read_text())
    return None


def _gc(deck_dir: pathlib.Path):
    builds = sorted((d for d in deck_dir.iterdir() if d.is_dir()), key=lambda d: d.name)
    for stale in builds[:-KEEP_BUILDS]:
        shutil.rmtree(stale, ignore_errors=True)

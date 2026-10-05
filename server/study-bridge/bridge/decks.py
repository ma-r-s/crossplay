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
KEEP_BUILDS = 3

# What the reader says after "<deck> could not be built: ". Keyed by the
# `reason:` line the converter prints when a deck converts to nothing, plus
# "font" for the face step. A code missing here sends no sentence, and the
# reader falls back to its plain "could not be built." SHORT ON PURPOSE: the
# verdict's body is four lines at the large face, and the first version
# ("its cards have a picture on the front, and the reader shows text there")
# was cut off at "its cards have a pi..." -- the reason, which is the point.
REASONS = {
    "picture-front": "its fronts are pictures",
    "blank-front": "its fronts are empty",
    "empty-cloze": "every cloze in it was edited out",
    "empty-deck": "it has no cards",
    "missing-deck": "it is gone from Anki",
    "font": "its Chinese fonts would not convert",
}


class BuildFailed(RuntimeError):
    """A deck that could not be built, and the converter's reason code for it."""

    def __init__(self, reason: str, detail: str):
        super().__init__(detail)
        self.reason = reason


def reason_sentence(exc: BaseException) -> str:
    return REASONS.get(getattr(exc, "reason", ""), "")


# The face the installer page builds a CJK deck from when its package carries
# none (site/study/NotoSansCJK.otf). deploy.sh ships the same file into the
# image beside the tools, so the bridge and the page give a deck the same face.
BUNDLED_CJK = None  # set by app startup


def _needs_faces(deck_dir: pathlib.Path) -> bool:
    """study.deck_has_cjk: the decision the installer page makes, from the glyph
    files the converter wrote. study.py imports only the standard library, so
    unlike make_fonts it is safe to load into the service."""
    if str(TOOLS) not in sys.path:
        sys.path.insert(0, str(TOOLS))
    import study

    return study.deck_has_cjk(deck_dir)


_FACE_FILES = None


def face_files() -> set[str]:
    """The media filenames make_fonts.py builds faces from, read out of its own
    FACES table rather than retyped here.

    The build used to run the face step whenever the media folder held ANY font
    file, while make_fonts.py only ever looks for these five. Three users on
    2026-10-05 had convertible decks -- two of them pure Latin -- failing whole
    on "no faces built", because a note template somewhere in their collection
    had left _inter-regular.ttf or _NotoSansJP-Regular.ttf in the media folder.
    Parsed, not imported: importing make_fonts would load fontTools and freetype
    into the service, and the subprocess boundary exists to keep them out."""
    global _FACE_FILES
    if _FACE_FILES is None:
        import ast

        tree = ast.parse((TOOLS / "make_fonts.py").read_text())
        for node in tree.body:
            if (
                isinstance(node, ast.Assign)
                and any(isinstance(t, ast.Name) and t.id == "FACES" for t in node.targets)
            ):
                _FACE_FILES = {filename for filename, _family in ast.literal_eval(node.value)}
                break
        else:
            raise RuntimeError("make_fonts.py has no FACES table; the face step cannot be gated")
    return _FACE_FILES


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
        m = re.search(r"^reason: (\S+)$", convert.stderr, re.M)
        raise BuildFailed(
            m.group(1) if m else "", f"deck convert failed: {convert.stderr.strip()[-400:]}"
        )

    # Faces are built when the DECK needs them, decided from the characters the
    # converter just wrote out, and from the same source the installer page
    # uses: the template's own faces when the collection carries them, else
    # the bundled Noto CJK. It used to be decided by "is there any font file
    # in the media folder", which built SimSun for Portuguese and failed every
    # Latin deck whose collection happened to hold an unrelated font.
    if _needs_faces(out):
        media = store.collection_path.with_suffix(".media")
        if media.is_dir() and any((media / name).is_file() for name in face_files()):
            source = ["--media", str(media)]
        else:
            source = ["--font", str(BUNDLED_CJK)]
        fonts = _run(
            [
                str(TOOLS / "make_fonts.py"),
                *source,
                "--deck",
                str(out),
                "--out",
                str(out / "fonts"),
            ]
        )
        if fonts.returncode != 0:
            shutil.rmtree(out, ignore_errors=True)
            raise BuildFailed("font", f"font build failed: {fonts.stderr.strip()[-400:]}")

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

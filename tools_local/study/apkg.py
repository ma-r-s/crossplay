#!/usr/bin/env python3
"""Open an .apkg export and hand back what the other tools want.

The rest of the pipeline reads a live collection: anki_to_deck.py wants a
schema-18 SQLite file plus a media directory, and that is exactly what an
.apkg is once unwrapped. This module does the unwrapping, so the same
converter runs against a user's exported deck -- on the website through
Pyodide, or on a computer through study.py -- without either growing a
second parser.

Three package generations exist, told apart by the `meta` member:

  version 3 (Anki 2.1.50+, the default for years): collection.anki21b is a
    zstd-compressed SQLite file with the modern schema; media files are
    individually zstd-compressed and named by index, with a zstd-compressed
    protobuf listing their real names. This is the format we support.
  version 2 / version 1 (the "support older Anki versions" checkbox, or
    ancient exporters): a plain SQLite file with the pre-2.1.28 schema, where
    decks live as JSON inside the `col` table. The converter cannot read that
    schema, so these are refused with instructions rather than half-read.

Version 3 packages also carry a decoy collection.anki2 whose single purpose
is to show "please update Anki" to old clients. Never open it: it is valid
SQLite with none of the user's cards.
"""

import json
import pathlib
import sqlite3
import struct
import zipfile

MODERN_TABLES = {"decks", "notetypes", "deck_config"}

# Media worth carrying to the device: the fonts a CJK deck ships, and the
# images the sentence view shows. Audio and video have no player to play
# them, so extracting them would only cost memory -- on the website the
# "disk" is RAM.
FONT_SUFFIXES = (".ttf", ".otf", ".ttc")
IMAGE_SUFFIXES = (".jpg", ".jpeg", ".png", ".webp", ".gif", ".bmp")
# Never extracted: the device has no speaker. Counted so the page can say so.
AUDIO_SUFFIXES = (".mp3", ".ogg", ".wav", ".m4a", ".flac", ".opus", ".aac")

REEXPORT_ADVICE = (
    "this .apkg could not be read even as an old-format package. In Anki,"
    ' import it, then export the deck again with "Support older Anki'
    ' versions" unchecked (and "Include scheduling information" checked),'
    " then retry."
)


class ApkgError(Exception):
    """A package we cannot use, with a message meant for the user's eyes."""


def _varint(buf, i):
    result = shift = 0
    while True:
        byte = buf[i]
        i += 1
        result |= (byte & 0x7F) << shift
        if not byte & 0x80:
            return result, i
        shift += 7


def _package_version(zf):
    """The `meta` member is a protobuf with one varint field: the version."""
    if "meta" not in zf.namelist():
        return 1
    blob = zf.read("meta")
    i = 0
    while i < len(blob):
        try:
            tag, i = _varint(blob, i)
        except IndexError:
            break
        field, wire = tag >> 3, tag & 7
        if wire == 0:
            value, i = _varint(blob, i)
            if field == 1:
                return value
        elif wire == 2:
            length, i = _varint(blob, i)
            i += length
        elif wire == 1:
            i += 8
        elif wire == 5:
            i += 4
        else:
            break
    return 1


def _zstd_decompress(data):
    try:
        import zstandard
    except ImportError as exc:
        raise ApkgError(
            "reading a modern .apkg needs the zstandard package (pip install zstandard)"
        ) from exc
    # 1GB cap, and not a bit more: this runs on wasm32 too, where 1 << 31
    # overflows the C ssize_t the binding converts into.
    return zstandard.ZstdDecompressor().decompress(data, max_output_size=1 << 30)


def _media_names_v3(blob):
    """Decode the MediaEntries protobuf: index in the list is the zip name."""
    names = []
    i = 0
    while i < len(blob):
        tag, i = _varint(blob, i)
        field, wire = tag >> 3, tag & 7
        if wire != 2:
            if wire == 0:
                _, i = _varint(blob, i)
                continue
            break
        length, i = _varint(blob, i)
        entry = blob[i : i + length]
        i += length
        if field != 1:
            continue
        # MediaEntry { string name = 1; uint32 size = 2; bytes sha1 = 3; }
        name = ""
        j = 0
        while j < len(entry):
            etag, j = _varint(entry, j)
            efield, ewire = etag >> 3, etag & 7
            if ewire == 2:
                elen, j = _varint(entry, j)
                if efield == 1:
                    name = entry[j : j + elen].decode("utf-8", "replace")
                j += elen
            elif ewire == 0:
                _, j = _varint(entry, j)
            else:
                break
        names.append(name)
    return names


def _media_map(zf, version):
    """zip member name -> real filename, for the media we keep."""
    if "media" not in zf.namelist():
        return {}
    raw = zf.read("media")
    if version >= 3:
        names = _media_names_v3(_zstd_decompress(raw))
        return {str(i): n for i, n in enumerate(names)}
    # Legacy packages: a plain JSON object. Some very old ones are empty.
    try:
        return {k: v for k, v in json.loads(raw).items()}
    except (ValueError, AttributeError):
        return {}


def _keepable(name):
    """Flat basenames only: a media name with a path in it is nothing Anki
    writes, and following one would let an archive write outside media_dir.
    Both flavors of separator, because the CLI also runs on Windows."""
    if not name or name.startswith("."):
        return False
    if name != pathlib.PurePosixPath(name).name:
        return False
    if name != pathlib.PureWindowsPath(name).name:
        return False
    return name.lower().endswith(FONT_SUFFIXES + IMAGE_SUFFIXES)


def _protobuf_varint(value):
    out = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        if value:
            out.append(byte | 0x80)
        else:
            out.append(byte)
            return bytes(out)


def _packed_floats(field, values):
    payload = b"".join(struct.pack("<f", float(v)) for v in values)
    return _protobuf_varint((field << 3) | 2) + _protobuf_varint(len(payload)) + payload


def _upgrade_legacy(collection_path):
    """Materialise the modern tables a schema-11 collection keeps as JSON.

    Every AnkiWeb shared deck arrives in this format on purpose (so old
    clients can import it), which makes 'legacy' the common case for the
    installer's target user, not a corner. The cards/notes/revlog tables are
    already shaped right; only the metadata moved: notetypes, fields,
    templates, decks and deck presets live as JSON blobs in the `col` row.
    This writes them into real tables in OUR extracted copy, shaped exactly
    as the modern queries expect, protobuf and all, so anki_to_deck.py runs
    unchanged. Returns True if the collection now looks modern.
    """
    db = sqlite3.connect(collection_path)
    try:
        try:
            row = db.execute("select models, decks, dconf, conf from col").fetchone()
        except sqlite3.OperationalError:
            return False
        if row is None:
            return False
        models = json.loads(row[0] or "{}")
        decks = json.loads(row[1] or "{}")
        dconf = json.loads(row[2] or "{}")
        conf = json.loads(row[3] or "{}")

        db.executescript(
            """
            create table notetypes (id integer primary key, name text);
            create table fields (ntid integer, ord integer, name text);
            create table templates (ntid integer, ord integer, name text);
            create table decks (id integer primary key, name text, kind blob);
            create table deck_config (id integer primary key, config blob);
            create table config (key text primary key, val blob);
            """
        )

        for mid, model in models.items():
            db.execute(
                "insert into notetypes values (?, ?)", (int(mid), model.get("name", ""))
            )
            for field in model.get("flds", []):
                db.execute(
                    "insert into fields values (?, ?, ?)",
                    (int(mid), field.get("ord", 0), field.get("name", "")),
                )
            for template in model.get("tmpls", []):
                db.execute(
                    "insert into templates values (?, ?, ?)",
                    (int(mid), template.get("ord", 0), template.get("name", "")),
                )

        for did, deck in decks.items():
            # Modern names use the unit separator where the JSON used '::',
            # and the preset id travels as DeckKind{ normal{ config_id } }.
            name = str(deck.get("name", "")).replace("::", "\x1f")
            inner = b"\x08" + _protobuf_varint(int(deck.get("conf", 1) or 1))
            kind = b"\x0a" + _protobuf_varint(len(inner)) + inner
            db.execute("insert into decks values (?, ?, ?)", (int(did), name, kind))

        for conf_id, preset in dconf.items():
            blob = b""
            new_delays = (preset.get("new") or {}).get("delays") or []
            lapse_delays = (preset.get("lapse") or {}).get("delays") or []
            if new_delays:
                blob += _packed_floats(1, new_delays)
            if lapse_delays:
                blob += _packed_floats(2, lapse_delays)
            per_day = (preset.get("new") or {}).get("perDay")
            if per_day is not None:
                blob += _protobuf_varint((9 << 3) | 0) + _protobuf_varint(int(per_day))
            rev_per_day = (preset.get("rev") or {}).get("perDay")
            if rev_per_day is not None:
                blob += _protobuf_varint((10 << 3) | 0) + _protobuf_varint(
                    int(rev_per_day)
                )
            max_interval = preset.get("maxIvl")
            if max_interval is not None:
                blob += _protobuf_varint((16 << 3) | 0) + _protobuf_varint(
                    int(max_interval)
                )
            db.execute("insert into deck_config values (?, ?)", (int(conf_id), blob))

        if conf.get("rollover") is not None:
            db.execute(
                "insert into config values ('rollover', ?)",
                (str(int(conf["rollover"])).encode(),),
            )

        db.commit()
    finally:
        db.close()
    return _probe_schema(collection_path)


def _connect(collection_path):
    """Read-only, with Anki's collation: name columns declare `unicase`, and
    even an ORDER BY over them fails without it registered."""
    db = sqlite3.connect(f"file:{collection_path}?mode=ro", uri=True)
    db.create_collation(
        "unicase", lambda a, b: (a.lower() > b.lower()) - (a.lower() < b.lower())
    )
    return db


def _probe_schema(collection_path):
    db = _connect(collection_path)
    try:
        tables = {
            row[0]
            for row in db.execute("select name from sqlite_master where type='table'")
        }
    finally:
        db.close()
    return MODERN_TABLES.issubset(tables)


def extract(apkg_path, out_dir):
    """Unpack an .apkg into out_dir and return what the pipeline needs.

    Returns a dict: collection (Path), media_dir (Path), version (int),
    media_kept (int), media_skipped (int). Raises ApkgError with a
    user-facing message for anything we refuse to half-read.
    """
    apkg_path = pathlib.Path(apkg_path)
    out_dir = pathlib.Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    media_dir = out_dir / "media"
    media_dir.mkdir(exist_ok=True)

    try:
        zf = zipfile.ZipFile(apkg_path)
    except zipfile.BadZipFile as exc:
        raise ApkgError(f"{apkg_path.name} is not a zip file, so not an .apkg") from exc

    with zf:
        version = _package_version(zf)
        members = set(zf.namelist())

        collection = out_dir / "collection.anki2"
        if version >= 3:
            if "collection.anki21b" not in members:
                raise ApkgError(
                    f"{apkg_path.name} says it is a modern package but has no"
                    " collection.anki21b inside; re-export it from Anki"
                )
            collection.write_bytes(_zstd_decompress(zf.read("collection.anki21b")))
        else:
            member = (
                "collection.anki21"
                if "collection.anki21" in members
                else "collection.anki2"
                if "collection.anki2" in members
                else None
            )
            if member is None:
                raise ApkgError(f"{apkg_path.name} has no collection inside")
            collection.write_bytes(zf.read(member))

        if not _probe_schema(collection) and not _upgrade_legacy(collection):
            raise ApkgError(REEXPORT_ADVICE)

        kept = skipped = 0
        # Counted by kind, because "media skipped" is not a number a user can
        # act on but "this deck has 431 sounds the reader cannot play" is.
        audio = pictures = 0
        for zip_name, real_name in _media_map(zf, version).items():
            if zip_name not in members:
                continue
            if not _keepable(real_name):
                skipped += 1
                if real_name.lower().endswith(AUDIO_SUFFIXES):
                    audio += 1
                continue
            if real_name.lower().endswith(IMAGE_SUFFIXES):
                pictures += 1
            data = zf.read(zip_name)
            if version >= 3:
                data = _zstd_decompress(data)
            (media_dir / real_name).write_bytes(data)
            kept += 1

    return {
        "collection": collection,
        "media_dir": media_dir,
        "version": version,
        "media_kept": kept,
        "media_skipped": skipped,
        "audio": audio,
        "pictures": pictures,
    }


def list_decks(collection_path):
    """Every non-empty deck in the collection, with its card count.

    Returns [(name, cards)] sorted by name, '::' separated like Anki shows
    them. Counts are per-deck, not rolled up: the page shows the tree and the
    converter's own subtree query does the rolling up.
    """
    db = _connect(collection_path)
    try:
        rows = db.execute(
            """select d.name, count(c.id) from decks d
               left join cards c on c.did = d.id
               group by d.id order by d.name"""
        ).fetchall()
    finally:
        db.close()
    out = []
    for name, cards in rows:
        pretty = name.replace("\x1f", "::")
        if cards:
            out.append((pretty, cards))
    return out


def scheduling_summary(collection_path):
    """How much scheduling state came along, so the page can say so.

    An export made without "Include scheduling information" arrives with
    every card new and an empty revlog; the deck still converts, it just
    starts fresh. Saying that out loud beats the user discovering it.
    """
    db = _connect(collection_path)
    try:
        (total,) = db.execute("select count(*) from cards").fetchone()
        (seen,) = db.execute("select count(*) from cards where type != 0").fetchone()
        (reviews,) = db.execute("select count(*) from revlog").fetchone()
    finally:
        db.close()
    return {"cards": total, "cards_with_state": seen, "reviews": reviews}

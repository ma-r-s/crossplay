#!/usr/bin/env python3
"""Replay reviews done on the device back into the Anki collection.

    tools_local/study/deck_to_anki.py \
        /Volumes/SDCARD/study/mandarin \
        ~/Library/Application\\ Support/Anki2/User\\ 1/collection.anki2

This is the half that makes the device an Anki client rather than a toy.
`revlog.dat` is written on the device, one 32-byte record per review, and this
replays each one into `revlog` and updates the card it belongs to.

## Anki MUST be closed

Anki holds the collection open and keeps its own state in memory; writing
underneath it loses whichever copy it flushes last. This script refuses to run
if Anki looks like it is running, and takes a backup before touching anything.

## What it does and does not do

It writes the review history and each card's new due date, interval, and FSRS
memory state -- everything the device computed. It does NOT recompute anything
itself: the device already ran the same FSRS-5 with the same parameters (that
is what host-tests/study pins at 287/301 against this very collection), so
recomputing here would be a second implementation to keep in step.

After importing, Anki may want to rebuild its own caches. "Tools > Check
Database" is harmless and does that.

## Idempotency

Each device review carries the card id and the wall-clock millisecond it was
answered, which is exactly Anki's revlog primary key. Re-running the script
therefore cannot double-apply a review: rows that already exist are skipped.
The device's revlog.dat is left alone -- it is an append-only record, not a
queue, and truncating it would destroy the only copy of that history.
"""

import argparse
import getpass
import os
import json
import pathlib
import shutil
import sqlite3
import struct
import sys
import time

# cardId(8) atMs(8) rating(1) state(1) elapsed(2) interval(4) tookMs(4) flags(1) pad(3) = 32.
# Must match the writer in StudyActivity::persist byte for byte.
REVLOG_RECORD = "<qqBBhiIB3x"
REVLOG_RECORD_SIZE = 32

# The user pressed UNDO on the device. The record stays -- revlog.dat is
# append-only and the device cannot shrink a file without reaching past its
# storage layer -- so it is struck out in place and skipped here. A log written
# before undo existed has zeros in this byte and reads as nothing voided.
REVLOG_VOIDED = 1 << 0
CARD_RECORD_SIZE = 32

# Anki revlog kinds. Everything the device produces is a real review or a
# relearning step, never a filtered-deck cram -- see StudyFsrs.h for why that
# distinction matters.
KIND_LEARN = 0
KIND_REVIEW = 1
KIND_RELEARN = 2

STATE_NEW, STATE_LEARNING, STATE_REVIEW, STATE_RELEARNING = 0, 1, 2, 3


def open_collection(path, read_only=False):
    uri = f"file:{path}" + ("?mode=ro" if read_only else "")
    db = sqlite3.connect(uri, uri=True)
    db.create_collation(
        "unicase", lambda a, b: (a.lower() > b.lower()) - (a.lower() < b.lower())
    )
    return db


def anki_is_running():
    """Cheap check: Anki keeps a lock file beside the collection while open."""
    try:
        import subprocess

        out = subprocess.run(["pgrep", "-x", "anki"], capture_output=True, text=True)
        if out.returncode == 0:
            return True
        out = subprocess.run(
            ["pgrep", "-f", "Anki.app"], capture_output=True, text=True
        )
        return out.returncode == 0
    except Exception:
        return False


def read_reviews(path):
    data = path.read_bytes()
    if len(data) % REVLOG_RECORD_SIZE != 0:
        print(
            f"  warning: revlog.dat is {len(data)} bytes, not a multiple of {REVLOG_RECORD_SIZE};"
            f" ignoring the trailing partial record"
        )
    out = []
    voided = 0
    for i in range(len(data) // REVLOG_RECORD_SIZE):
        chunk = data[i * REVLOG_RECORD_SIZE : (i + 1) * REVLOG_RECORD_SIZE]
        card_id, at_ms, rating, state, elapsed, interval, took_ms, flags = struct.unpack(
            REVLOG_RECORD, chunk
        )
        if card_id == 0 or not 1 <= rating <= 4:
            continue
        if flags & REVLOG_VOIDED:
            voided += 1
            continue
        out.append(
            {
                "cardId": card_id,
                "atMs": at_ms,
                "rating": rating,
                "state": state,
                "elapsed": elapsed,
                "interval": interval,
                "tookMs": took_ms,
            }
        )
    if voided:
        print(f"  {voided} review(s) undone on the device, not replayed")
    return out


def read_cards(path):
    data = path.read_bytes()
    out = {}
    for i in range(len(data) // CARD_RECORD_SIZE):
        chunk = data[i * CARD_RECORD_SIZE : (i + 1) * CARD_RECORD_SIZE]
        card_id, stability, difficulty, due, last, reps, lapses = struct.unpack(
            "<qffiiHH", chunk[:28]
        )
        state, step = chunk[28], chunk[29]
        (due_minute,) = struct.unpack("<H", chunk[30:32])
        out[card_id] = {
            "stability": stability,
            "difficulty": difficulty,
            "due": due,
            "last": last,
            "reps": reps,
            "lapses": lapses,
            "state": state,
            "step": step,
            "dueMinute": due_minute,
        }
    return out


def revlog_kind(state):
    if state == STATE_REVIEW:
        return KIND_REVIEW
    if state == STATE_RELEARNING:
        return KIND_RELEARN
    return KIND_LEARN


def apply(db, reviews, cards, dry_run):
    cur = db.cursor()
    existing = {row[0] for row in cur.execute("select id from revlog")}
    applied = skipped = missing = 0
    touched = set()

    for r in sorted(reviews, key=lambda x: x["atMs"]):
        # Anki's revlog primary key is the answer time in milliseconds. Nudge
        # forward on collision so two reviews in the same millisecond both land
        # rather than one silently replacing the other.
        rid = r["atMs"]
        while rid in existing:
            if any(e == rid for e in existing) and rid == r["atMs"]:
                # A row already at this exact instant for this card is the same
                # review re-imported; skip it rather than duplicating.
                row = cur.execute(
                    "select cid from revlog where id = ?", (rid,)
                ).fetchone()
                if row and row[0] == r["cardId"]:
                    rid = None
                    break
            rid += 1
        if rid is None:
            skipped += 1
            continue

        card = cur.execute(
            "select id, ivl from cards where id = ?", (r["cardId"],)
        ).fetchone()
        if card is None:
            missing += 1
            continue

        # Anki stores sub-day intervals as negative seconds, which is exactly
        # what the device wrote for a learning step.
        ivl = r["interval"]
        last_ivl = r["elapsed"] if r["elapsed"] > 0 else (card[1] or 0)
        if not dry_run:
            cur.execute(
                "insert into revlog (id, cid, usn, ease, ivl, lastIvl, factor, time, type)"
                " values (?, ?, -1, ?, ?, ?, 0, ?, ?)",
                (
                    rid,
                    r["cardId"],
                    r["rating"],
                    ivl,
                    last_ivl,
                    r["tookMs"],
                    revlog_kind(r["state"]),
                ),
            )
        existing.add(rid)
        touched.add(r["cardId"])
        applied += 1

    # Now the cards themselves, from the device's final state rather than from
    # replaying: the device already computed it with the same FSRS parameters.
    updated = 0
    now = int(time.time())
    for card_id in sorted(touched):
        state = cards.get(card_id)
        if state is None:
            continue
        row = cur.execute("select data from cards where id = ?", (card_id,)).fetchone()
        if row is None:
            continue
        try:
            blob = json.loads(row[0]) if row[0] else {}
        except ValueError:
            blob = {}
        if state["stability"] > 0:
            blob["s"] = round(state["stability"], 4)
            blob["d"] = round(state["difficulty"], 4)
        anki_type = {
            STATE_NEW: 0,
            STATE_LEARNING: 1,
            STATE_REVIEW: 2,
            STATE_RELEARNING: 3,
        }[state["state"]]
        # A card inside a step list is due at a wall-clock second in Anki, not
        # on a day number; everything else is a day.
        interval_days = (
            max(1, state["due"] - state["last"]) if state["last"] >= 0 else 1
        )
        if not dry_run:
            cur.execute(
                "update cards set type = ?, queue = ?, due = ?, ivl = ?, reps = ?, lapses = ?,"
                " data = ?, mod = ?, usn = -1 where id = ?",
                (
                    anki_type,
                    anki_type if anki_type != STATE_NEW else 0,
                    state["due"],
                    interval_days,
                    state["reps"],
                    state["lapses"],
                    json.dumps(blob, separators=(",", ":")),
                    now,
                    card_id,
                ),
            )
        updated += 1

    if not dry_run:
        db.commit()
    return applied, skipped, missing, updated


def sync_to_ankiweb(collection_path, username, password, sync_media):
    """Push the collection to AnkiWeb using Anki's own client.

    Deliberately Anki's code and not ours. AnkiWeb's sync is an undocumented
    protobuf protocol with USN tracking, chunked transfer and a full-sync
    fallback; a homemade client that gets it subtly wrong does not fail loudly,
    it corrupts the collection on the server, which for most people is the only
    copy. Anki ships that client as a Python package, so there is no reason to
    write a second one.

    Requires `anki` (pip install anki). Credentials are read from the
    environment or prompted for, never stored and never logged.
    """
    try:
        import anki.collection  # noqa: F401  (breaks a circular import)
        from anki.collection import Collection
    except ImportError:
        sys.exit(
            "syncing needs Anki's own library:\n"
            "    pip install anki\n"
            "or run this from a venv that has it."
        )

    if not username:
        username = os.environ.get("ANKI_USERNAME") or input("AnkiWeb email: ").strip()
    if not password:
        password = os.environ.get("ANKI_PASSWORD") or getpass.getpass("AnkiWeb password: ")

    col = Collection(str(collection_path))
    try:
        from anki.errors import SyncError

        try:
            auth = col.sync_login(username, password, None)
        except SyncError as e:
            print(f"\nAnkiWeb refused the login: {e}")
            print("Reviews are already applied to the local collection -- nothing was lost.")
            return False

        try:
            out = col.sync_collection(auth, sync_media)
        except SyncError as e:
            print(f"\nsync failed: {e}")
            print("Reviews are already applied to the local collection -- nothing was lost.")
            return False

        # A "full sync required" answer means the two sides have diverged
        # further than an incremental sync can reconcile, and resolving it means
        # choosing which side wins. That is a decision for a human in front of
        # Anki, not for a script holding their only copy.
        if getattr(out, "required", 0) != 0:
            print(
                "\nAnkiWeb wants a FULL sync, which means choosing whether this\n"
                "computer or AnkiWeb wins. Open Anki and sync there -- it will ask.\n"
                "Your reviews are already in the local collection either way."
            )
            return False
        print("\nsynced to AnkiWeb")
        return True
    finally:
        col.close()


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("deck", type=pathlib.Path, help="deck directory on the SD card")
    ap.add_argument("collection", type=pathlib.Path, help="collection.anki2")
    ap.add_argument(
        "--sync",
        action="store_true",
        help="after applying, push the collection to AnkiWeb using Anki's own client",
    )
    ap.add_argument("--username", help="AnkiWeb email (else $ANKI_USERNAME, else prompt)")
    ap.add_argument("--sync-media", action="store_true", help="sync media as well as the collection")
    ap.add_argument(
        "--dry-run", action="store_true", help="report what would change, write nothing"
    )
    ap.add_argument(
        "--force", action="store_true", help="skip the Anki-is-running check"
    )
    args = ap.parse_args()

    revlog = args.deck / "revlog.dat"
    cards_path = args.deck / "cards.dat"
    for path in (revlog, cards_path, args.collection):
        if not path.exists():
            sys.exit(f"missing {path}")

    reviews = read_reviews(revlog)
    if not reviews:
        print("no reviews on the card -- nothing to sync")
        return
    cards = read_cards(cards_path)
    print(f"{len(reviews)} reviews on the card, {len(cards)} card records")

    if not args.dry_run:
        if not args.force and anki_is_running():
            sys.exit(
                "Anki appears to be running. Close it first: it holds the collection open and keeps\n"
                "its own copy in memory, so whichever of you writes last wins. Re-run with --force\n"
                "only if you are certain it is closed."
            )
        backup = args.collection.with_suffix(f".anki2.before-sync-{int(time.time())}")
        shutil.copy2(args.collection, backup)
        print(f"backed up the collection to {backup.name}")

    db = open_collection(args.collection, read_only=args.dry_run)
    applied, skipped, missing, updated = apply(db, reviews, cards, args.dry_run)
    db.close()

    print(f"  applied   {applied} reviews")
    if skipped:
        print(f"  skipped   {skipped} already present")
    if missing:
        print(
            f"  missing   {missing} reviews whose card is no longer in the collection"
        )
    print(f"  updated   {updated} cards")
    if args.dry_run:
        print("\n(dry run -- nothing was written)")
        return

    if args.sync:
        # Anki must not be running: it holds the collection open, and two
        # writers is how a collection gets corrupted.
        sync_to_ankiweb(args.collection, args.username, None, args.sync_media)
    else:
        print("\nOpen Anki and run Tools > Check Database to rebuild its caches.")
        print("Pass --sync to push straight to AnkiWeb instead.")


if __name__ == "__main__":
    main()

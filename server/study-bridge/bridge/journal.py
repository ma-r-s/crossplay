"""The review journal: the one store a full sync cannot destroy.

A device review is acked to the device ONLY once it is durable here, and it
leaves ONLY after AnkiWeb has confirmed a push. Between those two moments the
mirror collection is expendable: a forced full download replaces it, and
`reset_applied()` re-queues everything not yet pushed for re-application.
That is the whole reason this is a separate sqlite file and not rows in the
mirror.

Row shape matches deck_to_anki.py's review dicts exactly (cardId, atMs,
rating, state, elapsed, interval, tookMs), so unapplied() feeds its apply()
with no translation layer to get wrong. Idempotency is the primary key
(card_id, at_ms) -- the same key Anki's revlog uses and the one apply()
replays on -- so a retried POST or re-posted tail can never double-apply.
"""

import pathlib
import sqlite3
import time

FIELDS = ("cardId", "atMs", "rating", "state", "elapsed", "interval", "tookMs")


class Journal:
    def __init__(self, path: pathlib.Path):
        self.db = sqlite3.connect(str(path))
        self.db.execute(
            """create table if not exists reviews (
                 card_id   integer not null,
                 at_ms     integer not null,
                 rating    integer not null,
                 state     integer not null,
                 elapsed   integer not null,
                 interval  integer not null,
                 took_ms   integer not null,
                 ingested_at integer not null,
                 applied   integer not null default 0,
                 primary key (card_id, at_ms)
               )"""
        )
        # WAL so the nightly snapshot (sqlite backup API) never blocks ingest.
        self.db.execute("pragma journal_mode=wal")
        self.db.commit()

    def ingest(self, reviews: list[dict]) -> int:
        """Insert new reviews (already parsed and de-voided by wire.py);
        returns how many were new. The commit happens here -- the caller may
        ack the device the moment this returns."""
        new = 0
        now = int(time.time())
        for r in reviews:
            cur = self.db.execute(
                "insert or ignore into reviews"
                " (card_id, at_ms, rating, state, elapsed, interval, took_ms, ingested_at)"
                " values (?, ?, ?, ?, ?, ?, ?, ?)",
                (
                    r["cardId"],
                    r["atMs"],
                    r["rating"],
                    r["state"],
                    r["elapsed"],
                    r["interval"],
                    r["tookMs"],
                    now,
                ),
            )
            new += cur.rowcount
        self.db.commit()
        return new

    def unapplied(self) -> list[dict]:
        rows = self.db.execute(
            "select card_id, at_ms, rating, state, elapsed, interval, took_ms"
            " from reviews where applied = 0 order by at_ms"
        ).fetchall()
        return [dict(zip(FIELDS, row)) for row in rows]

    def mark_applied(self, reviews: list[dict]):
        self.db.executemany(
            "update reviews set applied = 1 where card_id = ? and at_ms = ?",
            [(r["cardId"], r["atMs"]) for r in reviews],
        )
        self.db.commit()

    def clear_pushed(self):
        """After AnkiWeb confirmed a push: applied reviews are upstream and
        the journal's duty for them ends."""
        self.db.execute("delete from reviews where applied = 1")
        self.db.commit()

    def reset_applied(self):
        """After the mirror was replaced (forced full download): everything
        not yet pushed must be applied again to the new mirror."""
        self.db.execute("update reviews set applied = 0")
        self.db.commit()

    def counts(self) -> dict:
        (total,) = self.db.execute("select count(*) from reviews").fetchone()
        (applied,) = self.db.execute(
            "select count(*) from reviews where applied = 1"
        ).fetchone()
        return {"held": total, "applied": applied}

    def close(self):
        self.db.close()

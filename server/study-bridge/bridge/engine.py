"""The sync cycle: journal -> mirror -> AnkiWeb -> journal, in that order.

Everything here is blocking (pylib is synchronous); jobs.py runs it in a
thread under the user's lock. The rules, from the plan and enforced here:

- Reviews enter the mirror only from the journal, only through
  deck_to_anki.apply() (the same code the CLI has always used), only via
  pylib's own db handle -- never a second sqlite connection while the
  Collection is open (the Rust backend caches pages).
- A full sync demanded by the server is answered with DOWNLOAD, always:
  the user's AnkiWeb copy is the truth and replacing the mirror costs
  nothing, because the journal holds everything not yet pushed and is
  re-applied afterwards. UPLOAD is never chosen automatically -- if
  download is not on offer, the account freezes into needs_attention and a
  human decides.
- The journal drops reviews only after the SECOND sync (the push) came back
  clean. A crash anywhere re-runs as a safe retry.
"""

import logging

import deck_to_anki as d2a
from anki.collection import Collection

log = logging.getLogger("bridge.engine")

# SyncOutput.required values (anki 26.8.1, rslib ChangesRequired):
# 0 none, 1 normal, 2 full sync either direction, 3 full download,
# 4 full upload. Verified empirically in tests/spike_roundtrip.py: an
# empty server against a populated client answered 4; a fresh client
# against a populated server answered 3.
REQUIRED_NONE = 0
REQUIRED_FULL_EITHER = 2
REQUIRED_FULL_DOWNLOAD = 3
REQUIRED_FULL_UPLOAD = 4


class Frozen(Exception):
    """The cycle refused to continue without a human. The account status
    is already needs_attention; the message is user-facing."""


class _CursorShim:
    """deck_to_anki.apply() speaks sqlite3 cursor idiom; pylib's DBProxy
    speaks execute/all/first. Bridge the two without touching either."""

    def __init__(self, db):
        self._db = db
        self._rows = None

    def execute(self, sql, params=()):
        params = tuple(params) if not isinstance(params, tuple) else params
        if sql.lstrip().lower().startswith("select"):
            self._rows = self._db.all(sql, *params)
        else:
            self._db.execute(sql, *params)
            self._rows = None
        return self

    def fetchone(self):
        return self._rows[0] if self._rows else None

    def __iter__(self):
        return iter(self._rows or [])


class _DbShim:
    def __init__(self, col):
        self._col = col

    def cursor(self):
        return _CursorShim(self._col.db)

    def commit(self):
        # pylib commits through the backend on save points; an explicit
        # commit here is a no-op by design -- the set_config bump below is
        # what persists and marks the collection modified.
        pass


def _sync_media(col, auth, summary, timeout_s: int = 7200):
    """Media rides separately in pylib (a background task inside the Rust
    backend); start it and wait it out. The deck builds need the media dir
    (CJK font TTFs, sentence images), so the cycle is not done without it.
    A first sync of a media-heavy collection is the slow path the job model
    exists for; the timeout is a backstop, not an expectation."""
    import time as _time

    col.sync_media(auth)
    deadline = _time.time() + timeout_s
    while _time.time() < deadline:
        status = col.media_sync_status()
        if not status.active:
            summary["media"] = True
            return
        _time.sleep(2)
    log.warning("media sync still running after %ss; carrying on", timeout_s)
    summary["media"] = False


def sync_cycle(store, journal, hostkey: str, endpoint: str, device_cards: dict) -> dict:
    """One full cycle. device_cards: {card_id: device state} from the posted
    cards.dat, used by apply() to set final card state for touched cards.
    Returns a summary dict for the job status. Raises Frozen when a human
    is needed."""
    from anki.errors import SyncError, SyncErrorKind
    from anki.sync import SyncAuth

    col = Collection(str(store.collection_path))
    summary = {"applied": 0, "skipped": 0, "missing": 0, "updated": 0, "pulled": False}
    try:
        auth = SyncAuth(hkey=hostkey, endpoint=endpoint)

        out = col.sync_collection(auth, sync_media=False)
        if out.new_endpoint:
            auth.endpoint = out.new_endpoint
        if out.required in (REQUIRED_FULL_DOWNLOAD, REQUIRED_FULL_EITHER):
            # The server's copy is the truth; ours is expendable (the
            # journal is not part of the mirror). Download, then re-apply.
            col.full_upload_or_download(
                auth=auth, server_usn=out.server_media_usn, upload=False
            )
            journal.reset_applied()
            summary["pulled"] = True
        elif out.required == REQUIRED_FULL_UPLOAD:
            raise Frozen(
                "AnkiWeb wants this account's collection replaced by an"
                " upload, and the bridge never makes that choice. Sync from"
                " the Anki app once, then try again."
            )

        pending = journal.unapplied()
        if pending:
            applied, skipped, missing, updated = d2a.apply(
                _DbShim(col), pending, device_cards, dry_run=False
            )
            summary.update(
                {
                    "applied": applied,
                    "skipped": skipped,
                    "missing": missing,
                    "updated": updated,
                }
            )
            # The gap the round-trip spike proved real: rows alone do not
            # move the collection mod time, and the meta exchange starts
            # there. set_config writes through the backend and bumps it.
            col.set_config("bridgeLastApply", int(pending[-1]["atMs"]))
            journal.mark_applied(pending)

        out2 = col.sync_collection(auth, sync_media=False)
        _sync_media(col, auth, summary)
        if out2.required != REQUIRED_NONE:
            # The push immediately demanded another full sync: something
            # changed under us mid-cycle. Keep the journal (nothing is
            # confirmed pushed) and let the next cycle resolve it; if it is
            # an upload demand, freeze like above.
            if out2.required == REQUIRED_FULL_UPLOAD:
                raise Frozen(
                    "AnkiWeb wants this account's collection replaced by an"
                    " upload, and the bridge never makes that choice. Sync"
                    " from the Anki app once, then try again."
                )
            log.warning("push answered required=%s; journal kept", out2.required)
        else:
            journal.clear_pushed()
        return summary
    except SyncError as e:
        # An expired or revoked AnkiWeb session is not an outage, and the
        # reader cannot tell the two apart from a generic failure: it kept
        # advising "try again in a few minutes" for a condition no amount of
        # waiting fixes. Freezing carries this sentence to the screen instead.
        if getattr(e, "kind", None) == SyncErrorKind.AUTH:
            raise Frozen(
                "The bridge is signed out of AnkiWeb. Sign in again on the"
                " pairing page on a computer, then sync from here."
            ) from e
        raise
    finally:
        col.close()

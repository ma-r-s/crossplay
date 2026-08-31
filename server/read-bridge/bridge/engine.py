"""The sync cycle: intents out, delta in, text converted, summary back.

Blocking; runs in a thread under the per-user lock (jobs.py).

The order is the design. Intents run BEFORE the list, or the article the user
just archived on the reader comes straight back down in the same response
that was supposed to remove it. Everything after that is Instapaper's own
delta engine doing the work: the device's index composes the `have` string,
and one call answers what is new, what changed, what was deleted, and pushes
the reader's progress up in the same breath.

What this deliberately does not do: keep a mirror. There is no second copy of
the reading list on this box to fall out of step with the account -- only a
cache of converted text, which is derived and can be thrown away at any time.
"""

import hashlib
import json
import logging
import shutil
import time

from . import article as art
from . import instapaper as ip
from .jobs import Refused

log = logging.getLogger("bridge.engine")

# How many articles get their text fetched and converted in one cycle. Each
# is a round trip plus a parse, and a first sync on a fat unread queue would
# otherwise run for minutes while the reader shows a spinner. The rest are
# reported as withheld and arrive on the next sync, which the device says out
# loud rather than looking like it stopped early.
MAX_FETCH_PER_SYNC = 25

# The device's own library cap (InstapaperLibrary::kMaxArticles). Sending more
# than the reader can hold would have it drop rows silently, and then re-ask
# for them forever because its `have` list never mentions them.
MAX_ARTICLES = 120

# A progress timestamp this far in the future is a device with a wrong clock,
# not a reading session. Passing it on would pin the account's progress to a
# date nothing can beat, so the phone could never move it again.
MAX_CLOCK_SKEW_S = 86400


def _sha(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()[:16]


def _meta_path(st, bid):
    return st.article_dir(bid) / "meta.json"


def _load_meta(st, bid) -> dict:
    p = _meta_path(st, bid)
    if not p.exists():
        return {}
    try:
        return json.loads(p.read_text())
    except ValueError:
        return {}


def _keep_only(st, bid, keep_path):
    """One text file per article. The hash changes on a title edit or a
    progress push, so without this every read would leave another copy."""
    for f in st.article_dir(bid).glob("*.txt"):
        if f != keep_path:
            f.unlink(missing_ok=True)


def sanitize_have(entries, now: int | None = None) -> list[dict]:
    """The device's posted index, trimmed to what may be sent to Instapaper.

    Separate from compose_have so the clock rule is testable without a
    server: a future timestamp is dropped back to no-progress rather than
    dropping the whole entry, because the id still has to be in `have` or
    Instapaper re-sends the article on every sync."""
    now = int(time.time()) if now is None else now
    out = []
    for e in entries[:MAX_ARTICLES]:
        try:
            bid = int(e["id"])
        except (KeyError, TypeError, ValueError):
            continue
        at = int(e.get("progressAt") or 0)
        progress = float(e.get("progress") or 0.0)
        if at > now + MAX_CLOCK_SKEW_S:
            log.info("dropping a progress stamp %ss in the future", at - now)
            at, progress = 0, 0.0
        out.append(
            {
                "id": bid,
                "hash": str(e.get("hash") or ""),
                "progress": progress,
                "progressAt": at,
            }
        )
    return out


def _fetch_text(client, st, bm, bid, bhash) -> tuple[str, bool]:
    """-> (text, renderable). Reuses cached text when Instapaper's hash moved
    for a reason that cannot have changed the words.

    Instapaper computes that hash from the URL, title, description and
    reading progress -- explicitly not from the content. So a hash change
    with the same URL means metadata moved, and re-fetching would spend a
    round trip to receive the same article. The corollary, stated plainly
    because it is a real limitation: an article whose CONTENT changes at a
    stable URL is not re-fetched, because nothing in the API says it did."""
    path = st.article_path(bid, bhash)
    meta = _load_meta(st, bid)
    if path.exists():
        return path.read_text(encoding="utf-8"), bool(meta.get("renderable", True))

    url = str(bm.get("url") or "")
    previous = st.article_path(bid, meta.get("hash", "")) if meta.get("hash") else None
    if meta and url and meta.get("url") == url and previous is not None and previous.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(previous, path)
        return path.read_text(encoding="utf-8"), bool(meta.get("renderable", True))

    markup = client.get_text(bid)
    converted = art.convert(markup)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(converted["text"], encoding="utf-8")
    _meta_path(st, bid).write_text(
        json.dumps(
            {
                "hash": bhash,
                "url": url,
                "renderable": converted["renderable"],
                "words": converted["words"],
            }
        )
    )
    return converted["text"], converted["renderable"]


def sync_cycle(st, token: str, secret: str, have: list[dict], archive_ids: list[int]) -> dict:
    """The whole cycle. Returns the summary the device polls for."""
    client = ip.Instapaper(token, secret)

    # 1. Intents first, so nothing archived here comes back in step 2.
    archived, archive_failed = [], []
    for bid in archive_ids[:MAX_ARTICLES]:
        try:
            client.archive(int(bid))
            archived.append(int(bid))
        except ip.ApiError as e:
            # 1241 is "no such bookmark", which for an archive intent means
            # the job is already done by some other client. Counting it as
            # archived is what stops the device retrying it forever.
            if e.code == 1241:
                archived.append(int(bid))
            else:
                archive_failed.append({"id": int(bid), "why": str(e)})
                log.info("archive of %s refused: %s", bid, e)
        except (TypeError, ValueError):
            continue

    clean = sanitize_have(have)
    try:
        data = client.bookmarks_list(ip.compose_have(clean))
    except ip.ApiError as e:
        raise Refused(str(e)) from e

    bookmarks = [b for b in data.get("bookmarks", []) if isinstance(b, dict)]
    delete_ids = [int(i) for i in data.get("delete_ids", []) if str(i).lstrip("-").isdigit()]

    # The limit guard, and it needs more care than it looks like it does.
    #
    # delete_ids means "in your `have` and not in the first `limit` of this
    # folder", so a listing that filled its window says nothing about what
    # lies past it and its deletions are not deletions. The obvious check --
    # did we get `limit` bookmarks back? -- CANNOT see that, because `have`
    # suppresses the unchanged ones: a window of 500 that the device already
    # holds in full comes back with zero bookmarks in it.
    #
    # So count the window instead, from below. Every id we sent that did not
    # come back as deleted was inside it, and so was every bookmark returned
    # that we had not sent. That sum is a lower bound on the window's size,
    # and if the bound alone reaches the limit then the window was full.
    #
    # Suppressing wrongly costs a stale row until the next sync. Trusting
    # wrongly wipes articles off a reader that are still in the account.
    have_ids = {e["id"] for e in clean}
    returned_ids = {int(b["bookmark_id"]) for b in bookmarks if str(b.get("bookmark_id", "")).isdigit()}
    window_floor = (len(have_ids) - len(delete_ids)) + len(returned_ids - have_ids)
    if delete_ids and window_floor >= ip.LIST_LIMIT:
        log.warning(
            "listing filled its %d window; suppressing %d delete_ids",
            ip.LIST_LIMIT,
            len(delete_ids),
        )
        delete_ids = []

    articles, failed = [], list(archive_failed)
    fetched = 0
    withheld = 0
    for bm in bookmarks:
        try:
            bid = int(bm.get("bookmark_id"))
        except (TypeError, ValueError):
            continue
        bhash = str(bm.get("hash") or "")
        cached = st.article_path(bid, bhash).exists()
        if not cached and fetched >= MAX_FETCH_PER_SYNC:
            withheld += 1
            continue
        try:
            text, renderable = _fetch_text(client, st, bm, bid, bhash)
        except (ip.ApiError, art.Unconvertible) as e:
            failed.append({"id": bid, "why": str(e)})
            continue
        except Exception:
            log.exception("converting %s failed", bid)
            failed.append({"id": bid, "why": "This one could not be prepared for the reader."})
            continue
        if not cached:
            fetched += 1
        _keep_only(st, bid, st.article_path(bid, bhash))
        url = str(bm.get("url") or "")
        words = art.word_count(text)
        articles.append(
            {
                "id": bid,
                "hash": bhash,
                "title": art.clean_title(bm.get("title")) or art.domain_of(url) or "Untitled",
                "url": url,
                "domain": art.domain_of(url),
                "savedAt": int(bm.get("time") or 0),
                "progress": float(bm.get("progress") or 0.0),
                "progressAt": int(bm.get("progress_timestamp") or 0),
                "bytes": len(text.encode("utf-8")),
                "words": words,
                "minutes": art.reading_minutes(words),
                "renderable": renderable,
                "sha": _sha(text),
            }
        )

    for bid in delete_ids:
        shutil.rmtree(st.article_dir(bid), ignore_errors=True)

    return {
        "articles": articles,
        "deleteIds": delete_ids,
        "archived": archived,
        "failed": failed,
        "withheld": withheld,
        "syncedAt": int(time.time()),
    }

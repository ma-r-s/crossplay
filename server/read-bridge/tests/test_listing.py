#!/usr/bin/env python3
"""What Instapaper actually answers to bookmarks/list, held as a fixture.

The first real sync of a real account was refused by this bridge with the
sentence "Instapaper answered the article list in a shape this bridge does not
know", and it was right: the client required an object with a "bookmarks" key,
which the live API has never sent. The fake API agreed with the client, because
both were written from the same paragraph of the same inaccurate documentation,
so no suite in this repo could see it.

So the fixture below is not derived from the docs and not derived from the
fake. It is the SHAPE recorded off the live API on 2026-09-03 -- every key,
every value type, the element order and the string-vs-list detail that matters
-- with every value replaced by a redacted stand-in. There is nothing here from
anybody's reading list.

The two things it pins:

  * the envelope is a JSON ARRAY of objects discriminated by "type", not an
    object with a "bookmarks" key;
  * delete_ids rides in the meta element as a COMMA-SEPARATED STRING. That is
    the dangerous half. A fix that only stopped demanding a dict would iterate
    that string, and every character of "424242424,424242425" passes an
    isdigit() test -- so the engine would have been handed the ids 4, 2, 4, 2,
    ... and would have deleted cached articles that Instapaper never named.

Run: .venv/bin/python tests/test_listing.py
"""

import logging
import os
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

from bridge import instapaper as ip  # noqa: E402

checks = 0
failures = 0


def ok(cond, label):
    global checks, failures
    checks += 1
    if not cond:
        failures += 1
        print(f"FAIL {label}")


# Values that must never appear in a log line. They stand in for the real
# ones -- a title, a URL, an email address, a token -- and the privacy checks
# below search describe_shape's output for them.
SECRET_TITLE = "SECRET-ARTICLE-TITLE"
SECRET_URL = "https://secret.example.invalid/an-article-he-is-reading"
SECRET_USER = "secret-person@example.invalid"
SECRETS = (SECRET_TITLE, SECRET_URL, SECRET_USER)


def bookmark(bid: int, bhash: str) -> dict:
    """Every key the live API sent, with the same types and redacted values."""
    return {
        "type": "bookmark",
        "bookmark_id": bid,
        "hash": bhash,
        "url": SECRET_URL,
        "title": SECRET_TITLE,
        "description": "",
        "time": 1756800000,
        "progress": 0.0,
        "progress_timestamp": 0,
        "starred": "0",
        "private_source": "",
        "tags": [],
    }


USER_ELEMENT = {
    "type": "user",
    "user_id": 1234567,
    "username": SECRET_USER,
    "subscription_is_active": "1",
}

# 28 elements on the wire: meta, user, and 26 bookmarks. Two is enough here.
LIVE_SHAPE = [{"type": "meta"}, USER_ELEMENT, bookmark(1001, "abcdef12"), bookmark(1002, "abcdef34")]

# The same, as it comes back when `have` named ids the account no longer holds.
LIVE_SHAPE_WITH_DELETES = [
    {"type": "meta", "delete_ids": "424242424,424242425"},
    USER_ELEMENT,
    bookmark(1001, "abcdef12"),
]

# And what a fully up-to-date reader gets: `have` suppressed every bookmark, so
# only meta and user come back. This is a NORMAL answer and refusing it would
# break the steady state, which is the state the device is in almost always.
LIVE_SHAPE_ALL_SUPPRESSED = [{"type": "meta"}, USER_ELEMENT]


class Capture(logging.Handler):
    """Everything the bridge logged, so the privacy rule can be asserted on
    it rather than trusted."""

    def __init__(self):
        super().__init__()
        self.lines = []

    def emit(self, record):
        # getMessage() already interpolates record.args; doing it again is a
        # TypeError the moment a message carries a %d.
        self.lines.append(record.getMessage())


def main():
    log = logging.getLogger("bridge.instapaper")
    cap = Capture()
    log.addHandler(cap)
    log.setLevel(logging.DEBUG)

    # ------------------------------------------------ the live shape parses
    out = ip.normalise_listing(LIVE_SHAPE)
    ok(len(out["bookmarks"]) == 2, "the live array shape yields its two bookmarks")
    ok(
        [b["bookmark_id"] for b in out["bookmarks"]] == [1001, 1002],
        "bookmarks come back in order, meta and user are not among them",
    )
    ok(out["user"].get("user_id") == 1234567, "the user element is picked out")
    ok(out["delete_ids"] == [], "no meta delete_ids means no deletions")

    # ---------------------------------------- delete_ids: the character trap
    out = ip.normalise_listing(LIVE_SHAPE_WITH_DELETES)
    ok(
        out["delete_ids"] == [424242424, 424242425],
        "a comma-separated delete_ids string parses into two WHOLE ids",
    )
    ok(
        len(out["delete_ids"]) == 2,
        "and into two of them -- not the 19 characters of the string",
    )
    ok(all(i > 999 for i in out["delete_ids"]), "no single-digit id came out of the string")
    ok(len(out["bookmarks"]) == 1, "the bookmark alongside the deletions still parses")

    # ------------------------------------------- the steady state is not an error
    out = ip.normalise_listing(LIVE_SHAPE_ALL_SUPPRESSED)
    ok(out["bookmarks"] == [], "a fully suppressed listing yields no bookmarks")
    ok(out["delete_ids"] == [], "and, above all, no deletions")

    # ------------------------------------------------ the documented envelope
    # Liberal means BOTH: if Instapaper ever sends what its docs describe,
    # this must keep working rather than swap one brittleness for another.
    out = ip.normalise_listing(
        {
            "user": USER_ELEMENT,
            "bookmarks": [bookmark(1001, "abcdef12")],
            "highlights": [],
            "delete_ids": [7, "8"],
        }
    )
    ok(len(out["bookmarks"]) == 1, "the object envelope still parses its bookmarks")
    ok(out["delete_ids"] == [7, 8], "and its delete_ids, as ints or as strings")

    # ------------------------------------------------------ liberal, but not credulous
    out = ip.normalise_listing([{"bookmark_id": 55, "hash": "aa", "url": ""}])
    ok(len(out["bookmarks"]) == 1, "an element with a bookmark_id but no type is a bookmark")

    out = ip.normalise_listing([{"type": "meta", "delete_ids": "12,not-an-id,34"}])
    ok(out["delete_ids"] == [12, 34], "a delete id that is not a whole integer is dropped")

    out = ip.normalise_listing([{"type": "meta", "delete_ids": ""}])
    ok(out["delete_ids"] == [], "an empty delete_ids string deletes nothing")

    out = ip.normalise_listing([])
    ok(out["bookmarks"] == [] and out["delete_ids"] == [], "an empty array is an empty listing")

    # --------------------------------------------------- and it still refuses
    for body, label in (
        ("a bare string", "a string body"),
        (12, "a number body"),
        ({"error": "nope"}, "an object with no bookmarks key"),
        (["a", "b"], "an array of no objects at all"),
    ):
        cap.lines.clear()
        try:
            ip.normalise_listing(body)
            ok(False, f"{label} is refused")
        except ip.ApiError:
            ok(True, f"{label} is refused")
        # THE SECOND BUG: a refusal that records nothing cannot be diagnosed.
        ok(any("shape was" in line for line in cap.lines), f"{label} logs its shape")

    # ------------------------------------- the refusal log says enough to act on
    cap.lines.clear()
    try:
        ip.normalise_listing({"data": {"items": []}, "count": 3, "cursor": "abc"})
    except ip.ApiError:
        pass
    line = " ".join(cap.lines)
    ok("dict{3}" in line, "the log names the top-level type")
    for key in ("data", "count", "cursor"):
        ok(key in line, f"the log names the key {key!r}")
    ok("int" in line and "str[3]" in line and "dict{1}" in line, "the log names each value's type")

    # A wrapper object is the shape where the element keys matter MOST -- the
    # payload is one level down, and "items: list[2]" alone says nothing about
    # what is in it. One level of descent, so the log names the keys of what a
    # nested list and a nested object hold.
    cap.lines.clear()
    try:
        ip.normalise_listing({"results": [bookmark(1, "aa"), bookmark(2, "bb")], "page": {"next": "x"}})
    except ip.ApiError:
        pass
    line = " ".join(cap.lines)
    ok("results: list[2]" in line, "a nested list is named with its length")
    ok("bookmark_id: int" in line, "AND the keys of what it holds")
    ok("next: str[1]" in line, "a nested object's keys too")

    described = ip.describe_shape(LIVE_SHAPE)
    ok(described.startswith("list[4]"), "a list is described with its length")
    ok("bookmark_id: int" in described, "and with the field names of its elements")
    ok("type='bookmark'" in described, "the type discriminator is quoted -- it is the answer")
    ok("progress: float" in described, "and every value's type")

    # -------------------------------------------------------------- privacy
    # This runs against a real person's reading list. A log that leaks is
    # worse than no log, so the redaction is asserted, not assumed.
    for blob in (LIVE_SHAPE, LIVE_SHAPE_WITH_DELETES, {"bookmarks": LIVE_SHAPE}):
        text = ip.describe_shape(blob)
        for secret in SECRETS:
            ok(secret not in text, f"describe_shape does not print {secret[:18]!r}...")
    ok(
        "424242424" not in ip.describe_shape(LIVE_SHAPE_WITH_DELETES),
        "not even the delete_ids string's contents",
    )
    ok(
        str(USER_ELEMENT["user_id"]) not in ip.describe_shape([USER_ELEMENT]),
        "and not the user id",
    )

    cap.lines.clear()
    try:
        ip.normalise_listing({"unexpected": [bookmark(1, "aa")], "who": SECRET_USER})
    except ip.ApiError:
        pass
    for secret in SECRETS:
        ok(all(secret not in line for line in cap.lines), f"the REFUSAL log omits {secret[:18]!r}...")

    # ------------------------------------------- the same blindness, twin path
    # get_text's last refusal had the identical bug: a non-200 with no error
    # element, reported to the user and recorded nowhere. Reachable only by
    # standing in for the transport, which is why no suite had been through it.
    os.environ.setdefault("READ_CONSUMER_KEY", "test-key")
    os.environ.setdefault("READ_CONSUMER_SECRET", "test-secret")

    class FakeResponse:
        status_code = 502
        content = b'{"unexpected": [1, 2, 3], "note": "html error page"}'
        text = ""

        def json(self):
            return {"unexpected": [1, 2, 3], "note": "html error page"}

    client = ip.Instapaper("t", "s")
    client._post = lambda *a, **k: FakeResponse()
    cap.lines.clear()
    try:
        client.get_text(1)
        ok(False, "get_text refuses a 502 with no error element")
    except ip.ApiError:
        ok(True, "get_text refuses a 502 with no error element")
    line = " ".join(cap.lines)
    ok("502" in line, "and the log names the status it got")
    ok("unexpected" in line and "note" in line, "and the keys it got")
    ok("html error page" not in line, "and not the values")

    print(f"{checks} checks, {failures} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

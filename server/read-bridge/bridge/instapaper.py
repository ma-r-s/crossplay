"""The Instapaper Full API client: OAuth 1.0a signing and the five calls.

Hand-rolled signing rather than a dependency. It is forty lines of HMAC over
a canonical string, it is the only place in this service that must be exactly
right about percent-encoding, and a vendored copy is easier to reason about
than an oauth library's opinions about which encoding layer owns what.

The API, as documented at instapaper.com/api/full and as used here:

  POST /api/1/oauth/access_token   xAuth; the ONLY way to get a token
  POST /api/1/bookmarks/list       the delta engine; see `have` below
  POST /api/1/bookmarks/get_text   text/html, not the JSON envelope
  POST /api/1/bookmarks/archive    idempotent
  POST /api/1/account/verify_credentials

WHAT bookmarks/list ACTUALLY ANSWERS, observed against the live API on
2026-09-03 rather than read off the documentation, because the documentation
is what produced the bug this paragraph exists to close:

    [ {"type": "meta"},                      # or with "delete_ids"
      {"type": "user", "user_id": int, "username": str,
       "subscription_is_active": str},
      {"type": "bookmark", "bookmark_id": int, "hash": str, "url": str,
       "title": str, "description": str, "time": int, "progress": float,
       "progress_timestamp": int, "starred": str, "private_source": str,
       "tags": list}, ... ]

A JSON ARRAY of typed objects. Not an object with a "bookmarks" key -- that
is what this file used to require, and the first real sync was refused by it.
There is no top-level "highlights" key either.

And the detail that matters more than the envelope: **delete_ids arrives
inside the meta element as a COMMA-SEPARATED STRING**, not a list. Iterating
that string yields characters, and every character of "424242424,424242425"
passes an isdigit() test -- so a reader that merely stopped demanding a dict
would have handed the engine the bookmark ids 4, 2, 4, 2 ... and deleted
cached articles nobody named. parse_delete_ids() below is the whole answer to
that, and it is why the envelope fix is not a one-line fix.

Deliberately NOT wrapped, and this is a security property rather than an
oversight: bookmarks/delete. The token this service holds can permanently
destroy a user's reading list, and no code path here can ask it to.

Every call is POST, every parameter goes in the body, and the oauth_* set
goes in the Authorization header. Only HMAC-SHA1 is supported by the server.
"""

import base64
import hashlib
import hmac
import logging
import os
import secrets
import time
import urllib.parse

import httpx

log = logging.getLogger("bridge.instapaper")

# Overridable so the suites can point the whole service at
# tests/fake_instapaper.py without a consumer key or a network.
BASE = os.environ.get("READ_INSTAPAPER_BASE", "https://www.instapaper.com").rstrip("/")

# The documented maximum, and load-bearing rather than greedy: bookmarks named
# in `have` that fall outside the limit come back as deletions, so asking for
# fewer than the account holds turns real articles into delete_ids. See
# docs/apps/instapaper-plan.md.
LIST_LIMIT = 500

TIMEOUT = httpx.Timeout(20.0, connect=10.0)


def consumer() -> tuple[str, str]:
    key = os.environ.get("READ_CONSUMER_KEY", "")
    secret = os.environ.get("READ_CONSUMER_SECRET", "")
    if not key or not secret:
        # Refusing to serve is the safe failure: an unsigned request would be
        # rejected by Instapaper anyway, and the resulting 401 would be read
        # as "your password is wrong" by everyone who saw it.
        raise RuntimeError(
            "READ_CONSUMER_KEY/READ_CONSUMER_SECRET are unset; this bridge has"
            " no Instapaper application credentials"
        )
    return key, secret


def _quote(value) -> str:
    # RFC 5849's percent-encoding: unreserved is ALPHA / DIGIT / "-" / "." /
    # "_" / "~" and nothing else.
    return urllib.parse.quote(str(value), safe="~")


def base_string(method: str, url: str, params) -> str:
    """RFC 5849 section 3.4.1.1: METHOD & encoded-URI & encoded-parameters.

    Takes a dict or an iterable of pairs. Pairs matter for the RFC's own
    worked example, which repeats a key; Instapaper never does, but a
    signature implementation tested only against inputs it cannot get wrong
    is not tested (tests/test_oauth.py holds it to the RFC's vector).

    Sorting is by the ENCODED key and then the encoded value, which is the
    spec's rule and not the same as sorting the raw strings."""
    items = params.items() if hasattr(params, "items") else params
    encoded = sorted((_quote(k), _quote(v)) for k, v in items)
    joined = "&".join(f"{k}={v}" for k, v in encoded)
    return "&".join([method.upper(), _quote(url), _quote(joined)])


def signature(method: str, url: str, params, consumer_secret: str, token_secret: str) -> str:
    """The HMAC-SHA1 over the base string, base64'd.

    `params` must already contain every oauth_* field except the signature
    itself AND every body parameter: the spec signs them together, and
    omitting the body is the classic way to produce a signature that
    validates locally and is refused by the server."""
    key = f"{_quote(consumer_secret)}&{_quote(token_secret)}"
    digest = hmac.new(key.encode(), base_string(method, url, params).encode(), hashlib.sha1).digest()
    return base64.b64encode(digest).decode()


def auth_header(
    method: str,
    url: str,
    body: dict,
    consumer_key: str,
    consumer_secret: str,
    token: str = "",
    token_secret: str = "",
    extra: dict | None = None,
) -> str:
    oauth = {
        "oauth_consumer_key": consumer_key,
        "oauth_nonce": secrets.token_hex(16),
        "oauth_signature_method": "HMAC-SHA1",
        "oauth_timestamp": str(int(time.time())),
        "oauth_version": "1.0",
    }
    if token:
        oauth["oauth_token"] = token
    if extra:
        oauth.update(extra)
    signing = dict(oauth)
    signing.update(body)
    oauth["oauth_signature"] = signature(method, url, signing, consumer_secret, token_secret)
    parts = ", ".join(f'{_quote(k)}="{_quote(v)}"' for k, v in sorted(oauth.items()))
    return "OAuth " + parts


class ApiError(Exception):
    """Carries a sentence for the user and the numeric code for the log."""

    def __init__(self, message: str, code: int = 0):
        super().__init__(message)
        self.code = code


# The error codes worth translating. Everything else becomes a generic
# sentence: the docs say the `message` field is for developers and is not
# intended to be shown to users, so it is logged and not forwarded.
FRIENDLY = {
    1040: "Instapaper is rate-limiting this bridge. Try again in a while.",
    1041: "That needs an Instapaper Premium account.",
    1042: "This bridge's Instapaper application has been suspended.",
    1241: "Instapaper no longer has that article.",
    1500: "Instapaper had a problem on its side. Try again in a while.",
    1550: "Instapaper could not produce a text version of this article.",
}


UNKNOWN_LIST = "Instapaper answered the article list in a shape this bridge does not know."

# A "type" value is a protocol discriminator ("meta", "user", "bookmark",
# "error") and is the one field describe_shape may quote. Constrained to short
# lowercase tokens so that no title, URL, username or token can ever match it.
_TYPE_CHARS = set("abcdefghijklmnopqrstuvwxyz_")


def _kind(v) -> str:
    """The TYPE of a value and, for the sized ones, its size. Never the value."""
    if isinstance(v, bool):
        return "bool"
    if v is None:
        return "null"
    if isinstance(v, int):
        return "int"
    if isinstance(v, float):
        return "float"
    if isinstance(v, str):
        return f"str[{len(v)}]"
    if isinstance(v, list):
        return f"list[{len(v)}]"
    if isinstance(v, dict):
        return f"dict{{{len(v)}}}"
    return type(v).__name__


def _fields(d: dict, depth: int = 0) -> str:
    """The field names and value types of one object.

    Descends ONE level into a nested list or object, because a wrapper is the
    shape where the answer is one level down: "results: list[2]" on its own
    says nothing about what results holds, and that is precisely the body
    somebody will be staring at."""
    out = []
    for k in sorted(d):
        v = d[k]
        if k == "type" and isinstance(v, str) and 0 < len(v) <= 24 and set(v) <= _TYPE_CHARS:
            out.append(f"type={v!r}")
        elif depth < 1 and isinstance(v, dict) and v:
            out.append(f"{k}: {_kind(v)} " + _fields(v, depth + 1))
        elif depth < 1 and isinstance(v, list) and v:
            out.append(f"{k}: {_kind(v)} of [{_elements(v, depth + 1)}]")
        else:
            out.append(f"{k}: {_kind(v)}")
    return "{" + ", ".join(out) + "}"


def _elements(items: list, depth: int = 0, max_shapes: int = 5) -> str:
    """Each DISTINCT element shape in a list, with how many share it. Distinct
    rather than just the first, because the element that differs is usually the
    one that explains the body -- the meta object is element zero exactly once."""
    shapes: dict = {}
    other = 0
    for item in items:
        sig = tuple(sorted(item)) if isinstance(item, dict) else _kind(item)
        if sig in shapes:
            shapes[sig][1] += 1
        elif len(shapes) < max_shapes:
            shapes[sig] = [_fields(item, depth) if isinstance(item, dict) else _kind(item), 1]
        else:
            other += 1
    inner = ", ".join(f"{desc} x{n}" for desc, n in shapes.values())
    if other:
        inner += f", +{other} of further shapes"
    return inner


def describe_shape(data, max_shapes: int = 5) -> str:
    """A refusal that does not say what it refused cannot be diagnosed.

    This is the sentence that goes in the log when a body is not understood:
    the top-level type, the keys present, the type of every value, a list's
    length and the field set of each distinct element shape it holds.

    KEYS AND TYPES ONLY. No titles, no URLs, no usernames, no tokens, no
    bookmark text -- this runs against a real person's reading list, and a
    log that helps is worth nothing if it is a log that leaks. String values
    are reported as their length; only a field literally named "type" is
    quoted, and only when it is a short lowercase token."""
    if isinstance(data, dict):
        return f"dict{{{len(data)}}} " + _fields(data)
    if not isinstance(data, list):
        return _kind(data)
    return f"list[{len(data)}] of [{_elements(data, 0, max_shapes)}]"


def parse_delete_ids(raw) -> list[int]:
    """-> whole integer ids, from whatever Instapaper felt like sending.

    The live API sends a comma-separated STRING in the meta element; the
    documentation's envelope implies a list. Both are accepted, and so is a
    bare id. Strictness lives HERE rather than in the caller: every id this
    returns costs the reader an article, so a fragment that is not a whole
    integer is dropped and logged rather than guessed at. See this module's
    docstring for the character-splitting failure this closes."""
    if raw is None:
        return []
    if isinstance(raw, str):
        parts = raw.split(",")
    elif isinstance(raw, (list, tuple, set)):
        parts = list(raw)
    else:
        parts = [raw]
    out, dropped = [], 0
    for part in parts:
        text = str(part).strip()
        if not text:
            continue
        try:
            out.append(int(text))
        except (TypeError, ValueError):
            dropped += 1
    if dropped:
        log.warning("ignored %d delete id(s) that were not whole integers", dropped)
    return out


def normalise_listing(data) -> dict:
    """Either bookmarks/list shape -> {"bookmarks", "user", "delete_ids"}.

    Liberal on purpose: this API is old, its documentation is inaccurate, and
    the cost of refusing a listing we could have read is a reader that will
    not sync at all. So an array of typed objects (what the live API sends)
    and an object with a "bookmarks" key (what the docs imply) both parse,
    an element missing its "type" is still taken as a bookmark if it carries
    a bookmark_id, and delete_ids is read from wherever it turns up.

    What it will NOT do is invent a deletion. Everything unrecognised becomes
    an empty result, never a delete id -- an empty listing costs a stale row
    until the next sync, and a wrong delete id costs an article."""
    if isinstance(data, list):
        bookmarks, user, delete_ids, saw_dict = [], {}, [], False
        for item in data:
            if not isinstance(item, dict):
                continue
            saw_dict = True
            if "delete_ids" in item:
                delete_ids.extend(parse_delete_ids(item.get("delete_ids")))
            kind = str(item.get("type") or "").strip().lower()
            if kind == "bookmark" or (not kind and "bookmark_id" in item):
                bookmarks.append(item)
            elif kind == "user" and not user:
                user = item
        # A listing with no bookmarks in it is NORMAL and must not refuse:
        # `have` suppresses everything the device already holds, so a fully
        # up-to-date reader gets back exactly meta + user. Only a non-empty
        # body with no object in it at all is a shape we cannot read.
        if data and not saw_dict:
            log.warning("bookmarks/list not understood; shape was %s", describe_shape(data))
            raise ApiError(UNKNOWN_LIST)
        return {"bookmarks": bookmarks, "user": user, "delete_ids": delete_ids}

    if isinstance(data, dict):
        if "bookmarks" not in data:
            log.warning("bookmarks/list not understood; shape was %s", describe_shape(data))
            raise ApiError(UNKNOWN_LIST)
        user = data.get("user")
        return {
            "bookmarks": [b for b in (data.get("bookmarks") or []) if isinstance(b, dict)],
            "user": user if isinstance(user, dict) else {},
            "delete_ids": parse_delete_ids(data.get("delete_ids")),
        }

    log.warning("bookmarks/list not understood; shape was %s", describe_shape(data))
    raise ApiError(UNKNOWN_LIST)


class Instapaper:
    """One user's authenticated session. Blocking; callers use to_thread."""

    def __init__(self, token: str = "", token_secret: str = ""):
        self.key, self.secret = consumer()
        self.token = token
        self.token_secret = token_secret

    def _post(self, path: str, body: dict, extra_oauth: dict | None = None) -> httpx.Response:
        url = f"{BASE}{path}"
        body = {k: str(v) for k, v in body.items() if v is not None}
        header = auth_header(
            "POST", url, body, self.key, self.secret, self.token, self.token_secret, extra_oauth
        )
        with httpx.Client(timeout=TIMEOUT, follow_redirects=False) as client:
            return client.post(
                url,
                data=body,
                headers={
                    "Authorization": header,
                    "Content-Type": "application/x-www-form-urlencoded",
                    "User-Agent": "CrossPlay read-bridge",
                },
            )

    def _json(self, path: str, body: dict):
        r = self._post(path, body)
        if r.status_code == 503 or not r.content:
            raise ApiError("Instapaper is busy right now. Try again in a while.")
        try:
            data = r.json()
        except ValueError:
            # The documented contract: output that is not valid JSON means a
            # 503, whatever status line came with it. Logged with its shape
            # rather than its bytes, for the same reason describe_shape
            # exists: a refusal nobody can diagnose is half a bug.
            log.warning(
                "%s answered %s with %s, %d bytes, and it is not JSON",
                path,
                r.status_code,
                r.headers.get("content-type", "no content-type"),
                len(r.content),
            )
            raise ApiError("Instapaper is busy right now. Try again in a while.") from None
        if isinstance(data, dict):
            return data
        for item in data:
            if isinstance(item, dict) and item.get("type") == "error":
                code = int(item.get("error_code") or 0)
                log.info("instapaper error %s: %s", code, item.get("message"))
                raise ApiError(FRIENDLY.get(code, "Instapaper refused that request."), code)
        return data

    # ---------------------------------------------------------------- account
    def access_token(self, username: str, password: str) -> tuple[str, str]:
        """xAuth. Returns (token, secret). The password is not stored, not
        logged, and not held beyond this call."""
        body = {
            "x_auth_username": username,
            "x_auth_password": password,
            "x_auth_mode": "client_auth",
        }
        r = self._post("/api/1/oauth/access_token", body)
        if r.status_code != 200:
            if r.status_code in (400, 401, 403):
                raise ApiError("Instapaper did not accept that email and password.")
            raise ApiError("Instapaper could not be reached. Try again in a while.")
        parsed = urllib.parse.parse_qs(r.text.strip())
        token = (parsed.get("oauth_token") or [""])[0]
        secret = (parsed.get("oauth_token_secret") or [""])[0]
        if not token or not secret:
            raise ApiError("Instapaper did not accept that email and password.")
        return token, secret

    def verify_credentials(self) -> dict:
        data = self._json("/api/1/account/verify_credentials", {})
        # A list of typed objects, same as bookmarks/list. The isinstance
        # guard is not defensive noise: a bare string in that array would
        # otherwise raise AttributeError and be reported as a bridge fault.
        for item in data if isinstance(data, list) else [data]:
            if isinstance(item, dict) and item.get("type") == "user":
                return item
        log.warning("verify_credentials not understood; shape was %s", describe_shape(data))
        raise ApiError("Instapaper did not recognise this connection.")

    # -------------------------------------------------------------- bookmarks
    def bookmarks_list(self, have: str = "", folder: str = "unread") -> dict:
        body = {"limit": LIST_LIMIT, "folder_id": folder}
        if have:
            body["have"] = have
        return normalise_listing(self._json("/api/1/bookmarks/list", body))

    def get_text(self, bookmark_id: int) -> str:
        r = self._post("/api/1/bookmarks/get_text", {"bookmark_id": bookmark_id})
        if r.status_code == 200:
            return r.text
        try:
            items = r.json()
        except ValueError:
            items = []
        if isinstance(items, list):
            for item in items:
                if isinstance(item, dict) and item.get("type") == "error":
                    code = int(item.get("error_code") or 0)
                    raise ApiError(FRIENDLY.get(code, "Instapaper could not send this article."), code)
        # A non-200 carrying no error element: the same blind refusal that
        # cost a day on bookmarks/list, in the twin path. Say what arrived.
        log.warning(
            "get_text answered %s with %d bytes and no error element; shape was %s",
            r.status_code,
            len(r.content),
            describe_shape(items) if items else "not JSON",
        )
        raise ApiError("Instapaper could not send this article.")

    def archive(self, bookmark_id: int) -> None:
        """Idempotent by the API's own semantics: archiving an archived
        bookmark returns it unchanged. That is what lets the device treat its
        archive queue as at-least-once and never lose one to a dropped
        response."""
        self._json("/api/1/bookmarks/archive", {"bookmark_id": bookmark_id})


def compose_have(entries: list[dict]) -> str:
    """The device's index -> Instapaper's `have` string.

    id:hash:progress:timestamp per entry. Progress is included only when the
    device actually has some AND has a timestamp for it: an entry claiming
    0.0 at time 0 would be read as "read to the start, just now" and would
    roll back progress set on the phone.
    """
    parts = []
    for e in entries:
        try:
            bid = int(e["id"])
        except (KeyError, TypeError, ValueError):
            continue
        h = str(e.get("hash") or "").strip()
        if not h:
            parts.append(str(bid))
            continue
        try:
            progress = float(e.get("progress") or 0.0)
            at = int(e.get("progressAt") or 0)
        except (TypeError, ValueError):
            progress, at = 0.0, 0
        if progress > 0.0 and at > 0:
            progress = min(1.0, max(0.0, progress))
            parts.append(f"{bid}:{h}:{progress:.3f}:{at}")
        else:
            parts.append(f"{bid}:{h}")
    return ",".join(parts)

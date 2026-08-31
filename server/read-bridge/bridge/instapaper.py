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
            # 503, whatever status line came with it.
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
        for item in data:
            if item.get("type") == "user":
                return item
        raise ApiError("Instapaper did not recognise this connection.")

    # -------------------------------------------------------------- bookmarks
    def bookmarks_list(self, have: str = "", folder: str = "unread") -> dict:
        body = {"limit": LIST_LIMIT, "folder_id": folder}
        if have:
            body["have"] = have
        data = self._json("/api/1/bookmarks/list", body)
        if not isinstance(data, dict):
            raise ApiError("Instapaper answered the article list in a shape this bridge does not know.")
        return data

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

#!/usr/bin/env python3
"""A stand-in for Instapaper's Full API, good enough to prove the bridge.

Exists because the real API needs a human-reviewed consumer key that this
repo does not have and must never contain, and because a suite that cannot
run without someone's live reading list is a suite nobody runs.

It implements the five endpoints the bridge uses, the `have` delta rules, and
-- the part that earns its keep -- OAuth signature verification. The
verification is written out here rather than imported from
bridge/instapaper.py on purpose: a fake that checks signatures with the
client's own function agrees with every mistake the client makes. The
independent oracle for the algorithm itself is tests/test_oauth.py, which
holds the client to RFC 5849's published base string.

State lives in a JSON file so a test can inspect what landed:

    FAKE_INSTAPAPER_STATE=/tmp/x.json uvicorn tests.fake_instapaper:app

    POST /_test/reset   replace the whole state
    GET  /_test/state   read it back
"""

import base64
import hashlib
import hmac
import json
import os
import pathlib
import urllib.parse

from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse, PlainTextResponse

app = FastAPI()

CONSUMER_KEY = os.environ.get("FAKE_CONSUMER_KEY", "fake-consumer-key")
CONSUMER_SECRET = os.environ.get("FAKE_CONSUMER_SECRET", "fake-consumer-secret")


def state_path() -> pathlib.Path:
    return pathlib.Path(os.environ["FAKE_INSTAPAPER_STATE"])


def load() -> dict:
    p = state_path()
    if not p.exists():
        return {"users": {}, "bookmarks": []}
    return json.loads(p.read_text())


def save(state: dict):
    state_path().write_text(json.dumps(state, indent=1))


# ------------------------------------------------------------------- signing
def quote(value) -> str:
    return urllib.parse.quote(str(value), safe="~")


def parse_auth(header: str) -> dict:
    if not header.startswith("OAuth "):
        return {}
    out = {}
    for part in header[6:].split(","):
        part = part.strip()
        if "=" not in part:
            continue
        k, v = part.split("=", 1)
        out[urllib.parse.unquote(k.strip())] = urllib.parse.unquote(v.strip().strip('"'))
    return out


def expected_signature(method, url, params, consumer_secret, token_secret) -> str:
    pairs = sorted((quote(k), quote(v)) for k, v in params)
    base = "&".join(
        [method.upper(), quote(url), quote("&".join(f"{k}={v}" for k, v in pairs))]
    )
    key = f"{quote(consumer_secret)}&{quote(token_secret)}"
    return base64.b64encode(hmac.new(key.encode(), base.encode(), hashlib.sha1).digest()).decode()


class Refuse(Exception):
    def __init__(self, code, message):
        self.code, self.message = code, message


def error(code: int, message: str):
    return JSONResponse([{"type": "error", "error_code": code, "message": message}])


async def authed(request: Request) -> tuple[dict, dict, dict]:
    """-> (oauth params, body params, the user this token belongs to)."""
    form = await request.form()
    body = {k: str(v) for k, v in form.items()}
    oauth = parse_auth(request.headers.get("authorization", ""))
    if oauth.get("oauth_consumer_key") != CONSUMER_KEY:
        raise Refuse(1042, "unknown consumer key")

    state = load()
    user = None
    token_secret = ""
    token = oauth.get("oauth_token", "")
    if token:
        for username, u in state["users"].items():
            if u["token"] == token:
                user, token_secret = dict(u, username=username), u["secret"]
                break
        if user is None:
            raise Refuse(1042, "unknown token")

    signed = [(k, v) for k, v in oauth.items() if k != "oauth_signature"]
    signed += list(body.items())
    # The URL the client signed is the one it built from READ_INSTAPAPER_BASE,
    # which the harness sets to this server's own address.
    url = str(request.url).split("?")[0]
    want = expected_signature("POST", url, signed, CONSUMER_SECRET, token_secret)
    if want != oauth.get("oauth_signature"):
        raise Refuse(1042, "bad signature")
    return oauth, body, user


@app.exception_handler(Refuse)
async def refused(_req, exc: Refuse):
    return error(exc.code, exc.message)


# ------------------------------------------------------------------ the hash
def bookmark_hash(bm: dict) -> str:
    """Instapaper's hash is documented as computed from the URL, title,
    description and reading progress -- notably NOT the content. Mirrored
    here, because the bridge's cache-reuse rule depends on that property."""
    raw = "|".join(
        [
            str(bm.get("url", "")),
            str(bm.get("title", "")),
            str(bm.get("description", "")),
            f"{float(bm.get('progress') or 0):.3f}",
        ]
    )
    return hashlib.sha1(raw.encode()).hexdigest()[:10]


def public(bm: dict) -> dict:
    out = {
        "type": "bookmark",
        "bookmark_id": bm["bookmark_id"],
        "url": bm.get("url", ""),
        "title": bm.get("title", ""),
        "description": bm.get("description", ""),
        "time": bm.get("time", 0),
        "progress": float(bm.get("progress") or 0),
        "progress_timestamp": bm.get("progress_timestamp", 0),
        "starred": bm.get("starred", "0"),
        "private_source": bm.get("private_source", ""),
    }
    out["hash"] = bookmark_hash(bm)
    return out


# ----------------------------------------------------------------- endpoints
@app.post("/api/1/oauth/access_token")
async def access_token(request: Request):
    form = await request.form()
    body = {k: str(v) for k, v in form.items()}
    oauth = parse_auth(request.headers.get("authorization", ""))
    if oauth.get("oauth_consumer_key") != CONSUMER_KEY:
        return PlainTextResponse("bad consumer", status_code=401)
    signed = [(k, v) for k, v in oauth.items() if k != "oauth_signature"] + list(body.items())
    url = str(request.url).split("?")[0]
    if expected_signature("POST", url, signed, CONSUMER_SECRET, "") != oauth.get("oauth_signature"):
        return PlainTextResponse("bad signature", status_code=401)

    state = load()
    user = state["users"].get(body.get("x_auth_username", ""))
    if user is None or user["password"] != body.get("x_auth_password", ""):
        return PlainTextResponse("Invalid xAuth credentials", status_code=401)
    return PlainTextResponse(
        f"oauth_token={user['token']}&oauth_token_secret={user['secret']}"
    )


@app.post("/api/1/account/verify_credentials")
async def verify(request: Request):
    _, _, user = await authed(request)
    return JSONResponse([{"type": "user", "user_id": 1, "username": user["username"]}])


@app.post("/api/1/bookmarks/list")
async def bookmarks_list(request: Request):
    _, body, user = await authed(request)
    state = load()
    folder = body.get("folder_id", "unread")
    limit = int(body.get("limit", 25))

    # Parse `have`: id[:hash[:progress:timestamp]]
    have: dict[int, dict] = {}
    for entry in (body.get("have", "") or "").split(","):
        entry = entry.strip()
        if not entry:
            continue
        parts = entry.split(":")
        try:
            bid = int(parts[0])
        except ValueError:
            continue
        have[bid] = {
            "hash": parts[1] if len(parts) > 1 else None,
            "progress": float(parts[2]) if len(parts) > 3 else None,
            "at": int(parts[3]) if len(parts) > 3 else None,
        }

    # The device's progress wins when it is more recent, which is the whole
    # reason progress rides in `have` at all.
    changed = False
    for bm in state["bookmarks"]:
        h = have.get(bm["bookmark_id"])
        if not h or h["progress"] is None:
            continue
        if int(h["at"] or 0) > int(bm.get("progress_timestamp") or 0):
            bm["progress"] = h["progress"]
            bm["progress_timestamp"] = h["at"]
            changed = True
    if changed:
        save(state)

    in_folder = [b for b in state["bookmarks"] if b.get("folder", "unread") == folder]
    window = in_folder[:limit]
    out = []
    for bm in window:
        h = have.get(bm["bookmark_id"])
        if h and h["hash"] and h["hash"] == bookmark_hash(bm):
            continue  # the client already has this one, unchanged
        out.append(public(bm))

    window_ids = {b["bookmark_id"] for b in window}
    delete_ids = [bid for bid in have if bid not in window_ids]
    return JSONResponse(
        {
            "user": {"type": "user", "user_id": 1, "username": user["username"]},
            "bookmarks": out,
            "highlights": [],
            "delete_ids": delete_ids,
        }
    )


@app.post("/api/1/bookmarks/get_text")
async def get_text(request: Request):
    _, body, _ = await authed(request)
    state = load()
    try:
        bid = int(body.get("bookmark_id", ""))
    except ValueError:
        return error(1241, "Invalid or missing bookmark_id")
    for bm in state["bookmarks"]:
        if bm["bookmark_id"] == bid:
            if bm.get("text_fails"):
                return JSONResponse(
                    [{"type": "error", "error_code": 1550, "message": "no text"}],
                    status_code=400,
                )
            return PlainTextResponse(bm.get("text", ""), media_type="text/html")
    return JSONResponse(
        [{"type": "error", "error_code": 1241, "message": "no such bookmark"}],
        status_code=400,
    )


@app.post("/api/1/bookmarks/archive")
async def archive(request: Request):
    _, body, _ = await authed(request)
    state = load()
    try:
        bid = int(body.get("bookmark_id", ""))
    except ValueError:
        return error(1241, "Invalid or missing bookmark_id")
    for bm in state["bookmarks"]:
        if bm["bookmark_id"] == bid:
            bm["folder"] = "archive"
            save(state)
            return JSONResponse([public(bm)])
    return error(1241, "Invalid or missing bookmark_id")


@app.post("/api/1/bookmarks/delete")
async def delete(request: Request):
    # Present so the suite can assert the bridge NEVER calls it. Any hit here
    # is a bug worth failing loudly on rather than a feature.
    state = load()
    state["delete_was_called"] = True
    save(state)
    return JSONResponse([])


# --------------------------------------------------------------- test control
@app.post("/_test/reset")
async def reset(request: Request):
    save(await request.json())
    return {"ok": True}


@app.get("/_test/state")
async def get_state():
    return load()

"""Post one event to the board, and never get in the way of a sync.

The contract is docs/workflow/events.md: one POST to
<SUPABASE_URL>/rest/v1/events with the public key. Where to post arrives as
SUPABASE_URL and SUPABASE_ANON_KEY in the environment (the pi's .env, passed
through by compose.yaml). With either missing every post is a no-op and the
service says so once in its log, at startup or at the first post, whichever
comes first.

Three rules, because the board is where numbers go and never something a
sync waits on:

- post() never raises. A prop that is not JSON, a dead network, a 4xx from
  the board: one log line, the event is dropped, the caller never knows.
- post() never blocks the caller. The request runs on its own daemon thread
  with a short timeout, so the worst case costs the process one idle thread
  for TIMEOUT_S and the sync nothing at all.
- The device field is a hash, never an identifier. device_id() folds the
  caller's id (a token hash, an account id) through sha256 with a fixed salt,
  so the board can count devices and nothing on it names one.

Byte-identical twin of the other bridge's bridge/events.py (study-bridge and
read-bridge). Neither Dockerfile can COPY a file from outside its own
directory without a build change, so the module is duplicated rather than
shared; each service's tests/test_events.py checks the twin still matches.
"""

import hashlib
import json
import logging
import os
import threading
import urllib.request

log = logging.getLogger("bridge.events")

TIMEOUT_S = 3.0

# Fixed and public on purpose. The salt is not a secret: it only keeps a
# device hash from being the plain sha256 of its input, so a value that leaks
# from elsewhere (a token hash in a state file, an account id in a log) cannot
# be matched to a row on the board by hashing it once more.
SALT = "crossplay-events"

# The whole HTTP layer, as one name the tests replace.
_urlopen = urllib.request.urlopen
_off_logged = False


def device_id(raw: str) -> str:
    """The stable, non-reversible id the board counts a device or account by."""
    return hashlib.sha256(f"{SALT}:{raw}".encode()).hexdigest()


def enabled() -> bool:
    """True when both variables are set. Logs once, the first time they are not."""
    global _off_logged
    url = os.environ.get("SUPABASE_URL", "").strip()
    key = os.environ.get("SUPABASE_ANON_KEY", "").strip()
    if url and key:
        return True
    if not _off_logged:
        _off_logged = True
        log.info("events are off: SUPABASE_URL and SUPABASE_ANON_KEY are not both set")
    return False


def post(
    service: str,
    event: str,
    *,
    device: str = "",
    level: str = "info",
    props: dict | None = None,
) -> threading.Thread | None:
    """Queue one event for the board. Never raises, never blocks.

    Returns the thread carrying the request, or None when nothing was sent;
    only the tests have a reason to join it."""
    try:
        if not enabled():
            return None
        record = {"service": service, "event": event, "level": level}
        if device:
            record["device"] = device
        record["props"] = props or {}
        body = json.dumps(record).encode()
        url = os.environ["SUPABASE_URL"].strip().rstrip("/") + "/rest/v1/events"
        key = os.environ["SUPABASE_ANON_KEY"].strip()
        t = threading.Thread(
            target=_deliver, args=(url, key, body), name="events-post", daemon=True
        )
        t.start()
        return t
    except Exception as e:  # a prop that is not JSON, a thread that cannot start
        log.warning("event %s/%s dropped before sending: %s", service, event, e)
        return None


def _deliver(url: str, key: str, body: bytes) -> None:
    req = urllib.request.Request(
        url,
        data=body,
        method="POST",
        headers={
            "apikey": key,
            "Authorization": "Bearer " + key,
            "Content-Type": "application/json",
            "Prefer": "return=minimal",
        },
    )
    try:
        resp = _urlopen(req, timeout=TIMEOUT_S)
        try:
            resp.read()
        finally:
            resp.close()
    except Exception as e:
        log.warning("event dropped, the board did not take it: %s", e)

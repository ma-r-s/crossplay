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
- The device field is a hash, never an identifier. A device names itself with
  the pseudonymous id in its X-CrossPlay-Device header; when it does not,
  device_id() folds the caller's id (a token hash, an account id) through
  sha256 with a fixed salt, so the board can count devices and nothing on it
  names one.

The device headers. A device never makes a request just to report; every
request it makes to a CrossPlay service carries what it has to say
(docs/workflow/events.md, "What a device sends"):

    X-CrossPlay-Device: <64 hex>          the same id on every request
    X-CrossPlay-Board:  x4pro | sticky
    X-CrossPlay-Report: {"battery_pct":N,"heap_min_kb":N,"uptime_h":N,
                         "crash":{...}, "ota":{...}}    compact JSON

and its firmware version rides in User-Agent: CrossPlay-ESP32-<version>.
client_of() reads all of it off a request without trusting any of it: an id
that is not 64 hex is no id, a board that is not a short word is no board, a
report over REPORT_MAX bytes or not a JSON object is ignored with one debug
line, and only numbers are copied out of the health fields. The usage event
the service posts anyway (a sync) then carries the device, its board and
version and the three health numbers; Client.report() posts the crash and the
update attempt the device is carrying as firmware events of their own.

Byte-identical twin of the other bridge's bridge/events.py (study-bridge and
read-bridge). Neither Dockerfile can COPY a file from outside its own
directory without a build change, so the module is duplicated rather than
shared; each service's tests/test_events.py checks the twin still matches.
"""

import hashlib
import json
import logging
import os
import re
import threading
import urllib.request
from dataclasses import dataclass, field

log = logging.getLogger("bridge.events")

TIMEOUT_S = 3.0

# Fixed and public on purpose. The salt is not a secret: it only keeps a
# device hash from being the plain sha256 of its input, so a value that leaks
# from elsewhere (a token hash in a state file, an account id in a log) cannot
# be matched to a row on the board by hashing it once more.
SALT = "crossplay-events"

# The report header is at most 600 bytes from the firmware; anything past
# this cap is not a report, whatever it says it is.
REPORT_MAX = 1000
# The three numbers every report carries, copied onto the usage event.
HEALTH_KEYS = ("battery_pct", "heap_min_kb", "uptime_h")

_HEX64 = re.compile(r"^[0-9a-fA-F]{64}$")
_BOARD = re.compile(r"^[a-z0-9]{1,16}$")
_UA = re.compile(r"^CrossPlay-ESP32-(\S{1,32})")

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
    version: str = "",
    board: str = "",
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
        if version:
            record["version"] = version
        if board:
            record["board"] = board
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


# ------------------------------------------------------------ the device
@dataclass
class Client:
    """What one request says about the device that made it.

    Every field may be empty: a browser, a curl, an old firmware. device is
    the header's id when it was a valid one, else the fallback the service
    passed (its own hash of the token), else nothing. health holds whichever
    of HEALTH_KEYS the report carried as numbers. crash and ota are the
    report's objects of those names, verbatim, or None."""

    device: str = ""
    board: str = ""
    version: str = ""
    health: dict = field(default_factory=dict)
    crash: dict | None = None
    ota: dict | None = None

    def props(self, base: dict | None = None) -> dict:
        """The props of a usage event: the caller's, plus the health numbers.
        The caller's win on a shared key."""
        p = dict(self.health)
        p.update(base or {})
        return p

    def report(self, via: str) -> int:
        """Post what the device is carrying: one firmware/crash event at level
        error, one firmware/update event. via names the service that relayed
        it. Returns how many events went out. Never raises: a request must
        not fail because of what rode along with it."""
        n = 0
        try:
            if self.crash is not None:
                c = self.crash
                post(
                    "firmware",
                    "crash",
                    level="error",
                    device=self.device,
                    # The version that crashed, written down at the boot after
                    # the panic; the running one when the record lacks it.
                    version=str(c.get("version") or self.version),
                    board=self.board,
                    props={
                        "message": str(c.get("message") or ""),
                        "backtrace": str(c.get("backtrace") or ""),
                        "app": "firmware",
                        "via": via,
                    },
                )
                log.info("device report via %s: crash on %s", via, c.get("version"))
                n += 1
            if self.ota is not None:
                o = self.ota
                props = dict(o)
                props["app"] = "firmware"
                level = "info"
                if o.get("ok") is False and o.get("error"):
                    level = "error"
                    props["message"] = (
                        f"update failed: {o.get('error')} ({o.get('path') or 'unknown'})"
                    )
                post(
                    "firmware",
                    "update",
                    level=level,
                    device=self.device,
                    version=self.version,
                    board=self.board,
                    props=props,
                )
                log.info("device report via %s: update %s", via, level)
                n += 1
        except Exception as e:  # a report shaped to break str() or dict()
            log.warning("device report via %s dropped: %s", via, e)
        return n


def client_of(headers, default_device: str = "") -> Client:
    """Read the device headers off a request. headers is anything with .get()
    keyed case-insensitively (Starlette's Headers) or by lowercase name (a
    dict in a test). Never raises."""
    c = Client(device=default_device)
    try:
        get = lambda name: str(headers.get(name) or "")  # noqa: E731
        dev = get("x-crossplay-device").strip()
        if _HEX64.match(dev):
            c.device = dev.lower()
        board = get("x-crossplay-board").strip().lower()
        if _BOARD.match(board):
            c.board = board
        m = _UA.match(get("user-agent"))
        if m:
            c.version = m.group(1)
        raw = get("x-crossplay-report")
        if raw:
            report = _parse_report(raw)
            if report is not None:
                for k in HEALTH_KEYS:
                    v = report.get(k)
                    if isinstance(v, (int, float)) and not isinstance(v, bool):
                        c.health[k] = v
                if isinstance(report.get("crash"), dict):
                    c.crash = report["crash"]
                if isinstance(report.get("ota"), dict):
                    c.ota = report["ota"]
    except Exception as e:
        log.debug("device headers ignored: %s", e)
    return c


def client_for(request, default_device: str = "") -> Client:
    """client_of(), once per request. The Client rides the ASGI scope, so the
    handler that posts the usage event and the middleware that posts the
    device's report read the headers once and say so once; the handler runs
    first, so its default_device is the one both see."""
    c = request.scope.get("crossplay_client")
    if c is None:
        c = client_of(request.headers, default_device)
        request.scope["crossplay_client"] = c
    return c


def _parse_report(raw: str) -> dict | None:
    # Header values are one byte per character on the wire, so the length of
    # the string is the length of the header.
    if len(raw) > REPORT_MAX:
        log.debug("X-CrossPlay-Report ignored: %d bytes, the cap is %d", len(raw), REPORT_MAX)
        return None
    try:
        report = json.loads(raw)
    except ValueError as e:
        log.debug("X-CrossPlay-Report ignored: not JSON (%s)", e)
        return None
    if not isinstance(report, dict):
        log.debug("X-CrossPlay-Report ignored: not an object")
        return None
    return report

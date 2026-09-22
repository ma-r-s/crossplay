"""One fridge's state on disk: who may write to it, what it shows, how often
the reader looks.

A bind mount and atomic writes, the same shape as the other two bridges. No
database: a fridge is one small JSON file and one 48062-byte image, and the
whole service is a few hundred of them.

WHAT IS NOT STORED. No account, no email, no name the sender did not type
themselves. A fridge is identified by an opaque id nobody chose, and the only
secrets kept are HASHES of tokens, so a copy of this directory cannot be
replayed against the service.
"""

import datetime
import hashlib
import json
import os
import pathlib
import secrets
import shutil
import struct
import tempfile
import time
import zlib
import zoneinfo

# The reader's sleep canvas: 480x800 at 1 bit, header and palette included.
# Byte-exact on purpose. The reader's own uploader checks the same number, and
# a wrong-sized file that still parses is drawn half-rendered forever rather
# than rejected (see WallpapersActivity's copy path).
# The reader's sleep canvas, 480x800. Two sizes are accepted, both byte-exact.
#
# 96070 is TWO bits per pixel with a four-entry palette: four real grey levels,
# which is what the panel actually does (its driver declares AbsolutePlanes
# grayscale, and the sleep screen's renderer takes that path when the panel
# supports it).
#
# 48062 is the one-bit file the Wallpapers app has always used, still accepted
# so anything already producing it keeps working.
#
# Byte-exact on purpose, either way: a wrong-sized file that still PARSES is
# drawn half-rendered on the reader forever rather than refused.
IMAGE_BYTES_1BIT = 48062
IMAGE_BYTES_2BIT = 96070
IMAGE_SIZES = (IMAGE_BYTES_1BIT, IMAGE_BYTES_2BIT)
IMAGE_BYTES = IMAGE_BYTES_1BIT  # kept for callers that predate the grey file

# Four, and enforced HERE rather than only where the reader draws them.
#
# The Live screen clamps its list to four. A drawing limit is not a limit: a
# fifth sender allowed by the service would exist, be able to write to the
# fridge, and be invisible on the one screen that can revoke it. Somebody with
# access you cannot see is worse than a refusal you can act on.
MAX_SENDERS = 4

DEFAULT_INTERVAL_S = 86400
MIN_INTERVAL_S = 900
MAX_INTERVAL_S = 7 * 86400

# WHAT A SCHEDULE MAY BE SET TO, which is not the same question as what the
# reader will accept.
#
# MIN/MAX above bound X-Next-Wake, and they have to stay a range: under a daily
# schedule the number handed out is however many seconds are left until the next
# 07:00, which is a different figure every time. This tuple is the separate,
# FINITE question of what somebody may choose, and it is finite on purpose. The
# reader draws the cadence in words, host-tests/wallcaption measures every
# sentence the reader can draw in the device's real font, and a corpus it cannot
# enumerate is a corpus it cannot prove. See cadence_words.
ALLOWED_INTERVALS = (900, 21600, 43200, 86400, 172800, 604800)

# TWO SHAPES AND NO MORE: repeat every so often, or once a day at a time. A
# fridge magnet does not need a cron expression, and the reader could not run
# one -- it has no wall clock worth trusting, so it is told a number of seconds
# to sleep for and this service owns the calendar entirely.
DEFAULT_SCHEDULE = {
    "mode": "every",
    "interval_s": DEFAULT_INTERVAL_S,
    "daily_time": "07:00",
    "tz": "UTC",
}


def normalise_schedule(raw: dict) -> dict | None:
    """The schedule a request asks for, or None when it asks for nonsense."""
    if not isinstance(raw, dict):
        return None
    mode = str(raw.get("mode", "every"))
    if mode not in ("every", "daily"):
        return None
    out = dict(DEFAULT_SCHEDULE)
    out["mode"] = mode
    try:
        seconds = int(raw.get("intervalSeconds", DEFAULT_INTERVAL_S))
    except (TypeError, ValueError):
        return None
    if seconds not in ALLOWED_INTERVALS:
        return None
    out["interval_s"] = seconds
    hhmm = str(raw.get("dailyTime", "07:00"))
    if len(hhmm) != 5 or hhmm[2] != ":" or not (hhmm[:2] + hhmm[3:]).isdigit():
        return None
    if not (0 <= int(hhmm[:2]) <= 23 and 0 <= int(hhmm[3:]) <= 59):
        return None
    out["daily_time"] = hhmm
    tz = str(raw.get("tz", "UTC"))
    try:
        zoneinfo.ZoneInfo(tz)
    except Exception:  # noqa: BLE001 - any zoneinfo failure is a bad name
        return None
    out["tz"] = tz
    return out


# THE WORDS, and they are the service's, not the reader's.
#
# The reader draws a decision this service made verbatim and never rewords one
# (see BridgeHttp.h), and the website prints the same phrase on its schedule
# chip, so one function produces the cadence for both surfaces. Two surfaces
# telling one person two different stories is the failure this feature has spent
# its whole life fighting.
INTERVAL_WORDS = {
    900: "every 15 minutes",
    21600: "every 6 hours",
    43200: "every 12 hours",
    86400: "every 24 hours",
    172800: "every 2 days",
    604800: "every 7 days",
}


def cadence_words(schedule: dict) -> str:
    if schedule.get("mode") == "daily":
        return f"{schedule.get('daily_time', '07:00')} daily"
    return INTERVAL_WORDS.get(
        int(schedule.get("interval_s", DEFAULT_INTERVAL_S)), "every 24 hours"
    )


def cadence_seconds(schedule: dict) -> int:
    """How often it repeats, which is NOT how long the next sleep is.

    Under a daily schedule the sleep is a part-day whenever the schedule
    changed or a check was missed, and the reader composes "Every N hours" on
    its panel from whatever figure it was handed. Sent apart (X-Cadence beside
    X-Next-Wake) it announces the cadence rather than the leftover.
    """
    if schedule.get("mode") == "daily":
        return 86400
    return int(schedule.get("interval_s", DEFAULT_INTERVAL_S))


def same_cadence(a: dict, b: dict) -> bool:
    """Whether two schedules name the same thing to a person.

    The words, not the dict: a timezone that moved without moving the local
    time is not a change anybody can see, and a pending line nobody can explain
    is worse than none.
    """
    return cadence_words(a) == cadence_words(b)


def next_after(schedule: dict, anchor: int, now: int) -> int:
    """When the reader is due to look next, as an epoch.

    `anchor` is what a repeating schedule counts from (the last check-in, or the
    pairing instant before there has been one). A daily schedule ignores it: the
    next 07:00 is the next 07:00 whatever happened last.
    """
    if schedule.get("mode") == "daily":
        zone = zoneinfo.ZoneInfo(schedule.get("tz", "UTC"))
        local = datetime.datetime.fromtimestamp(now, zone)
        hhmm = schedule.get("daily_time", "07:00")
        target = local.replace(
            hour=int(hhmm[:2]), minute=int(hhmm[3:]), second=0, microsecond=0
        )
        if target <= local:
            target += datetime.timedelta(days=1)
        return int(target.timestamp())
    return anchor + int(schedule.get("interval_s", DEFAULT_INTERVAL_S))


def data_root() -> pathlib.Path:
    return pathlib.Path(os.environ.get("FRIDGE_DATA", "/data"))


def new_fridge_id() -> str:
    return secrets.token_hex(16)


def _atomic_write(path: pathlib.Path, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=path.parent, prefix=".tmp-")
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(payload)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, path)
    finally:
        if os.path.exists(tmp):
            os.unlink(tmp)


# --------------------------------------------------------------- thumbnails
#
# THE TILE IS THE REAL BYTES, downsampled here rather than uploaded beside them.
#
# The rail on the website is a strip of thumbnails and the file behind each one
# is 96070 bytes of 2-bit BMP, which browsers render inconsistently and which
# nobody should spend a megabyte of somebody's cellular data fetching to paint a
# 46px tile. Generated here, from the picture the reader is actually sent, the
# tile cannot disagree with what is on the glass -- and no image library is
# added to a service whose whole point is that it has no compiled dependencies.
# THE PANEL'S OWN SIZE. 60x100 was a SEVEN-TIMES upscale by the time a phone
# drew a rail tile: 152 CSS pixels is 456 real ones at 3x, and a browser
# cannot invent what is not there. The source is exactly 480x800, so serving
# that means every tile is downscaled and never stretched, at any density.
# It also costs nothing worth counting: the full image beside it is 96KB, and
# a four-level PNG of the same pixels is a fraction of that.
THUMB_W = 480
THUMB_H = 800


def _bmp_levels(payload: bytes) -> list[list[int]] | None:
    """The greys of one of our own BMPs, as rows top-down. None if unreadable.

    Only the two shapes this service accepts: 1-bit and 2-bit, bottom-up, rows
    padded to four bytes, palette immediately after a 40-byte header.
    """
    try:
        if len(payload) < 58 or payload[:2] != b"BM":
            return None
        off = struct.unpack_from("<I", payload, 10)[0]
        width = struct.unpack_from("<i", payload, 18)[0]
        height = struct.unpack_from("<i", payload, 22)[0]
        depth = struct.unpack_from("<H", payload, 28)[0]
        if depth not in (1, 2) or width <= 0 or height <= 0:
            return None
        palette = []
        for i in range(1 << depth):
            b = payload[54 + i * 4]
            g = payload[54 + i * 4 + 1]
            r = payload[54 + i * 4 + 2]
            palette.append((r * 77 + g * 150 + b * 29) >> 8)
        row_bytes = ((width * depth + 31) >> 5) << 2
        per_byte = 8 // depth
        mask = (1 << depth) - 1
        rows = []
        for y in range(height):
            base = off + (height - 1 - y) * row_bytes
            if base + row_bytes > len(payload):
                return None
            row = []
            for x in range(width):
                byte = payload[base + x // per_byte]
                shift = (per_byte - 1 - (x % per_byte)) * depth
                row.append(palette[(byte >> shift) & mask])
            rows.append(row)
        return rows
    except (IndexError, struct.error):
        return None


def _png_grey(width: int, height: int, rows: list[bytes]) -> bytes:
    """An 8-bit greyscale PNG, by hand. zlib is in the standard library and a
    PNG is four chunks; a dependency for this would be a dependency to audit."""

    def chunk(tag: bytes, data: bytes) -> bytes:
        body = tag + data
        return (
            struct.pack(">I", len(data))
            + body
            + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)
        )

    raw = b"".join(b"\x00" + bytes(r) for r in rows)
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 6))
        + chunk(b"IEND", b"")
    )


def thumbnail(payload: bytes) -> bytes | None:
    """A THUMB_W x THUMB_H greyscale PNG of a reader picture, or None if it
    cannot be read.

    Box-averaged rather than sampled: a 480x800 line drawing point-sampled to a
    tenth of its size loses most of its strokes, and a rail of tiles that are
    mostly blank paper is a rail nobody can pick from.
    """
    levels = _bmp_levels(payload)
    if not levels:
        return None
    height = len(levels)
    width = len(levels[0])
    out = []
    for ty in range(THUMB_H):
        y0 = (ty * height) // THUMB_H
        y1 = max(y0 + 1, ((ty + 1) * height) // THUMB_H)
        row = bytearray(THUMB_W)
        for tx in range(THUMB_W):
            x0 = (tx * width) // THUMB_W
            x1 = max(x0 + 1, ((tx + 1) * width) // THUMB_W)
            total = 0
            count = 0
            for y in range(y0, y1):
                src = levels[y]
                for x in range(x0, x1):
                    total += src[x]
                    count += 1
            row[tx] = total // count if count else 255
        out.append(row)
    return _png_grey(THUMB_W, THUMB_H, out)


class Fridge:
    def __init__(self, fridge_id: str):
        self.id = fridge_id
        self.root = data_root() / "fridges" / fridge_id

    @property
    def state_path(self) -> pathlib.Path:
        return self.root / "state.json"

    def image_path(self, sha: str) -> pathlib.Path:
        return self.root / "images" / f"{sha}.bmp"

    def thumb_path(self, sha: str) -> pathlib.Path:
        return self.root / "thumbs" / f"{sha}.png"

    def exists(self) -> bool:
        return self.state_path.exists()

    def load(self) -> dict:
        try:
            return json.loads(self.state_path.read_text())
        except (OSError, ValueError):
            return {}

    def save(self, state: dict) -> None:
        _atomic_write(self.state_path, json.dumps(state, indent=2).encode())

    def create(self, device_token_hash: str) -> dict:
        state = {
            # WHEN THE PAIRING WAS CLAIMED, and the anchor the first countdown
            # is measured from. A fridge is made by /api/claim, so this is the
            # moment somebody actually typed the code -- and a reader that
            # pairs again gets a NEW fridge with a new stamp, so re-pairing
            # re-anchors by construction rather than by a migration.
            "created": int(time.time()),
            "device_token_hash": device_token_hash,
            "last_checkin": 0,
            # HOW MANY TIMES THE READER HAS COME BACK, and when it first did.
            # last_checkin alone is a pure overwrite, so it can only answer
            # "ever" or "never" -- and "ever" counts a reader that pulled once
            # during setup the same as one that has been on a fridge for a
            # month. The second check-in is the first evidence that anybody
            # kept it, so it has to be countable.
            "checkins": 0,
            "first_checkin": 0,
            # WHAT THE READER SAID ITS OWN ALARM IS, converted to this clock
            # when it said it. 0 until it has spoken once.
            "next_wake": 0,
            # Pairing turns Live on at the reader, so a fridge starts on. It
            # goes false when the reader says so (POST /api/off) and true again
            # on the check-in that follows switching it back on.
            "live_on": True,
            # The schedule somebody set, and the one the READER is asleep on.
            #
            # They differ for as long as it takes the reader to wake up once,
            # and that window is the whole reason `pending` exists: a reader
            # told at midnight to change to 07:00 daily is still armed for the
            # old cadence until its next check-in, and both surfaces have to
            # say so rather than pretending the change already happened.
            #
            # `armed` starts at the default because that is exactly what the
            # reader seeds ITSELF with before this service has ever spoken to
            # it: live::kDefaultIntervalSeconds equals DEFAULT_INTERVAL_S, and
            # host-tests/live regenerates the check from this file so the two
            # cannot drift apart again.
            "schedule": dict(DEFAULT_SCHEDULE),
            "armed": dict(DEFAULT_SCHEDULE),
            # EVERYTHING EVER SENT, newest first, and it is SHARED: it is the
            # record of what this reader has shown, not of what any one phone
            # sent. Each entry names the phone that sent it.
            "history": [],
            "selected": None,
            "senders": [],
        }
        self.save(state)
        return state

    # ------------------------------------------------------------- history

    def schedule(self) -> dict:
        s = self.load().get("schedule")
        return dict(s) if isinstance(s, dict) else dict(DEFAULT_SCHEDULE)

    def armed(self) -> dict:
        s = self.load().get("armed")
        return dict(s) if isinstance(s, dict) else dict(DEFAULT_SCHEDULE)

    def add_entry(self, payload: bytes, kind: str, by: str) -> dict:
        """Puts one more picture in the history and points the reader at it.

        The FILE is content-addressed and the ENTRY is not: sending the same
        picture twice is two moments in the record and one file on the disk,
        which is also what makes a re-send free for the reader (the ETag does
        not move, so it answers 304 and spends no wake repainting what is
        already on the glass).
        """
        sha = hashlib.sha256(payload).hexdigest()[:16]
        if not self.image_path(sha).exists():
            _atomic_write(self.image_path(sha), payload)
        if not self.thumb_path(sha).exists():
            png = thumbnail(payload)
            if png:
                _atomic_write(self.thumb_path(sha), png)
        state = self.load()
        entry = {
            "id": secrets.token_hex(8),
            "sha": sha,
            "kind": kind if kind in ("drawing", "message", "photo") else "drawing",
            "at": int(time.time()),
            "by": by[:24] or "A phone",
        }
        state.setdefault("history", []).insert(0, entry)
        state["selected"] = entry["id"]
        self.save(state)
        return entry

    def entry(self, entry_id: str) -> dict | None:
        for e in self.load().get("history", []):
            if e.get("id") == entry_id:
                return e
        return None

    def select(self, entry_id: str) -> bool:
        state = self.load()
        if not any(e.get("id") == entry_id for e in state.get("history", [])):
            return False
        state["selected"] = entry_id
        self.save(state)
        return True

    def remove_entry(self, entry_id: str) -> bool:
        """Deletes an entry for EVERYBODY on this reader, and moves the pick.

        Leaving the pick on a deleted entry would point a device in another
        country at a file that is gone; moving it silently would change what is
        on somebody's fridge as a side effect of tidying. It moves to the newest
        remaining entry and both surfaces say so.
        """
        state = self.load()
        history = state.get("history", [])
        kept = [e for e in history if e.get("id") != entry_id]
        if len(kept) == len(history):
            return False
        state["history"] = kept
        if state.get("selected") == entry_id:
            state["selected"] = kept[0]["id"] if kept else None
        self.save(state)
        # The file goes only when nothing else in the record points at it: two
        # sends of one picture share one file, and deleting either must not
        # blank the other.
        gone = next((e for e in history if e.get("id") == entry_id), {})
        sha = gone.get("sha")
        if sha and not any(e.get("sha") == sha for e in kept):
            for path in (self.image_path(sha), self.thumb_path(sha)):
                try:
                    path.unlink()
                except OSError:
                    pass
        return True

    def selected_entry(self) -> dict | None:
        state = self.load()
        want = state.get("selected")
        if not want:
            return None
        for e in state.get("history", []):
            if e.get("id") == want:
                return e
        return None

    def read_image(self, sha: str) -> bytes | None:
        try:
            return self.image_path(sha).read_bytes()
        except OSError:
            return None

    def read_thumb(self, sha: str) -> bytes | None:
        try:
            return self.thumb_path(sha).read_bytes()
        except OSError:
            return None

    def touch_checkin(self, wake_in: int, live_on: bool) -> int:
        """The reader spoke, was handed `wake_in` seconds, and has picked the
        current schedule up. Returns which check-in this was: 1 the first
        time, 2 the second, and so on.


        `wake_in` is SECONDS FROM NOW, converted here to this service's clock.
        Never an absolute time from the reader: its only clock comes from
        X-Server-Time, and a device whose battery went flat comes back at the
        epoch, so a timestamp it sent would be a number from 1970 stored as a
        fact.

        THIS IS ALSO WHERE `armed` MOVES. A pull the reader got an answer to
        makes it adopt the reply's figure, so the moment we answer, the schedule
        it is asleep on IS the schedule we just used. Nothing pending survives a
        check-in, by construction rather than by a second write somewhere else.

        The check-in counter costs no extra write: this method already saves. A
        fridge made before the counter existed has no `checkins` key and starts
        from whatever it can prove -- 1 if it has ever checked in, 0 if not --
        so an old record reads as "at least this many" rather than as zero.
        """
        now = int(time.time())
        state = self.load()
        if "checkins" not in state:
            state["checkins"] = 1 if state.get("last_checkin") else 0
        state["checkins"] = int(state.get("checkins") or 0) + 1
        if not state.get("first_checkin"):
            state["first_checkin"] = state.get("last_checkin") or now
        state["last_checkin"] = now
        state["live_on"] = bool(live_on)
        state["next_wake"] = now + int(wake_in) if live_on and wake_in > 0 else 0
        state["armed"] = dict(
            state.get("schedule") or DEFAULT_SCHEDULE,
        )
        self.save(state)
        return int(state["checkins"])

    def set_live(self, on: bool) -> None:
        """The reader saying Live was switched off on it.

        Off clears the alarm rather than leaving the last one standing: there
        is no next check while Live is off, and a countdown to a moment nothing
        will happen at is exactly the fake number this field exists to avoid.
        """
        state = self.load()
        state["live_on"] = bool(on)
        if not on:
            state["next_wake"] = 0
        self.save(state)

    def live_on(self) -> bool:
        return bool(self.load().get("live_on", True))

    def next_expected(self) -> int:
        """When the reader is due to look again, as an epoch. 0 means never.

        THE READER OWNS THIS NUMBER, not the service. It is the thing holding
        the timer, so it reports the alarm it is about to arm on every check-in
        and this is that alarm on this clock. Derived instead from
        `last_checkin + interval_s`, the figure was wrong every time somebody
        changed the schedule from the website: the reader was still asleep on
        its old alarm and the countdown had already jumped to the new one. A
        stored alarm cannot do that. It moves when the reader says it moved,
        which is the check-in after it picks the new interval up.

        BEFORE THE FIRST CHECK-IN there is no reported alarm, so the anchor is
        the pairing instant: `created + interval_s`. That is the one number
        anybody can know then, and without it the page had nothing at all to
        show in the minute after pairing, which is the minute somebody watches
        to find out whether this thing works.

        It is an ESTIMATE either way and both surfaces say so in words: the
        reader's sleep timer runs off an RC oscillator and drifts
        percent-level, it only fetches on its way into sleep, and a wake missed
        for want of Wi-Fi is invisible until the one after it.
        """
        state = self.load()
        if not state.get("live_on", True):
            return 0
        wake = int(state.get("next_wake", 0))
        if wake > 0:
            return wake
        anchor = int(state.get("last_checkin", 0)) or int(state.get("created", 0))
        if anchor <= 0:
            return 0
        # BEFORE THE FIRST CHECK-IN the reader is asleep on what it seeded
        # itself with, not on what somebody has since chosen on the website. The
        # figure has to be the ARMED schedule for the same reason the pending
        # line exists at all: a reader paired an hour ago and switched to "every
        # 15 minutes" is still going to wake a day from pairing.
        return next_after(self.armed(), anchor, int(time.time()))

    def pending_cadence(self) -> str | None:
        """The cadence that takes effect after the reader's next check-in, or
        None when nothing is pending.

        ABSENT, never empty and never equal to the current one: the reader draws
        the line only when it differs, which is what keeps that screen to one
        line of news rather than a permanent restatement of the obvious.
        """
        state = self.load()
        schedule = self.schedule()
        armed = self.armed()
        if not state.get("live_on", True):
            # Live is off; there is no next check to take effect after, and a
            # promise about one would be the fake number this field exists to
            # avoid.
            return None
        if same_cadence(schedule, armed):
            return None
        return cadence_words(schedule)


def fridge_for_sender(sender_token: str) -> Fridge | None:
    """Every fridge a sender token opens. One flat index so this is a dict
    lookup rather than a walk of every fridge on the disk."""
    index = _load_index()
    fid = index.get(_hash(sender_token))
    return Fridge(fid) if fid else None


def fridge_for_device(device_token: str) -> Fridge | None:
    index = _load_index()
    fid = index.get(_hash(device_token))
    return Fridge(fid) if fid else None


def _hash(token: str) -> str:
    import hashlib

    return hashlib.sha256(token.encode()).hexdigest()


def _index_path() -> pathlib.Path:
    return data_root() / "tokens.json"


def _load_index() -> dict:
    try:
        return json.loads(_index_path().read_text())
    except (OSError, ValueError):
        return {}


def index_token(token: str, fridge_id: str) -> None:
    index = _load_index()
    index[_hash(token)] = fridge_id
    _atomic_write(_index_path(), json.dumps(index).encode())


def sweep_orphans(older_than_s: int = 3600) -> int:
    """Delete fridges nobody ever claimed. Returns how many went.

    Repairs the records the old pairing flow left behind: until 2026-09-21 a
    fridge was written when the reader SHOWED a code, so every visit to the
    Live setup screen made one and nothing ever removed it. Nineteen hours of
    one person testing left 67, which is why "how many fridges" was not a
    number anybody could use.

    The new flow cannot make one, so this only ever has old records to find.
    It stays because the repair has to run on the deployed data, not only in
    the code: a rule that is right for new writes and leaves the old ones
    wrong still shows the wrong number.

    An orphan is a fridge with NO SENDER and NO CHECK-IN that is older than
    `older_than_s`. A claimed fridge gets its first sender in the same request
    that creates it, so nothing live can match. The age bound is belt and
    braces against a claim caught mid-flight.
    """
    root = data_root() / "fridges"
    if not root.is_dir():
        return 0
    now = int(time.time())
    index = _load_index()
    gone, changed = 0, False
    for d in sorted(root.iterdir()):
        if not d.is_dir():
            continue
        try:
            state = json.loads((d / "state.json").read_text())
        except (OSError, ValueError):
            continue
        if state.get("senders") or state.get("last_checkin") or state.get("checkins"):
            continue
        if now - int(state.get("created", 0)) < older_than_s:
            continue
        for h in [k for k, v in index.items() if v == d.name]:
            del index[h]
            changed = True
        shutil.rmtree(d, ignore_errors=True)
        gone += 1
    if changed:
        _atomic_write(_index_path(), json.dumps(index).encode())
    return gone


def add_sender(fridge: "Fridge", token: str, name: str) -> bool:
    """Records a sender. False when the fridge is full, so the caller can say so."""
    state = fridge.load()
    senders = state.setdefault("senders", [])
    if len(senders) >= MAX_SENDERS:
        return False
    senders.append(
        {
            "name": name[:24] or "A phone",
            "paired_at": int(time.time()),
            "token_hash": _hash(token),
        }
    )
    fridge.save(state)
    index_token(token, fridge.id)
    return True


def sender_name(fridge: "Fridge", token: str) -> str:
    """Whose phone this is, for the history's attribution.

    The history is shared and every entry names its sender, so this is read
    from the fridge's own list rather than from anything the browser sends: a
    name a caller could choose per request is a name a caller could forge.
    """
    want = _hash(token)
    for s in fridge.load().get("senders", []):
        if s.get("token_hash") == want:
            return str(s.get("name") or "A phone")
    return "A phone"


def revoke_sender(fridge: "Fridge", token_hash: str) -> bool:
    """Removes a sender and its token in one step.

    Both halves or neither: a sender dropped from the list while its token
    still opened the fridge would be revoked on the screen and not in fact,
    which is the worst way for this to fail.
    """
    state = fridge.load()
    senders = state.get("senders", [])
    kept = [s for s in senders if s.get("token_hash") != token_hash]
    if len(kept) == len(senders):
        return False
    state["senders"] = kept
    fridge.save(state)
    forget_token_hash(token_hash)
    return True


def forget_token_hash(token_hash: str) -> None:
    index = _load_index()
    if index.pop(token_hash, None) is not None:
        _atomic_write(_index_path(), json.dumps(index).encode())

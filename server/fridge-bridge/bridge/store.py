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

import json
import os
import pathlib
import secrets
import tempfile
import time

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


class Fridge:
    def __init__(self, fridge_id: str):
        self.id = fridge_id
        self.root = data_root() / "fridges" / fridge_id

    @property
    def state_path(self) -> pathlib.Path:
        return self.root / "state.json"

    @property
    def image_path(self) -> pathlib.Path:
        return self.root / "image.bmp"

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
            # WHEN THE READER WAS SYNCED, and the anchor the first countdown is
            # measured from. A fridge is made by /api/pair/start, which is the
            # reader showing its code, so this is within a minute of the moment
            # somebody finished pairing -- and a reader that pairs again gets a
            # NEW fridge with a new stamp, so re-pairing re-anchors by
            # construction rather than by a migration.
            "created": int(time.time()),
            "device_token_hash": device_token_hash,
            "interval_s": DEFAULT_INTERVAL_S,
            "last_checkin": 0,
            # WHAT THE READER SAID ITS OWN ALARM IS, converted to this clock
            # when it said it. 0 until it has spoken once.
            "next_wake": 0,
            # Pairing turns Live on at the reader, so a fridge starts on. It
            # goes false when the reader says so (POST /api/off) and true again
            # on the check-in that follows switching it back on.
            "live_on": True,
            "image_id": None,
            "image_set_at": 0,
            "senders": [],
        }
        self.save(state)
        return state

    def set_image(self, payload: bytes) -> str:
        """Stores the image and returns its id.

        The id is the content hash, so a resend of the same picture does not
        make the reader spend a wake downloading and repainting what is already
        on the glass.
        """
        import hashlib

        image_id = hashlib.sha256(payload).hexdigest()[:16]
        _atomic_write(self.image_path, payload)
        state = self.load()
        state["image_id"] = image_id
        state["image_set_at"] = int(time.time())
        self.save(state)
        return image_id

    def read_image(self) -> bytes | None:
        try:
            return self.image_path.read_bytes()
        except OSError:
            return None

    def touch_checkin(self, wake_in: int, live_on: bool) -> None:
        """The reader spoke, and said when it will be back.

        `wake_in` is SECONDS FROM NOW, converted here to this service's clock.
        Never an absolute time from the reader: its only clock comes from
        X-Server-Time, and a device whose battery went flat comes back at the
        epoch, so a timestamp it sent would be a number from 1970 stored as a
        fact.
        """
        now = int(time.time())
        state = self.load()
        state["last_checkin"] = now
        state["live_on"] = bool(live_on)
        state["next_wake"] = now + int(wake_in) if live_on and wake_in > 0 else 0
        self.save(state)

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
        return anchor + int(state.get("interval_s", DEFAULT_INTERVAL_S))


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

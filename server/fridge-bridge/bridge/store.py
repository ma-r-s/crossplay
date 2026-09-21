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
IMAGE_BYTES = 48062

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
            "created": int(time.time()),
            "device_token_hash": device_token_hash,
            "interval_s": DEFAULT_INTERVAL_S,
            "last_checkin": 0,
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

    def touch_checkin(self) -> None:
        state = self.load()
        state["last_checkin"] = int(time.time())
        self.save(state)

    def next_expected(self) -> int:
        """When the reader is due to look again, as an epoch.

        The SERVICE owns this number, not the reader: a sleeping device is
        unreachable by construction, so nothing can ask it. It is an ESTIMATE
        and the page must say so in words -- the reader's sleep timer runs off
        an RC oscillator and drifts percent-level, a refresh taken on the way
        into sleep shifts the schedule until the next check-in, and a wake
        missed for want of Wi-Fi is invisible until the one after it.
        """
        state = self.load()
        return int(state.get("last_checkin", 0)) + int(state.get("interval_s", DEFAULT_INTERVAL_S))


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


def forget_token_hash(token_hash: str) -> None:
    index = _load_index()
    if index.pop(token_hash, None) is not None:
        _atomic_write(_index_path(), json.dumps(index).encode())

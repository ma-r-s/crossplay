"""Per-user data layout, state, and the locks that keep users apart.

Layout, per docs/apps/study-sync-bridge-plan.md:

    <BRIDGE_DATA>/users/<uid>/
        collection.anki2      the AnkiWeb mirror (pylib-managed)
        collection.media/     its media dir (pylib-managed)
        journal.db            ingested-but-not-pushed reviews (journal.py)
        decks/<slug>/<build>/ versioned device deck builds
        state.json            account + devices + chosen decks

uid is a hex digest of the AnkiWeb username: usernames are email-shaped and
do not belong in filesystem paths, and the digest keeps the mapping stable
without storing the username anywhere it was not already stored (state.json
holds it, encrypted alongside the hostKey's ciphertext).
"""

import asyncio
import hashlib
import json
import os
import pathlib
import tempfile

from cryptography.fernet import Fernet


def data_root() -> pathlib.Path:
    root = os.environ.get("BRIDGE_DATA")
    if not root:
        raise RuntimeError("BRIDGE_DATA is unset; refusing to guess a data directory")
    return pathlib.Path(root)


def fernet() -> Fernet:
    key = os.environ.get("BRIDGE_FERNET_KEY")
    if not key:
        # Refusing to serve is the safe failure (the Getbooks rule): starting
        # with credentials stored in plaintext because an env var was
        # forgotten is not.
        raise RuntimeError("BRIDGE_FERNET_KEY is unset; refusing to store secrets")
    return Fernet(key.encode())


def uid_for(username: str) -> str:
    return hashlib.sha256(username.strip().lower().encode()).hexdigest()[:16]


class UserStore:
    """One user's directory, state file, and lock."""

    def __init__(self, uid: str):
        self.uid = uid
        self.root = data_root() / "users" / uid
        self.state_path = self.root / "state.json"

    def ensure(self):
        (self.root / "decks").mkdir(parents=True, exist_ok=True)
        return self

    @property
    def collection_path(self) -> pathlib.Path:
        return self.root / "collection.anki2"

    @property
    def journal_path(self) -> pathlib.Path:
        return self.root / "journal.db"

    def load_state(self) -> dict:
        if not self.state_path.exists():
            return {
                "username_enc": None,
                "hostkey_enc": None,
                "status": "new",  # new | ok | needs_login | needs_attention
                "devices": {},  # token_hash -> {name, created, last_seen, ack_offsets}
                "chosen_decks": [],
                "last_sync": None,
            }
        return json.loads(self.state_path.read_text())

    def save_state(self, state: dict):
        # Atomic: a crash mid-write must never leave a torn state.json,
        # because it holds the ack offsets that gate review re-sends.
        fd, tmp = tempfile.mkstemp(dir=self.root, prefix=".state-")
        try:
            with os.fdopen(fd, "w") as f:
                json.dump(state, f, indent=1, sort_keys=True)
                f.flush()
                os.fsync(f.fileno())
            os.replace(tmp, self.state_path)
        finally:
            if os.path.exists(tmp):
                os.unlink(tmp)


def _token_index_path() -> pathlib.Path:
    return data_root() / "tokens.json"


def _load_token_index() -> dict:
    p = _token_index_path()
    return json.loads(p.read_text()) if p.exists() else {}


def _save_token_index(index: dict):
    p = _token_index_path()
    p.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=p.parent, prefix=".tokens-")
    with os.fdopen(fd, "w") as f:
        json.dump(index, f)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, p)


def register_device(uid: str, token_hash: str, name: str):
    """Bind a paired device: the hash goes in the user's state (the record
    of the pairing, revocable there) and in the flat index (the O(1) lookup
    a bearer token resolves through)."""
    st = UserStore(uid).ensure()
    state = st.load_state()
    state["devices"][token_hash] = {
        "name": name,
        "created": int(__import__("time").time()),
        "last_seen": None,
        "ack_offsets": {},
    }
    st.save_state(state)
    index = _load_token_index()
    index[token_hash] = uid
    _save_token_index(index)


def revoke_device(uid: str, token_hash: str) -> bool:
    st = UserStore(uid)
    state = st.load_state()
    if token_hash not in state["devices"]:
        return False
    del state["devices"][token_hash]
    st.save_state(state)
    index = _load_token_index()
    index.pop(token_hash, None)
    _save_token_index(index)
    return True


def uid_for_token_hash(token_hash: str) -> str | None:
    """The index is the fast path; the user's state is the authority. A
    token that is in the index but no longer in the state (revoked mid-race)
    is refused."""
    uid = _load_token_index().get(token_hash)
    if uid is None:
        return None
    if token_hash not in UserStore(uid).load_state()["devices"]:
        return None
    return uid


class Locks:
    """Per-user asyncio locks. Valid only under a single event loop --
    which is why uvicorn runs with workers=1 (pinned in compose)."""

    def __init__(self):
        self._locks: dict[str, asyncio.Lock] = {}

    def for_user(self, uid: str) -> asyncio.Lock:
        if uid not in self._locks:
            self._locks[uid] = asyncio.Lock()
        return self._locks[uid]


LOCKS = Locks()

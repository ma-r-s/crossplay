"""Pairing: a device shows a code, a signed-in human claims it, the device
confirms the human it got.

Twin of server/study-bridge/bridge/pairing.py, unchanged in substance. Codes
are 8 chars from an unambiguous alphabet (no 0/O/1/I), expire in five
minutes, and are single-use. The device token is 32 random bytes and the
store keeps only its hash. Both directions of the pairing race are closed
elsewhere: claim is CSRF-guarded (app.py) and the device shows
"Paired to <username> -- confirm?" before persisting anything.
"""

import hashlib
import secrets
import time

CODE_ALPHABET = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ"
CODE_TTL_S = 300


def token_hash(token: str) -> str:
    return hashlib.sha256(token.encode()).hexdigest()


class Pairings:
    """In-memory: a reboot forgets pending pairings, which costs the user one
    more QR scan and nothing else. Paired tokens live in state.json."""

    def __init__(self):
        self._pending: dict[str, dict] = {}

    def _sweep(self):
        now = time.time()
        for code in [c for c, p in self._pending.items() if p["expires"] < now]:
            del self._pending[code]

    def start(self) -> dict:
        self._sweep()
        code = "".join(secrets.choice(CODE_ALPHABET) for _ in range(8))
        poll_token = secrets.token_urlsafe(24)
        self._pending[code] = {
            "poll_token": poll_token,
            "expires": time.time() + CODE_TTL_S,
            "claimed_by": None,
            "username": None,
            "device_token": None,
        }
        return {"code": code, "pollToken": poll_token, "expiresIn": CODE_TTL_S}

    def claim(self, code: str, uid: str, username: str) -> bool:
        self._sweep()
        p = self._pending.get(code.strip().upper())
        if p is None or p["claimed_by"] is not None:
            return False
        p["claimed_by"] = uid
        p["username"] = username
        p["device_token"] = secrets.token_urlsafe(32)
        return True

    def abandon(self, poll_token: str) -> None:
        """The device walked away from this code. Without this the code stays
        claimable until its TTL, and a web user claiming it waits forever for
        a confirm the reader will never show."""
        self._sweep()
        for code, p in list(self._pending.items()):
            if p["poll_token"] == poll_token:
                del self._pending[code]
                return

    def poll(self, poll_token: str) -> dict | None:
        """None: unknown/expired. {'pending': True}: not yet claimed. Else the
        one-shot result; the pairing is consumed on delivery."""
        self._sweep()
        for code, p in self._pending.items():
            if p["poll_token"] != poll_token:
                continue
            if p["claimed_by"] is None:
                return {"pending": True}
            result = {
                "pending": False,
                "uid": p["claimed_by"],
                "username": p["username"],
                "deviceToken": p["device_token"],
            }
            del self._pending[code]
            return result
        return None


PAIRINGS = Pairings()

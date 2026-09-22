"""Pairing: the reader shows a code, a browser claims it, the reader collects
its token.

Shaped after server/read-bridge/bridge/pairing.py, with two deliberate
departures.

SIX DIGITS, NOT EIGHT LETTERS. The read and study bridges show an 8-character
code because it is scanned off a QR or typed by the person holding the device.
This one is read down a TELEPHONE to somebody in another country -- that is the
entire reason Live uses a code rather than a QR, because a QR can only be
scanned by someone already holding the reader, and the day the Wi-Fi changes
that person is on another continent. Digits survive being spoken; "J" and "K"
do not.

Six digits is a million, not a trillion, so the guessing is held off by the
attempt caps here rather than by the size of the space: a code dies after
WRONG_GUESSES_PER_CODE wrong answers, and app.py rate-limits claims per address
and globally. A code also dies at CODE_TTL_S, and is single-use.

NO ACCOUNT CLAIMS IT. The other bridges require a signed-in user because they
hold that user's upstream credentials. A fridge holds a picture, and asking
somebody's mother to make an account to be sent one is the friction this
feature exists to remove. The claimer gets an opaque sender token in a cookie
instead, and the reader can revoke any sender.
"""

import hashlib
import secrets
import time

CODE_TTL_S = 600
WRONG_GUESSES_PER_CODE = 5


def token_hash(token: str) -> str:
    return hashlib.sha256(token.encode()).hexdigest()


def new_code() -> str:
    # secrets.randbelow, not randint: this is a credential for ten minutes.
    return f"{secrets.randbelow(1000000):06d}"


class Pairings:
    """In memory. A restart forgets codes in flight, which costs whoever was
    mid-setup one more look at the screen and nothing else; issued tokens live
    in the store."""

    def __init__(self):
        self._pending: dict[str, dict] = {}

    def _sweep(self) -> None:
        now = time.time()
        for code in [c for c, p in self._pending.items() if p["expires"] < now]:
            del self._pending[code]

    def start(self, fridge_id: str, device_token: str, joining: bool = False) -> dict:
        self._sweep()
        # A code already in flight would be claimed by the wrong reader, so
        # keep drawing. The space is a million against a handful of pending
        # codes, so this effectively never spins.
        code = new_code()
        while code in self._pending:
            code = new_code()
        poll_token = secrets.token_urlsafe(24)
        self._pending[code] = {
            "fridge_id": fridge_id,
            "device_token": device_token,
            # A JOIN code adds a sender to a fridge that already exists. A
            # setup code makes one. Conflating them is how "add somebody"
            # would have silently orphaned the first sender and the picture
            # with it, because /api/pair/start mints a NEW fridge.
            "joining": joining,
            "poll_token": poll_token,
            "expires": time.time() + CODE_TTL_S,
            "wrong": 0,
            "sender_token": None,
        }
        return {"code": code, "pollToken": poll_token, "expiresIn": CODE_TTL_S}

    def claim(self, code: str) -> dict | None:
        """Returns {fridge_id, sender_token, joining, device_token} on success,
        None otherwise.

        device_token is in there because a SETUP fridge is written when its
        code is claimed, not when it is shown, and the writer needs the token
        to hash into the new record. It never leaves the service: claim()'s
        caller puts only fridge_id in the response body.

        A wrong guess is charged against the code it guessed at, not against
        nothing: without that, six digits is a million tries at whatever rate
        the address limits allow, spread over as many addresses as the guesser
        has. With it, a code is gone after five.
        """
        self._sweep()
        code = "".join(ch for ch in code.strip() if ch.isdigit())
        p = self._pending.get(code)
        if p is None:
            # Charge the miss to every live code, so a sweep of the space burns
            # the space rather than costing the guesser nothing.
            for other in self._pending.values():
                other["wrong"] += 1
            self._burn_exhausted()
            return None
        if p["sender_token"] is not None:
            return None  # single use
        p["sender_token"] = secrets.token_urlsafe(32)
        return {
            "fridge_id": p["fridge_id"],
            "sender_token": p["sender_token"],
            "joining": p["joining"],
            "device_token": p["device_token"],
        }

    def _burn_exhausted(self) -> None:
        for code in [c for c, p in self._pending.items() if p["wrong"] >= WRONG_GUESSES_PER_CODE]:
            del self._pending[code]

    def poll(self, poll_token: str) -> dict | None:
        """The reader asking whether anybody has claimed its code yet."""
        self._sweep()
        for code, p in list(self._pending.items()):
            if secrets.compare_digest(p["poll_token"], poll_token):
                if p["sender_token"] is None:
                    return None
                del self._pending[code]
                return {"fridge_id": p["fridge_id"], "device_token": p["device_token"]}
        return None

    def abandon(self, poll_token: str) -> None:
        """The reader left the setup screen. Without this the code stays live
        for its full ten minutes and somebody else can still claim it."""
        for code, p in list(self._pending.items()):
            if secrets.compare_digest(p["poll_token"], poll_token):
                del self._pending[code]
                return

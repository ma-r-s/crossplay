"""Log in with Instapaper: exchange the password for an OAuth token, keep
only that.

The password exists in this process for the duration of one xAuth call and is
never stored, logged, or echoed. The token is Fernet-encrypted at rest --
which on a live compromised box buys nothing (the key is in the env), and
exists so that backups and stray copies of the data directory carry no usable
credentials. The plan's honesty paragraph owns that tradeoff.

Login is a credential-stuffing oracle by construction, so callers must gate it
with the rate limits in app.py, and registration is refused for usernames
outside READ_ALLOWLIST.

Instapaper accounts may have NO PASSWORD AT ALL. The docs are explicit that an
empty password cannot be treated as a user error, so nothing here rejects one;
the API decides.
"""

import logging
import os

from . import store
from .instapaper import ApiError, Instapaper

log = logging.getLogger("bridge.accounts")


def allowlist() -> set[str] | None:
    """None means closed to everyone (fail closed); {"*"} means open."""
    raw = os.environ.get("READ_ALLOWLIST", "").strip()
    if not raw:
        return None
    return {u.strip().lower() for u in raw.split(",") if u.strip()}


def allowed(username: str) -> bool:
    lst = allowlist()
    if lst is None:
        return False
    return "*" in lst or username.strip().lower() in lst


def login(username: str, password: str) -> store.UserStore:
    """Blocking (runs via to_thread). Raises ValueError with a user-facing
    sentence on refusal."""
    if not allowed(username):
        raise ValueError("This bridge is invitation-only for now.")
    try:
        token, secret = Instapaper().access_token(username, password)
    except ApiError as e:
        log.info("login refused for a user: %s", e.code or "no code")
        raise ValueError(str(e)) from e

    f = store.fernet()
    st = store.UserStore(store.uid_for(username)).ensure()
    state = st.load_state()
    state["username_enc"] = f.encrypt(username.encode()).decode()
    state["token_enc"] = f.encrypt(token.encode()).decode()
    state["secret_enc"] = f.encrypt(secret.encode()).decode()
    state["status"] = "ok"
    st.save_state(state)
    log.info("account %s: logged in", st.uid)
    return st


def credentials_of(state: dict) -> tuple[str, str]:
    f = store.fernet()
    return (
        f.decrypt(state["token_enc"].encode()).decode(),
        f.decrypt(state["secret_enc"].encode()).decode(),
    )

"""Log in with AnkiWeb: exchange the password for a hostKey, keep only that.

The password exists in this process for the duration of one sync_login call
and is never stored, logged, or echoed. The hostKey is Fernet-encrypted at
rest -- which on a live compromised box buys nothing (the key is in the env),
and exists so that backups and stray copies of the data directory carry no
usable credentials. The plan's honesty paragraph owns this tradeoff.

Login is a credential-stuffing oracle by construction, so callers must gate
it with the rate limits in app.py, and registration is refused for usernames
outside BRIDGE_ALLOWLIST until Mario opens it.
"""

import logging
import os

from . import store

log = logging.getLogger("bridge.accounts")

ANKIWEB_ENDPOINT = os.environ.get("BRIDGE_ANKIWEB_ENDPOINT")  # None = real AnkiWeb


def allowlist() -> set[str] | None:
    """None means closed to everyone (fail closed); {"*"} means open."""
    raw = os.environ.get("BRIDGE_ALLOWLIST", "").strip()
    if not raw:
        return None
    return {u.strip().lower() for u in raw.split(",") if u.strip()}


def allowed(username: str) -> bool:
    lst = allowlist()
    if lst is None:
        return False
    return "*" in lst or username.strip().lower() in lst


def login(username: str, password: str) -> store.UserStore:
    """Blocking (runs via to_thread). Exchanges credentials for a hostKey
    through Anki's own client and persists the account. Raises ValueError
    with a user-facing sentence on refusal."""
    if not allowed(username):
        raise ValueError("This bridge is invitation-only for now.")

    # A throwaway collection: sync_login is a Collection method but touches
    # only the network. It must never run against a real mirror.
    import tempfile

    from anki.collection import Collection

    with tempfile.TemporaryDirectory(prefix="bridge-login-") as tmp:
        col = Collection(os.path.join(tmp, "login.anki2"))
        try:
            auth = col.sync_login(username, password, ANKIWEB_ENDPOINT)
        except Exception as e:
            log.info("login refused for a user: %s", type(e).__name__)
            raise ValueError("AnkiWeb did not accept that email and password.") from e
        finally:
            col.close()

    f = store.fernet()
    st = store.UserStore(store.uid_for(username)).ensure()
    state = st.load_state()
    state["username_enc"] = f.encrypt(username.encode()).decode()
    state["hostkey_enc"] = f.encrypt(auth.hkey.encode()).decode()
    state["endpoint"] = auth.endpoint or ANKIWEB_ENDPOINT
    state["status"] = "ok"
    st.save_state(state)
    log.info("account %s: logged in", st.uid)
    return st


def hostkey_of(state: dict) -> str:
    return store.fernet().decrypt(state["hostkey_enc"].encode()).decode()

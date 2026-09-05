"""Break a bridge on purpose, so its attack suite can be watched going red.

A check that has never failed is not a check. This repository has shipped two
gates that could not fail and spent a night removing them; adding a third would
be worse than adding none. So every check in attacks.py has a weakening here
that must make it red, and running the matrix (server/verify_attacks.sh) is how
that stays true rather than remaining a claim in a commit message.

The weakenings are applied to the imported module at runtime, never to the file
on disk. Nothing here can escape the test process, and a weakening that fails to
apply is an error rather than a silent no-op -- otherwise "the check went green
under the weakening" would be indistinguishable from "the weakening did
nothing", which is the same failure the checks themselves exist to prevent.

Each entry names the checks it must redden. verify_attacks.sh asserts exactly
that, both ways: the named checks go red, and the run without a weakening is
green.
"""

WEAKENINGS = {}


def weakening(name, reddens):
    def wrap(fn):
        fn.reddens = reddens
        WEAKENINGS[name] = fn
        return fn

    return wrap


def _windows(appmod):
    from bridge.ratelimit import Window

    found = [v for v in vars(appmod).values() if isinstance(v, Window)]
    if not found:
        raise RuntimeError("no Window limiters found to weaken; has app.py moved?")
    return found


def _lockouts(appmod):
    try:
        from bridge.ratelimit import Lockout
    except ImportError:
        return []
    return [v for v in vars(appmod).values() if isinstance(v, Lockout)]


@weakening(
    "rate-limits-off",
    reddens=[
        "credential stuffing: many passwords, one account, one address",
        "credential stuffing: many accounts, one address",
        "credential stuffing: a fresh address AND a fresh account every time",
        "brute forcing a pairing code is cut off",
        "an unauthenticated stranger cannot post to the board at will",
    ],
)
def rate_limits_off(appmod):
    """Every counter says yes. This is the service as it was before any limit
    existed, and the state a misconfigured limit silently reaches."""
    for w in _windows(appmod):
        w.allow = lambda key: True
    for lk in _lockouts(appmod):
        lk.locked_for = lambda key, now=None: 0.0


@weakening(
    "global-login-ceiling-off",
    reddens=["credential stuffing: a fresh address AND a fresh account every time"],
)
def global_login_ceiling_off(appmod):
    """Only the ceiling with one of it. Per-IP and per-username limits stay,
    which is exactly the shape study-bridge shipped: every other sign-in check
    still passes and the distributed spray walks straight through."""
    if not hasattr(appmod, "GLOBAL_LOGIN"):
        raise RuntimeError("no GLOBAL_LOGIN on this service to weaken")
    appmod.GLOBAL_LOGIN.allow = lambda key: True


@weakening("claim-limit-off", reddens=["brute forcing a pairing code is cut off"])
def claim_limit_off(appmod):
    for name in ("CLAIM_IP", "CLAIM_USER"):
        if not hasattr(appmod, name):
            raise RuntimeError(f"no {name} on this service to weaken")
        getattr(appmod, name).allow = lambda key: True


@weakening(
    "report-limit-off",
    reddens=["an unauthenticated stranger cannot post to the board at will"],
)
def report_limit_off(appmod):
    for name in ("REPORT_IP", "GLOBAL_REPORT"):
        if not hasattr(appmod, name):
            raise RuntimeError(f"no {name} on this service to weaken")
        getattr(appmod, name).allow = lambda key: True


@weakening(
    "path-containment-off",
    reddens=["one account's device cannot read another account's files"],
)
def path_containment_off(appmod):
    """Put back the bug exactly as it was on study-bridge: the served
    directory is built from URL segments, and containment is then checked
    against that same moved directory.

    On READ-BRIDGE this weakening claims nothing, and the reason is the
    finding. Its route is /api/article/{bookmark_id:int}/{bookmark_hash}: the
    id is coerced to an integer and a path segment cannot hold a slash, so no
    dot segment survives ROUTING and the handler is never reached. There is no
    way to express the attack against it, which is why the twin was safe while
    this one was not. The traversal check is therefore demonstrated red on
    study-bridge and passes on read-bridge by construction; that difference is
    stated rather than hidden, because a weakening that quietly reddens
    nothing is indistinguishable from a check that does not work.
    """
    import pathlib as _pathlib
    import re as _re

    if not hasattr(appmod, "_SLUG_RE"):
        print("  (this service has no URL-built path to weaken: its route")
        print("   coerces the id to an integer, so a traversal cannot be spelled)")
        return []
    anything = _re.compile(r"^.*$", _re.S)
    appmod._SLUG_RE = anything
    appmod._BUILD_RE = anything
    _pathlib.Path.is_relative_to = lambda self, other: True
    return None


@weakening(
    "pairing-not-one-shot",
    reddens=[
        "a delivered pairing cannot be replayed",
        "a pairing code cannot be claimed twice",
    ],
)
def pairing_not_one_shot(appmod):
    """A code that stays claimable and a poll that keeps answering: two
    readers, one account, and the second one wins."""
    from bridge import pairing

    real_poll = pairing.Pairings.poll
    real_claim = pairing.Pairings.claim

    def poll(self, poll_token):
        for code, p in list(self._pending.items()):
            if p["poll_token"] == poll_token and p["claimed_by"] is not None:
                self._pending[code] = dict(p)  # never consumed
                return {
                    "pending": False,
                    "uid": p["claimed_by"],
                    "username": p["username"],
                    "deviceToken": p["device_token"],
                }
        return real_poll(self, poll_token)

    def claim(self, code, uid, username):
        p = self._pending.get(code.strip().upper())
        if p is not None:
            p["claimed_by"] = None  # forget that anyone claimed it
        return real_claim(self, code, uid, username)

    pairing.Pairings.poll = poll
    pairing.Pairings.claim = claim


class _AnyUid(str):
    """A uid that satisfies `j.uid != uid` for every caller. That comparison
    IS the ownership check, so this is precisely the check being removed."""

    def __eq__(self, other):
        return True

    def __ne__(self, other):
        return False

    def __hash__(self):
        return hash(str(self))


@weakening(
    "job-owner-check-off",
    reddens=["a device cannot ask about another account's job"],
)
def job_owner_check_off(appmod):
    """The status endpoint stops asking whose job it is, and hands a summary
    to whoever guesses the id."""
    from bridge import jobs

    class AnyJob:
        uid = _AnyUid("somebody-else")
        status = "done"
        summary = {"articles": 3, "applied": 3}
        message = ""

    real_get = jobs.Jobs.get

    def get(self, job_id):
        found = real_get(self, job_id)
        return found if found is not None else AnyJob()

    jobs.Jobs.get = get


@weakening(
    "session-cookie-flags-off",
    reddens=["the session cookie is HttpOnly, Secure and SameSite"],
)
def session_cookie_flags_off(appmod):
    """The cookie goes out readable by script and over plaintext."""
    from starlette.responses import Response

    real = Response.set_cookie

    def set_cookie(self, key, value="", **kw):
        kw["httponly"] = False
        kw["secure"] = False
        kw["samesite"] = "none"
        return real(self, key, value, **kw)

    Response.set_cookie = set_cookie


@weakening(
    "device-auth-off",
    reddens=[
        "device endpoints refuse an unauthenticated caller",
        "a forged bearer token is refused",
        "a revoked device token stops working immediately",
    ],
)
def device_auth_off(appmod):
    """Every device endpoint stops asking for a token.

    Through app.dependency_overrides, not by patching require_device: FastAPI
    captured the function object when the routes were built, so rebinding the
    module attribute changes nothing a request ever reaches. A weakening that
    silently does nothing is the failure this whole file exists to prevent.
    """
    from bridge import store

    def anybody():
        """The first account on disk, and a token hash it really holds.

        The hash has to be a REGISTERED one: the sync handler indexes
        state["devices"][th] directly, so an invented hash is a KeyError and a
        500 -- which would redden the malformed-body check for a reason that
        has nothing to do with malformed bodies. A weakening must remove one
        property and no others, or the matrix stops meaning anything."""
        root = store.data_root() / "users"
        uid = "nobody"
        if root.is_dir():
            uid = next((d.name for d in sorted(root.iterdir())), "nobody")
        devices = store.UserStore(uid).load_state().get("devices") or {}
        return uid, next(iter(devices), "no-device")

    appmod.app.dependency_overrides[appmod.require_device] = anybody


@weakening(
    "body-limits-off",
    reddens=[
        "an oversized body is refused rather than buffered",
        "a malformed body is never a stack trace",
    ],
)
def body_limits_off(appmod):
    """No ceiling on any body, and the parse raises something the handler does
    not catch -- which is what a shape assumption really costs: json.loads
    returning a list where a dict was assumed raised AttributeError, and
    AttributeError is not ValueError, so it left the handler as a 500."""
    for name in ("MAX_SYNC_BODY", "MAX_CHOOSE_BODY"):
        if hasattr(appmod, name):
            setattr(appmod, name, 1 << 40)
    import json as _json

    real_loads = _json.loads

    def loads(*a, **k):
        try:
            return real_loads(*a, **k)
        except ValueError as e:
            # The handlers catch ValueError. Nothing catches this.
            raise AttributeError(str(e)) from None

    _json.loads = loads


@weakening("csrf-off", reddens=["claiming a code needs a session and its CSRF token"])
def csrf_off(appmod):
    """The claim form stops checking the token bound to the session."""
    from starlette.datastructures import FormData

    real_get = FormData.get

    def get(self, key, default=None):
        if key == "csrf":
            # Whatever was sent is accepted, including nothing.
            import json as _json
            import os

            from cryptography.fernet import Fernet

            return _CSRF_HOLDER.get("value", default)
        return real_get(self, key, default)

    FormData.get = get
    _install_csrf_capture(appmod)


_CSRF_HOLDER: dict = {}


def _install_csrf_capture(appmod):
    """Remember the CSRF the session really holds, so the weakened form
    always 'matches' it however the caller spelled theirs."""
    real_unseal = appmod._unseal

    def unseal(cookie):
        data = real_unseal(cookie)
        if data and "csrf" in data:
            _CSRF_HOLDER["value"] = data["csrf"]
        return data

    appmod._unseal = unseal


def _replace_route(appmod, path, handler):
    """Swap one route's endpoint in place, keeping its dependencies."""
    from fastapi import Depends

    for route in appmod.app.routes:
        if getattr(route, "path", None) == path:
            import inspect

            params = list(inspect.signature(handler).parameters)
            if "dev" in params:
                handler.__signature__ = inspect.Signature(
                    [
                        inspect.Parameter(
                            n,
                            inspect.Parameter.POSITIONAL_OR_KEYWORD,
                            default=(
                                Depends(appmod.require_device)
                                if n == "dev"
                                else inspect.Parameter.empty
                            ),
                            annotation=(str if n != "dev" else inspect.Parameter.empty),
                        )
                        for n in params
                    ]
                )
            route.endpoint = handler
            route.dependant = None
            from fastapi.dependencies.utils import get_dependant

            route.dependant = get_dependant(path=route.path_format, call=handler)
            route.app = None
            from fastapi.routing import get_request_handler

            route.app = get_request_handler(
                dependant=route.dependant,
                body_field=route.body_field,
                status_code=route.status_code,
                response_class=route.response_class,
                response_field=route.secure_cloned_response_field,
                response_model_include=route.response_model_include,
                response_model_exclude=route.response_model_exclude,
                response_model_by_alias=route.response_model_by_alias,
                response_model_exclude_unset=route.response_model_exclude_unset,
                response_model_exclude_defaults=route.response_model_exclude_defaults,
                response_model_exclude_none=route.response_model_exclude_none,
                dependency_overrides_provider=route.dependency_overrides_provider,
            )
            return
    raise RuntimeError(f"no route {path} to weaken")


def apply(name, appmod):
    if name not in WEAKENINGS:
        raise SystemExit(
            f"unknown weakening {name!r}; have: {', '.join(sorted(WEAKENINGS))}"
        )
    fn = WEAKENINGS[name]
    own = fn(appmod)
    # A weakening may return its own list (usually empty) when this service
    # has nothing of that shape to break; otherwise the declared list stands.
    return fn.reddens if own is None else own

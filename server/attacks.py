"""The attacks an outsider would actually run, as a suite that fails a deploy.

Both bridges are open to the world and their source is public: the rate
limits, the code alphabet, the lockout windows and the token lifetimes are all
readable by whoever is attacking them. Security therefore cannot rest on any
of them being unknown, and this file is how that is checked rather than
asserted. It is the sibling of scripts/isolation_test.sh -- the precedent that
a claim about safety which nothing runs is not a claim -- and it is wired into
both deploy scripts for the same reason.

ONE FILE, TWO SERVICES, deliberately. read-bridge and study-bridge are twins
and every security bug found in them so far has been a fix that landed on one
twin and not the other (the fix-the-twin-too memory; the cross-user traversal
this suite was written to catch was exactly that shape -- read-bridge sanitised
the filename it took off the wire, study-bridge did not). A shared checklist
cannot drift. What differs between the services is described by the Service
profile below and nothing else.

The suite runs against the service's real ASGI app, over its real HTTP surface,
with a real fake upstream behind it. It does not import the limiters and ask
them questions; it asks the service.

Two ways to run it, and only the first proves anything about the CODE:

  hermetic (tests/attack_test.py, no arguments) -- a throwaway data directory
      and a fake upstream. Every check runs, including the floods. This is
      what gates a deploy.
  live (tests/attack_test.py --base https://...) -- against a deployed
      service. Only the checks that are SAFE against production run: nothing
      that floods a shared rate limiter, locks a real account out, or writes.
      Which checks those are is a property of the check (safe_live), not a
      flag at the call site, so the list cannot rot.

Adding a check: write it, then WATCH IT GO RED. weaken.py exists so that the
checks which pass today can be seen failing against a service broken on
purpose. A check that has never been red is a check that does not work; this
repository has already shipped two of those.
"""

from dataclasses import dataclass, field
from typing import Awaitable, Callable

# ---------------------------------------------------------------------------
# What differs between the two services.
# ---------------------------------------------------------------------------


@dataclass
class Service:
    """One bridge, described only where it differs from the other."""

    name: str
    # Session cookie name: read_session / bridge_session.
    cookie: str
    # An account the fake upstream will accept, and its password.
    good_user: str
    good_password: str
    # The device endpoint used to prove a token is or is not accepted. Must be
    # a POST that answers 401 without a valid Bearer token.
    sync_path: str
    # A body that endpoint parses. Content, not correctness: these checks never
    # get far enough for a real sync to matter.
    sync_body: bytes
    # The file-download endpoint, as a template taking one path segment string.
    # The traversal probe fills it with dot segments; see traversal_reach().
    download_template: str
    # Where the victim's secret sits, relative to the data root, and a string
    # that must never appear in a response.
    victim_relative: str
    sentinel: str
    # Clears the service's rate-limit state. Called before EVERY check, never
    # inside one: each check is its own scenario and must not fail because the
    # check before it spent the global ceiling. Within a check the limiters are
    # untouched, which is where they are actually under test.
    reset: object = None
    # Filled in by the harness before run().
    client: object = None
    victim_uid: str = ""
    attacker_uid: str = ""
    attacker_token: str = ""
    # Endpoints that must refuse an unauthenticated caller outright, as
    # (method, path, takes_a_body).
    device_paths: list = field(default_factory=list)


# ---------------------------------------------------------------------------
# Bookkeeping. One line per check, and the failures say what was expected.
# ---------------------------------------------------------------------------


class Report:
    def __init__(self, live: bool = False):
        self.checks = 0
        self.failures = 0
        self.skipped = 0
        self.live = live
        # Which named checks went red. verify_attacks.sh reads these off
        # stdout to assert that a given weakening reddens exactly the checks
        # that claim to cover it.
        self.red: list = []
        self.current = ""

    def ok(self, condition, what, detail=""):
        self.checks += 1
        if condition:
            print(f"  ok    {what}")
        else:
            self.failures += 1
            if self.current and self.current not in self.red:
                self.red.append(self.current)
            print(f"  FAIL  {what}")
            if detail:
                for line in str(detail).splitlines()[:6]:
                    print(f"        {line}")

    def skip(self, what, why):
        self.skipped += 1
        print(f"  skip  {what} -- {why}")


CHECKS: list = []


def check(name, *, safe_live=False):
    """Register one attack. safe_live=True means it may be run against a
    deployed service without harming it or anyone using it."""

    def wrap(fn):
        fn.check_name = name
        fn.safe_live = safe_live
        CHECKS.append(fn)
        return fn

    return wrap


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


async def sign_in(svc, username, password, ip=None):
    headers = {}
    if ip:
        headers["CF-Connecting-IP"] = ip
    return await svc.client.post(
        "/login", data={"username": username, "password": password}, headers=headers
    )


def refused(response):
    """Did the service turn this sign-in away for RATE, rather than for the
    password? Both answer 200 with a page, because the page is for a person;
    the status cannot tell them apart and the body can."""
    return "Slow down" in response.text or response.status_code == 429


def signed_in(response, svc):
    return response.status_code == 303 and bool(response.cookies.get(svc.cookie))


def traversal_reach(svc, up: int, tail: str) -> str:
    """A download URL whose leading path segments climb `up` directories.

    Percent-encoded, because an HTTP client normalises ".." out of a path
    before it is sent and the server would never see it. The server decodes
    before routing, so this is what a hostile client actually gets to say.
    """
    segments = ["%2E%2E"] * up
    return svc.download_template.format(segments="/".join(segments), tail=tail)


# ---------------------------------------------------------------------------
# Sign-in
# ---------------------------------------------------------------------------


@check("credential stuffing: many passwords, one account, one address")
async def stuffing_one_account(svc, r):
    tries = 25
    accepted = 0
    for i in range(tries):
        resp = await sign_in(svc, svc.good_user, f"guess-{i}", ip="203.0.113.10")
        if refused(resp):
            break
        accepted += 1
    r.ok(
        accepted < tries,
        f"guessing one account's password is cut off (got {accepted} of {tries} through)",
        "Nothing stopped 25 consecutive wrong passwords for one account.",
    )


@check("credential stuffing: many accounts, one address")
async def stuffing_one_ip(svc, r):
    tries = 25
    accepted = 0
    for i in range(tries):
        resp = await sign_in(
            svc, f"victim-{i}@example.com", "hunter2", ip="203.0.113.20"
        )
        if refused(resp):
            break
        accepted += 1
    r.ok(
        accepted < tries,
        f"spraying many accounts from one address is cut off (got {accepted} of {tries} through)",
        "One address tried 25 different accounts and was never slowed.",
    )


@check("credential stuffing: a fresh address AND a fresh account every time")
async def stuffing_distributed(svc, r):
    """The attack that per-IP and per-username limits are both blind to.

    A botnet has many addresses and a leaked password list has many accounts,
    so the attacker never repeats either key. Only a ceiling with ONE of it --
    a global counter -- can see this, which is why both services must have one.

    CF-Connecting-IP is what the service believes: it sits behind cloudflared,
    which overwrites that header, so in production it cannot be spoofed. This
    check spoofs it anyway, because that is exactly the assumption that must
    not be load-bearing -- if the origin is ever reachable off the tunnel, the
    per-IP limit is worth nothing and the global ceiling is all that is left.
    """
    tries = 80
    accepted = 0
    for i in range(tries):
        resp = await sign_in(
            svc, f"spray-{i}@example.com", "hunter2", ip=f"198.51.100.{i % 254 + 1}"
        )
        if refused(resp):
            break
        accepted += 1
    r.ok(
        accepted < tries,
        f"a distributed spray still hits a ceiling (got {accepted} of {tries} through)",
        "Every per-IP and per-username limit was sidestepped by using each key\n"
        "once, and nothing global stopped the run. This is the shape of a real\n"
        "credential-stuffing run against a public service.",
    )


@check("a wrong password and an unknown account are refused alike", safe_live=True)
async def login_is_not_an_account_oracle(svc, r):
    """Otherwise the sign-in page answers 'does this person have an account
    here', which is worth stealing on its own."""
    a = await sign_in(svc, svc.good_user, "definitely-wrong", ip="203.0.113.30")
    b = await sign_in(
        svc, "no-such-person@example.com", "definitely-wrong", ip="203.0.113.31"
    )
    if refused(a) or refused(b):
        r.skip(
            "sign-in refusals do not name which half was wrong",
            "rate-limited before both landed",
        )
        return
    import re

    def gist(resp):
        text = re.sub(r"<[^>]+>", " ", resp.text)
        return " ".join(text.split())

    r.ok(
        gist(a) == gist(b),
        "sign-in does not say whether the account exists",
        f"known account: {gist(a)[:160]}\nunknown account: {gist(b)[:160]}",
    )


@check("the sign-in page does not reflect what was typed into it", safe_live=True)
async def login_does_not_reflect(svc, r):
    payload = "</p><script>alert(1)</script>"
    resp = await sign_in(svc, payload, "x", ip="203.0.113.40")
    r.ok(
        "<script>alert(1)</script>" not in resp.text,
        "a script tag in the username does not come back as markup",
        resp.text[:400],
    )


@check("the session cookie is HttpOnly, Secure and SameSite")
async def session_cookie_flags(svc, r):
    resp = await sign_in(svc, svc.good_user, svc.good_password, ip="203.0.113.50")
    raw = resp.headers.get("set-cookie", "")
    import re as _re

    body = " ".join(_re.sub(r"<[^>]+>", " ", resp.text).split())[:240]
    r.ok(
        signed_in(resp, svc),
        "the good password still signs in",
        f"status {resp.status_code}, set-cookie {raw[:80]!r}\npage said: {body}",
    )
    low = raw.lower()
    r.ok("httponly" in low, "Set-Cookie is HttpOnly", raw[:200] or "no Set-Cookie header")
    r.ok("secure" in low, "Set-Cookie is Secure", raw[:200] or "no Set-Cookie header")
    r.ok(
        "samesite=lax" in low or "samesite=strict" in low,
        "Set-Cookie is SameSite",
        raw[:200] or "no Set-Cookie header",
    )


@check("a hand-made session cookie is not a session", safe_live=True)
async def forged_session(svc, r):
    import base64
    import json as _json

    forged = base64.urlsafe_b64encode(
        _json.dumps({"uid": svc.victim_uid, "username": "victim", "csrf": "x"}).encode()
    ).decode()
    for cookie in (forged, "gAAAAA-not-a-real-token", ""):
        resp = await svc.client.get(
            "/devices", cookies={svc.cookie: cookie}, follow_redirects=False
        )
        r.ok(
            resp.status_code in (302, 307) or "Sign in" in resp.text,
            f"a forged session cookie ({cookie[:12] or 'empty'}...) is signed out",
            f"status {resp.status_code}: {resp.text[:160]}",
        )


# ---------------------------------------------------------------------------
# Pairing
# ---------------------------------------------------------------------------


async def fresh_pairing(svc, ip="203.0.113.60"):
    resp = await svc.client.post("/api/pair/start", headers={"CF-Connecting-IP": ip})
    return resp.json()


async def session_for(svc, username=None, password=None, ip="203.0.113.61"):
    """A signed-in browser: cookie plus the CSRF token bound to it."""
    import json as _json
    import os

    from cryptography.fernet import Fernet

    resp = await sign_in(
        svc, username or svc.good_user, password or svc.good_password, ip=ip
    )
    cookie = resp.cookies.get(svc.cookie)
    if not cookie:
        return None, None
    key = os.environ.get("READ_FERNET_KEY") or os.environ["BRIDGE_FERNET_KEY"]
    csrf = _json.loads(Fernet(key.encode()).decrypt(cookie.encode()))["csrf"]
    return cookie, csrf


@check("brute forcing a pairing code is cut off")
async def pairing_brute_force(svc, r):
    """A pairing code is eight characters from a 32-glyph alphabet and lives
    five minutes, so guessing it is not the risk; guessing it FOR FREE is. An
    endpoint that will answer an unlimited number of wrong codes is an
    unlimited oracle over every code pending for every user at once, and the
    only account it costs the attacker is one of their own.
    """
    cookie, csrf = await session_for(svc, ip="203.0.113.62")
    if not cookie:
        r.ok(
            False, "brute forcing a pairing code is cut off", "could not sign in to try"
        )
        return
    await fresh_pairing(svc)  # something to guess at
    tries = 60
    answered = 0
    for i in range(tries):
        resp = await svc.client.post(
            "/api/pair/claim",
            data={"code": f"{i:08d}".replace("0", "A"), "csrf": csrf},
            cookies={svc.cookie: cookie},
            headers={"CF-Connecting-IP": "203.0.113.62"},
        )
        if resp.status_code == 429 or "Slow down" in resp.text:
            break
        answered += 1
    r.ok(
        answered < tries,
        f"wrong pairing codes stop being answered (got {answered} of {tries} through)",
        "The claim endpoint answered 60 wrong codes without complaint. It is\n"
        "the only guess-limiter the pairing code has.",
    )


@check("a pairing code cannot be claimed twice", safe_live=False)
async def pairing_claim_is_single_use(svc, r):
    cookie, csrf = await session_for(svc, ip="203.0.113.63")
    if not cookie:
        r.ok(
            False, "a pairing code cannot be claimed twice", "could not sign in to try"
        )
        return
    pair = await fresh_pairing(svc)
    first = await svc.client.post(
        "/api/pair/claim",
        data={"code": pair["code"], "csrf": csrf},
        cookies={svc.cookie: cookie},
    )
    second = await svc.client.post(
        "/api/pair/claim",
        data={"code": pair["code"], "csrf": csrf},
        cookies={svc.cookie: cookie},
    )
    r.ok(
        "confirm on the reader" in first.text,
        "the first claim is accepted",
        first.text[:200],
    )
    r.ok(
        "did not work" in second.text,
        "the same code claimed a second time is refused",
        "A code claimed twice binds one reader to two accounts, and the second\n"
        "claimant's reader is handed the first claimant's token.",
    )


@check("a delivered pairing cannot be replayed", safe_live=False)
async def pairing_poll_is_one_shot(svc, r):
    cookie, csrf = await session_for(svc, ip="203.0.113.64")
    if not cookie:
        r.ok(
            False, "a delivered pairing cannot be replayed", "could not sign in to try"
        )
        return
    pair = await fresh_pairing(svc)
    await svc.client.post(
        "/api/pair/claim",
        data={"code": pair["code"], "csrf": csrf},
        cookies={svc.cookie: cookie},
    )
    first = await svc.client.get(f"/api/pair/poll?pollToken={pair['pollToken']}")
    replay = await svc.client.get(f"/api/pair/poll?pollToken={pair['pollToken']}")
    r.ok(
        first.status_code == 200 and first.json().get("deviceToken"),
        "the device collects its token once",
        first.text[:200],
    )
    r.ok(
        replay.status_code == 410,
        "replaying the poll token yields nothing",
        "A pollToken that keeps answering hands a second device the same\n"
        "account, and the poll token travels in a URL.",
    )


@check("claiming a code needs a session and its CSRF token", safe_live=True)
async def pairing_claim_needs_csrf(svc, r):
    pair = await fresh_pairing(svc)
    no_session = await svc.client.post(
        "/api/pair/claim", data={"code": pair["code"], "csrf": "x"}
    )
    r.ok(
        no_session.status_code == 401,
        "claiming without a session is refused",
        f"status {no_session.status_code}: {no_session.text[:160]}",
    )
    cookie, csrf = await session_for(svc, ip="203.0.113.65")
    if not cookie:
        r.skip(
            "claiming with the wrong CSRF token is refused", "could not sign in to try"
        )
        return
    for bad in ("", "not-the-token", csrf[:-1]):
        resp = await svc.client.post(
            "/api/pair/claim",
            data={"code": pair["code"], "csrf": bad},
            cookies={svc.cookie: cookie},
        )
        r.ok(
            resp.status_code == 401,
            f"claiming with CSRF {bad[:10]!r} is refused",
            f"status {resp.status_code}: {resp.text[:160]}",
        )


# ---------------------------------------------------------------------------
# Device authentication
# ---------------------------------------------------------------------------


@check("device endpoints refuse an unauthenticated caller", safe_live=True)
async def device_needs_auth(svc, r):
    for method, path, _body in svc.device_paths:
        resp = await svc.client.request(method, path)
        r.ok(
            resp.status_code == 401,
            f"{method} {path} without a token is refused",
            f"status {resp.status_code}: {resp.text[:160]}",
        )


@check("a forged bearer token is refused", safe_live=True)
async def device_forged_token(svc, r):
    for token in ("nonsense", "", "Bearer", "a" * 64, svc.attacker_token + "x"):
        resp = await svc.client.post(
            svc.sync_path,
            content=svc.sync_body,
            headers={"Authorization": f"Bearer {token}"},
        )
        r.ok(
            resp.status_code == 401,
            f"a made-up token ({token[:10]!r}) is refused",
            f"status {resp.status_code}: {resp.text[:160]}",
        )


@check("one account's device cannot read another account's files")
async def cross_user_files(svc, r):
    """The check the study bridge failed on 2026-09-05.

    Its deck endpoint built the directory it was going to serve out of FROM
    THE URL, resolved it, and only then asked whether the target was inside
    that directory. The comparison was true by construction: dot segments in
    the leading parameters moved the base as well as the target. Two of them
    reached <data>/users/, from which every other account's state.json --
    their encrypted AnkiWeb hostKey and every device token hash paired to
    them -- was one segment away.

    The twin was safe for a reason worth writing down: read-bridge strips
    everything but alphanumerics out of the filename it takes off the wire, so
    the escape cannot be spelled at all. The lesson is not "add a check", it is
    that a path from the network must be sanitised before it becomes a path.

    This probe climbs every depth from one to six, so it cannot pass merely
    because the depth it happened to try was wrong.
    """
    headers = {"Authorization": f"Bearer {svc.attacker_token}"}
    leaked = []
    tried = []
    # Both spellings at every depth. The right one depends on how many
    # directories the endpoint's own base sits below <data>/, which differs
    # between the two services -- and a probe that guesses that wrong reports
    # a clean run against a service that is wide open.
    tails = [svc.victim_relative, "users/" + svc.victim_relative]
    for up in range(1, 7):
        for tail in tails:
            url = traversal_reach(svc, up, tail)
            resp = await svc.client.get(url, headers=headers)
            tried.append(f"{up}:{resp.status_code}")
            if resp.status_code == 200 and svc.sentinel in resp.text:
                leaked.append(f"  {url}\n    -> {resp.text[:100]}")
    r.ok(
        not leaked,
        f"no depth of dot segments reaches another account's files ({', '.join(tried)})",
        "Another account's file came back over the download endpoint:\n"
        + "\n".join(leaked),
    )


@check("a device cannot ask about another account's job", safe_live=False)
async def cross_user_job(svc, r):
    """Job ids are handed out per account and the status endpoint must bind
    them to the caller, or a summary of somebody else's sync is one guess of
    a short id away."""
    headers = {"Authorization": f"Bearer {svc.attacker_token}"}
    seen = []
    for job in ("1", "2", "3", "job-1", "0"):
        resp = await svc.client.get(f"/api/sync/status?job={job}", headers=headers)
        body = (
            resp.json()
            if resp.headers.get("content-type", "").startswith("application/json")
            else {}
        )
        if body.get("status") in ("done", "running", "queued") or "summary" in body:
            seen.append(f"job={job} -> {resp.text[:120]}")
    r.ok(not seen, "guessed job ids belong to nobody", "\n".join(seen) or "")


@check("a revoked device token stops working immediately", safe_live=False)
async def revoked_token_is_dead(svc, r):
    import importlib

    store = importlib.import_module("bridge.store")
    pairing = importlib.import_module("bridge.pairing")
    th = pairing.token_hash(svc.attacker_token)
    before = await svc.client.post(
        svc.sync_path,
        content=svc.sync_body,
        headers={"Authorization": f"Bearer {svc.attacker_token}"},
    )
    r.ok(
        before.status_code != 401,
        "the paired token works before revocation",
        f"status {before.status_code}: {before.text[:160]}",
    )
    store.revoke_device(svc.attacker_uid, th)
    after = await svc.client.post(
        svc.sync_path,
        content=svc.sync_body,
        headers={"Authorization": f"Bearer {svc.attacker_token}"},
    )
    r.ok(
        after.status_code == 401,
        "and is refused the instant it is revoked",
        f"status {after.status_code}: {after.text[:160]}",
    )
    store.register_device(svc.attacker_uid, th, "X4 Pro")


# ---------------------------------------------------------------------------
# What an unauthenticated stranger can spend
# ---------------------------------------------------------------------------


@check("an unauthenticated stranger cannot post to the board at will")
async def report_flood(svc, r):
    """Every response under 400 relays the caller's X-CrossPlay-Report header
    to the board as a firmware event, on EVERY endpoint including /healthz --
    which needs no token at all. Each one costs a thread and a row in Mario's
    Supabase project. Unbounded, that is a stranger writing to the board as
    fast as they can open sockets, and the container's pids_limit is 128.
    """
    import importlib

    events = importlib.import_module("bridge.events")
    sent = []
    real_post = events.post

    def counting_post(*a, **k):
        sent.append(k.get("event"))
        return None

    events.post = counting_post
    try:
        for i in range(120):
            await svc.client.get(
                "/healthz",
                headers={
                    "CF-Connecting-IP": "203.0.113.90",
                    "X-CrossPlay-Report": '{"crash":{"message":"flood","backtrace":"x"}}',
                },
            )
    finally:
        events.post = real_post
    r.ok(
        len(sent) < 120,
        f"device reports from one address are capped ({len(sent)} of 120 relayed)",
        "An unauthenticated GET /healthz relayed 120 crash reports to the board.\n"
        "Nothing about the caller was checked, and each one costs a thread.",
    )


@check("the interactive API documentation is not published", safe_live=True)
async def no_docs(svc, r):
    for path in ("/docs", "/redoc", "/openapi.json"):
        resp = await svc.client.get(path)
        r.ok(
            resp.status_code == 404,
            f"{path} is not served",
            f"status {resp.status_code}",
        )


@check("a malformed body is never a stack trace", safe_live=True)
async def malformed_bodies(svc, r):
    headers = {"Authorization": f"Bearer {svc.attacker_token}"}
    junk = [
        b"",
        b"{",
        b"not json at all",
        b"\x00\xff\xfe",
        b"[]",
        b'{"decks": "not a list"}',
        b"\xff\xff\xff\xff" + b"x" * 32,
    ]
    for method, path, takes_body in svc.device_paths:
        if not takes_body:
            continue
        for body in junk:
            resp = await svc.client.request(method, path, content=body, headers=headers)
            r.ok(
                resp.status_code < 500,
                f"{method} {path} survives {body[:14]!r}",
                f"status {resp.status_code}: {resp.text[:200]}",
            )


@check("an oversized body is refused rather than buffered")
async def oversized_bodies(svc, r):
    """Every endpoint that takes a body must have a ceiling on it. One that
    does not is a way to spend the container's 512MB from the outside, and it
    is reachable by anyone holding any device token -- including one for their
    own account."""
    headers = {"Authorization": f"Bearer {svc.attacker_token}"}
    big = b'{"decks": ["' + b"A" * (12 * 1024 * 1024) + b'"]}'
    for method, path, takes_body in svc.device_paths:
        if not takes_body:
            continue
        resp = await svc.client.request(method, path, content=big, headers=headers)
        r.ok(
            resp.status_code in (400, 413),
            f"{method} {path} refuses a 12MB body (status {resp.status_code})",
            f"status {resp.status_code}: {resp.text[:200]}",
        )


# ---------------------------------------------------------------------------
# Runner
# ---------------------------------------------------------------------------


async def run(svc: Service, *, live: bool = False) -> Report:
    r = Report(live=live)
    print(
        f"attacking {svc.name}" + (" (live: only the harmless checks)" if live else "")
    )
    for fn in CHECKS:
        if live and not fn.safe_live:
            r.skip(fn.check_name, "not safe against a service people are using")
            continue
        print(f"\n{fn.check_name}")
        r.current = fn.check_name
        if svc.reset is not None:
            svc.reset()
        try:
            await fn(svc, r)
        except Exception as e:  # a check that explodes is a failed check
            r.failures += 1
            r.checks += 1
            if fn.check_name not in r.red:
                r.red.append(fn.check_name)
            print(f"  FAIL  {fn.check_name} raised {type(e).__name__}: {e}")
    for name in r.red:
        print(f"RED: {name}")
    print(f"\n{svc.name}: {r.checks} checks, {r.failures} failed, {r.skipped} skipped")
    return r


CallableCheck = Callable[[Service, Report], Awaitable[None]]

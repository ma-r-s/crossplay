#!/usr/bin/env python3
"""Run server/attacks.py against read-bridge.

This file is the harness only: a throwaway data directory, a fake Instapaper,
one victim account with something worth stealing and one attacker holding a
device token for their OWN account. Every check lives in server/attacks.py and
is shared with study-bridge, so a fix can never land on one twin alone.

    .venv/bin/python tests/attack_test.py                 hermetic, gates deploy
    .venv/bin/python tests/attack_test.py --base https://...   live, safe subset

Run: scripts/deploy.sh runs the hermetic form before it ships anything.
"""

import asyncio
import json
import os
import pathlib
import shutil
import socket
import subprocess
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT.parent))  # server/, for attacks.py

BASE_PORT = int(os.environ.get("BRIDGE_TEST_PORT", "8996"))
FAKE_PORT = BASE_PORT + 3

VICTIM, VICTIM_PW = "victim@example.com", "victim-pw"
ATTACKER, ATTACKER_PW = "attacker@example.com", "attacker-pw"
CONSUMER_KEY, CONSUMER_SECRET = "fake-consumer-key", "fake-consumer-secret"

SENTINEL = "VICTIM-OAUTH-TOKEN-CIPHERTEXT"


def port_is_free(port):
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        s.bind(("127.0.0.1", port))
        return True
    except OSError:
        return False
    finally:
        s.close()


def require_free_port(port, what):
    """Refuse to run against somebody else's server.

    wait_port() proves that A process is listening, never that it is the one
    this suite started -- and on 2026-09-05 that difference cost an hour: port
    9003 was held by an orphaned read-bridge from another worktree, four days
    old, and this suite spent every run signing AnkiWeb credentials into it.
    Every sign-in failed, which read exactly like a bridge refusing a password.
    A probe whose passing state is indistinguishable from the symptom proves
    nothing.
    """
    if port_is_free(port):
        return
    print(f"port {port} is already in use, so {what} cannot start there and this")
    print("suite would attack whatever IS listening. Find it and decide:")
    print(f"  lsof -nP -iTCP:{port} -sTCP:LISTEN")
    print("Set BRIDGE_TEST_PORT to move this tree's whole slice.")
    raise SystemExit(2)


def assert_alive(proc, port, what):
    """And that it is still MY process answering, after the wait."""
    if proc.poll() is not None:
        raise RuntimeError(
            f"{what} exited with {proc.returncode} before it could serve anything"
        )


def _reap(proc):
    """SIGTERM the process group, then SIGKILL what is left."""
    import signal

    try:
        pgid = os.getpgid(proc.pid)
    except (ProcessLookupError, PermissionError):
        return
    # Never signal our own group. Without this line a Popen that did not get
    # start_new_session=True makes cleanup kill the suite, its shell and
    # whatever started them -- which is exactly what happened on 2026-09-05
    # when a formatter reflowed the Popen call and the edit adding
    # start_new_session silently matched nothing. Absent code compiles; this
    # is the guard that makes it fail loudly instead.
    if pgid == os.getpgrp():
        raise RuntimeError(
            "refusing to kill my own process group: the child was started "
            "without start_new_session=True"
        )
    for sig in (signal.SIGTERM, signal.SIGKILL):
        try:
            os.killpg(pgid, sig)
        except (ProcessLookupError, PermissionError):
            return
        try:
            proc.wait(timeout=8)
            return
        except subprocess.TimeoutExpired:
            continue


def wait_port(port, timeout=25):
    deadline = time.time() + timeout
    while time.time() < deadline:
        s = socket.socket()
        s.settimeout(0.5)
        try:
            s.connect(("127.0.0.1", port))
            s.close()
            return
        except OSError:
            time.sleep(0.2)
    raise RuntimeError(f"nothing opened port {port}")


def fixture_state():
    """Both accounts exist upstream, so the allowlist is not what refuses a
    guess: an attack suite whose sign-ins all bounce off an allowlist proves
    nothing about the sign-in path."""
    return {
        "users": {
            VICTIM: {"password": VICTIM_PW, "token": "tok-v", "secret": "sec-v"},
            ATTACKER: {"password": ATTACKER_PW, "token": "tok-a", "secret": "sec-a"},
        },
        "bookmarks": [],
    }


async def main_async():
    import httpx

    import attacks
    from bridge import app as appmod
    from bridge import pairing, store
    from bridge.app import app

    def reset_limits():
        """Every limiter this service holds, emptied. Listed by walking the
        module rather than by name, so a limiter added later is reset too --
        a hand-written list would silently leave the new one loaded and make
        the next check fail for the wrong reason."""
        from bridge.ratelimit import Lockout, Window

        for value in vars(appmod).values():
            if isinstance(value, Window):
                value.hits.clear()
            elif isinstance(value, Lockout):
                value._state.clear()

    victim_uid = store.uid_for(VICTIM)
    attacker_uid = store.uid_for(ATTACKER)
    for uid in (victim_uid, attacker_uid):
        store.UserStore(uid).ensure()

    # The victim's secret, where the service really keeps it.
    victim = store.UserStore(victim_uid)
    victim.save_state(
        {
            "username_enc": "x",
            "token_enc": SENTINEL,
            "secret_enc": "x",
            "status": "ok",
            "devices": {},
            "last_sync": None,
        }
    )
    # And an article of the victim's, in case a probe reaches sideways rather
    # than upwards.
    art = victim.article_dir(999)
    art.mkdir(parents=True, exist_ok=True)
    (art / "aaaa.txt").write_text(SENTINEL)

    # The attacker's own account, CONNECTED. Without this every device call
    # stops at "this account is not connected yet", which is a 401 for a
    # reason that has nothing to do with the token -- and the checks about
    # tokens would pass while proving nothing.
    f = store.fernet()
    attacker = store.UserStore(attacker_uid)
    attacker.save_state({
        "username_enc": f.encrypt(ATTACKER.encode()).decode(),
        "token_enc": f.encrypt(b"tok-a").decode(),
        "secret_enc": f.encrypt(b"sec-a").decode(),
        "status": "ok",
        "devices": {},
        "last_sync": None,
    })
    token = "attacker-device-token-0123456789"
    store.register_device(attacker_uid, pairing.token_hash(token), "X4 Pro")

    svc = attacks.Service(
        name="read-bridge",
        cookie="read_session",
        good_user=ATTACKER,
        good_password=ATTACKER_PW,
        sync_path="/api/sync",
        sync_body=b'{"have": [], "archive": []}',
        # /api/article/{bookmark_id}/{bookmark_hash}
        download_template="/api/article/{segments}/{tail}",
        victim_relative=f"users/{victim_uid}/state.json",
        sentinel=SENTINEL,
        victim_uid=victim_uid,
        attacker_uid=attacker_uid,
        attacker_token=token,
        device_paths=[
            ("POST", "/api/sync", True),
            ("GET", "/api/sync/status?job=1", False),
        ],
        reset=reset_limits,
    )

    # raise_app_exceptions=False so an unhandled exception in a handler comes
    # back as the 500 uvicorn would really send, instead of tearing down the
    # suite. A handler that raises is a finding, not a crashed test run.
    # A weakening, if one was asked for. Applied to the imported modules only;
    # nothing here touches a file. See server/weaken.py.
    if "--weaken" in sys.argv:
        import weaken

        expected = weaken.apply(sys.argv[sys.argv.index("--weaken") + 1], appmod)
        if not expected:
            # Machine-readable, because verify_attacks.sh must not have to
            # infer this from prose. It means "this service has nothing of
            # that shape", never "the weakening silently did nothing".
            print("NOTHING-TO-WEAKEN")
        print("weakened; these checks must go red:")
        for name in expected:
            print(f"  EXPECT-RED: {name}")

    transport = httpx.ASGITransport(app=app, raise_app_exceptions=False)
    async with httpx.AsyncClient(
        transport=transport, base_url="https://bridge", timeout=60
    ) as client:
        svc.client = client
        report = await attacks.run(svc)
    return 1 if report.failures else 0


async def live_async(base):
    import httpx

    import attacks

    svc = attacks.Service(
        name=f"read-bridge at {base}",
        cookie="read_session",
        good_user="nobody-attack-suite@example.invalid",
        good_password="not-a-real-password",
        sync_path="/api/sync",
        sync_body=b'{"have": [], "archive": []}',
        download_template="/api/article/{segments}/{tail}",
        victim_relative="users/nobody/state.json",
        sentinel=SENTINEL,
        attacker_token="not-a-real-token",
    )
    svc.device_paths = [
        ("POST", "/api/sync", True),
        ("GET", "/api/sync/status?job=1", False),
    ]
    async with httpx.AsyncClient(base_url=base, timeout=30) as client:
        svc.client = client
        report = await attacks.run(svc, live=True)
    return 1 if report.failures else 0


def main():
    if "--base" in sys.argv:
        base = sys.argv[sys.argv.index("--base") + 1]
        return asyncio.run(live_async(base))

    tmp = tempfile.mkdtemp(prefix="readbridge-attack-")
    state_file = pathlib.Path(tmp) / "fake.json"
    state_file.write_text(json.dumps(fixture_state()))

    from cryptography.fernet import Fernet

    env = dict(os.environ)
    env.update(
        {
            "FAKE_INSTAPAPER_STATE": str(state_file),
            "FAKE_CONSUMER_KEY": CONSUMER_KEY,
            "FAKE_CONSUMER_SECRET": CONSUMER_SECRET,
            "READ_DATA": str(pathlib.Path(tmp) / "data"),
            "READ_FERNET_KEY": Fernet.generate_key().decode(),
            # OPEN, which is the state this suite exists for. An allowlisted
            # bridge refuses every attack before it reaches any of the code
            # under test, and would report a clean run for a service with no
            # rate limiting at all.
            "READ_ALLOWLIST": "*",
            "READ_CONSUMER_KEY": CONSUMER_KEY,
            "READ_CONSUMER_SECRET": CONSUMER_SECRET,
            "READ_INSTAPAPER_BASE": f"http://127.0.0.1:{FAKE_PORT}",
            "PYTHONPATH": str(ROOT),
        }
    )
    # Before the bridge is imported: instapaper.BASE is read at import time and
    # a stale value would point this suite at the real Instapaper.
    os.environ.update({k: v for k, v in env.items() if k.startswith("READ_")})
    os.environ.pop("SUPABASE_URL", None)
    os.environ.pop("SUPABASE_ANON_KEY", None)

    procs = []
    try:
        require_free_port(FAKE_PORT, "the fake Instapaper")
        procs.append(
            subprocess.Popen(
                [
                    sys.executable,
                    "-m",
                    "uvicorn",
                    "tests.fake_instapaper:app",
                    "--host",
                    "127.0.0.1",
                    "--port",
                    str(FAKE_PORT),
                    "--log-level",
                    "warning",
                ],
                cwd=ROOT,
                env=env,
                # Its own process group, so _reap() can take the WHOLE tree
                # rather than just the launcher -- and, critically, so that
                # killpg cannot reach the process running this suite.
                start_new_session=True,
            )
        )
        wait_port(FAKE_PORT)
        assert_alive(procs[-1], FAKE_PORT, "the fake Instapaper")
        return asyncio.run(main_async())
    finally:
        # By GROUP, not by process. `python -m anki.syncserver` and uvicorn
        # both hold their listening socket in a child, so terminating the
        # launcher leaves the port held -- and the NEXT run then refuses to
        # start, or worse, attacks whatever is still sitting there. One of
        # these leaked for two hours on 2026-09-05 and blocked the matrix.
        for p in procs:
            _reap(p)
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())

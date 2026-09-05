#!/usr/bin/env python3
"""Run server/attacks.py against study-bridge.

The harness only. Every check lives in server/attacks.py and is shared with
read-bridge, so a fix can never land on one twin alone -- which is the exact
shape of the cross-user traversal this suite found on THIS service and not on
its twin.

Upstream is a real anki.syncserver on loopback with two throwaway accounts, so
signing in exercises the same code path a real AnkiWeb sign-in does. It never
points anywhere near a real account.

    .venv/bin/python tests/attack_test.py                 hermetic, gates deploy
    .venv/bin/python tests/attack_test.py --base https://...   live, safe subset
"""

import asyncio
import os
import pathlib
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
REPO = ROOT.parent.parent
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT.parent))  # server/, for attacks.py
sys.path.insert(0, str(REPO / "tools_local" / "study"))

BASE_PORT = int(os.environ.get("BRIDGE_TEST_PORT", "8996"))
SYNC_PORT = BASE_PORT + 7

VICTIM, VICTIM_PW = "victim@example.com", "victim-pw"
ATTACKER, ATTACKER_PW = "attacker@example.com", "attacker-pw"

SENTINEL = "VICTIM-ANKIWEB-HOSTKEY-CIPHERTEXT"

# [u32 header_len][JSON header][blobs]; an empty deck list is well-formed.
_HEADER = b'{"decks": []}'
SYNC_BODY = struct.pack("<I", len(_HEADER)) + _HEADER


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


def wait_port(port, timeout=40):
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


async def main_async():
    import logging

    import httpx

    # The service's own log, on. A refusal from upstream and a refusal from a
    # limiter reach this suite as the same page, and only the log tells them
    # apart -- which is how an hour went into "sign in failed" once already.
    logging.basicConfig(level=logging.INFO, stream=sys.stderr,
                        format="    [%(name)s] %(message)s")

    import attacks
    from bridge import app as appmod
    from bridge import pairing, store
    from bridge.app import app

    def reset_limits():
        """Every limiter this service holds, emptied. Found by walking the
        module rather than by name, so a limiter added later is reset too --
        a hand-written list would silently leave the new one loaded and make
        the next check fail for the wrong reason."""
        from bridge.ratelimit import Window

        try:
            from bridge.ratelimit import Lockout
        except ImportError:
            Lockout = ()
        for value in vars(appmod).values():
            if isinstance(value, Window):
                value.hits.clear()
            elif Lockout and isinstance(value, Lockout):
                value._state.clear()

    victim_uid = store.uid_for(VICTIM)
    attacker_uid = store.uid_for(ATTACKER)
    for uid in (victim_uid, attacker_uid):
        store.UserStore(uid).ensure()

    # The victim's secret, where the service really keeps it.
    store.UserStore(victim_uid).save_state(
        {
            "username_enc": "x",
            "hostkey_enc": SENTINEL,
            "status": "ok",
            "devices": {},
            "chosen_decks": [],
            "last_sync": None,
        }
    )

    # The attacker's own account, CONNECTED. Without this every device call
    # stops at "this account is not connected yet", a 401 for a reason that
    # has nothing to do with the token, and the token checks would pass while
    # proving nothing.
    f = store.fernet()
    store.UserStore(attacker_uid).save_state(
        {
            "username_enc": f.encrypt(ATTACKER.encode()).decode(),
            "hostkey_enc": f.encrypt(b"not-a-real-hostkey").decode(),
            "endpoint": f"http://127.0.0.1:{SYNC_PORT}/",
            "status": "ok",
            "devices": {},
            "chosen_decks": [],
            "last_sync": None,
        }
    )
    token = "attacker-device-token-0123456789"
    store.register_device(attacker_uid, pairing.token_hash(token), "X4 Pro")

    svc = attacks.Service(
        name="study-bridge",
        cookie="bridge_session",
        good_user=ATTACKER,
        good_password=ATTACKER_PW,
        sync_path="/api/sync",
        sync_body=SYNC_BODY,
        # /api/deck/{slug}/{build}/{path:path}
        download_template="/api/deck/{segments}/{tail}",
        victim_relative=f"{victim_uid}/state.json",
        sentinel=SENTINEL,
        victim_uid=victim_uid,
        attacker_uid=attacker_uid,
        attacker_token=token,
        device_paths=[
            ("POST", "/api/sync", True),
            ("POST", "/api/decks/choose", True),
            ("GET", "/api/decks", False),
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
        name=f"study-bridge at {base}",
        cookie="bridge_session",
        good_user="nobody-attack-suite@example.invalid",
        good_password="not-a-real-password",
        sync_path="/api/sync",
        sync_body=SYNC_BODY,
        download_template="/api/deck/{segments}/{tail}",
        victim_relative="nobody/state.json",
        sentinel=SENTINEL,
        attacker_token="not-a-real-token",
    )
    svc.device_paths = [
        ("POST", "/api/sync", True),
        ("POST", "/api/decks/choose", True),
        ("GET", "/api/decks", False),
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

    tmp = tempfile.mkdtemp(prefix="ankibridge-attack-")
    # anki.syncserver does not create SYNC_BASE. Without this it starts, opens
    # its port, and then fails every request -- which reaches the suite as
    # "sign in failed" and reads exactly like a bridge refusing a password.
    sync_base = pathlib.Path(tmp) / "syncserver"
    sync_base.mkdir(parents=True, exist_ok=True)

    from cryptography.fernet import Fernet

    env = dict(os.environ)
    env.update(
        {
            "SYNC_USER1": f"{ATTACKER}:{ATTACKER_PW}",
            "SYNC_USER2": f"{VICTIM}:{VICTIM_PW}",
            "SYNC_BASE": str(sync_base),
            "SYNC_HOST": "127.0.0.1",
            "SYNC_PORT": str(SYNC_PORT),
            "PYTHONPATH": os.pathsep.join(
                [str(ROOT), str(REPO / "tools_local" / "study")]
            ),
        }
    )
    os.environ.update(
        {
            "BRIDGE_DATA": str(pathlib.Path(tmp) / "data"),
            "BRIDGE_FERNET_KEY": Fernet.generate_key().decode(),
            # OPEN, which is the state this suite exists for. An allowlisted
            # bridge refuses every attack before it reaches any of the code
            # under test, and would report a clean run for a service with no
            # rate limiting at all.
            "BRIDGE_ALLOWLIST": "*",
            "BRIDGE_ANKIWEB_ENDPOINT": f"http://127.0.0.1:{SYNC_PORT}/",
        }
    )
    os.environ.pop("SUPABASE_URL", None)
    os.environ.pop("SUPABASE_ANON_KEY", None)

    procs = []
    try:
        require_free_port(SYNC_PORT, "the local anki syncserver")
        procs.append(
            subprocess.Popen(
                [sys.executable, "-m", "anki.syncserver"],
                cwd=str(ROOT),
                env=env,
                # Kept, not discarded: a syncserver that comes up and then
                # refuses everything is indistinguishable from a bridge that
                # refuses a password, and the difference is in this log.
                stdout=open(pathlib.Path(tmp) / "syncserver.log", "w"),
                stderr=subprocess.STDOUT,
            )
        )
        wait_port(SYNC_PORT)
        assert_alive(procs[-1], SYNC_PORT, "the local anki syncserver")
        return asyncio.run(main_async())
    finally:
        for p in procs:
            p.terminate()
        for p in procs:
            try:
                p.wait(timeout=10)
            except subprocess.TimeoutExpired:
                p.kill()
        if os.environ.get("ATTACK_KEEP_TMP"):
            print(f"kept: {tmp}")
        else:
            shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())

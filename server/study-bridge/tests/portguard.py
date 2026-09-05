"""Refuse to test against somebody else's server, and never leak our own.

Card #286. Every harness in here launches a server and then wait_port()s for the
port to answer. That proves A process is listening, never that it is the one
this suite started. On 2026-09-05 an orphaned read-bridge from a deleted
worktree held port 9003 for four days, and a bridge attack suite spent an hour
signing credentials into it: every sign-in bounced, which reads exactly like a
bridge correctly refusing a password. A probe whose passing state is
indistinguishable from the symptom proves nothing.

The orphan existed at all because `python -m anki.syncserver` and uvicorn hold
the listening socket in a CHILD process. Terminate only the launcher and the
child survives, reparents to init, and keeps the port -- so the NEXT run refuses
to start, or worse, tests whatever is still sitting there.

So a harness that launches a server:
  * require_free_port() before it starts, refusing loudly (and naming the pid)
    rather than testing a stranger;
  * popen_group() to start the child in its OWN process group;
  * assert_alive() right after the wait, so a child that died during startup is
    a loud failure and not a wait that happened to find someone else's port;
  * reap() in teardown, which kills the whole GROUP, not just the launcher.

Stdlib only, on purpose: this module is imported by suites that run before their
venv is guaranteed, and it has a host-test (host-tests/portguard) that must run
with nothing installed.
"""

import os
import signal
import socket
import subprocess


def port_is_free(port, host="127.0.0.1"):
    """True if we can bind the port right now. SO_REUSEADDR so this probe does
    not itself leave the port in TIME_WAIT for the server we are about to run."""
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        s.bind((host, port))
        return True
    except OSError:
        return False
    finally:
        s.close()


def _listener_hint(port):
    """Best-effort: the lsof line for whatever holds the port, so the refusal
    names the culprit instead of only the port. Never fatal -- lsof may be
    absent, and a missing hint must not turn a clean refusal into a crash."""
    try:
        out = subprocess.run(
            ["lsof", "-nP", f"-iTCP:{port}", "-sTCP:LISTEN"],
            capture_output=True,
            text=True,
            timeout=5,
        )
        return out.stdout.strip()
    except Exception:
        return ""


def require_free_port(port, what):
    """Refuse to run against somebody else's server. Exits 2 (loud, non-zero) so
    a harness under check.sh's bridge loop FAILS rather than silently passing
    against a stranger."""
    if port_is_free(port):
        return
    print(f"port {port} is already in use, so {what} cannot start there and this")
    print("suite would test whatever IS listening -- which proves nothing about")
    print("this tree. Find it and clear it:")
    print(f"  lsof -nP -iTCP:{port} -sTCP:LISTEN")
    hint = _listener_hint(port)
    for line in hint.splitlines():
        print("  " + line)
    print("Set BRIDGE_TEST_PORT to move this tree's whole port slice.")
    raise SystemExit(2)


def popen_group(cmd, **kwargs):
    """Popen with the child in its own session/process group, so reap() can take
    the child (uvicorn, anki.syncserver) and not just the launcher -- and so
    killpg can never reach the process running this suite."""
    kwargs.setdefault("start_new_session", True)
    return subprocess.Popen(cmd, **kwargs)


def assert_alive(proc, what):
    """The child is still MY process, after the wait. A wait_port that returned
    because a STRANGER answered would otherwise sail past here; a child that
    crashed on startup is caught instead of mistaken for a stranger's port."""
    if proc.poll() is not None:
        raise RuntimeError(
            f"{what} exited with {proc.returncode} before it could serve anything"
        )


def reap(proc):
    """SIGTERM then SIGKILL the child's whole process GROUP. Killing only the
    launcher is what leaked the orphans this module exists for."""
    if proc is None:
        return
    try:
        pgid = os.getpgid(proc.pid)
    except (ProcessLookupError, PermissionError):
        return
    # Never signal our own group. A child started without start_new_session=True
    # shares this suite's group, and killpg would then take the suite, its shell
    # and whatever started them. Fail loudly instead of committing suicide.
    if pgid == os.getpgrp():
        raise RuntimeError(
            "refusing to kill my own process group: the child was started "
            "without start_new_session=True (use popen_group)"
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

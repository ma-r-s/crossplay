"""Refuse a device build that would race another tree's device build.

The firmware lock in .pio-cache only protects builds that go through
check.sh. A raw `pio run -e x4pro` takes no lock, and on 2026-08-31 one ran
concurrently with a legitimate holder and took a sibling session's build down
with it -- `FileNotFoundError: .sconsign314.tmp -> .sconsign314.dblite`, an
error naming no file of ours. The workspace CLAUDE.md has warned against this
for days. A warning is not a guard, and the session that broke it was the one
writing the fix for the same race.

So the check moves to the only place a raw invocation cannot skip: inside the
build itself.

It refuses ONLY the destructive case -- a lock held by a live process that is
not us. A hand-run build on a quiet workspace still works, because that was
never the problem, and a guard that made every flash go through check.sh would
be routed around within the hour.

Silent no-op outside Mario's multi-tree workspace: CI checks out a single tree
with no .xteink-workspace marker above it, and there is nothing there to race.

WHAT THIS DOES NOT DO, so nobody assumes coverage it does not have: the check
runs ONCE, at build start. A raw `pio run` that starts during a quiet window and
a check.sh that takes the lock a moment later still race, and neither sees the
other. This NARROWS the unlocked-build hole; it does not close it. A hole
believed closed stops being looked at.

`_alive()` also says true for a recycled pid, so a dead owner whose number was
reused would refuse a legitimate build. Vanishingly rare, and the override
exists for it.
"""

import os
import sys

Import("env")  # noqa: F821  -- injected by SCons/PlatformIO


def _workspace(start):
    """The directory holding .xteink-workspace, or None outside one."""
    d = os.path.abspath(start)
    while d != "/":
        if os.path.exists(os.path.join(d, ".xteink-workspace")):
            return d
        d = os.path.dirname(d)
    return None


def _alive(pid):
    try:
        os.kill(int(pid), 0)
        return True
    except (OSError, ValueError):
        return False


def _refuse(message):
    print("\n*** REFUSING TO BUILD ***\n" + message + "\n", file=sys.stderr)
    env.Exit(1)  # noqa: F821


def check():
    if os.environ.get("XTEINK_ALLOW_UNLOCKED_BUILD"):
        return

    # PLATFORMIO_BUILD_CACHE_DIR first, and not merely as a default for the
    # marker walk. A --committed run builds in a trial worktree under TMPDIR,
    # where there is NO .xteink-workspace above it -- so a marker-only test
    # no-ops for every --committed build, which is exactly the release-gate
    # builds. check.sh exports the inherited cache dir precisely because its own
    # marker walk dead-ends there, and that env var is the honest signal that we
    # are inside the shared workspace. CI sets neither, so CI stays untouched.
    cache = os.environ.get("PLATFORMIO_BUILD_CACHE_DIR")
    if not cache:
        ws = _workspace(env.subst("$PROJECT_DIR"))  # noqa: F821
        if ws is None:
            return  # a standalone clone or CI; no siblings to race
        cache = os.path.join(ws, ".pio-cache")

    lock = os.path.join(cache, "x4pro.lock")
    owner_file = os.path.join(lock, "owner")
    if not os.path.isdir(lock):
        return  # nobody is building

    try:
        with open(owner_file) as f:
            owner = f.read().strip()
    except OSError:
        # A lock with no owner file is a pre-locklive holder. It cannot name
        # itself, so it is treated as HELD -- the same fail-safe direction
        # check.sh's waiter uses, and for the same reason: waiting too long is
        # a delay, building anyway is two builds against one ~/.platformio.
        _refuse(
            "another tree holds the firmware build lock (it does not record a\n"
            "pid, so it is an older check.sh) and this build takes no lock.\n"
            "  lock: %s\n"
            "Run this through ./scripts_local/check.sh, or wait for it to\n"
            "finish. Set XTEINK_ALLOW_UNLOCKED_BUILD=1 to override." % lock
        )
        return

    owner_pid = owner.split()[0] if owner else ""

    # Our own check.sh exports its pid when it takes the lock, so a build
    # underneath it recognises the lock as its own rather than as a stranger's.
    if owner_pid and owner_pid == os.environ.get("XTEINK_FW_LOCK_OWNER", ""):
        return

    if owner_pid and _alive(owner_pid):
        _refuse(
            "another tree is building firmware under the workspace lock, and\n"
            "this build takes no lock. Two device builds share one\n"
            "~/.platformio and one SCons signature database, and the loser\n"
            "fails with an error naming no file of ours.\n"
            "  holder: %s\n"
            "  lock:   %s\n"
            "Run this through ./scripts_local/check.sh, which queues properly.\n"
            "Set XTEINK_ALLOW_UNLOCKED_BUILD=1 to override deliberately." % (owner, lock)
        )


check()

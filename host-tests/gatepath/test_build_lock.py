"""require_build_lock.py, against every lock state it can meet.

The guard runs inside pio, so a bug in it fails EVERY device build in the
workspace rather than one. The cases below are therefore weighted toward the
false-refusal direction: a guard that wrongly blocks is worse than the race it
prevents, because the race is occasional and the block is total.

The real file is exec'd with a stubbed Import/env rather than copied, so the
test cannot drift from the script it is testing.

    python3 host-tests/gatepath/test_build_lock.py
"""

import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "..", "..", "scripts_local", "require_build_lock.py")

PASS = 0
FAIL = 0


def ok(msg):
    global PASS
    PASS += 1
    print("  ok   %s" % msg)


def bad(msg):
    global FAIL
    FAIL += 1
    print("  FAIL %s" % msg)


class Exited(Exception):
    pass


class FakeEnv:
    def __init__(self, project):
        self.project = project

    def subst(self, key):
        return self.project if key == "$PROJECT_DIR" else key

    def Exit(self, code):
        raise Exited(code)


def run(project, environ):
    """Exec the real guard. Returns True if it allowed the build."""
    saved = dict(os.environ)
    os.environ.clear()
    os.environ.update(environ)
    g = {"Import": lambda _name: None, "env": FakeEnv(project), "__name__": "guard"}
    # The guard prints its refusal to stderr. Swallow it: an expected failure
    # printing a scary block on every green run teaches people to skim output,
    # which is the habit this whole suite exists to fight.
    err = sys.stderr
    sys.stderr = open(os.devnull, "w")
    try:
        with open(SCRIPT) as f:
            exec(compile(f.read(), SCRIPT, "exec"), g)  # noqa: S102
        return True
    except Exited:
        return False
    finally:
        sys.stderr.close()
        sys.stderr = err
        os.environ.clear()
        os.environ.update(saved)


def _workspace_above(start):
    """Mirror of the guard's marker walk, for asserting the fixture is honest."""
    d = os.path.abspath(start)
    while d != "/":
        if os.path.exists(os.path.join(d, ".xteink-workspace")):
            return d
        d = os.path.dirname(d)
    return None


def case(label, expect_allowed, project, environ):
    allowed = run(project, environ)
    if allowed == expect_allowed:
        ok("%s -> %s" % (label, "builds" if allowed else "refused"))
    else:
        bad("%s -> %s, expected %s" % (label, "builds" if allowed else "refused",
                                       "builds" if expect_allowed else "refused"))


def main():
    work = tempfile.mkdtemp()
    ws = os.path.join(work, "workspace")
    tree = os.path.join(ws, "wt", "mine")
    os.makedirs(tree)
    open(os.path.join(ws, ".xteink-workspace"), "w").close()
    cache = os.path.join(ws, ".pio-cache")
    os.makedirs(cache)
    lock = os.path.join(cache, "x4pro.lock")

    print("require_build_lock")

    # No workspace marker at all: a standalone clone, or CI. Nothing to race,
    # and refusing here would break every release build GitHub runs.
    outside = os.path.join(work, "loner")
    os.makedirs(outside)
    case("outside any workspace (CI)", True, outside, {})

    # In a workspace, but nobody is building.
    case("no lock present", True, tree, {})

    # A live stranger holds it: the case the guard exists for.
    os.makedirs(lock)
    with open(os.path.join(lock, "owner"), "w") as f:
        f.write("%d someone-else\n" % os.getpid())
    case("live stranger holds the lock", False, tree, {})

    # Our own check.sh holds it. This is the false-refusal case that would
    # break every legitimate build if it were wrong.
    case("our own check.sh holds it", True, tree,
         {"XTEINK_FW_LOCK_OWNER": str(os.getpid())})

    # Deliberate override.
    case("override set", True, tree, {"XTEINK_ALLOW_UNLOCKED_BUILD": "1"})

    # A dead owner is not a holder; the lock is stale and must not block.
    with open(os.path.join(lock, "owner"), "w") as f:
        f.write("999999 long-gone\n")
    case("stale lock, owner dead", True, tree, {})

    # No owner file: a pre-locklive holder that cannot name itself. Fail safe.
    os.remove(os.path.join(lock, "owner"))
    case("ownerless lock (older check.sh)", False, tree, {})

    # PLATFORMIO_BUILD_CACHE_DIR must be honoured, or --committed trial
    # worktrees would look at the wrong lock and never guard anything.
    alt = os.path.join(work, "altcache")
    os.makedirs(os.path.join(alt, "x4pro.lock"))
    with open(os.path.join(alt, "x4pro.lock", "owner"), "w") as f:
        f.write("%d elsewhere\n" % os.getpid())
    case("honours PLATFORMIO_BUILD_CACHE_DIR", False, tree,
         {"PLATFORMIO_BUILD_CACHE_DIR": alt})

    # A --committed trial worktree lives under TMPDIR with NO .xteink-workspace
    # above it, so a marker-only test would no-op for every --committed build --
    # which is precisely the release-gate builds. check.sh exports the inherited
    # cache dir there, and that must be enough on its own.
    trial = os.path.join(work, "xteink-committed-abc123")
    os.makedirs(trial)
    assert _workspace_above(trial) is None, "fixture is wrong: marker found above the trial dir"
    case("--committed trial worktree, no marker above it", False, trial,
         {"PLATFORMIO_BUILD_CACHE_DIR": alt})

    print("%d checks, %d failed" % (PASS + FAIL, FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())

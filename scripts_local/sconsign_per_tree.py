"""Give this tree its own SCons signature database.

PlatformIO conflates two unrelated things. From its builder/main.py:

    env.SConsignFile(os.path.join(
        "$BUILD_CACHE_DIR" if env.subst("$BUILD_CACHE_DIR") else "$BUILD_DIR",
        ".sconsign%d%d" % (sys.version_info[0], sys.version_info[1])))

With no build cache dir the signature database lives in BUILD_DIR, which is
per-tree and private. The moment you set a SHARED BUILD_CACHE_DIR -- which this
workspace does, so a new tree is mostly cache hits instead of a cold compile --
the database moves into that shared directory too.

So sharing the object cache, which we want, forces sharing the signature
database, which we do not. SCons writes that file by renaming a temp file over
it, so any two builds finishing near each other race, and the loser gets:

    FileNotFoundError: [Errno 2] No such file or directory:
      '.pio-cache/.sconsign314.tmp' -> '.pio-cache/.sconsign314.dblite'

**The firmware lock does not prevent this.** That lock serialises DEVICE builds
only; the host and simulator builds run unlocked in every tree and write the
same database. Five sessions gating at once is an ordinary evening here, and
this has been misread as concurrent-build corruption -- for which the standing
advice, "retry with the workspace quiet", works only because retrying
eventually wins the race.

PlatformIO runs pre-scripts AFTER its own SConsignFile call (main.py: 160 then
167), so calling it again here wins. The object cache stays shared and keeps
saving cold builds; only the database becomes private.
"""

import os

Import("env")  # noqa: F821  -- injected by SCons

build_dir = env.subst("$BUILD_DIR")  # noqa: F821
if build_dir:
    os.makedirs(build_dir, exist_ok=True)
    env.SConsignFile(  # noqa: F821
        os.path.join(build_dir, ".sconsign")
    )

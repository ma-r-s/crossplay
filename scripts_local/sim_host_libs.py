"""Link the host libraries the simulator's own headers assume.

The simulator's MD5Builder is two implementations behind one header:
CommonCrypto on macOS, which the system links for free, and `<openssl/md5.h>`
on Linux, which needs `-lcrypto` at link time. The library declares neither, so
on Linux the whole thing compiles and then fails in `ld` with three undefined
references to MD5_Init / MD5_Update / MD5_Final, named against KOReaderSync
rather than against the simulator.

Nobody upstream hits this: their ci.yml builds the firmware and runs cppcheck
and clang-format, and never builds the simulator on Linux. This fork's CI does,
which is how it surfaced, and a Linux contributor cloning the repo would hit it
on their first build.

Kept as a `pre:` hook rather than a flag in platformio.sim.ini because the flag
is wrong on macOS: there is no linkable libcrypto there, so a static entry in
build_flags would trade a Linux failure for a macOS one.
"""

import sys

Import("env")  # noqa: F821  (SCons injects this)

if sys.platform.startswith("linux"):
    env.Append(LIBS=["crypto"])  # noqa: F821
    print("[sim-host-libs] linking libcrypto for the simulator's MD5Builder")

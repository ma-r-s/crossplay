# Testing and Debugging

CrossPoint runs on real hardware, so debugging usually combines local build checks and on-device logs.

## Local checks

Make sure `clang-format` 21+ is installed and available in `PATH` before running the formatting step.
If needed, see [Getting Started](./getting-started.md).

```sh
./bin/clang-format-fix
pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
pio run
```

## Flash and monitor

Flash firmware:

```sh
pio run --target upload
```

Open serial monitor:

```sh
pio device monitor
```

Optional enhanced monitor:

```sh
python3 -m pip install pyserial colorama matplotlib
python3 scripts/debugging_monitor.py
```

## Useful bug report contents

- Firmware version and build environment
- Exact steps to reproduce
- Expected vs actual behavior
- Serial logs from boot through failure
- Whether issue reproduces after clearing `.crosspoint/` cache on SD card

## Common troubleshooting references

- [User Guide troubleshooting section](../../USER_GUIDE.md#7-troubleshooting-issues--escaping-bootloop)
- [Webserver troubleshooting](../troubleshooting.md)

## The environment a check runs in is part of the check

Three failures in one night, all the same shape: something was true where the
check was run and not true where it matters. None of them is a bug in the code
under test, and none of them looks like an environment problem from the inside.

**A suite can pass where its author runs it and fail where it actually runs.**
`host-tests/gatepath/run.sh` is green standalone and red inside every
`./scripts_local/check.sh --committed`, because `--committed` exports
`CHECK_BUILD_RELEASE_ENVS=1` and the suite inherits it -- so the four cases
that assert "no device build needed" get "needed (release envs requested)" and
fail. A suite testing a decision must CONTROL that decision's inputs, not
inherit the ambient ones. Reproduce before diagnosing: run it once bare and
once with the variable set, and the answer takes ten seconds instead of an
afternoon reading a diff that is fine.

**The simulator is a place where things are true that are not true on the
device.** Two of tonight's three came from reading it as evidence about
hardware. The bridge URL override is compiled out of device builds
(`#if !defined(FREEINK_NET_WOLFSSL)`), so a sync that works in the simulator
against a local service cannot happen on hardware at all until the real
hostname exists. The simulator also fakes buttons the X4 Pro does not have and
refreshes instantly where the panel takes hundreds of milliseconds. Before
claiming a simulator run proves something about the device, check whether the
code path you exercised is even compiled there.

**A merge is a new tree, and it can be bigger than either input.** Checking a
merge for what it silently REMOVED -- a shelf row, an icon, a line from a test
list -- is the well-known half. The other half is what it ADDED: a merge that
brings a whole new app in makes every size measurement taken before it stale,
and a clean compile says nothing about headroom. Re-measure flash after any
merge you intend to ship, and see the flash-budget notes for why an image that
fits here can still be un-installable on an older device.

**Reproduce the failure where it fails, BEFORE you believe the fix.** This is
the positive form of the rule and the only one on this page you can act on
rather than merely avoid. `host-tests/cacheguard` was red in CI and green here,
three checks, for as long as the cache cap had existed. The cause was one line:
`stat -f '%m %z %N'` is BSD, and on GNU `-f` means `--file-system`, so the flag
is consumed, the format is not a filesystem format, `2>/dev/null` eats the
complaint, the pipeline yields nothing, the deletion loop runs zero iterations,
and the prune deletes NOTHING -- having already printed `trimming oldest
first`. On Linux the guard announced it was guarding and let the disk fill.

You do not need the other operating system to reproduce that. A stub named
`stat` early on `PATH`, answering `--version` the way GNU does and implementing
`-c` on top of the real BSD `stat` so the values stay genuine, reproduces CI's
three failures verbatim on this machine.

**And the stub is itself a thing that can fail silently.** The first version of
that one passed the mutation: it fell through to the real BSD `stat` for `-f`,
so it never emulated the half that breaks. The old, broken line sailed through
a harness built to catch it. A surviving mutant is either a missing test or a
broken harness, and assuming the first is how a green run gets recorded for a
fix nobody exercised. When a mutation survives, suspect your harness before you
conclude the test has a gap.

## A check that fails silently is worse than one that fails loudly

**In a shell harness, `set -o pipefail` turns "matched nothing" into "the
script is over."** `ls dir/*.part` exits non-zero when the glob matches
nothing, so the assertion that PASSES by finding nothing kills the run -- and
because the kill happens after the last line it printed, the output reads as a
clean finish with the remaining assertions simply absent. Use `find`, or `||
true`, and never let a passing case be the one that exits. The same trap
applies to `grep -c` in a substitution and to `[ -n "$x" ] && ...` as the last
command in a loop body.

**In a build, an infrastructure fault presents as your code being broken.** A
full disk, a concurrent device build, a stale submodule pin and a warm cache
hiding a dependency change all surface as a compiler error naming no file of
ours. Before hunting a diff that a build blames, check `df -h` (the Avail
column, never the percentage -- this machine's APFS volumes share a container
and report 41% and 97% for the same free space) and check whether another tree
is building.

The connecting rule for both sections: the gate is the only thing a session can
certify itself with. Anything that makes a green look green when it did not
run, or a red look like your fault when it is not, costs far more than the
check was worth.

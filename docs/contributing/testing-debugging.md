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

**A filter is a hypothesis about what the failure will look like.** Grepping a
device log for `Frontlight|LIGHT|Device:` missed the loudest line in it --
ESP-IDF's `E (723) ledc: requested frequency 25000 ... can not be achieved`,
which contains none of those tokens and names the repair outright. The filter
was reasonable and it was wrong, and a filter that is wrong is indistinguishable
from a log that says nothing. Read the window unfiltered ONCE before narrowing
it; the line you did not predict is the one worth having.

The same applies to a `grep` over a codebase, a suite run against a subset, and
a screenshot taken at the moment you expected the bug -- every one of them
decides its own scope from an assumption about the answer.

And the reason it keeps happening, which is the part worth internalising:
**narrowing feels like rigour.** A grep with four tokens in it looks more
careful than `cat`, and it is less careful, because it has silently answered
the question it was meant to ask. The person who wrote that filter was
investigating the very fault it hid, and still built it from the subsystem's
name rather than from what the failure would say.

**And a summary is a filter over the source.** Everything above is one shape at
different scales: a grep is a filter over a log, a suite is a filter over the
behaviour, a gate is a filter over the languages, and a handoff note is a filter
over the code. Each drops what its author judged unimportant, and the thing it
drops is exactly what the next person needed -- which is why claims that travel
through a summary change shape, and why the fix is always the same one. Re-read
at the source before you act on it, especially when the summary came from
someone careful. A careful summary is a better filter, not an absent one.

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

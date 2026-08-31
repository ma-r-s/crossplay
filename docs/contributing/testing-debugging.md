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

## A check that fails silently is worse than one that fails loudly

Two shapes of this have cost real time, and they look nothing alike until you
line them up.

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

The connecting rule: the gate is the only thing a session can certify itself
with. Anything that makes a green look green when it did not run, or a red
look like your fault when it is not, costs far more than the check was worth.

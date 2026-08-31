# The object cache, and why it fills the disk

`check.sh` points every worktree at one shared PlatformIO object cache through
`PLATFORMIO_BUILD_CACHE_DIR`, so a tree that has never built before is mostly
cache hits rather than a cold compile. That is worth keeping. What it costs is
that the cache has no floor.

## What was measured

On 2026-08-30, with 31 worktrees in the workspace:

- **66 GB in 252,825 files**
- **not one file older than seven days**
- 13 GB free on a 926 GB disk

That second line is the whole design. **This is not stale junk accumulating.**
PlatformIO's `build_cache_dir` is content-addressed, and every code change in
every tree mints new object hashes that nothing ever removes. A week of thirty
trees building is a quarter of a million objects, all of them recent.

So **pruning by age frees nothing**, because nothing is old. The only policy
that works is a cap on SIZE, evicting oldest-first.

`pio system prune` does not help either: it prunes PlatformIO's own download and
package caches, not `build_cache_dir`. Its dry run offered 962 MB, none of it
the 66 GB.

## How it fails

Never as a disk warning. `[Errno 28] No space left on device` arrives from
inside the espressif32 builder while extracting framework libs the fork does not
even use, in a 50 KB traceback naming no file of ours. It reads exactly like the
concurrent-build corruption documented in the workspace guide, and it is not
that.

And the headroom you check is not the headroom you keep: a cold build writes
into the cache **as it runs**, so free space falls during the build. That is why
the floor is generous rather than tight.

## The guard

`scripts_local/cache-guard.sh`, sourced by `check.sh`.

```bash
./scripts_local/cache-guard.sh --status    # what is there
./scripts_local/cache-guard.sh --prune     # trim to the cap, oldest first
```

| Knob | Default | Meaning |
| --- | --- | --- |
| `CACHE_CAP_GB` | 25 | trim above this |
| `AVAIL_FLOOR_GB` | 10 | refuse to start device builds below this much free |

`check.sh` calls it **after acquiring the firmware build lock and before the
first device build**. That placement is the point: holding the lock is the only
moment no other tree is reading those objects. Pruning outside it deletes inputs
from under somebody's running build and surfaces as a link error naming no file
of ours -- the same illegible shape the guard exists to prevent.

When trimming cannot get the disk above the floor it refuses, and says so in a
sentence about the disk, with the two commands worth running.

## Two bugs the tests found, both about locks

The build lock is an **empty directory** -- that is how `mkdir` gives an atomic
mutex. So:

- an unqualified `find -type d -empty -delete` **deletes the lock the caller is
  holding**, and the next tree's `mkdir` then succeeds while a device build is
  already running. That is precisely the collision the lock prevents,
  reintroduced by a cleanup meant to be harmless. The sweep excludes `*.lock`.
- the same sweep removed the cache **root** when a prune emptied it, leaving
  later trees to lock against a directory that no longer meant anything.
  `-mindepth 1` keeps it.

Neither was visible by reading. `host-tests/cacheguard/run.sh` builds a
synthetic cache with known sizes and mtimes and asserts both, plus that age
alone never triggers a prune -- the measured fact the whole policy rests on.

## What else was large

For context, when this was written the cache was the *second* biggest thing on
the machine. `~/Library/Application Support/CrossOver` was 260 GB, and 25 merged
and clean worktrees were holding 41 GB of build output between them. A cache cap
is worth having, but it is not where the disk went.

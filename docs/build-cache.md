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

## Three causes, one signature

The reason this is hard to debug is that **three unrelated failures look
identical**: an error minutes into a run, from inside the espressif32 builder,
naming no file of ours.

| Cause | What stops it | Fixed? |
| --- | --- | --- |
| The disk is full | this guard's `AVAIL_FLOOR_GB` | **yes** |
| Two **device** builds at once | `check.sh`'s firmware lock | yes, unless bypassed |
| Any two builds racing the SCons database | a per-tree `.sconsign` | **yes** |
| Two builds racing shared `~/.platformio` | nothing but convention | **no** |
| The lock removed by the 900s stale path | `app/locklive`'s liveness check | **yes** |

**Four of the five are fixed; one is protected only by convention**, and that
distinction matters more than fixed-versus-not. "Retry with the workspace quiet"
is the folk advice for all five and cures some of them only by accident --
retrying eventually wins a race.

The one that is not fixed:

**The shared `~/.platformio` framework.** Every device build reaches into it and
it cannot be sharded (it is 10GB). A build that loses the race reports framework
headers missing -- `Arduino.h`, `HWCDC.h`, `esp32-hal.h` -- **which are present
on disk the moment you look**. Nothing prevents this except everyone going
through `check.sh` rather than a raw `pio run`, and the lock only helps if it is
actually taken.

### Fixed elsewhere: the lock's own stale path

`check.sh` used to remove a lock held longer than 900 seconds with no check that
the holder was still alive -- firing most readily when the machine is loaded,
which is exactly when a second concurrent build does the most damage. Fixed on
**`app/locklive`** (`7b1fd824`, plus ownership guards on the delete paths): the
lock records its holder's pid and the waiter uses `kill -0` on **that pid**, so
the decision never rests on matching a command shape. This workspace has already
had a `pkill -f` on a command-line shape kill a sibling session's release train,
because two sessions had identical wrapper idioms.

**A macOS trap worth knowing if you touch that code**: `pgrep -c` does not
exist in BSD pgrep. It exits 2 with a usage message, so the common
`N=$(pgrep -c -f "..." || echo 0)` yields a **silent constant zero**. For a
liveness check that is the dangerous direction: a probe that always answers
"holder is dead" turns the timer into an unconditional lock break, strictly
worse than the bug it replaces. Use `pgrep -f "..." | wc -l`, or better
`kill -0 "$PID"`, and assert in a test that the probe finds a known-live
process -- which is the only check that catches "never ran" rather than
"answered wrong".

Its signature is the same as the rest, so the standing advice -- "retry with the
workspace quiet" -- appeared to work, because retrying eventually wins the race.
The tell that it IS the race: `xteink-bf`'s gate failed on `gh_release_sticky`
with `x4pro` green, and the retry failed on `gh_release_x4pro` with `sticky`
green. Same commit, opposite envs. Nothing about the code distinguishes them.

Anyone debugging one of these is looking at three indistinguishable candidates,
so all three belong in the same place.

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

## The signature database is per-tree now

PlatformIO conflates two unrelated things, in `builder/main.py`:

```python
env.SConsignFile(os.path.join(
    "$BUILD_CACHE_DIR" if env.subst("$BUILD_CACHE_DIR") else "$BUILD_DIR",
    ".sconsign%d%d" % (sys.version_info[0], sys.version_info[1])))
```

With no build cache dir the database lives in `BUILD_DIR`, per-tree and private.
**Setting a shared `BUILD_CACHE_DIR` moves it into the shared directory too.** So
sharing the object cache -- which this workspace wants, because it makes a new
tree mostly cache hits -- forces sharing the signature database, which races.

`scripts_local/sconsign_per_tree.py` calls `SConsignFile` again with a per-tree
path. PlatformIO runs pre-scripts *after* its own call (main.py line 160, then
167), so ours wins. Measured: the shared cache dir gets **no** `.sconsign`, the
build dir gets its own, and 298 objects still land in the shared cache -- the
saving is kept and the race is gone.

### The simulator build is NOT safe to run in parallel

The workspace notes say device builds are serialised by the firmware lock while
"the simulator build stays parallel; it touches none of that". **That is wrong,
and it is the reason this fix has to cover the simulator env.** The simulator
build touches no ESP32 framework, but it uses the same SCons signature database
as everything else, so two trees building it at once race on the same file:

```
FileNotFoundError: .pio-cache/.sconsign314.tmp -> .pio-cache/.sconsign314.dblite
  (SCons dblite.py _os_replace, at SConsign.write())
```

Observed on 2026-08-31 on a simulator build that **compiled and linked
successfully and then died at the end**, with 37Gi free and the cache intact.
Clean on retry. That makes it the most frequently hit of the five causes, since
the simulator build is the one everybody runs constantly and the one the
documented model calls safe.

It is wired into **both** `[base]` in `platformio.ini` and the simulator env in
`platformio.sim.ini`. The simulator does not extend `base`, which is exactly the
gap that made the first version of this look like it did nothing: the script
never ran, and the build passed anyway.

One INI trap it cost an attempt to find: **a continuation line starting with `;`
inside a multi-line value is part of the VALUE, not a comment.** PlatformIO then
skips the whole list silently and the build still succeeds. Comments go above
the key.

## Never delete SCons state

The cache directory holds two unrelated kinds of thing, and only one of them is
cache:

- **content-addressed object files** -- disposable, that is the point
- **SCons and PlatformIO state at the root**: `.sconsign*.dblite`, lock
  directories, anything dotted

`.sconsign314.dblite` is the signature database every build reads and rewrites,
and it was **523 MB** in the real cache -- old, large, and therefore first in
line for an oldest-first sweep. Deleting it mid-build gives

```
FileNotFoundError: [Errno 2] No such file or directory:
  '.pio-cache/.sconsign314.tmp' -> '.pio-cache/.sconsign314.dblite'
```

as SCons renames its temp file into place and finds the target gone. Deleting it
*between* builds is quieter and worse: every tree cold-rebuilds and nothing says
why.

**Holding the firmware lock does not make this safe**, which is why the
exclusion is structural rather than a timing argument. That lock serialises
*device* builds only; the native simulator build runs unlocked in every tree and
uses the same database.

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

## What is still not fixed, and why it will bite someone

**Four mutable things are shared between every worktree in this workspace**:

1. the object cache (`.pio-cache`) -- capped by this guard
2. the SCons signature database -- now per-tree, see above
3. `~/.platformio`, ~10GB of frameworks and toolchains -- **unprotected**
4. one firmware build lock -- ownership-checked since `app/locklive`

Three of the four had no protection at all until 2026-08-30. The one that still
does not is `~/.platformio`.

It cannot be sharded: it is 10GB, every device build reaches into it, and
PlatformIO offers no per-tree equivalent. **Its only defence is the convention
that everybody goes through `check.sh` rather than a raw `pio run`** -- because
only `check.sh` takes the firmware lock.

A losing build reports framework headers missing -- `Arduino.h`, `HWCDC.h`,
`esp32-hal.h` -- **which are present on disk the moment you look**. Verified
that way on 2026-08-30: a gate failed on `sticky` while `x4pro` before it and
both release envs after it passed at the same commit, and all five named headers
existed seconds later.

A convention is exactly what a session under time pressure abandons, and this
one has already corrupted a sibling's build here once. If this bites again, the
fix is not another guard: it is making the lock impossible to bypass, or giving
the ESP32 builds a package directory each and paying the disk for it. Both are
larger than a size cap and are Mario's call.

## What else was large

For context, when this was written the cache was the *second* biggest thing on
the machine. `~/Library/Application Support/CrossOver` was 260 GB, and 25 merged
and clean worktrees were holding 41 GB of build output between them. A cache cap
is worth having, but it is not where the disk went.

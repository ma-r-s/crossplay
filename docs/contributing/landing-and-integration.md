# When a branch may land, and what to do when integration goes red

Written 2026-08-31, after eight branches queued behind one build lock for a
number that only the merged tree ever has.

## The bar for landing

**A branch lands when its host gate is green, EXCEPT when it touches code the
host target does not compile. Then it keeps its device build first.**

Ask the tool rather than judging by eye:

```bash
scripts_local/device-build-needed.sh --device-only
```

Exit 0 means the device gate is required before landing. Exit 1 means host-green
is sufficient. It is deliberately conservative: anything it cannot rule out
keeps its device build.

### Why the exception is not a formality

A per-branch device build mostly re-measures something that is superseded on the
next merge. Flash size is a property of the SUM, not of any branch: Instapaper
measured 79.5% alone and 78.8% once merged. The only run that ever measures what
ships is the release gate.

But that argument collapses for device-only code. **The simulator target does
not compile what sits behind `FREEINK_DEVICE_*` guards, what calls ESP-IDF
directly, or anything in the SDK driver layer.** For such a branch a green host
gate is not weak evidence, it is NO evidence, and the breakage would not be in
the branch's own logic -- it would be in code no host suite ever saw, and it
would become the base every later branch built on.

`app/sdkbump` is the case that settled it: a submodule bump carrying 69 upstream
commits of display driver, e-ink init and touch mirroring. Its host suites went
green before either device build had run.

## When the release gate goes red

The release gate is the device authority. When it fails, the suspects are
**every branch landed since the last GREEN release gate** -- not since the last
release, and not since this morning.

Find them:

```bash
# the commit the last green release gate verified
git tag --sort=-creatordate | head -1          # e.g. v1.11.1
git log --oneline --first-parent v1.11.1..origin/xteink
```

Every one of those merges has a host-green commit of its own, so bisect over the
merges rather than over individual commits.

**Before blaming a branch, rule out the machine.** A red gate here is ambiguous
by default: six distinct infrastructure faults all produce an error naming no
file of ours, and the advice for every one of them is "retry with the workspace
quiet". Check in this order, because each is cheaper than the last:

1. `df -h /System/Volumes/Data` -- read the **Avail** column, never the
   percentage. Both APFS volumes share one container and report wildly
   different percentages for identical free space.
2. `git submodule status` -- a leading `-` means the submodule was never
   initialised, and that presents as three unrelated suites failing.
3. `ls -d ~/.platformio/packages/framework-arduinoespressif32-libs` -- if it is
   missing, the shared package set is mid-reinstall. Wait and re-run.
4. `pgrep -fl "[b]in/pio run"` -- another tree building concurrently.

Only after those does the diff become the suspect.

## What this does not change

Every branch still gets a gate. This changes WHICH gate, not whether there is
one. A batch of branches behind a single gate would trade bisection for
throughput, and that is a bad trade at any speed.

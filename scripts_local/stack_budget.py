#!/usr/bin/env python3
"""Prove every FreeRTOS task's deepest call path fits in the stack it was given.

    ./scripts_local/stack-budget.sh            # instrumented build, then check
    ./scripts_local/stack-budget.sh --verbose  # and print the deepest path

Why this exists
---------------
CrossPlay v1.0.0 crashed on the first device it ever ran on. Not a logic bug: the
render task was created with 8192 bytes and one trip through
ShelfFolderActivity::render() needed 8016 of them, so the drawing underneath ran
off the end and tripped the stack canary. Nothing caught it because nothing
measured it, and the simulator hands every task a host thread whose stack is a
thousand times larger. See https://github.com/ma-r-s/crossplay/issues/1.

A host test cannot catch this. Frame sizes are a property of the target ABI, so
the only numbers worth trusting come from the compiler that builds the firmware.
GCC hands them over for free:

  -fstack-usage          one .su per object: each function's own frame
  -fcallgraph-info=su    one .ci per object: those frames plus who calls whom

Together they are a weighted call graph, and the deepest path from a task's entry
is a sum along it. Inlining needs no special case: an inlined callee has no frame
of its own because its locals are already counted in its caller.

What it cannot see
------------------
Stated plainly, because a budget that is trusted further than it can see is worse
than no budget:

  * calls through a function pointer. No edge exists, so such a path is cut short.
    Virtual calls have the same problem but are handled: see `fanout` below.
  * anything compiled without the flags, which is most of ESP-IDF.
  * recursion, which has no finite worst case. Cycles are reported, never summed.

A pass means "no path the compiler can see overflows", never "cannot overflow".
That is still the difference between catching this in CI and having a stranger
with a serial cable catch it.
"""

import argparse
import pathlib
import re
import sys

# Task name, the stack it asks for, where that number is written, the entry
# function, and the signature of any virtual call the entry makes.
#
# That last field is the difference between this script working and merely
# appearing to. renderTaskLoop reaches an activity through
# `currentActivity->render()`. A virtual call emits no edge, so walking real
# edges alone stops at the task loop's own 48-byte frame and declares 8144 bytes
# free on the one task known to have overflowed. The first version did exactly
# that and passed, which is how this comment came to be here.
#
# A fanout names the override set by signature. Every function whose demangled
# name contains it is a possible callee, and the task is charged the worst.
TASKS = [
    (
        "ActivityManagerRender",
        "CROSSPOINT_RENDER_TASK_STACK",
        "platformio.ini, default in src/activities/ActivityManager.cpp",
        "ActivityManager::renderTaskLoop",
        "::render(RenderLock&&)",
    ),
    (
        "fi_input",
        4096,
        "freeink-sdk/libs/hardware/InputManager/src/InputManager.cpp:175",
        "asyncTaskTrampoline",
        None,
    ),
    # AudioManager's "audio_play" task is deliberately absent. The SDK creates
    # one, but FREEINK_CAP_AUDIO is (MURPHY || M5) and AudioManager is not in
    # this firmware's lib_deps, so on an Xteink the task does not exist: no
    # symbol in firmware.map, no "audio_play" string in the binary.
    #
    # Listing it anyway produced a "not in the graph" line on every run, and
    # that line has to keep meaning "this task exists and I could not measure
    # it". A warning that is usually noise is one people learn to scroll past,
    # which costs exactly the warning that matters. Add it back the day
    # AudioManager joins lib_deps.
]

# A task that fits with 40 bytes to spare is one commit away from not fitting,
# and the interrupt frame lands on this stack too.
MIN_HEADROOM = 1024

NODE = re.compile(r'node:\s*\{\s*title:\s*"([^"]+)"\s+label:\s*"([^"]*)"', re.S)
EDGE = re.compile(r'edge:\s*\{\s*sourcename:\s*"([^"]+)"\s+targetname:\s*"([^"]+)"')
BYTES = re.compile(r"(\d+)\s+bytes")


def load(build_dir):
    """Read every .ci file.

    Node titles are mangled and are what edges reference, so the graph is keyed
    on those. The readable signature lives in the label, kept alongside so tasks
    and overrides can be named the way a person writes them.
    """
    frames, calls, pretty = {}, {}, {}
    files = list(build_dir.rglob("*.ci"))
    for ci in files:
        text = ci.read_text(errors="replace")
        for title, label in NODE.findall(text):
            parts = label.split("\\n")
            size = 0
            if (m := BYTES.search(parts[-1] if parts else label)) is not None:
                size = int(m.group(1))
            # One symbol can appear in several objects; keep the worst frame.
            if size > frames.get(title, -1):
                frames[title] = size
            pretty.setdefault(title, parts[0] if parts else title)
        for src, dst in EDGE.findall(text):
            calls.setdefault(src, set()).add(dst)
    return frames, calls, pretty, len(files)


def deepest(root, frames, calls):
    """Heaviest path from root. Returns (bytes, path, hit_cycle)."""
    best = {}

    def walk(fn, on_stack):
        if fn in on_stack:
            return 0, [f"{fn} (recursion)"], True
        if fn in best:
            return best[fn]
        own = frames.get(fn, 0)
        deep, path, cycle = 0, [], False
        for callee in calls.get(fn, ()):
            depth, trail, looped = walk(callee, on_stack | {fn})
            cycle = cycle or looped
            if depth > deep:
                deep, path = depth, trail
        result = (own + deep, [fn] + path, cycle)
        # Only memoise cycle-free answers; a cycle's cost depends on the caller.
        if not cycle:
            best[fn] = result
        return result

    return walk(root, frozenset())


def budget_of(spec):
    """Resolve a task's stack size.

    An int is the literal in the source. A string is a macro, looked up in
    platformio.ini and falling back to the #ifndef default beside the
    xTaskCreate call. Reading it beats restating it: the first run after the
    stack was raised to 16384 still checked against a hardcoded 8192 and failed
    a task that fits, which is the same drift this script exists to catch.
    """
    if isinstance(spec, int):
        return spec, "literal"
    ini = pathlib.Path("platformio.ini")
    if ini.exists():
        m = re.search(rf"^\s*-D{spec}=(\d+)", ini.read_text(), re.M)
        if m:
            return int(m.group(1)), "platformio.ini"
    for src in pathlib.Path("src").rglob("*.cpp"):
        m = re.search(
            rf"^#define\s+{spec}\s+(\d+)", src.read_text(errors="replace"), re.M
        )
        if m:
            return int(m.group(1)), f"{src} default"
    return None, "unresolved"


def macro_is_wired(spec):
    """Is the macro this budget is read from actually compiled into the task?

    Reading -D<spec> out of platformio.ini says what the flag is SET to, never
    that anything consumes it. For the whole of v1.12.45 the flag said 16384,
    this gate printed "9520 of 16384, headroom 6864" on every CI run, and
    xTaskCreatePinnedToCore was passed a literal 8192 -- because a sync had
    taken upstream's copy of ActivityManager.cpp wholesale and dropped the
    fork's override with it. Study and xkcd then panicked on the first repaint
    of their own header, and this gate was green the entire time.

    So: a use has to exist. Definition lines and comments do not count, because
    the dead state has both of those and nothing else.
    """
    for src in list(pathlib.Path("src").rglob("*.cpp")) + list(
        pathlib.Path("src").rglob("*.h")
    ):
        for line in src.read_text(errors="replace").splitlines():
            code = re.sub(r"//.*|/\*.*?\*/", "", line).strip()
            if spec not in code:
                continue
            if re.match(rf"#\s*(ifndef|ifdef|define|undef|if)\b", code):
                continue
            return True, str(src)
    return False, None


def match(needle, pretty, frames):
    """Symbols whose readable signature contains `needle`, worst frame first."""
    hits = [sym for sym, name in pretty.items() if needle in name]
    return sorted(hits, key=lambda s: -frames.get(s, 0))


def show(sym, pretty, frames):
    name = pretty.get(sym, sym)
    return f"{name if len(name) <= 62 else name[:59] + '...'} ({frames.get(sym, 0)})"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", default=".pio/build/x4pro")
    ap.add_argument("--verbose", action="store_true", help="print the deepest path")
    args = ap.parse_args()

    build = pathlib.Path(args.build_dir)
    frames, calls, pretty, n = load(build)
    if not frames:
        sys.exit(
            f"no call-graph data under {build}.\n"
            "Build with -fstack-usage -fcallgraph-info=su first; "
            "scripts_local/stack-budget.sh does that for you."
        )
    print(
        f"{len(frames)} functions, {sum(len(v) for v in calls.values())} calls, "
        f"from {n} objects\n"
    )

    failed = False
    for task, budget_spec, where, entry, fanout in TASKS:
        budget, budget_src = budget_of(budget_spec)
        if budget is None:
            print(f"  ?  {task:<24} stack {budget_spec!r} could not be resolved")
            failed = True
            continue
        if isinstance(budget_spec, str) and budget_src == "platformio.ini":
            wired, site = macro_is_wired(budget_spec)
            if not wired:
                print(
                    f"  !! {task:<24} {budget_spec} is set to {budget} in "
                    f"platformio.ini and NOTHING IN src/ USES IT, so the "
                    f"firmware runs on the #ifndef default and every number "
                    f"below would be measured against a stack this task does "
                    f"not have. Pass {budget_spec} to the task's create call."
                )
                failed = True
                continue
        found = match(entry, pretty, frames)
        if not found:
            # Not a warning. A task whose entry is missing is a task this run
            # did not check, and saying "all tasks fit" underneath that is a
            # false pass. It happens on an incremental build, where only the
            # recompiled objects have .ci files: 13 objects instead of 533 once
            # reported both real tasks missing and still printed success.
            print(f"  !! {task:<24} entry {entry!r} not in the graph, so NOT checked")
            failed = True
            continue
        used, path, cycle = deepest(found[0], frames, calls)

        if fanout:
            impls = match(fanout, pretty, frames)
            worst, worst_path = 0, []
            for impl in impls:
                depth, trail, looped = deepest(impl, frames, calls)
                cycle = cycle or looped
                if depth > worst:
                    worst, worst_path = depth, trail
            if worst:
                used += worst
                path = path + [f"[virtual: {len(impls)} overrides, worst below]"]
                path += worst_path

        head = budget - used
        if head < MIN_HEADROOM:
            failed = True
        print(
            f"  {'ok ' if head >= MIN_HEADROOM else '!! '}{task:<24}"
            f"{used:>6} of {budget:<6} headroom {head:>6}"
            f"{'   RECURSION' if cycle else ''}"
        )
        if args.verbose or head < MIN_HEADROOM:
            print(f"      {budget} from {budget_src} ({where})")
            for step in path[:14]:
                print(
                    f"        {step if step.startswith('[') else show(step, pretty, frames)}"
                )

    print()
    if failed:
        print(
            f"FAIL: a task is unchecked, or within {MIN_HEADROOM} bytes of its stack."
        )
        print(
            "Shrink the frames on that path, or raise the stack where it is declared."
        )
        print("If a task was not checked, the build was incremental and the graph is")
        print("partial: run scripts_local/stack-budget.sh, which builds all of it.")
        return 1
    # Say how many, not just that they fit. "All tasks fit" over an empty list is
    # true and worthless, and it is what this printed before an unchecked task
    # started counting as a failure.
    print(f"All {len(TASKS)} tasks fit, on every path the compiler can see.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

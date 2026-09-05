#!/usr/bin/env python3
"""Build the X4 Pro simulator for the browser.

The desktop simulator is a PlatformIO `native` build: the whole firmware
compiled for the host with SDL2 for the panel and a FreeRTOS shim on pthreads.
Emscripten supports all three of those, so the port is mostly a translation
rather than a rewrite -- take the exact compile lines PlatformIO already
produces and run them through em++ instead of g++.

    pio run -e simulator_x4_pro -t compiledb   # once, to write compile_commands.json
    source ../.emsdk/emsdk_env.sh
    python tools_local/wasm/build.py

Why compile_commands.json rather than a hand-written source list: the source
set, the include paths and the twenty-odd -D flags are all decided by
platformio.sim.ini and its lib dependency resolution. Duplicating them here
would be a second source of truth that silently drifts the first time someone
adds a library, and the failure mode is a browser build that is subtly not the
firmware.

Three things genuinely differ from the desktop build and are handled below:

  * SDL does not come along. The desktop simulator uses it for the panel and
    the keyboard; in a browser the page already has a canvas and already gets
    pointer events, and Emscripten's SDL port wants a blocking main loop on the
    thread that owns the GL context -- which the firmware's render task is not.
    So stubs/SDL.h + src/sdl_browser.cpp answer the twenty-three SDL functions
    the simulator's two HAL files call with plain memory, and src/wasm_main.cpp
    replaces simulator_main.cpp with a main() that starts the firmware on a
    worker and returns. The page then reads the composited frame straight out
    of the heap. Nothing in src/ or lib/ changes.
  * The SD card is a directory on the host. --preload-file packages it into a
    virtual filesystem mounted at the same path.
  * Networking is libcurl, which does not exist here. See stubs/.

How this got here, so nobody spends the same three hours
--------------------------------------------------------
The first version kept SDL and reached for Emscripten flags to make main()'s
`while (!display.shouldQuit())` loop survive a browser. Every one of them cost
a day and none of them worked: -sPROXY_TO_PTHREAD puts main on a worker but
leaves SDL's canvas on the wrong thread; OFFSCREENCANVAS_SUPPORT then breaks
the runtime's own main-thread getContext; OFFSCREEN_FRAMEBUFFER trips an assert
in the proxying queue; -sASYNCIFY finally booted but every screen drawn through
FreeInkUI arrived as a fragment.

The answer was not another flag. The browser simulator on the fork's earlier
base does not use SDL at all -- the page owns the canvas, the module exports a
framebuffer pointer and an input function, and main() returns immediately with
the firmware running on a worker. That is the design here, arrived at by stubbing SDL rather than
writing a second HAL, so our own HalDisplay keeps doing the compositing and the
grayscale preview comes along.

Two real bugs also fell out of that detour and the fixes are kept:

  * `char` is unsigned on both targets this firmware runs on (ESP32-C3 RISC-V,
    Apple Silicon) and signed on wasm32, so byte arithmetic above 127 differed
    here only. -fno-signed-char makes wasm match.
  * The object cache keyed on timestamps alone, so adding a flag rebuilt
    nothing. That is why -fno-signed-char was once written off as "not the fix"
    when no object had ever seen it. compile_one() now stamps the command line.
"""

import json
import os
import shlex
import pathlib
import shutil
import subprocess
import sys
import concurrent.futures

REPO = pathlib.Path(__file__).resolve().parents[2]
OUT = REPO / "site" / "emulator"
OBJ = REPO / ".pio" / "build" / "wasm"
STUBS = REPO / "tools_local" / "wasm" / "stubs"
SRC = REPO / "tools_local" / "wasm" / "src"
SDROOT = REPO / "tools_local" / "wasm" / "sdcard"

# Sources whose desktop implementation cannot work in a browser.
SKIP_SOURCES = {
    # Owns the SDL window and a main() that never returns. src/wasm_main.cpp
    # replaces it with one that starts the firmware on a worker and returns to
    # the browser's event loop.
    #
    # This set stays as small as it can be. The first attempt also skipped the
    # webserver sources, assuming they were the simulator's own desktop-only
    # plumbing; CrossPointWebServer is firmware code that several activities
    # link against, and the simulator's WebServer/WebSocketsServer shims build
    # under emcc unchanged. Skipping a source means stubbing every activity
    # that references it, which is almost never the cheaper trade.
    "simulator_main.cpp",
    # Replaced by src/http_canned.cpp, which answers from files on the card.
    # Hacker News and the Connections daily are the two apps that look broken
    # without a network, and the browser build has no sockets.
    "HttpDownloader.cpp",
}

# Our own translation units, compiled with the same flags as the rest by
# borrowing the first entry's command line.
EXTRA_SOURCES = [
    SRC / "sdl_browser.cpp",
    SRC / "wasm_main.cpp",
    SRC / "link_browser.cpp",
    SRC / "http_canned.cpp",
]


def load_entries():
    cc = REPO / "compile_commands.json"
    if not cc.exists():
        sys.exit("compile_commands.json missing -- run: pio run -e simulator_x4_pro -t compiledb\n"
                 "(plain `pio run` builds but does NOT write the database)")

    # The database IS the source list -- this build does not glob src/ -- so a
    # file added since it was written is simply not in the build. Adding
    # ToyBattleHowTo.cpp cost a link error, which is the lucky version: a new
    # translation unit nothing references yet would have been quietly absent,
    # and the page would have run the old code with no sign of it. Same family
    # as the header-dependency bug this file already carries a fix for.
    stale = [
        p
        for p in (REPO / "src").rglob("*.cpp")
        if p.stat().st_mtime > cc.stat().st_mtime
    ]
    if stale:
        names = ", ".join(sorted(p.name for p in stale)[:4])
        sys.exit(
            f"compile_commands.json is older than {len(stale)} source(s) ({names})\n"
            "It is the source list for this build, so anything newer is not in it.\n"
            "Run: pio run -e simulator_x4_pro -t compiledb"
        )

    entries = json.loads(cc.read_text())
    out = []
    for e in entries:
        src = pathlib.Path(e["file"])
        if not src.is_absolute():
            src = (REPO / src).resolve()
        if src.name in SKIP_SOURCES:
            continue
        if not src.exists():
            continue
        cmd = e.get("command")
        args = shlex.split(cmd) if cmd else list(e["arguments"])
        out.append((src, args))
    if not out:
        sys.exit("compile_commands.json has no usable entries")
    # Our own sources are not in compile_commands.json, so give them a real
    # entry's flags. Pick the widest one rather than the first: PlatformIO gives
    # each library only the include paths it declared, and wasm_main.cpp needs
    # the simulator's own headers (HalDisplay.h, the FreeRTOS shim) which only
    # the firmware's translation units see.
    template = max(
        (a for _, a in out), key=lambda a: sum(x.startswith("-I") for x in a)
    )
    for extra in EXTRA_SOURCES:
        if not extra.exists():
            sys.exit(f"missing {extra}")
        out.append((extra, template))
    return out


def translate(args, obj_path, is_c):
    """Rewrite one g++/gcc command line for em++/emcc."""
    out = ["emcc" if is_c else "em++"]
    out.append(f"-I{STUBS}")
    skip_next = False
    for i, a in enumerate(args[1:], start=1):
        if skip_next:
            skip_next = False
            continue
        if a == "-o":
            skip_next = True
            continue
        if a == "-c":
            continue
        # Host SDL2 headers. There is no SDL in this build at all; stubs/SDL.h
        # is what the simulator's HAL sees, and it must not be shadowed.
        if a.startswith("-I") and "SDL2" in a:
            continue
        if a.startswith("-l") and "SDL2" in a:
            continue
        # The source file itself is the last argument; re-added by the caller.
        if a.endswith((".cpp", ".c", ".cc")):
            continue
        if a.startswith("-D_THREAD_SAFE"):
            continue
        if out[0] == "emcc" and a.startswith("-std=gnu++"):
            continue
        out.append(a)
    out += [
        "-pthread",
        # `char` is unsigned on both targets this firmware really runs on --
        # ESP32-C3 (RISC-V) and Apple Silicon -- and signed on wasm32. Code that
        # does arithmetic on a byte above 127 therefore came out negative here
        # only, and the renderer rejected every row it was asked to draw at a
        # negative y. This makes wasm agree with both real targets.
        "-fno-signed-char",
        "-Wno-unused-command-line-argument",
        # -Oz throughout: this build is a download before it is a hot loop, and
        # an e-ink frame takes about a millisecond either way.
        "-Oz",
        "-c",
        "-o",
        str(obj_path),
    ]
    return out


def deps_unchanged(dep, obj_mtime):
    """True if every header in the .d file is older than the object.

    A missing .d means this object predates dependency tracking, so it cannot
    be trusted and is rebuilt. Same for a header that has since been deleted --
    unreadable is treated as changed, never as fine.
    """
    if not dep.exists():
        return False
    try:
        text = dep.read_text()
    except OSError:
        return False
    # "obj: src a.h b.h \" across continuation lines. Drop the target and the
    # line continuations; everything left is a real path.
    words = text.replace("\\\n", " ").split()
    for w in words[1:] if words and words[0].endswith(":") else words:
        if w.endswith(":"):
            continue
        try:
            if os.stat(w).st_mtime > obj_mtime:
                return False
        except OSError:
            return False
    return True


def compile_one(job):
    src, args = job
    rel = src.relative_to(REPO) if src.is_relative_to(REPO) else pathlib.Path(src.name)
    obj = OBJ / (str(rel).replace("/", "_") + ".o")
    obj.parent.mkdir(parents=True, exist_ok=True)
    dep = obj.with_suffix(obj.suffix + ".d")
    cmd = translate(args, obj, src.suffix == ".c") + ["-MMD", "-MF", str(dep), str(src)]
    # Skip work the object is already newer than -- but only if it was built by
    # this exact command line AND no header it includes has changed since.
    #
    # Timestamps on the .cpp alone are not enough, twice over. Adding a flag to
    # translate() leaves every object untouched and newer than its source, so
    # the next run silently relinks the old ones; that is how -fno-signed-char
    # was added, "tested", and written off as not the fix, when no object had
    # ever seen it. The command-line stamp fixed that half.
    #
    # The other half went unnoticed until 2026-08-11: editing only a HEADER also
    # left every object "fresh", because nothing compared against headers at
    # all. A change to a struct's default member value in a .h shipped a browser
    # build where one .cpp had been recompiled and its neighbour had not -- the
    # map picker took the fix and the screen reading the same struct did not.
    # That is worse than not rebuilding, because the artifact looks rebuilt and
    # only disagrees with itself. -MMD now records every header actually opened,
    # and all of them are checked.
    stamp = obj.with_suffix(obj.suffix + ".cmd")
    want = " ".join(cmd)
    fresh = (
        obj.exists()
        and obj.stat().st_mtime > src.stat().st_mtime
        and stamp.exists()
        and stamp.read_text() == want
        and deps_unchanged(dep, obj.stat().st_mtime)
    )
    if fresh:
        return (src, obj, 0, "")
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode == 0:
        stamp.write_text(want)
    return (src, obj, p.returncode, p.stderr)


def write_provenance():
    """Record the source revision this artifact was built from.

    check.sh compares the last COMMIT touching site/emulator against the last
    one touching src/lib/assets_local/tools_local/wasm, which is a proxy for
    "was the artifact built from this source". The proxy breaks whenever a
    source change produces an identical artifact -- a static_assert, a comment,
    a test-only edit -- because then a rebuild changes no bytes, there is
    nothing to commit, and the check can never be satisfied. That happened the
    first time the check ever fired, on the GameId assert.

    This file always changes when a rebuild happens, so the artifact gets a
    commit of its own and the ordering means what it is supposed to mean. It
    also makes the artifact say out loud what it was built from, which no
    amount of commit archaeology did before.
    """
    rev, dirty = "unknown", ""
    try:
        rev = subprocess.run(["git", "rev-parse", "HEAD"], cwd=REPO, capture_output=True, text=True).stdout.strip()
        if subprocess.run(["git", "status", "--porcelain", "--", "src", "lib", "assets_local", "tools_local"],
                          cwd=REPO, capture_output=True, text=True).stdout.strip():
            dirty = " (working tree had uncommitted source changes)"
    except OSError:
        pass
    (OUT / "BUILT_FROM").write_text(f"{rev}{dirty}\n")


def main():
    if not shutil.which("em++"):
        sys.exit("em++ not on PATH -- source .emsdk/emsdk_env.sh first")

    if "--clean" in sys.argv and OBJ.exists():
        shutil.rmtree(OBJ)
    entries = load_entries()
    print(f"compiling {len(entries)} translation units")
    OBJ.mkdir(parents=True, exist_ok=True)

    objs, failures = [], []
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count()) as ex:
        for src, obj, rc, err in ex.map(compile_one, entries):
            if rc == 0:
                objs.append(obj)
            else:
                failures.append((src, err))

    print(f"  ok {len(objs)}   failed {len(failures)}")
    if failures:
        print("\nfirst failures:")
        for src, err in failures[:8]:
            first = "\n".join(
                l for l in err.splitlines() if "error:" in l or "fatal" in l
            )[:600]
            print(f"\n--- {src}")
            print(first or err[:400])
        sys.exit(1)

    OUT.mkdir(parents=True, exist_ok=True)
    exported = [
        "_main",
        "_malloc",
        "_free",
        "_crossplay_frame_ptr",
        "_crossplay_frame_width",
        "_crossplay_frame_height",
        "_crossplay_frame_rotation",
        "_crossplay_consume_dirty",
        "_crossplay_touch",
        "_crossplay_key",
        "_crossplay_link_bind",
        "_crossplay_link_inbox",
        "_crossplay_link_inbox_size",
        "_crossplay_link_deliver",
    ]
    link = [
        "em++",
        *[str(o) for o in objs],
        "-o",
        str(OUT / "crossplay.js"),
        # No HTML shell: the site's own page is the front end (site/assets/
        # emulator.js), so the device boots inside the hero rather than on a
        # page of its own.
        # No SDL, no GL, no ASYNCIFY. main() returns as soon as it has started
        # the firmware worker, and the page drives everything after that.
        "-pthread",
        "-sPTHREAD_POOL_SIZE=8",
        # The simulator's FreeRTOS shim drops xTaskCreate's stackDepth on the
        # floor and hands the task to std::thread, so every task takes the
        # platform default. macOS gives a spawned thread 512KB; emscripten gives
        # a pthread 64KB, which is under what an EPUB layout pass wants.
        "-sSTACK_SIZE=4MB",
        "-sDEFAULT_PTHREAD_STACK_SIZE=4MB",
        "-sALLOW_MEMORY_GROWTH=1",
        "-sINITIAL_MEMORY=134217728",
        # The runtime must outlive main(): the firmware worker is still running
        # and every exported function below has to stay callable.
        "-sEXIT_RUNTIME=0",
        "-sMODULARIZE=1",
        "-sEXPORT_NAME=createCrossplay",
        "-sENVIRONMENT=web,worker",
        # ENV is how a page sets getenv-visible variables before main() runs;
        # the installer preview uses it for CROSSPLAY_AUTOSTART.
        "-sEXPORTED_RUNTIME_METHODS=ccall,cwrap,HEAPU8,HEAPU32,FS,ENV",
        "-sEXPORTED_FUNCTIONS=" + ",".join(exported),
        # Off for the shipped build: the page has no console to read them in
        # and they cost about a megabyte. Turn them back on while debugging.
        "-sASSERTIONS=0",
        # -Oz over -O2: this is a download before it is a hot loop, and an e-ink
        # frame takes 1ms either way.
        "-Oz",
        f"--preload-file={SDROOT}@/fs_",
    ]
    print("linking ...")
    p = subprocess.run(link, capture_output=True, text=True)
    if p.returncode != 0:
        print(p.stderr[-4000:])
        sys.exit("link failed")
    write_provenance()
    print(f"wrote {OUT}")

    # The site serves site/emulator/ with Content-Encoding: br, so what was
    # just linked has to be compressed before it is committed. Doing it here
    # rather than as a step to remember is the difference between a build and
    # a broken page. See tools_local/site/precompress.py for why.
    precompress = REPO / "tools_local" / "site" / "precompress.py"
    if precompress.exists():
        # The venv, not whatever interpreter happened to run this script.
        # brotli lives in .venv-study, and sys.executable here is the system
        # python3, so the build reported "the brotli module is missing" and
        # told you to install a package that was already installed -- with the
        # only actionable line being "do not commit site/emulator as-is", which
        # is exactly the state that ships a 34-second download.
        venv = REPO / ".venv-study" / "bin" / "python"
        runner = str(venv) if venv.exists() else sys.executable
        result = subprocess.run([runner, str(precompress)], capture_output=True, text=True)
        if result.returncode != 0:
            print(result.stdout + result.stderr)
            sys.exit("pre-compression failed; do not commit site/emulator as-is")
        print("pre-compressed for the site")

    # And say what to do with it. These files are NOT committed any more: they
    # go to the `emulator` GitHub release and the repository keeps the ~1KB
    # pointer beside them. Printing it here is the difference between that being
    # a rule in site/README.md and a rule anyone hits.
    print(
        "\nsite/emulator/ is not committed. To publish it:\n"
        "  python3 tools_local/site/publish_emulator.py   # uploads, rewrites the manifest\n"
        "Landing a source change is normally enough -- crossplay-emulator.yml does\n"
        "this on xteink by itself."
    )


if __name__ == "__main__":
    main()

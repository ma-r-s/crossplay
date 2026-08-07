#!/usr/bin/env python3
"""Build the X4 Pro simulator for the browser.

The desktop simulator is a PlatformIO `native` build: the whole firmware
compiled for the host with SDL2 for the panel and a FreeRTOS shim on pthreads.
Emscripten supports all three of those, so the port is mostly a translation
rather than a rewrite -- take the exact compile lines PlatformIO already
produces and run them through em++ instead of g++.

    pio run -e simulator_x4_pro          # once, to refresh compile_commands.json
    source ../.emsdk/emsdk_env.sh
    python tools_local/wasm/build.py

Why compile_commands.json rather than a hand-written source list: the source
set, the include paths and the twenty-odd -D flags are all decided by
platformio.sim.ini and its lib dependency resolution. Duplicating them here
would be a second source of truth that silently drifts the first time someone
adds a library, and the failure mode is a browser build that is subtly not the
firmware.

Three things genuinely differ from the desktop build and are handled below:

  * main() blocks in a `while (!display.shouldQuit())` loop, which would hang
    the browser's main thread forever. -sPROXY_TO_PTHREAD moves main onto a
    worker, so the loop is fine exactly as written and no firmware code needs an
    emscripten_set_main_loop rewrite.
  * The SD card is a directory on the host. --preload-file packages it into a
    virtual filesystem mounted at the same path.
  * Networking is libcurl, which does not exist here. See stubs/.

KNOWN BROKEN, and the reason the site does not embed this yet
------------------------------------------------------------
Upstream's own screens render correctly -- Boot, Home, both shelf folders, the
EPUB reader, the Wi-Fi picker -- and clicks navigate between them. Every screen
drawn through FreeInkUI / Toybox (`src/apps_local/`) renders as a fragment:
a few tiles or a corner bracket, crammed small and offset, with the rest of the
panel left white.

The tell is in the log. Entering any of them floods:

    [ERR] [GFX] !! Outside range (10, -242) -> (-242, 469)

so the renderer is being handed negative y and rejecting those rows. The
transform shown is (x, y) -> (y, 479 - x), which is the expected portrait
rotation on a natively-landscape panel, so the rotation is not the problem --
the incoming coordinate already is.

Ruled out so far:

  * Not the canvas or compositing. The same fragments appear in an iframe, in
    the page, and standalone, and are stable across a further eight seconds
    rather than settling.
  * Not WebGL clearing the drawing buffer between composites. Handing the
    module a context with preserveDrawingBuffer via -sGL_PREINITIALIZED_CONTEXT
    changed the output not at all (byte-identical mean).
  * Not char signedness, which was the best guess: `char` is unsigned on both
    real targets (ESP32-C3 RISC-V, Apple Silicon) and signed on wasm32, so byte
    arithmetic above 127 differs. -fno-signed-char is kept below because it
    makes wasm match the targets and is correct regardless, but it did not fix
    this.

Next thing to try: the remaining 32/64-bit divergence between this build and
the desktop one. `long` is 8 bytes on arm64 macOS and 4 on wasm32, and
FreeInkUI passes geometry through several small structs. A layout arithmetic
overflow there would produce exactly this -- right-shaped elements at the wrong
scale and a negative origin -- while upstream's GfxRenderer path, which does
its own integer maths, stays fine.
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
SDROOT = REPO / "tools_local" / "wasm" / "sdcard"

# Sources whose desktop implementation cannot work in a browser. Each is
# replaced by a file of the same name under stubs/, which is put first on the
# include path.
SKIP_SOURCES = {
    # Nothing yet. Kept because the first attempt skipped the webserver sources
    # on the assumption they were the simulator's own desktop-only plumbing;
    # CrossPointWebServer is firmware code that several activities link against,
    # and the simulator's WebServer/WebSocketsServer shims build under emcc
    # unchanged. Skipping a source here means every activity that references it
    # must also be stubbed, which is almost never the cheaper trade.
}


def load_entries():
    cc = REPO / "compile_commands.json"
    if not cc.exists():
        sys.exit("compile_commands.json missing -- run: pio run -e simulator_x4_pro")
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
        # Host SDL2 headers; emscripten ships its own port.
        if a.startswith("-I") and "SDL2" in a:
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
        "-sUSE_SDL=2",
        "-pthread",
        # `char` is unsigned on both targets this firmware really runs on --
        # ESP32-C3 (RISC-V) and Apple Silicon -- and signed on wasm32. Code that
        # does arithmetic on a byte above 127 therefore came out negative here
        # only: the Jaipur and Solitaire menus asked the renderer to draw at
        # y=-242 and it rejected every such row, so those screens arrived as
        # fragments while pure 1-bit screens were fine.
        "-fno-signed-char",
        "-Wno-unused-command-line-argument",
        "-O2",
        "-c",
        "-o",
        str(obj_path),
    ]
    return out


def compile_one(job):
    src, args = job
    rel = src.relative_to(REPO) if src.is_relative_to(REPO) else pathlib.Path(src.name)
    obj = OBJ / (str(rel).replace("/", "_") + ".o")
    obj.parent.mkdir(parents=True, exist_ok=True)
    cmd = translate(args, obj, src.suffix == ".c") + [str(src)]
    # Skip work the object is already newer than. Header changes are not
    # tracked, so pass --clean when touching anything under stubs/.
    if obj.exists() and obj.stat().st_mtime > src.stat().st_mtime:
        return (src, obj, 0, "")
    p = subprocess.run(cmd, capture_output=True, text=True)
    return (src, obj, p.returncode, p.stderr)


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
    link = [
        "em++",
        *[str(o) for o in objs],
        "-o",
        str(OUT / "crossplay.html"),
        "-sUSE_SDL=2",
        "-pthread",
        # main() blocks in `while (!display.shouldQuit())`, which would freeze
        # the browser. Two ways out, and the first two attempts took the wrong
        # one: PROXY_TO_PTHREAD moves main to a worker, but then SDL's canvas is
        # on the wrong thread -- transferring it (OFFSCREENCANVAS_SUPPORT) makes
        # the runtime's own main-thread getContext fail, and proxying GL to it
        # (OFFSCREEN_FRAMEBUFFER) trips an assert in the proxying queue.
        #
        # ASYNCIFY instead keeps main on the browser's main thread, where SDL
        # already expects its canvas, and unwinds the stack at the SDL_Delay(1)
        # the loop already calls once a frame. No firmware code changes, no
        # cross-thread canvas, and the render task never touches SDL anyway --
        # it sets pendingPresent and main flushes it.
        # The panel driver pushes only the region that changed, exactly as it
        # does on the device. WebGL clears the drawing buffer after every
        # composite unless asked not to, so everything outside the dirty
        # rectangle disappeared and screens rendered as fragments. Let the page
        # create the context with preserveDrawingBuffer and hand it in.
        "-sGL_PREINITIALIZED_CONTEXT=1",
        "-sASYNCIFY",
        "-sASYNCIFY_STACK_SIZE=65536",
        "-sPTHREAD_POOL_SIZE=8",
        "-sALLOW_MEMORY_GROWTH=1",
        "-sINITIAL_MEMORY=134217728",
        "-sEXIT_RUNTIME=0",
        "-sASSERTIONS=1",
        "-O2",
        f"--preload-file={SDROOT}@/fs_",
        "--shell-file",
        str(REPO / "tools_local" / "wasm" / "shell.html"),
    ]
    print("linking ...")
    p = subprocess.run(link, capture_output=True, text=True)
    if p.returncode != 0:
        print(p.stderr[-4000:])
        sys.exit("link failed")
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()

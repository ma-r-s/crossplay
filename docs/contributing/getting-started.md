# Getting Started

This guide builds and runs **CrossPlay** locally. CrossPlay is a fork of
CrossPoint Reader, so most of what is here also applies upstream, but the
repository, the branch and the build environments are all different. If you
came looking for CrossPoint itself, it lives at
[crosspoint-reader/crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader).

## Read this before you build anything

> **Never build or flash without naming an environment.** `platformio.ini` sets
> `default_envs = default`, and `[env:default]` is upstream's: it defines
> `FREEINK_DEVICE_X4` and `FREEINK_DEVICE_X3`, which are the **ESP32-C3**
> devices. CrossPlay is for the **ESP32-S3** Xteink X4 Pro and Seeed reTerminal
> Sticky, and writing an S3 image to a C3 device used to brick it. Every build
> and every upload in this guide carries `-e <env>` for that reason.

The environments you will actually use:

| Env                | What it is                                                     |
| ------------------ | -------------------------------------------------------------- |
| `x4pro`            | Xteink X4 Pro firmware, debug log level, dev serial bridge on. |
| `sticky`           | Seeed reTerminal Sticky, same shape.                           |
| `simulator_x4_pro` | A desktop build: SDL2 plus a FreeRTOS shim. No device needed.  |

`gh_release_x4pro` and `gh_release_sticky` are what a tag builds; CI builds
them, you do not need to.

## Prerequisites

- PlatformIO Core (`pio`) or VS Code + PlatformIO IDE
- Python 3.8+
- `clang-format` **21**, on your `PATH` as `clang-format-21`.
  `bin/clang-format-fix` prefers that exact name and falls back to plain
  `clang-format`, which on a current machine is 22. 22 reformats files 21 leaves
  alone, so the fallback shows churn you did not write and CI then rejects.
- SDL2, for the simulator env only (`brew install sdl2`, or your distribution's
  `libsdl2-dev`)
- A USB-C cable and an X4 Pro or a Sticky, **only** for on-device testing. The
  simulator and the host test suites need no hardware.

If `./bin/clang-format-fix` fails with either of these errors, install
clang-format 21:

- `clang-format: No such file or directory`
- `.clang-format: error: unknown key 'AlignFunctionDeclarations'`

Examples:

```sh
# Debian/Ubuntu (try this first)
sudo apt-get update && sudo apt-get install -y clang-format-21

# If the package is unavailable, add LLVM apt repo and retry
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 21
sudo apt-get update
sudo apt-get install -y clang-format-21

# macOS (Homebrew): llvm@21 ships clang-format 21, unversioned, keg-only
brew install llvm@21
ln -s "$(brew --prefix llvm@21)/bin/clang-format" "$(brew --prefix)/bin/clang-format-21"
```

Then verify:

```sh
clang-format-21 --version
```

It must report 21. `bin/clang-format-fix` itself only refuses versions **below**
21, so a newer one runs happily and quietly reformats files CI would leave
alone; the wrapper cannot tell you that, which is why the version is pinned
here.

## Clone and initialize

```sh
git clone https://github.com/ma-r-s/crossplay
cd crossplay
git submodule update --init --recursive
```

**The submodule step is not optional.** `.gitmodules` pins `freeink-sdk`, which
is the platform layer every build compiles against. It tracks a branch rather
than upstream's default, so take the commit the superproject records and do not
update it casually. Without it the first build
fails on missing headers rather than on anything you did. `git clone
--recursive` does the same thing in one step; the two-step form is here because
it is also the fix for a clone you already made.

Enable the repository-managed Git hooks (once per clone):

```sh
git config core.hooksPath .githooks
chmod +x .githooks/pre-commit
```

## Build

```sh
./scripts_local/check.sh --tests        # host suites only, no device build (fast)
./scripts_local/check.sh                # host suites plus the device and simulator builds
```

`check.sh` is the entry point rather than a convenience wrapper: it takes a
workspace-wide build lock, and concurrent PlatformIO builds race the shared
`~/.platformio` and fail on framework headers that have nothing to do with your
diff.

**Read its verdict by grepping for the token, never by tailing the output:**

```sh
./scripts_local/check.sh --committed 2>&1 | tee "$out"
grep -o 'CHECKSH-VERDICT: [a-z-]*' "$out"
```

`green` and `host-green-device-skipped` pass. `withheld` (behind origin, or a
drifted submodule) and `failed` do not, and NOTHING AT ALL means the run never
reached its verdict -- also not a pass. `tail -1` returns a background wrapper's
`[exited with code 0]` rather than the gate's line, and `$?` is whatever your
own pipeline ended with; the token cannot be defeated by either. The exit code
is real too (0 pass, 1 failed, 3 withheld) but only when nothing wraps the
call.

To build one environment directly:

```sh
pio run -e x4pro
pio run -e simulator_x4_pro
```

## Flash

```sh
pio run -e x4pro --target upload      # or -e sticky
```

Once a device has CrossPlay on it, Developer Mode reflashes it over Wi-Fi with
no cable at all: [docs/developer-mode.md](../developer-mode.md). Installing
onto a device for the first time, by browser or by esptool, is
[docs/install.md](../install.md); if a flash goes wrong,
[docs/fix-bricked-xteink.md](../fix-bricked-xteink.md) is the way back.

## First checks before opening a pull request

```sh
./bin/clang-format-fix
pio check --fail-on-defect high
./scripts_local/check.sh --committed
```

`--committed` verifies `HEAD` instead of your working directory. Uncommitted
work masks a broken commit, which is how three apps once shipped against
symbols that were never committed while every check ran green.

Pull requests go into **`xteink`**, this fork's default branch, not `develop`.
CI compiles `gh_release_x4pro`, `gh_release_sticky` and `simulator_x4_pro` on a
runner, so you do not have to build for a device to land. Note what that does
**not** cover: `x4pro` and `sticky`, the dev envs in the table above, carry
`CROSSPOINT_DEV_SERIAL_BRIDGE` and CI never compiles them. Only your local
`check.sh` does.
[Landing and integration](./landing-and-integration.md) covers which gate a
branch actually needs and what to do when integration goes red.

## What to read next

- [LOCAL_SCOPE.md](../../LOCAL_SCOPE.md) -- what this fork owns and what it
  turns down. Read it before writing any code.
- [Architecture Overview](./architecture.md)
- [Development Workflow](./development-workflow.md)
- [Testing and Debugging](./testing-debugging.md)
- [Landing and Integration](./landing-and-integration.md)
- [Touch and UI Development](./touch-and-ui.md)
- [docs/building-apps.md](../building-apps.md) and
  [docs/shelf.md](../shelf.md) -- how an app on this fork is put together, and
  the contract it has with the shelf.

"""Bring the CrossPoint simulator up to this branch.

The simulator library (crosspoint-reader/crosspoint-simulator) tracks
CrossPoint's `develop`. This fork sits on `feat-touch-ui`, which is ahead of it,
and two things main.cpp uses are missing from the simulator's stubs:

  * BoardConfig::BoardProfile has no `input` member
  * Arduino.h does not declare digitalRead / digitalWrite

Header stubs in sim-stubs/ cannot fix these: a library's own include path wins
over the project's -I, so its BoardConfig.h and Arduino.h always shadow ours.
Patching the fetched copy is the only thing that works, so this runs as a `pre:`
hook on every simulator build.

Deliberately NOT sent upstream as a PR. Mario wants to see how CrossPoint solves
the same problem and compare, and a merged PR would make their answer ours.
See docs/crosspoint-migration.md.

Every edit is idempotent, and anything unexpected is reported rather than
swallowed -- when the simulator catches up, this should start saying so.
"""

import os
import pathlib

Import("env")  # noqa: F821  (SCons injects this)


def patch(path, anchor, replacement, what):
    if not path.exists():
        print(f"[sim-catchup] MISSING {path.name}: cannot apply '{what}'")
        return
    text = path.read_text()
    if replacement in text:
        return  # already applied this build cycle
    if anchor not in text:
        # Upstream changed shape. Either they fixed it, or they moved it.
        print(f"[sim-catchup] '{what}' no longer applies -- the simulator has "
              f"changed. Check whether the patch is still needed, then delete it.")
        return
    path.write_text(text.replace(anchor, replacement, 1))
    print(f"[sim-catchup] applied: {what}")


src = pathlib.Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV") / "simulator" / "src"

patch(
    src / "BoardConfig.h",
    "struct BoardProfile {\n  Board board;\n  const char *name;\n};",
    "struct InputPins {\n"
    "  int8_t back = -1, confirm = -1, left = -1, right = -1;\n"
    "  int8_t up = -1, down = -1, power = -1;\n"
    "  bool powerActiveHigh = false;\n"
    "};\n\n"
    "struct BoardProfile {\n  Board board;\n  const char *name;\n  InputPins input = {};\n};",
    "BoardProfile.input (mirrors the SDK's InputPins)",
)

arduino = src / "Arduino.h"
if arduino.exists() and "inline int digitalRead(int)" not in arduino.read_text():
    if "digitalRead" in arduino.read_text():
        print("[sim-catchup] Arduino.h now declares digitalRead -- drop this patch.")
    else:
        with arduino.open("a") as fh:
            fh.write("\n// sim-catchup: the simulator has no GPIO; reads are inert.\n"
                     "inline int digitalRead(int) { return 0; }\n"
                     "inline void digitalWrite(int, int) {}\n")
        print("[sim-catchup] applied: digitalRead / digitalWrite")

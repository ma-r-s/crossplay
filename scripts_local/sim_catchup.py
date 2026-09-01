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

Every edit is idempotent, and anything unexpected is reported rather than
swallowed -- when the simulator catches up, this should start saying so.
"""

import os
import pathlib

Import("env")  # noqa: F821  (SCons injects this)


def patch(path, anchor, replacement, what, marker=None):
    """Insert `replacement` in place of `anchor`, once.

    `marker` is how "already applied" is decided, and it needs to be something
    the insertion leaves behind for good -- a symbol name, not the replacement
    text. Keying on the replacement looks equivalent and is not: two patches
    sharing an anchor each stop matching once the other inserts itself in the
    middle, so both re-apply on every build and the file collects duplicate
    declarations until it will not compile. Editing a patch has the same
    effect on a tree already patched by the old version.
    """
    if not path.exists():
        print(f"[sim-catchup] MISSING {path.name}: cannot apply '{what}'")
        return
    text = path.read_text()
    if (marker or replacement) in text:
        return  # already applied
    if anchor not in text:
        # Upstream changed shape. Either they fixed it, or they moved it.
        print(
            f"[sim-catchup] '{what}' no longer applies -- the simulator has "
            f"changed. Check whether the patch is still needed, then delete it."
        )
        return
    path.write_text(text.replace(anchor, replacement, 1))
    print(f"[sim-catchup] applied: {what}")


src = (
    pathlib.Path(env.subst("$PROJECT_LIBDEPS_DIR"))
    / env.subst("$PIOENV")
    / "simulator"
    / "src"
)

# HalStorage has no way to add to an existing file: openFileForWrite carries
# O_TRUNC on both the device and here. lib/hal gained openFileForAppend for the
# xkcd pack, which grows by a few records when the device fetches the comics
# published since the pack was built -- the alternative was rewriting a 90MB
# file to add 30KB. The simulator ships its own HalStorage, and a library's own
# headers shadow ours, so it needs the same method.
# lib/hal gained freeBytes so an app can refuse a large write instead of
# discovering the card was full halfway through it. The simulator ships its own
# HalStorage and a library's own headers shadow ours, so without this the
# method exists on the device and NOT in the simulator -- and the build stays
# green until some app actually calls it, at which point the error names a file
# that app never touched. Trivia was the first caller and found exactly that.
#
# The implementation is real rather than a stub. The simulator's card is a host
# directory, so statvfs is the honest answer, and its failure is a genuine
# "could not answer" -- which means the Unknown branch, the one carrying the
# whole safety argument, can actually be exercised here. A stub returning true
# would make that branch permanently untestable.
patch(
    src / "HalStorage.h",
    "  bool removeDir(const char *path);",
    "  bool freeBytes(uint64_t &out);\n"
    "  bool removeDir(const char *path);",
    "HalStorage::freeBytes (header)",
    marker="freeBytes",
)

patch(
    src / "HalStorage.cpp",
    "#include <sys/stat.h>",
    "#include <sys/stat.h>\n#include <sys/statvfs.h>",
    "HalStorage::freeBytes (statvfs include)",
    marker="sys/statvfs.h",
)

patch(
    src / "HalStorage.cpp",
    "bool HalStorage::begin() {",
    "bool HalStorage::freeBytes(uint64_t &out) {\n"
    "  struct statvfs st {};\n"
    "  if (statvfs(configuredStorageRoot().c_str(), &st) != 0) return false;\n"
    "  out = static_cast<uint64_t>(st.f_bavail) *\n"
    "        static_cast<uint64_t>(st.f_frsize);\n"
    "  return true;\n"
    "}\n"
    "\n"
    "bool HalStorage::begin() {",
    "HalStorage::freeBytes (impl)",
    marker="HalStorage::freeBytes",
)

patch(
    src / "HalStorage.h",
    "  bool removeDir(const char *path);",
    "  bool openFileForAppend(const char *moduleName, const char *path,\n"
    "                         HalFile &file);\n"
    "  bool removeDir(const char *path);",
    "HalStorage::openFileForAppend (header)",
    marker="openFileForAppend",
)

patch(
    src / "HalStorage.h",
    "  bool removeDir(const char *path);",
    "  bool openFileForUpdate(const char *moduleName, const char *path,\n"
    "                         HalFile &file);\n"
    "  bool removeDir(const char *path);",
    "HalStorage::openFileForUpdate (header)",
    marker="openFileForUpdate",
)


patch(
    src / "HalStorage.cpp",
    "std::vector<String> HalStorage::listFiles(",
    "bool HalStorage::openFileForAppend(const char *moduleName, const char *path,\n"
    "                                   HalFile &file) {\n"
    "  (void)moduleName;\n"
    "  file = open(path, O_RDWR | O_CREAT | O_APPEND);\n"
    "  return file.isOpen();\n"
    "}\n"
    "\n"
    "std::vector<String> HalStorage::listFiles(",
    "HalStorage::openFileForAppend (implementation)",
    marker="HalStorage::openFileForAppend(",
)

patch(
    src / "HalStorage.cpp",
    "std::vector<String> HalStorage::listFiles(",
    "bool HalStorage::openFileForUpdate(const char *moduleName, const char *path,\n"
    "                                   HalFile &file) {\n"
    "  (void)moduleName;\n"
    "  file = open(path, O_RDWR);\n"
    "  return file.isOpen();\n"
    "}\n"
    "\n"
    "std::vector<String> HalStorage::listFiles(",
    "HalStorage::openFileForUpdate (implementation)",
    marker="HalStorage::openFileForUpdate(",
)

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
            fh.write(
                "\n// sim-catchup: the simulator has no GPIO; reads are inert.\n"
                "inline int digitalRead(int) { return 0; }\n"
                "inline void digitalWrite(int, int) {}\n"
            )
        print("[sim-catchup] applied: digitalRead / digitalWrite")

# The simulator's ESPMock answers 1024*1024 to every heap question, always. Two
# guards in this firmware read getMaxAllocHeap() and decide whether one more
# buffer fits (util/DictZip.cpp, util/Dictionary.cpp), so against a constant
# three times larger than an X4 Pro's whole heap, neither branch has ever been
# taken outside a device. A frozen number also makes a leak invisible.
#
# src/platform/sim_heap.cpp counts what the firmware allocates and answers from
# the device's budget. This points ESPMock at it. Patched here rather than
# shadowed from sim-stubs/ for the reason at the top of this file: a library's
# own include path wins over the project's -I.
patch(
    src / "Arduino.h",
    "struct ESPMock {\n"
    "  uint32_t getFreeHeap() { return 1024 * 1024; }\n"
    "  void restart() {}\n"
    "  uint32_t getHeapSize() { return 1024 * 1024; }\n"
    "  uint32_t getMinFreeHeap() { return 1024 * 1024; }\n"
    "  uint32_t getMaxAllocHeap() { return 1024 * 1024; }\n"
    "};",
    'extern "C" {\n'
    "uint32_t crossplay_sim_free_heap();\n"
    "uint32_t crossplay_sim_heap_size();\n"
    "uint32_t crossplay_sim_min_free_heap();\n"
    "uint32_t crossplay_sim_max_alloc_heap();\n"
    "}\n\n"
    "struct ESPMock {\n"
    "  uint32_t getFreeHeap() { return crossplay_sim_free_heap(); }\n"
    "  void restart() {}\n"
    "  uint32_t getHeapSize() { return crossplay_sim_heap_size(); }\n"
    "  uint32_t getMinFreeHeap() { return crossplay_sim_min_free_heap(); }\n"
    "  uint32_t getMaxAllocHeap() { return crossplay_sim_max_alloc_heap(); }\n"
    "};",
    "ESPMock reports the device's heap budget, not a constant megabyte",
)

# main.cpp:788 gates the idle downclock on HalPowerManager::IDLE_POWER_SAVING_MS,
# which is this fork's own constant (lib/hal/HalPowerManager.h). The simulator
# ships its own HalPowerManager and `lib_ignore = hal` means its copy is the one
# that compiles, so the constant has to exist there too. Upstream has since
# split the idea into IDLE_DOWNCLOCK_MS and IDLE_LIGHT_SLEEP_MS and dropped the
# name we still use, which broke the simulator build in every freshly created
# worktree while trees with an older cached libdeps kept working.
#
# Same value as lib/hal's, so the simulator idles on the same schedule the
# device does rather than on a number picked to make it compile.
patch(
    src / "HalPowerManager.h",
    "  static constexpr unsigned long IDLE_DOWNCLOCK_MS = 500;",
    "  static constexpr unsigned long IDLE_POWER_SAVING_MS = 3000;\n"
    "  static constexpr unsigned long IDLE_DOWNCLOCK_MS = 500;",
    "HalPowerManager::IDLE_POWER_SAVING_MS (mirrors lib/hal)",
    marker="IDLE_POWER_SAVING_MS",
)

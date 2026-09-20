"""Bring the CrossPoint simulator up to this branch.

The simulator library (crosspoint-reader/crosspoint-simulator) tracks
CrossPoint's `develop`, which lags this fork's device HAL. main.cpp and lib/hal
use methods and symbols the fetched simulator does not have yet, and header
stubs in sim-stubs/ cannot supply them: a library's own include path wins over
the project's -I, so the simulator's own HalStorage.h / Arduino.h / BoardConfig.h
always shadow ours. Patching the fetched copy is the only thing that works, so
this runs as a `pre:` hook on every simulator build.

Deliberately NOT sent upstream as a PR. Mario wants to see how CrossPoint solves
the same problem and compare, and a merged PR would make their answer ours.

WRITING A PATCH

Every edit is idempotent (keyed on `marker`, a symbol the insertion leaves
behind for good -- never on the replacement text; see patch()). Two rules keep
a patch robust across a re-fetch:

  * Anchor on text the PRISTINE simulator package already contains, never on a
    line another patch inserts later in the same file. A warm tree hides the
    difference -- the other patch already ran -- and a fresh tree does not: the
    anchor is missing, the patch silently no-ops, and the build breaks 30s later
    as an undefined symbol in a file the patch never touched.
  * Keep the anchor inside the replacement, so the order patches run in cannot
    matter.

WHEN A PATCH STOPS APPLYING

A patch whose anchor is gone is NEVER harmless: either the simulator caught up
and the patch is dead (delete it), or its anchor drifted and the build is about
to break somewhere unrelated. So a non-applying patch is collected and FAILS the
build at the end (require_all_applied), loudly and by name -- not a one-line
warning mid-scroll, which is how three dead patches taught everyone to ignore
the line a real one needed (card #140). "Already applied" (the marker is present)
is silent and fine; only a missing anchor or a missing file is the failure.
"""

import pathlib

try:
    Import("env")  # noqa: F821  (SCons injects this)
except NameError:
    # Imported outside a SCons build -- host-tests/simcatchup drives patch() and
    # require_all_applied() against a fixture. The build wiring below is skipped.
    env = None

# Patches whose anchor (or file) was not found this run. Collected rather than
# only printed, so require_all_applied() can fail the build with the whole list
# at once instead of one easily-missed line each.
_UNAPPLIED = []


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
        _UNAPPLIED.append((what, f"{path.name} is not in the simulator package"))
        print(f"[sim-catchup] !! DID NOT APPLY '{what}': {path.name} is missing")
        return
    text = path.read_text()
    if (marker or replacement) in text:
        return  # already applied
    if anchor not in text:
        # Upstream changed shape: they fixed it (the patch is dead -- delete it),
        # or the anchor drifted (the build is about to break unrelated). Either
        # way this is collected and fails the build in require_all_applied(),
        # rather than scrolling past as one more warning.
        _UNAPPLIED.append((what, f"anchor not found in {path.name}"))
        print(
            f"[sim-catchup] !! DID NOT APPLY '{what}': anchor not found in {path.name}"
        )
        return
    path.write_text(text.replace(anchor, replacement, 1))
    print(f"[sim-catchup] applied: {what}")


def require_all_applied():
    """Fail the build when any patch did not apply.

    A non-applying patch is never harmless (see the module docstring), and the
    whole point of card #140 is that a real one must not read like the dead ones
    that scrolled past for weeks. So this is a hard, named failure at the end of
    the run -- the one moment the whole list is known -- not a per-patch warning.
    Raising SystemExit is how the parity check below already fails a build.
    """
    if not _UNAPPLIED:
        return
    lines = [
        "",
        "[sim-catchup] BUILD STOPPED: %d patch(es) did not apply." % len(_UNAPPLIED),
        "",
        "  Each is either landed upstream (delete the patch() call) or its anchor",
        "  drifted (the build is about to break in an unrelated file). Neither is",
        "  safe to ignore -- that is exactly the silence this failure replaces.",
        "",
    ]
    for what, why in _UNAPPLIED:
        lines.append(f"    - {what}: {why}")
    lines.append("")
    raise SystemExit("\n".join(lines))


def main(env):
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
        "  bool freeBytes(uint64_t &out);\n  bool removeDir(const char *path);",
        "HalStorage::freeBytes (header)",
        marker="freeBytes",
    )

    # The 2026-09-04 upstream sync brought USB Drive (mass storage) in: lib/hal
    # gained prepareForDeepSleep(), beginUsbDrive(), disconnectUsbDriveHost(),
    # endUsbDrive() and usbDriveState(), plus the UsbDriveState enum they answer
    # with. The simulator ships its own HalStorage that shadows lib/hal, so it
    # needs all six or the build stops dead -- which is exactly what happened.
    #
    # These ARE stubs, unlike freeBytes above, and deliberately so: there is no
    # USB host attached to a host-side simulator, so the honest answer is
    # Unsupported. That is a real state the enum already carries and the UI
    # already has to handle, not a pretend success. beginUsbDrive() returning
    # false means "this board cannot", which is the truth here.
    # Simulator dep drift, 2026-09-04: upstream crosspoint changed
    # HalGPIO::verifyPowerButtonWakeup() to take no arguments (it reads the settings
    # itself now), and main.cpp calls it that way. The published simulator package
    # still declares the older two-argument form, so the sim build fails on a call
    # that is correct for lib/hal. Bring the simulator's copy to the current
    # signature; its body was already a constant true, because the host wake path is
    # synthetic and there is no button to hold.
    patch(
        src / "HalGPIO.h",
        "  bool verifyPowerButtonWakeup(uint16_t requiredDurationMs,\n"
        "                               bool shortPressAllowed);",
        "  bool verifyPowerButtonWakeup();",
        "HalGPIO::verifyPowerButtonWakeup (drop the removed arguments)",
        marker="verifyPowerButtonWakeup();",
    )

    patch(
        src / "HalGPIO.cpp",
        "bool HalGPIO::verifyPowerButtonWakeup(uint16_t /*requiredDurationMs*/,\n"
        "                                      bool /*shortPressAllowed*/) {",
        "bool HalGPIO::verifyPowerButtonWakeup() {",
        "HalGPIO::verifyPowerButtonWakeup impl (drop the removed arguments)",
        marker="verifyPowerButtonWakeup() {",
    )

    # Upstream's X4 Classic support (2026-09-04 sync) calls BoardConfig::isX4Classic()
    # from the reader. The simulator ships its own BoardConfig.h that shadows the
    # SDK's, and its Board enum has no XteinkX4Classic at all -- the simulator is an
    # X4 Pro. So the honest answer here is a constant false, not a board test: there
    # is no X4 Classic to be.
    patch(
        src / "BoardConfig.h",
        "inline bool hasTouch()",
        "inline bool isX4Classic() { return false; }  // no X4 Classic profile in the simulator\n"
        "inline bool hasTouch()",
        "BoardConfig::isX4Classic (simulator has no such board)",
        marker="isX4Classic",
    )

    patch(
        src / "HalStorage.h",
        "class HalFile;",
        "class HalFile;\n"
        "\n"
        "enum class UsbDriveState : uint8_t {\n"
        "  Unsupported,\n"
        "  WaitingForHost,\n"
        "  Connected,\n"
        "  Ejected,\n"
        "  Disconnected,\n"
        "  IoError,\n"
        "};",
        "UsbDriveState enum (header)",
        marker="enum class UsbDriveState",
    )

    patch(
        src / "HalStorage.h",
        "  bool removeDir(const char *path);",
        "  void prepareForDeepSleep();\n"
        "  bool beginUsbDrive();\n"
        "  bool disconnectUsbDriveHost();\n"
        "  void endUsbDrive();\n"
        "  UsbDriveState usbDriveState() const;\n"
        "  bool removeDir(const char *path);",
        "HalStorage USB Drive + deep sleep (header)",
        marker="usbDriveState",
    )

    patch(
        src / "HalStorage.cpp",
        "bool HalStorage::begin() {",
        "void HalStorage::prepareForDeepSleep() {}\n"
        "bool HalStorage::beginUsbDrive() { return false; }\n"
        "bool HalStorage::disconnectUsbDriveHost() { return false; }\n"
        "void HalStorage::endUsbDrive() {}\n"
        "UsbDriveState HalStorage::usbDriveState() const {\n"
        "  return UsbDriveState::Unsupported;\n"
        "}\n"
        "\n"
        "bool HalStorage::begin() {",
        "HalStorage USB Drive + deep sleep (impl)",
        marker="HalStorage::usbDriveState",
    )

    # Upstream's new Library index asks every book for a modification time, to
    # skip re-reading one whose bytes have not changed
    # (lib/LibraryIndex/LibraryBuilder.cpp). The simulator's HalFile has no such
    # method.
    #
    # Real, not a stub, and the same reasoning as freeBytes above: the
    # simulator's card is a host directory, so fstat is the honest answer and
    # the index's skip path actually gets exercised here.
    #
    # The ENCODINGS DIFFER AND THAT IS FINE, which is worth saying out loud
    # because it looks like a bug. The device packs a FAT date and time into one
    # uint32 (HalStorage.cpp: date << 16 | time); this returns epoch seconds.
    # Nothing decodes the value: LibraryBuilder only compares it for equality
    # against the one it stored last time, and treats 0 as "unknown" and
    # re-reads. The contract is "stable while the file is unchanged, different
    # after a write, non-zero when known", and both satisfy it.
    patch(
        src / "HalStorage.h",
        "  uint64_t fileSize64();",
        "  uint64_t fileSize64();\n  uint32_t modificationTime();",
        "HalFile::modificationTime (header)",
        marker="modificationTime",
    )

    patch(
        src / "HalStorage.cpp",
        "uint64_t HalFile::fileSize64() { return size(); }",
        "uint64_t HalFile::fileSize64() { return size(); }\n"
        "uint32_t HalFile::modificationTime() {\n"
        "  if (!impl || impl->fd < 0)\n"
        "    return 0;\n"
        "  struct stat st;\n"
        "  if (fstat(impl->fd, &st) != 0)\n"
        "    return 0;\n"
        "  return static_cast<uint32_t>(st.st_mtime);\n"
        "}",
        "HalFile::modificationTime (impl)",
        marker="HalFile::modificationTime",
    )

    # CrossPoint 1.6.5 replaced the raw UTC-offset setting with real timezones,
    # and src/util/Timezones.cpp pushes the chosen POSIX rule into the clock
    # through HalClock::setTimezone(). The simulator's HalClock predates that
    # and still takes an explicit utcOffsetQuarterHoursBiased in its formatters.
    #
    # The body is the device's, minus one line. On device, setTimezone() also
    # clears _lastPollMs so local time is re-derived under the new rule on the
    # next read; the simulator's HalClock has no such cache and its formatters
    # are handed an offset per call, so there is nothing to invalidate. What
    # DOES carry over is setenv+tzset: the simulator runs against a real libc,
    # so the process TZ it sets is the same mechanism the device uses, and
    # anything reading localtime() here behaves as it does on hardware.
    patch(
        src / "HalClock.h",
        "#include <cstddef>\n#include <cstdint>",
        "#include <cstddef>\n#include <cstdint>\n#include <cstdlib>\n#include <ctime>",
        "HalClock.h cstdlib/ctime for setTimezone",
        marker="#include <ctime>",
    )

    patch(
        src / "HalClock.h",
        "  bool syncFromNTP();",
        "  void setTimezone(const char *posixTz) {\n"
        "    ::setenv(\"TZ\", posixTz && posixTz[0] != '\\0' ? posixTz : \"UTC0\", 1);\n"
        "    ::tzset();\n"
        "  }\n"
        "  bool syncFromNTP();",
        "HalClock::setTimezone",
        marker="setTimezone",
    )

    # ...and three ESP.* accessors the simulator's ESPMock does not have.
    # ESPMock carries the heap family (getFreeHeap and friends) and nothing
    # about the chip, because until this screen existed nothing sim-compiled
    # asked. Every OTHER ESP.* the firmware names -- getEfuseMac, getPsramSize,
    # getFreePsram -- sits behind `#if defined(ARDUINO_ARCH_ESP32) &&
    # !defined(SIMULATOR)` and never reaches this build, which is why ESPMock
    # has gone without them for so long.
    #
    # The values describe the device the simulator stands in for, not the Mac
    # running it: ESP32-S3 and the 16MB flash every fork env declares
    # (board_build.flash_size in platformio.ini). That is the same convention
    # BoardProfile already follows here -- the simulator reports the simulated
    # board's panel, controller and name, not the host's. Reporting the host's
    # actual CPU would make the About screen a different screen in the
    # simulator than on hardware, which is the opposite of what it is for.
    patch(
        src / "Arduino.h",
        "  void restart() {}",
        "  void restart() {}\n"
        "  const char *getChipModel() { return \"ESP32-S3\"; }\n"
        "  uint8_t getChipRevision() { return 0; }\n"
        "  uint32_t getFlashChipSize() { return 16u * 1024u * 1024u; }",
        "ESPMock chip/flash accessors",
        marker="getChipModel",
    )

    # The same About screen reads three BoardProfile members the simulator's
    # copy of the struct does not have -- displayWidth, displayHeight and
    # touch.controller -- and names all six values of a TouchController enum the
    # simulator does not declare at all.
    #
    # The three members are APPENDED, after viewableInsets: every profile in the
    # package is aggregate-initialised positionally, so a member added anywhere
    # but the end silently shifts the values of the ones after it, and adding
    # them with defaults keeps all nine existing initialisers compiling
    # untouched.
    #
    # 800x480 mirrors the SDK's XTEINK_X4_PRO profile, and is right for every
    # board this package carries -- they are all the same panel size. It is a
    # literal only because BoardConfig.h here includes <cstdint> and nothing
    # else, deliberately, so HalDisplay::DISPLAY_WIDTH cannot be referenced from
    # it without giving the native build the display dependency the package
    # author kept out.
    #
    # touch.controller defaults to None, which is the truthful answer for a host
    # process: the simulator has no touch CHIP, and its taps are synthetic. The
    # About screen therefore reads "No" for Touch in the simulator while touch
    # input still works, and that is the honest reading rather than a hardware
    # fact invented for a debug screen.
    patch(
        src / "BoardConfig.h",
        "struct ViewableInsets {",
        "enum class TouchController : uint8_t { None, Chsc6x, Gt911, Ft5x06, Ft6336u, Gslx680 };\n"
        "\n"
        "struct TouchConfig {\n"
        "  TouchController controller = TouchController::None;\n"
        "};\n"
        "\n"
        "struct ViewableInsets {",
        "BoardConfig::TouchController + TouchConfig",
        marker="enum class TouchController",
    )

    patch(
        src / "BoardConfig.h",
        "  ViewableInsets viewableInsets = {};\n};",
        "  ViewableInsets viewableInsets = {};\n"
        "  uint16_t displayWidth = 800;\n"
        "  uint16_t displayHeight = 480;\n"
        "  TouchConfig touch = {};\n"
        "};",
        "BoardProfile displayWidth/displayHeight/touch",
        marker="uint16_t displayWidth",
    )

    # Upstream's new About screen names every display controller the SDK knows
    # (src/activities/settings/AboutActivity.cpp switches on all eight). The
    # simulator ships a four-value copy of the enum -- SSD1677, UC8253, UC8279,
    # UC8179 -- so ED2208, LgfxEpd, IT8951 and UC8279C do not exist there and the
    # switch stops the build.
    #
    # Mirrored from the SDK exactly, explicit values included
    # (libs/hardware/BoardConfig/include/BoardConfig.h), rather than appending
    # the four missing names. The simulator's implicit numbering had ALREADY
    # drifted -- its UC8253 is 1 where the SDK's is 2, its UC8279 is 2 where the
    # SDK's is 6 -- so appending would have left that divergence in place and
    # added four more names on top of it. Safe to renumber because every use of
    # this enum, in the simulator package and in src/ and lib/ alike, is by NAME:
    # the one comparison outside the About screen is HalGPIO.cpp's
    # `displayController == DisplayController::UC8279`. Nothing stores or
    # transmits the number.
    patch(
        src / "BoardConfig.h",
        "enum class DisplayController {\n"
        "  SSD1677,\n"
        "  UC8253,\n"
        "  UC8279,\n"
        "  UC8179,\n"
        "};",
        "enum class DisplayController : uint8_t {\n"
        "  SSD1677 = 0,\n"
        "  UC8253 = 2,\n"
        "  ED2208 = 3,\n"
        "  LgfxEpd = 4,\n"
        "  IT8951 = 5,\n"
        "  UC8279 = 6,\n"
        "  UC8179 = 7,\n"
        "  UC8279C = 8\n"
        "};",
        "BoardConfig::DisplayController (the SDK's full list)",
        marker="UC8279C",
    )

    # The same SDK bump added a third grayscale mode. The SDK declares
    # `enum class GrayscaleMode : uint8_t { Overlay, Absolute, Direct }`
    # (libs/display/FreeInkDisplay/include/GrayscaleCapabilities.h), lib/hal
    # aliases it, and upstream's SleepActivity now asks for Direct and falls back
    # to Absolute when the panel cannot do it. The simulator declares its OWN copy
    # of the enum with only the first two values, so the sleep screen stops the
    # build on "no member named 'Direct'".
    #
    # Appended, never inserted: these are uint8_t values an on-disk or on-wire
    # format could carry, and putting Direct anywhere but last would renumber
    # Absolute. It matches the SDK's own order, which is what makes the two
    # enums interchangeable at all.
    patch(
        src / "HalDisplay.h",
        "  enum class GrayscaleMode : uint8_t { Overlay, Absolute };",
        "  enum class GrayscaleMode : uint8_t { Overlay, Absolute, Direct };",
        "HalDisplay::GrayscaleMode::Direct",
        marker="Absolute, Direct",
    )

    # The 2026-09-19 SDK bump added UsbMassStorage::hostSuspended(), and lib/hal
    # exposes it as HalStorage::usbDriveHostSuspended(). It needs its OWN patch
    # rather than a line in the USB Drive block above, because that block is
    # skipped now: the published simulator package ships usbDriveState() itself,
    # so its marker matches and nothing inside it is reached. A method appended
    # to a patch whose marker already matches is a method that never gets added,
    # and the failure is silent until the sim build stops compiling.
    #
    # false is the honest answer, not a stub: a host-side simulator has no USB
    # host attached, so the host is never suspended. Same reasoning as
    # usbDriveState() answering Unsupported.
    patch(
        src / "HalStorage.h",
        "  bool removeDir(const char *path);",
        "  bool usbDriveHostSuspended() const;\n  bool removeDir(const char *path);",
        "HalStorage::usbDriveHostSuspended (header)",
        marker="usbDriveHostSuspended",
    )

    patch(
        src / "HalStorage.cpp",
        "bool HalStorage::begin() {",
        "bool HalStorage::usbDriveHostSuspended() const { return false; }\n"
        "\n"
        "bool HalStorage::begin() {",
        "HalStorage::usbDriveHostSuspended (impl)",
        marker="HalStorage::usbDriveHostSuspended",
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

    # lib/hal/HalSystem.h gained panicReasonRecorded() so the heartbeat can tell a
    # fresh panic reason from a stale one (the capture marker, read before
    # checkPanic() clears it). The simulator ships its own HalSystem, and its
    # copy never panics: no reason is ever recorded, so it answers false.
    patch(
        src / "HalSystem.h",
        "bool isRebootFromPanic();",
        "bool isRebootFromPanic();\nbool panicReasonRecorded();",
        "HalSystem::panicReasonRecorded (header)",
        marker="panicReasonRecorded",
    )

    patch(
        src / "HalSystem.cpp",
        "bool HalSystem::isRebootFromPanic() { return false; }",
        "bool HalSystem::isRebootFromPanic() { return false; }\n"
        "bool HalSystem::panicReasonRecorded() { return false; }",
        "HalSystem::panicReasonRecorded (impl)",
        marker="HalSystem::panicReasonRecorded",
    )

    # -- the seam, checked rather than remembered -------------------------------
    #
    # lib/hal/HalStorage.h declares one surface; the simulator ships a SECOND
    # implementation of the same class, and platformio.sim.ini's `lib_ignore = hal`
    # means a library's own headers shadow ours. Nothing in lib/hal says so. Add a
    # method there and the device gets it, the simulator does not, and the build
    # stays green until some app calls it -- at which point the error names a file
    # that app never touched. That is how freeBytes shipped (2026-08-31); Trivia
    # found it by being the first caller.
    #
    # The knowledge lived in two places neither reachable from the file you edit.
    # Now the two surfaces are compared here, after the patches above have run,
    # which is the one moment both exist in their final form. Divergence is zero
    # today, so this cannot go red on anything but a real one.
    def _public_methods(path):
        import re

        text = path.read_text()
        m = re.search(
            r"class HalStorage\b.*?public:(.*?)(?:\n\s*private:|\n\};)", text, re.S
        )
        body = m.group(1) if m else text
        names = set()
        for hit in re.finditer(
            r"^\s*(?:static\s+)?[A-Za-z_][\w:<>,\s\*&]*?\b(\w+)\s*\(", body, re.M
        ):
            name = hit.group(1)
            if name not in ("if", "for", "while", "return", "HalStorage"):
                names.add(name)
        return names

    _ours = pathlib.Path(env.subst("$PROJECT_DIR")) / "lib" / "hal" / "HalStorage.h"  # noqa: F821
    _theirs = src / "HalStorage.h"
    if _ours.exists() and _theirs.exists():
        _mine, _sim = _public_methods(_ours), _public_methods(_theirs)
        if not _mine or not _sim:
            # A parity check that parsed nothing reports parity forever. That is the
            # failure this whole file keeps meeting, so it is loud rather than quiet.
            raise SystemExit(
                "[sim-catchup] HalStorage parity check parsed "
                f"{len(_mine)} of ours and {len(_sim)} of theirs -- it is not "
                "checking anything. Fix the parser before trusting a green build."
            )
        _missing = sorted(_mine - _sim)
        if _missing:
            raise SystemExit(
                "[sim-catchup] lib/hal/HalStorage.h declares methods the simulator's "
                f"HalStorage does not have: {', '.join(_missing)}.\n"
                "  The simulator ships its own HalStorage and shadows lib/hal, so a "
                "fork-only method needs a patch() above -- see freeBytes for the "
                "shape.\n"
                "  Without one this builds green and breaks the first app that calls it."
            )
        print(
            f"[sim-catchup] HalStorage parity: {len(_mine)} methods, both sides agree"
        )
    require_all_applied()


if env is not None:
    main(env)

#pragma once

// The freestanding half of the Wallpapers app: the decisions that do not need
// a renderer, a device or the SD card, so host-tests/ui/ can assert them. The
// SD I/O (listing the folder, copying the pinned image, saving the setting)
// lives in WallpapersActivity; everything a test could get wrong lives here.

#include <cstdint>
#include <string>
#include <string_view>

namespace wallpapers {

// The library folder holds every wallpaper the user has put on the card -- the
// browser uploader writes here, and the picker lists it. Visible (not dotted)
// on purpose: the uploader's instructions and the File Transfer web UI both
// name this path to the user, and a "/.wallpapers" would read as a system
// folder they should not touch.
inline constexpr char kLibraryDir[] = "/wallpapers";

// Setting a wallpaper pins it by copying it to the card-root /sleep.bmp, which
// is the single-image slot the sleep system already checks FIRST in
// renderCustomSleepScreen -- so this app extends the existing mechanism rather
// than teaching SleepActivity a new path. The copy is self-contained: the
// chosen wallpaper keeps working even if the user later deletes it from the
// library.
inline constexpr char kPinnedSleep[] = "/sleep.bmp";

// Which library file is currently pinned, so the picker can mark it. A hint,
// not the source of truth: the source of truth is /sleep.bmp, and this is only
// trusted when that file exists and the sleep mode is CUSTOM.
inline constexpr char kActiveMarker[] = "/wallpapers/.active";

// A wallpaper file is a plain `.bmp` whose name does not start with '.'.
//
// This used to claim it "matches the sleep system's own findNextValidSleepImage
// filter exactly ... the two filters cannot drift, because they are the same
// rule". That was false, and a comment asserting a safety property nothing
// enforces is worse than no comment. They are two different rules and a
// 2026-09-05 audit found six ways they disagree:
//
//   * this one checks the NAME only; findNextValidSleepImage also requires
//     Bitmap::parseHeaders() == Ok, so a corrupt file named .bmp is listed here
//     and skipped there;
//   * a BMP over 2048x3072, at 16bpp, or RLE-compressed is listed here and
//     rejected there (Bitmap.cpp:109-131);
//   * .png is accepted there in overlay mode and never here;
//   * names of 128..255 bytes are listed there and dropped here (this app's
//     buffer is 128, and SdFat returns "" rather than truncating);
//   * the 257th file in a folder is counted there and hidden here (kMaxLibrary);
//   * a valid BMP whose size is not kWallpaperFileBytes is listed here and
//     cannot be set at all -- setWallpaper refuses and the caller shows nothing.
//
// The two also never read the same directory: this filter walks /wallpapers,
// and findNextValidSleepImage only ever walks /.sleep, /sleep and the two
// overlay folders. The only bridge between them is the byte copy to /sleep.bmp.
// host-tests/wallpapers asserts the name half, which is why the drift was
// untested rather than caught.
bool isSupportedWallpaper(std::string_view name);

// The free-space precondition. The device cannot measure free space reliably
// (see the firmware-cannot-see-free-space memory): HalStorage::freeBytes()
// returns false when the cluster walk FAILED, which is an abnormal card, never
// "the card is empty". So there are three outcomes and Unknown refuses --
// refusing costs a retry, while proceeding on Unknown costs the one user whose
// card was already in trouble.
// The name drawn under a thumbnail. A picker that shows "blake-door.bmp" and
// cuts it mid-name looks unfinished, so every built-in carries a real name held
// apart from its file name. `brief` is a SHORTER NAME for a narrow cell, never
// an ellipsis: the Toybox faces cannot draw U+2026 anyway, and a cut name is the
// defect this exists to avoid. A wallpaper the user added has no entry, so it
// falls back to its file name with the extension stripped.
struct DisplayName {
  std::string full;
  std::string brief;
};
DisplayName displayName(std::string_view fileName);

// The built-in library, walkable. A proof that "no caption collides" has to
// visit EVERY name, and a test that hand-copies the list stops covering the
// one that gets added next. host-tests/wallcaption iterates these.
// The size of that table, as a constant the table itself is static_assert'd
// against in WallpapersCore.cpp. Everything that needs "how many built-ins"
// reads THIS -- the download's size estimate, the offer screen's headline, the
// wallcaption proof -- so adding a 22nd wallpaper cannot leave a stale 21 in a
// sentence somewhere (derived-facts-written-as-literals).
inline constexpr size_t kBuiltInCount = 21;
size_t builtInCount();
const char* builtInStem(size_t index);

// Is this file one of the built-in set? The picker sorts a user's OWN
// wallpapers in front of the built-ins, so this decides the order and it has to
// be a real predicate rather than a binary_search over a list that is no longer
// plainly sorted.
bool isBuiltInFile(std::string_view fileName);

// Sort order for the picker: user uploads first, then the built-ins, each
// alphabetical. Mario's ask -- your own pictures should not be buried behind
// twenty-one defaults.
bool sortsBefore(std::string_view a, std::string_view b);

// What a tap that landed on a wallpaper cell MEANS. Freestanding because it is
// the one decision on this screen that a wrong answer makes destructive, and a
// decision left inside loop() cannot be asserted anywhere -- the simulator never
// compiles lib/hal and never runs InputManager, so the host suite is the only
// place this can be proved at all.
//
// `heldLong` is MappedInputManager::tapWasHeldLong(): the SDK's wasTouchTap has
// no duration gate, so a hold arrives here as an ordinary tap and only that
// call tells the two apart. It wins over every other branch, INCLUDING the
// already-set one: holding the wallpaper you are already using is how you reach
// its preview and its delete, and returning None there would make the sheet
// unreachable for exactly one wallpaper -- the one most likely to be held.
//
// `sleepBlocked` is card #354's rule, folded in here rather than left as an
// early return beside this call, because the two would otherwise compete for
// the same tap. "Already the sleep screen" is a reason to do NOTHING only when
// it can actually BE the sleep screen; when the settings block it, tapping the
// marked wallpaper again is what a person does after nothing happened, and it
// must re-pin and repair the settings. Holding it still opens the sheet, which
// is the ordering the two cards together require.
enum class CellAction : uint8_t {
  None,   // nothing to do (a plain tap on the wallpaper already in use)
  Set,    // pin it as the sleep screen
  Sheet,  // open the hold sheet for it
};
CellAction cellAction(bool heldLong, int index, int activeIndex, bool sleepBlocked);

// What the delete confirm has to say, before it is asked. Two clauses, each
// conditional, and a caveat assembled per render is a caveat that can silently
// lose a branch (a-warning-that-can-vanish) -- so it is built HERE, where a
// host test walks all four combinations rather than the one somebody looked at.
//
// Clause 1, for a built-in: recovery is NOT an undo. runSetDownload fetches the
// whole ~1MB pack unconditionally (no Range), behind the WiFi picker and behind
// a 12MB + pack free-space floor; only the unpack skips files already present.
// So the honest sentence is "the whole set again", not "you can get it back".
//
// Clause 2, for the wallpaper currently pinned: /sleep.bmp is a COPY, so
// deleting the library file does not take the picture off the sleep screen. A
// confirm that let the user believe it did would be wrong in the direction that
// makes them delete twice.
std::string deleteConsequence(bool builtIn, bool isActive);

// ---------------------------------------------------------------------------
// Does the pinned wallpaper actually reach the glass?
//
// Card #354: the picker marked a wallpaper with full confidence and the device
// then slept showing something else, with nothing on any screen saying why.
// Setting a wallpaper writes /sleep.bmp, but TWO settings decide whether
// SleepActivity ever draws that file, and neither of them lives in this app.
// The rules are mirrored here, freestanding, so a laptop can prove them and so
// the picker can say a true sentence instead of drawing a confident marker.
//
// The mirror is exact. SleepActivity::onEnter (src/activities/boot_sleep/,
// UPSTREAM -- not ours to change) decides in this order:
//
//   1. quick resume wins outright, either because the mode IS Quick Resume or
//      because this is a timeout sleep and the timeout flag is on;
//   2. TRANSPARENT_CUSTOM draws /sleep-overlay.*, never /sleep.bmp;
//   3. CUSTOM draws /sleep.bmp;
//   4. COVER_CUSTOM draws /sleep.bmp only when the sleep did not come from the
//      reader (inside a book the cover wins);
//   5. DARK, LIGHT, COVER and BLANK never look at it.
//
// The eight modes, mirrored from CrossPointSettings::SLEEP_SCREEN_MODE. That
// header pulls in ArduinoJson and the whole persistence layer, so it cannot be
// included on a host; WallpapersActivity.cpp static_asserts every value below
// against the real enum, which is what stops the two from drifting.
enum SleepScreenMode : uint8_t {
  kSleepDark = 0,
  kSleepLight = 1,
  kSleepCustom = 2,
  kSleepCover = 3,
  kSleepCoverCustom = 4,
  kSleepBlank = 5,
  kSleepQuickResume = 6,
  kSleepTransparentCustom = 7,
  kSleepModeCount = 8,
};

// True when SleepActivity would draw /sleep.bmp for this combination.
// `fromTimeout` is the idle auto-sleep (main.cpp's enterDeepSleep(true)) -- the
// ordinary way this device sleeps; the power-button hold and the Paper Mono
// short press both pass false. `fromReader` is APP_STATE.lastSleepFromReader.
//
// COVER_CUSTOM with fromReader returns false, which is the CONSERVATIVE answer
// rather than the complete one: upstream falls back to the wallpaper when the
// book has no usable cover, and this app must not promise a picture on a path
// whose outcome depends on a cover it cannot inspect.
bool drawsPinnedSleep(uint8_t sleepScreenMode, bool quickResumeAfterTimeout, bool fromTimeout, bool fromReader);

// What the picker has to tell the user, derived from the predicate above rather
// than restated -- a second copy of the rules is a second place to be wrong.
enum class Reach : uint8_t {
  Always,                // every sleep shows it
  OutsideReaderOnly,     // COVER_CUSTOM: the book cover wins when you sleep in a book
  BlockedByQuickResume,  // the mode is right, but quick resume beats it on idle sleep
  BlockedByMode,         // the sleep screen is set to something that never reads /sleep.bmp
};
Reach reachOfPinnedSleep(uint8_t sleepScreenMode, bool quickResumeAfterTimeout);

// Whether the hold sheet and the delete confirm get to SAY a wallpaper is on
// the sleep screen. Not the same question as the grid's marker, and the two
// came apart in the merge that brought #354 onto this branch.
//
// The marker asks "is this the pinned file", and #354 deliberately widened it:
// loadActive() is no longer gated on sleepScreen == CUSTOM, so a wallpaper
// wears the marker under DARK, LIGHT, BLANK and quick-resume too. The grid
// qualifies that with the hint strip. The sheet ("This one is on your sleep
// screen now.") and the confirm ("It stays on your sleep screen until you pick
// another.") carry no strip and no room for one, so they ask the stronger
// question here instead -- otherwise both assert something false under exactly
// the settings #354 exists to expose.
//
// OutsideReaderOnly still counts as yes: the picture does show on an ordinary
// sleep, and the book-cover exception is the strip's to explain, not a reason
// to deny the sentence outright.
bool saysOnSleepScreen(bool isMarked, Reach reach);

// The sentence for the picker's hint strip when NOTHING was selected this
// session, or nullptr when there is nothing to say. Short on purpose: the strip
// is one fixed 30px line and a cut sentence is the defect it exists to avoid
// (host-tests/wallcaption measures these in the face the strip resolves).
const char* reachHint(Reach reach);

// The mode's name in a sentence, for saying what a selection replaced.
const char* sleepScreenModeName(uint8_t sleepScreenMode);

// What tapping a wallpaper must leave the two settings at.
//
// The rule the app had before #354 kept the timeout quick-resume flag ON so
// wake would stay fast, and traded away the timeout sleep to get it -- which is
// every ordinary sleep, so the wallpaper the user had just chosen was the one
// thing it could never show. A picker whose whole purpose is "put this picture
// on the sleep screen" does not get to lose that argument to a wake time.
//
// A mode that ALREADY draws /sleep.bmp is left alone, so a deliberate
// COVER_CUSTOM survives being handed a new wallpaper.
struct SleepChoice {
  uint8_t sleepScreenMode = kSleepCustom;
  bool quickResumeAfterTimeout = false;
  bool tookOverMode = false;        // a deliberate mode choice was replaced
  bool clearedQuickResume = false;  // quick wake on idle sleep was turned off
  uint8_t previousMode = kSleepCustom;
};
SleepChoice choiceForSetWallpaper(uint8_t sleepScreenMode, bool quickResumeAfterTimeout);

// The strip's line after a selection: what the tap changed, PLUS any standing
// caveat that still applies. One line has to carry both, and the first version
// of this could not -- it returned the "what changed" note and suppressed the
// caveat behind it for the rest of the app session, which is the shape of
// card #354 reappearing inside its own fix ("a warning that can vanish").
//
// So the return value is not just a string: it declares which facts the
// sentence covers, and host-tests/wallpapers asserts that coverage EQUALS the
// facts present for every combination. That is a construction, not a text
// match: a sentence cannot pass by mentioning the right word (see the
// "a detector that matches the description" note).
struct StripLine {
  const char* text = nullptr;
  bool saysModeChanged = false;
  bool saysQuickResumeCleared = false;
  bool saysCaveat = false;
};
StripLine stripLineAfterSelection(const SleepChoice& choice, Reach reach);

enum class Room : uint8_t { Ok, TooFull, Unknown };
Room roomFor(bool queryOk, uint64_t freeBytes, uint64_t floorBytes);

// The floor is NOT sized to this app's own write (pinning is a ~48KB copy).
// It is sized by who PAYS when the card fills: Study's review log, which loses
// answers silently rather than refusing. A wallpaper library a user keeps
// adding to competes with books and saves for the same card, and the app that
// next tries to write is the one that gets hurt. A deliberate over-estimate,
// explicitly not derived from any current asset size (see the
// derived-facts-written-as-literals memory for why a pinned byte count rots).
inline constexpr uint64_t kCardFloorBytes = 12ull * 1024 * 1024;

// Every device wallpaper is exactly this size: 480x800, 1 bit, uncompressed,
// 62-byte header. The converter emits exactly this and the panel accepts
// nothing else, so the download's size estimate is DERIVED rather than typed
// into a sentence that would go stale the day a wallpaper is added.
inline constexpr uint64_t kWallpaperFileBytes = 48062;
inline constexpr uint64_t builtInPackBytes() { return kWallpaperFileBytes * kBuiltInCount; }

// What the card must have free before fetching the SET: the floor that protects
// other apps plus the whole set, not one file -- the download writes all of
// them, and checking for one would pass on a card that fills at wallpaper 9.
inline constexpr uint64_t kPackFloorBytes = kCardFloorBytes + builtInPackBytes();

}  // namespace wallpapers

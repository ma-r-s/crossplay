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

// A wallpaper file is a plain `.bmp` whose name does not start with '.'. This
// matches the sleep system's own findNextValidSleepImage filter exactly, so a
// file this app offers is a file the sleep screen will accept -- the two
// filters cannot drift, because they are the same rule.
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

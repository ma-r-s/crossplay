// Host tests for the Wallpapers picker core: the file-name filter that decides
// what the library lists, and the free-space precondition that decides whether
// the app warns before the card fills. Both are freestanding, so a laptop
// proves them with no device and no SD card.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "../../src/apps_local/wallpapers/WallpapersCore.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(bool cond, const char* what, int line) {
  ++checksRun;
  if (!cond) {
    ++checksFailed;
    std::printf("FAIL test_wallpapers.cpp:%d  %s\n", line, what);
  }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

// A file the picker lists is a file the sleep screen accepts: the same rule
// filters both, so they cannot drift. Only plain .bmp, never a dotfile, never
// another format.
void testFileNameFilter() {
  using wallpapers::isSupportedWallpaper;

  CHECK(isSupportedWallpaper("sunset.bmp"));
  CHECK(isSupportedWallpaper("SUNSET.BMP"));  // case-insensitive extension
  CHECK(isSupportedWallpaper("a.Bmp"));
  CHECK(isSupportedWallpaper("my wallpaper 2.bmp"));

  // The sleep system skips dotfiles (its own recent-window state is dotted),
  // and so must the picker, or the marker file itself would be offered.
  CHECK(!isSupportedWallpaper(".active"));
  CHECK(!isSupportedWallpaper(".hidden.bmp"));

  // Bitmap is the ONLY format the device draws (activity-render-contract): a
  // PNG or JPEG on the card would list and then fail to render.
  CHECK(!isSupportedWallpaper("photo.png"));
  CHECK(!isSupportedWallpaper("photo.jpg"));
  CHECK(!isSupportedWallpaper("photo.jpeg"));

  CHECK(!isSupportedWallpaper("noextension"));
  CHECK(!isSupportedWallpaper(""));
  CHECK(!isSupportedWallpaper(".bmp"));  // extension only, no name
  CHECK(!isSupportedWallpaper("bmp"));
  CHECK(!isSupportedWallpaper("trailingdot.bmp."));
}

// The precondition has THREE outcomes, not two, and Unknown refuses. The
// device cannot measure free space reliably, and a failed query must never read
// as "the card is empty" (firmware-cannot-see-free-space).
void testRoomFor() {
  using wallpapers::Room;
  using wallpapers::roomFor;
  constexpr uint64_t floor = wallpapers::kCardFloorBytes;

  // Plenty of room.
  CHECK(roomFor(true, floor * 2, floor) == Room::Ok);
  CHECK(roomFor(true, floor, floor) == Room::Ok);  // exactly the floor is Ok

  // Below the floor is TooFull, and the floor protects OTHER apps, not this
  // one: a wallpaper pin is ~48KB, well under a byte of the 12MB floor.
  CHECK(roomFor(true, floor - 1, floor) == Room::TooFull);
  CHECK(roomFor(true, 0, floor) == Room::TooFull);

  // A FAILED query is Unknown -- never TooFull and never Ok -- whatever number
  // came back with it, because that number is meaningless when queryOk is false.
  CHECK(roomFor(false, 0, floor) == Room::Unknown);
  CHECK(roomFor(false, floor * 100, floor) == Room::Unknown);

  // The floor is a deliberate over-estimate, not this app's write size.
  CHECK(wallpapers::kCardFloorBytes >= 8ull * 1024 * 1024);
}

// A picker that shows file names with extensions, cut mid-word, looks
// unfinished. Every built-in carries a real name; anything the user added falls
// back to its own file name with the extension stripped.
void testDisplayNames() {
  using wallpapers::displayName;

  // Built-ins get a real name, never the file name.
  CHECK(displayName("bauhaus.bmp").full == "Bauhaus");
  CHECK(displayName("checker.bmp").full == "Checker");
  CHECK(displayName("celestial.bmp").full == "Celestial Chart");
  CHECK(displayName("dragonflies.bmp").full == "Dragonflies");
  CHECK(displayName("ornament.bmp").full == "Ornament");

  // The engravings credit their maker, briefly.
  CHECK(displayName("durer-eden.bmp").full == "Durer: Adam and Eve");
  CHECK(displayName("durer-horsemen.bmp").full == "Durer: Four Horsemen");

  // The brief form is a shorter NAME, never an ellipsis, and never longer than
  // the full form.
  const char* stems[] = {"bauhaus.bmp",    "blake-door.bmp",  "celestial.bmp",  "checker.bmp",
                         "cubes.bmp",      "dragonflies.bmp", "durer-eden.bmp", "durer-horsemen.bmp",
                         "map-greece.bmp", "orb.bmp",         "ornament.bmp",   "owl.bmp"};
  for (const char* f : stems) {
    const wallpapers::DisplayName n = displayName(f);
    CHECK(!n.full.empty());
    CHECK(!n.brief.empty());
    CHECK(n.brief.size() <= n.full.size());
    CHECK(n.full.find("...") == std::string::npos);
    CHECK(n.brief.find("...") == std::string::npos);
    CHECK(n.full.find(".bmp") == std::string::npos);
    // The Toybox faces are subset to ASCII; a name outside it draws as nothing.
    for (unsigned char c : n.full) CHECK(c >= 0x20 && c < 0x7f);
    for (unsigned char c : n.brief) CHECK(c >= 0x20 && c < 0x7f);
  }

  // A wallpaper the user added keeps its own name, minus the extension.
  CHECK(displayName("my vacation.bmp").full == "my vacation");
  CHECK(displayName("my vacation.bmp").brief == "my vacation");
  CHECK(displayName("holiday.BMP").full == "holiday");
  CHECK(displayName("noextension").full == "noextension");
}

}  // namespace

int main() {
  testFileNameFilter();
  testDisplayNames();
  testRoomFor();

  std::printf("wallpapers: %d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}

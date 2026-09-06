// Host tests for the Wallpapers picker core: the file-name filter that decides
// what the library lists, and the free-space precondition that decides whether
// the app warns before the card fills. Both are freestanding, so a laptop
// proves them with no device and no SD card.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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
  CHECK(displayName("durer-eden.bmp").full == "Duerer: Adam and Eve");
  CHECK(displayName("durer-horsemen.bmp").full == "Duerer: Four Horsemen");

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

  // Uploads first, then built-ins, each alphabetical. Mario's ask, and the
  // ordering the picker's captions are indexed by -- get it wrong and every
  // caption names someone else's picture.
  {
    CHECK(wallpapers::isBuiltInFile("bauhaus.bmp"));
    CHECK(wallpapers::isBuiltInFile("durer-horsemen.bmp"));
    CHECK(!wallpapers::isBuiltInFile("holiday.bmp"));
    CHECK(!wallpapers::isBuiltInFile("bauhaus-of-mine.bmp"));  // not the same stem

    std::vector<std::string> names = {"waves.bmp", "holiday.bmp", "bauhaus.bmp", "zebra.bmp", "celestial.bmp"};
    std::sort(names.begin(), names.end(),
              [](const std::string& a, const std::string& b) { return wallpapers::sortsBefore(a, b); });
    const std::vector<std::string> want = {"holiday.bmp", "zebra.bmp", "bauhaus.bmp", "celestial.bmp", "waves.bmp"};
    CHECK(names == want);
    // The comparator must be a strict weak ordering or std::sort is UB.
    for (const std::string& a : want) {
      CHECK(!wallpapers::sortsBefore(a, a));
      for (const std::string& b : want) {
        if (wallpapers::sortsBefore(a, b)) CHECK(!wallpapers::sortsBefore(b, a));
      }
    }
  }

  // THE HOLD, and why it is proved here rather than anywhere else.
  //
  // InputManager::wasTouchTap has no duration gate, so a five-second hold is
  // delivered as an ordinary tap and only MappedInputManager::tapWasHeldLong()
  // can tell the two apart. The simulator never compiles lib/hal and never runs
  // InputManager (simulator-cannot-test-input-edges), so a screenshot run
  // cannot see this at all: a hold that never registers on hardware looks clean
  // there, and one that double-fires looks clean too. This is the proof.
  {
    using wallpapers::CellAction;
    using wallpapers::cellAction;

    // A plain tap keeps the meaning it has always had.
    CHECK(cellAction(false, 3, -1) == CellAction::Set);
    CHECK(cellAction(false, 3, 7) == CellAction::Set);
    // ... except on the wallpaper already in use, where it does nothing.
    CHECK(cellAction(false, 3, 3) == CellAction::None);

    // A hold opens the sheet, and NEVER sets the sleep screen. This is the one
    // that goes red without the fix: routing a hold as a tap would set a
    // wallpaper the user was reaching past, with no confirmation and no undo.
    CHECK(cellAction(true, 3, -1) == CellAction::Sheet);
    CHECK(cellAction(true, 3, 7) == CellAction::Sheet);
    // Including on the wallpaper that is already set. Returning None here would
    // make the sheet unreachable for exactly one wallpaper -- the one wearing
    // the marker, and so the one most likely to be held.
    CHECK(cellAction(true, 3, 3) == CellAction::Sheet);

    // No hold on a cell that is not a wallpaper.
    CHECK(cellAction(true, -1, 0) == CellAction::None);
    CHECK(cellAction(false, -1, 0) == CellAction::None);

    // Said as a property rather than as six cases: a hold is never Set, for any
    // index and any selection. A future branch added above the hold check would
    // fail here rather than on the panel.
    for (int idx = 0; idx < 8; ++idx) {
      for (int active = -1; active < 8; ++active) {
        CHECK(cellAction(true, idx, active) != CellAction::Set);
      }
    }
  }

  // The confirm's consequence, walked over all four combinations rather than
  // the one somebody looked at -- a caveat assembled per render is a caveat
  // that can silently lose a branch (a-warning-that-can-vanish).
  {
    using wallpapers::deleteConsequence;
    const auto has = [](const std::string& hay, const char* needle) { return hay.find(needle) != std::string::npos; };
    for (int builtIn = 0; builtIn <= 1; ++builtIn) {
      for (int active = 0; active <= 1; ++active) {
        const std::string said = deleteConsequence(builtIn != 0, active != 0);
        CHECK(!said.empty());
        // The recovery cost, in the terms it is actually paid in: runSetDownload
        // fetches the WHOLE pack unconditionally, so "the whole set again" is
        // the honest sentence and "you can get it back" is not.
        if (builtIn != 0) {
          CHECK(has(said, "whole set again"));
          CHECK(!has(said, "only copy"));
        } else {
          CHECK(has(said, "cannot be undone"));
          CHECK(!has(said, "whole set"));
        }
        // /sleep.bmp is a copy, so deleting the pinned wallpaper does not take
        // it off the sleep screen. A user who believed otherwise would delete a
        // second time looking for an effect that never comes.
        CHECK(has(said, "sleep screen") == (active != 0));
      }
    }
  }

  std::printf("wallpapers: %d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}

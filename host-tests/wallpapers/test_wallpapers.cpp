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

// ---------------------------------------------------------------------------
// Card #354: a wallpaper the picker marked never reached the glass.
//
// The picker writes /sleep.bmp and then draws a confident marker, but TWO
// settings decide whether SleepActivity ever draws that file, and the app was
// leaving them in a combination that guaranteed it would not -- on the IDLE
// TIMEOUT sleep, which is the ordinary way this device sleeps. The tests below
// are about that path specifically: a button-held sleep already worked and
// proves nothing.

// The mirror of SleepActivity::onEnter. If this drifts from upstream the picker
// starts lying again, so the shape of the rule is asserted, not just one case.
void testDrawsPinnedSleep() {
  using namespace wallpapers;

  // The whole bug in one line: mode is CUSTOM, the file is there, the user
  // chose it -- and the idle sleep shows something else.
  CHECK(!drawsPinnedSleep(kSleepCustom, /*qr=*/true, /*fromTimeout=*/true, /*fromReader=*/false));
  // Same settings, a button-held sleep. This is why a screenshot of a manual
  // sleep was never evidence.
  CHECK(drawsPinnedSleep(kSleepCustom, /*qr=*/true, /*fromTimeout=*/false, /*fromReader=*/false));

  // With the timeout flag off, CUSTOM shows on every sleep.
  CHECK(drawsPinnedSleep(kSleepCustom, false, true, false));
  CHECK(drawsPinnedSleep(kSleepCustom, false, false, false));
  CHECK(drawsPinnedSleep(kSleepCustom, false, true, true));

  // COVER_CUSTOM shows it outside the reader only.
  CHECK(drawsPinnedSleep(kSleepCoverCustom, false, true, false));
  CHECK(!drawsPinnedSleep(kSleepCoverCustom, false, true, true));

  // Exactly two of the eight modes ever put this file on the glass. The count
  // is walked rather than asserted mode by mode, so a ninth mode arriving from
  // upstream cannot slip past a hand-written list.
  int everDraws = 0;
  for (uint8_t mode = 0; mode < kSleepModeCount; ++mode) {
    bool any = false;
    for (int qr = 0; qr < 2; ++qr) {
      for (int t = 0; t < 2; ++t) {
        for (int r = 0; r < 2; ++r) {
          if (drawsPinnedSleep(mode, qr != 0, t != 0, r != 0)) any = true;
        }
      }
    }
    if (any) ++everDraws;
  }
  CHECK(everDraws == 2);

  // Quick Resume as the MODE beats the file on every path, timeout or not.
  for (int t = 0; t < 2; ++t) {
    for (int r = 0; r < 2; ++r) {
      CHECK(!drawsPinnedSleep(kSleepQuickResume, false, t != 0, r != 0));
    }
  }
  // And the transparent mode has its own art, so a wallpaper is ignored there.
  CHECK(!drawsPinnedSleep(kSleepTransparentCustom, false, false, false));
}

// Reach is what the picker SAYS. Blocked must be distinguishable from "shows,
// but not inside a book": one is a settings problem the user can fix, the other
// is normal.
void testReach() {
  using namespace wallpapers;

  CHECK(reachOfPinnedSleep(kSleepCustom, false) == Reach::Always);
  CHECK(reachOfPinnedSleep(kSleepCustom, true) == Reach::BlockedByQuickResume);
  CHECK(reachOfPinnedSleep(kSleepCoverCustom, false) == Reach::OutsideReaderOnly);
  CHECK(reachOfPinnedSleep(kSleepCoverCustom, true) == Reach::BlockedByQuickResume);
  CHECK(reachOfPinnedSleep(kSleepQuickResume, false) == Reach::BlockedByMode);
  CHECK(reachOfPinnedSleep(kSleepQuickResume, true) == Reach::BlockedByMode);
  CHECK(reachOfPinnedSleep(kSleepDark, false) == Reach::BlockedByMode);
  CHECK(reachOfPinnedSleep(kSleepLight, false) == Reach::BlockedByMode);
  CHECK(reachOfPinnedSleep(kSleepCover, false) == Reach::BlockedByMode);
  CHECK(reachOfPinnedSleep(kSleepBlank, false) == Reach::BlockedByMode);
  CHECK(reachOfPinnedSleep(kSleepTransparentCustom, false) == Reach::BlockedByMode);

  // Every blocked state must produce a sentence, and Always must produce none:
  // a hint strip that stays blank while the marker lies is the whole defect.
  for (uint8_t mode = 0; mode < kSleepModeCount; ++mode) {
    for (int qr = 0; qr < 2; ++qr) {
      const Reach reach = reachOfPinnedSleep(mode, qr != 0);
      const char* hint = reachHint(reach);
      if (reach == Reach::Always) {
        CHECK(hint == nullptr);
      } else {
        CHECK(hint != nullptr && hint[0] != '\0');
      }
    }
  }
}

// The acceptance criterion, as arithmetic: after tapping a wallpaper, from ANY
// starting combination of the two settings, the wallpaper shows on an IDLE
// TIMEOUT sleep.
//
// This is the test that goes red on the shipped rule. Shipped, setWallpaper
// turned the timeout quick-resume flag ON whenever the previous mode was Quick
// Resume, to keep wake fast -- which made the timeout sleep, the ordinary one,
// the single path the chosen wallpaper could never appear on.
void testChoiceReachesTheGlassOnTimeout() {
  using namespace wallpapers;

  for (uint8_t mode = 0; mode < kSleepModeCount; ++mode) {
    for (int qr = 0; qr < 2; ++qr) {
      const SleepChoice choice = choiceForSetWallpaper(mode, qr != 0);
      check(drawsPinnedSleep(choice.sleepScreenMode, choice.quickResumeAfterTimeout, /*fromTimeout=*/true,
                             /*fromReader=*/false),
            "after selecting a wallpaper the idle-timeout sleep must show it", __LINE__);
      // And it must not merely work by accident on the manual path.
      check(drawsPinnedSleep(choice.sleepScreenMode, choice.quickResumeAfterTimeout, /*fromTimeout=*/false,
                             /*fromReader=*/false),
            "after selecting a wallpaper a button-held sleep must show it too", __LINE__);
      check(
          reachOfPinnedSleep(choice.sleepScreenMode, choice.quickResumeAfterTimeout) != Reach::BlockedByMode &&
              reachOfPinnedSleep(choice.sleepScreenMode, choice.quickResumeAfterTimeout) != Reach::BlockedByQuickResume,
          "selecting a wallpaper must not leave the picker in a blocked state", __LINE__);
    }
  }
}

// A deliberate choice made in Settings is either kept or reported. Never
// overwritten in silence.
void testChoiceKeepsWhatItCan() {
  using namespace wallpapers;

  // COVER_CUSTOM already draws /sleep.bmp outside the reader. Handing it a new
  // wallpaper must leave the mode alone -- the user asked for book covers in
  // books and this picture everywhere else, and both still happen.
  const SleepChoice cover = choiceForSetWallpaper(kSleepCoverCustom, false);
  CHECK(cover.sleepScreenMode == kSleepCoverCustom);
  CHECK(!cover.tookOverMode);

  // Already Custom with nothing in the way: nothing changes, nothing is said.
  const SleepChoice plain = choiceForSetWallpaper(kSleepCustom, false);
  CHECK(plain.sleepScreenMode == kSleepCustom);
  CHECK(!plain.tookOverMode);
  CHECK(!plain.clearedQuickResume);
  CHECK(stripLineAfterSelection(plain, reachOfPinnedSleep(plain.sleepScreenMode, plain.quickResumeAfterTimeout)).text ==
        nullptr);

  // Quick Resume as the mode cannot survive: it is the one mode that means "do
  // not show a sleep image at all". So it is replaced, and reported.
  const SleepChoice fromQuickResume = choiceForSetWallpaper(kSleepQuickResume, true);
  CHECK(fromQuickResume.sleepScreenMode == kSleepCustom);
  CHECK(fromQuickResume.tookOverMode);
  CHECK(fromQuickResume.clearedQuickResume);

  for (uint8_t mode = 0; mode < kSleepModeCount; ++mode) {
    for (int qr = 0; qr < 2; ++qr) {
      const SleepChoice choice = choiceForSetWallpaper(mode, qr != 0);
      check(choice.previousMode == mode, "the choice lost the mode it replaced", __LINE__);
    }
  }
}

// THE COVERAGE PROOF, and the reason StripLine is a struct rather than a
// string.
//
// The first version of this fix returned the "what your tap changed" sentence
// and fell through to the standing caveat only when there was none. So a user
// on Cover + Custom with Quick Resume on Timeout who tapped a wallpaper got
// "Quick Resume on Timeout turned off." -- and the caveat that a book cover
// still wins inside a book was suppressed for the rest of the app session.
// That is card #354's own shape (a confident screen, a silent caveat) recurring
// inside its fix, and it is "a warning that can vanish".
//
// So the assertion is not that a sentence exists, and not that it CONTAINS some
// word -- a text check is satisfied by a mention (see "a detector that matches
// the description"). It is that the facts the line declares it covers are
// exactly the facts present, for every reachable combination.
void testStripLineCoversEveryFact() {
  using namespace wallpapers;

  int withCaveat = 0;
  int withBoth = 0;
  for (uint8_t mode = 0; mode < kSleepModeCount; ++mode) {
    for (int qr = 0; qr < 2; ++qr) {
      const SleepChoice choice = choiceForSetWallpaper(mode, qr != 0);
      const Reach reach = reachOfPinnedSleep(choice.sleepScreenMode, choice.quickResumeAfterTimeout);
      const StripLine line = stripLineAfterSelection(choice, reach);
      const bool caveat = reach != Reach::Always;
      const std::string at = " (mode " + std::to_string(static_cast<int>(mode)) + ", qr " + std::to_string(qr) + ")";

      check(line.saysModeChanged == choice.tookOverMode, ("the line does not report the mode takeover" + at).c_str(),
            __LINE__);
      check(line.saysQuickResumeCleared == choice.clearedQuickResume,
            ("the line does not report quick resume being cleared" + at).c_str(), __LINE__);
      check(line.saysCaveat == caveat, ("the line drops a standing caveat" + at).c_str(), __LINE__);

      const bool anything = choice.tookOverMode || choice.clearedQuickResume || caveat;
      check((line.text != nullptr && line.text[0] != '\0') == anything,
            ("a line that has something to say must say it, and one that has nothing must stay quiet" + at).c_str(),
            __LINE__);
      if (caveat) ++withCaveat;
      if (caveat && choice.clearedQuickResume) ++withBoth;
    }
  }
  // The loop must actually REACH the case that was broken, or it proves
  // nothing: a caveat surviving a selection, and one doing so alongside a
  // cleared quick-resume flag ("a test not seen fail").
  CHECK(withCaveat > 0);
  CHECK(withBoth > 0);

  // And with no selection at all the strip still carries the caveat.
  CHECK(reachHint(reachOfPinnedSleep(kSleepCoverCustom, false)) != nullptr);
  CHECK(reachHint(reachOfPinnedSleep(kSleepCustom, false)) == nullptr);
}

}  // namespace

int main() {
  testFileNameFilter();
  testDisplayNames();
  testRoomFor();
  testDrawsPinnedSleep();
  testReach();
  testChoiceReachesTheGlassOnTimeout();
  testChoiceKeepsWhatItCan();
  testStripLineCoversEveryFact();

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

  std::printf("wallpapers: %d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}

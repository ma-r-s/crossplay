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
    CHECK(cellAction(false, 3, -1, false) == CellAction::Set);
    CHECK(cellAction(false, 3, 7, false) == CellAction::Set);
    // ... except on the wallpaper already in use, where it does nothing.
    CHECK(cellAction(false, 3, 3, false) == CellAction::None);

    // Card #354's rule, which arrived on xteink while this branch was open and
    // has to survive the merge: "already the sleep screen" is a reason to do
    // nothing only while it can actually BE the sleep screen. With the settings
    // blocking it, a second tap on the marked wallpaper is the user asking
    // again after nothing happened, and it must re-pin rather than be
    // swallowed. This is the case that goes red if the hold work is resolved by
    // taking its own side of the merge wholesale.
    CHECK(cellAction(false, 3, 3, true) == CellAction::Set);
    // The blocked flag changes nothing anywhere else on the tap path.
    CHECK(cellAction(false, 3, 7, true) == CellAction::Set);

    // A hold opens the sheet, and NEVER sets the sleep screen. This is the one
    // that goes red without the fix: routing a hold as a tap would set a
    // wallpaper the user was reaching past, with no confirmation and no undo.
    CHECK(cellAction(true, 3, -1, false) == CellAction::Sheet);
    CHECK(cellAction(true, 3, 7, false) == CellAction::Sheet);
    // Including on the wallpaper that is already set. Returning None here would
    // make the sheet unreachable for exactly one wallpaper -- the one wearing
    // the marker, and so the one most likely to be held.
    CHECK(cellAction(true, 3, 3, false) == CellAction::Sheet);
    // And the hold wins over #354's re-pin too. The two rules would compete if
    // either were written as an early return beside the other; the hold is
    // asked FIRST, so a blocked-and-marked wallpaper that is HELD still opens
    // the sheet rather than silently repairing the settings the user did not
    // ask about.
    CHECK(cellAction(true, 3, 3, true) == CellAction::Sheet);

    // No hold on a cell that is not a wallpaper.
    CHECK(cellAction(true, -1, 0, false) == CellAction::None);
    CHECK(cellAction(false, -1, 0, false) == CellAction::None);
    CHECK(cellAction(true, -1, 0, true) == CellAction::None);
    CHECK(cellAction(false, -1, 0, true) == CellAction::None);

    // Said as a property rather than as six cases: a hold is never Set, for any
    // index and any selection, blocked or not. A future branch added above the
    // hold check would fail here rather than on the panel -- including one
    // added by a later merge of the settings rule, which is how the ordering
    // was lost the first time.
    for (int blocked = 0; blocked <= 1; ++blocked) {
      for (int idx = 0; idx < 8; ++idx) {
        for (int active = -1; active < 8; ++active) {
          CHECK(cellAction(true, idx, active, blocked != 0) != CellAction::Set);
        }
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

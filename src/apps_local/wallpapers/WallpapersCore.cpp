#include "WallpapersCore.h"

#include <cctype>
#include <cstring>

namespace wallpapers {

bool isSupportedWallpaper(std::string_view name) {
  if (name.empty() || name.front() == '.') return false;
  // Case-insensitive ".bmp" suffix. string_view, so no allocation and no
  // assumption of null termination.
  constexpr std::string_view kExt = ".bmp";
  if (name.size() <= kExt.size()) return false;
  const std::string_view tail = name.substr(name.size() - kExt.size());
  for (size_t i = 0; i < kExt.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(tail[i])) != kExt[i]) return false;
  }
  return true;
}

namespace {
// filename stem (no extension, no path) -> the name people read.
//
// "Duerer", not "Durer" and not "Dürer". EVERY Toybox cut lacks U+00FC (checked:
// toybox_10/14/20/30), and these strings are drawn in toybox_10 as the picker's
// captions -- where a missing glyph is a HOLE, not a box, so "Dürer" would read
// as "D rer" on the panel (typography-fold). "Duerer" is the accepted ASCII
// transliteration; "Durer" is simply a misspelling of an artist's name on a
// screen selling his work. The file STEMS stay `durer-*`: they are identifiers,
// not display text, and renaming them would repack and invalidate the published
// release asset. PROVENANCE.md keeps the real "Dürer" -- it is a citation read
// in a browser, not on a panel with no umlaut.
struct Entry {
  const char* stem;
  const char* full;
  const char* brief;  // used when the cell is too narrow for `full`
};
constexpr Entry kBuiltIns[] = {
    {"bauhaus", "Bauhaus", "Bauhaus"},
    {"bulge", "Bulge", "Bulge"},
    {"celestial", "Celestial Chart", "Celestial"},
    {"checker", "Checker", "Checker"},
    {"cubes", "Cubes", "Cubes"},
    {"dragonflies", "Dragonflies", "Dragonflies"},
    {"durer-eden", "Duerer: Adam and Eve", "Adam and Eve"},
    {"durer-horsemen", "Duerer: Four Horsemen", "Four Horsemen"},
    {"halftone", "Halftone", "Halftone"},
    {"hatch", "Hatch", "Hatch"},
    {"herringbone", "Herringbone", "Herringbone"},
    {"houndstooth", "Houndstooth", "Houndstooth"},
    {"orb", "Orb", "Orb"},
    {"ornament", "Ornament", "Ornament"},
    {"penrose", "Penrose", "Penrose"},
    {"rings", "Rings", "Rings"},
    {"sunburst", "Sunburst", "Sunburst"},
    {"topography", "Topography", "Topography"},
    {"truchet", "Truchet", "Truchet"},
    {"vortex", "Vortex", "Vortex"},
    {"waves", "Waves", "Waves"},
};
}  // namespace

// The header publishes the count as a constant so sentences and size estimates
// can be constexpr; this is what stops the two from drifting apart.
static_assert(sizeof(kBuiltIns) / sizeof(kBuiltIns[0]) == kBuiltInCount,
              "kBuiltInCount and the kBuiltIns table disagree -- update the constant with the table");

size_t builtInCount() { return kBuiltInCount; }

const char* builtInStem(const size_t index) {
  if (index >= builtInCount()) return "";
  return kBuiltIns[index].stem;
}

bool isBuiltInFile(const std::string_view fileName) {
  std::string_view stem = fileName;
  const size_t dot = stem.rfind('.');
  if (dot != std::string_view::npos) stem = stem.substr(0, dot);
  for (const Entry& e : kBuiltIns) {
    if (stem == e.stem) return true;
  }
  return false;
}

bool sortsBefore(const std::string_view a, const std::string_view b) {
  const bool ab = isBuiltInFile(a);
  const bool bb = isBuiltInFile(b);
  if (ab != bb) return !ab;  // a user's own wallpaper comes first
  return a < b;
}

DisplayName displayName(std::string_view fileName) {
  // Strip the extension: nobody wants ".bmp" under a picture.
  std::string_view stem = fileName;
  const size_t dot = stem.rfind('.');
  if (dot != std::string_view::npos && dot > 0) stem = stem.substr(0, dot);
  for (const Entry& e : kBuiltIns) {
    if (stem.size() == std::strlen(e.stem) && stem.compare(e.stem) == 0) {
      return DisplayName{e.full, e.brief};
    }
  }
  std::string own(stem);
  return DisplayName{own, own};
}

bool drawsPinnedSleep(const uint8_t sleepScreenMode, const bool quickResumeAfterTimeout, const bool fromTimeout,
                      const bool fromReader) {
  // 1. Quick resume short-circuits every mode, including the custom ones.
  if (sleepScreenMode == kSleepQuickResume) return false;
  if (fromTimeout && quickResumeAfterTimeout) return false;
  // 2. The transparent mode has its own art (/sleep-overlay.*), not this file.
  if (sleepScreenMode == kSleepTransparentCustom) return false;
  // 3. and 4.
  if (sleepScreenMode == kSleepCustom) return true;
  if (sleepScreenMode == kSleepCoverCustom) return !fromReader;
  // 5. DARK, LIGHT, COVER, BLANK.
  return false;
}

Reach reachOfPinnedSleep(const uint8_t sleepScreenMode, const bool quickResumeAfterTimeout) {
  // Every answer below is asked of drawsPinnedSleep rather than restated, so
  // the classification cannot disagree with the predicate it describes.
  const bool onTimeout = drawsPinnedSleep(sleepScreenMode, quickResumeAfterTimeout, true, false);
  const bool onManual = drawsPinnedSleep(sleepScreenMode, quickResumeAfterTimeout, false, false);
  if (!onTimeout && !onManual) return Reach::BlockedByMode;
  if (!onTimeout) return Reach::BlockedByQuickResume;
  return drawsPinnedSleep(sleepScreenMode, quickResumeAfterTimeout, true, true) ? Reach::Always
                                                                                : Reach::OutsideReaderOnly;
}

const char* reachHint(const Reach reach) {
  // Every sentence names the setting the way SETTINGS names it ("Sleep Screen",
  // "Quick Resume on Timeout", english.yaml), because a hint that describes a
  // setting the user then cannot find is not a hint.
  switch (reach) {
    case Reach::Always:
      return nullptr;
    case Reach::OutsideReaderOnly:
      return "Book covers win inside a book.";
    case Reach::BlockedByQuickResume:
      return "Quick Resume on Timeout hides this.";
    case Reach::BlockedByMode:
      return "Sleep Screen is not set to Custom.";
  }
  return nullptr;
}

const char* sleepScreenModeName(const uint8_t sleepScreenMode) {
  switch (sleepScreenMode) {
    case kSleepDark:
      return "Dark";
    case kSleepLight:
      return "Light";
    case kSleepCustom:
      return "Custom";
    case kSleepCover:
      return "Cover";
    case kSleepCoverCustom:
      return "Cover + Custom";
    case kSleepBlank:
      // What Settings calls it: SettingsList.h maps BLANK to STR_NONE_OPT.
      // Naming it "Blank" would send the user hunting for a value the menu
      // does not have.
      return "None";
    case kSleepQuickResume:
      return "Quick Resume";
    case kSleepTransparentCustom:
      return "Transparent";
    default:
      return "Unknown";
  }
}

SleepChoice choiceForSetWallpaper(const uint8_t sleepScreenMode, const bool quickResumeAfterTimeout) {
  SleepChoice choice;
  choice.previousMode = sleepScreenMode;

  // A mode that already draws /sleep.bmp on a non-reader sleep is kept, so a
  // deliberate COVER_CUSTOM is handed a new picture rather than replaced by
  // one. Asked of the predicate rather than listed, so a mode upstream adds is
  // classified by the rules and not by this function's memory of them.
  const bool modeAlreadyShowsIt = drawsPinnedSleep(sleepScreenMode, /*quickResumeAfterTimeout=*/false,
                                                   /*fromTimeout=*/true, /*fromReader=*/false);
  // The cast is load-bearing under g++ -Wextra: `cond ? uint8_t : SleepScreenMode`
  // is "enumerated and non-enumerated type in conditional expression", which is
  // an error in CI and silent under clang (see the ci-gcc-clang-gap memory).
  choice.sleepScreenMode = modeAlreadyShowsIt ? sleepScreenMode : static_cast<uint8_t>(kSleepCustom);
  choice.tookOverMode = !modeAlreadyShowsIt && sleepScreenMode != kSleepCustom;

  // The timeout flag always comes off. It is not a preference about sleep
  // screens, it is a short-circuit ABOVE them: while it is on, the idle sleep
  // -- the ordinary one -- shows the last screen and no wallpaper of any kind
  // can appear. Leaving it on to keep wake fast is the trade the app used to
  // make, and it cost the user the one thing they had just asked for.
  choice.quickResumeAfterTimeout = false;
  choice.clearedQuickResume = quickResumeAfterTimeout;
  return choice;
}

namespace {
// The mode-takeover half of the sentence. File-local: it is one branch of
// stripLineAfterSelection, not an API.
//
// Terse on purpose. The strip resolves to toybox_14 (buildGridChrome pins
// FONT_SLOT_SMALL) and leaves 446px for one line at the X4 Pro's bezel inset;
// host-tests/wallcaption measures every sentence here against that.
const char* modeTakeoverNote(const uint8_t previousMode) {
  switch (previousMode) {
    case kSleepDark:
      return "Was Dark, now Custom.";
    case kSleepLight:
      return "Was Light, now Custom.";
    case kSleepCover:
      return "Was Cover, now Custom.";
    case kSleepBlank:
      return "Was None, now Custom.";
    case kSleepQuickResume:
      return "Was Quick Resume, now Custom.";
    case kSleepTransparentCustom:
      return "Was Transparent, now Custom.";
    default:
      return nullptr;
  }
}
}  // namespace

StripLine stripLineAfterSelection(const SleepChoice& choice, const Reach reach) {
  StripLine line;
  const bool caveat = reach != Reach::Always;

  // The four states a tap can leave behind, each with a sentence that names
  // EVERY fact present. There is no fall-through: a combination with no
  // sentence would silently drop one of them, which is the defect this shape
  // exists to make impossible.
  //
  // Taking over the mode always ends at CUSTOM, which has no caveat, so
  // "mode changed" and "caveat" cannot co-occur; the loop in
  // host-tests/wallpapers is what establishes that rather than this comment.
  if (choice.tookOverMode) {
    if (choice.clearedQuickResume) {
      line.text = "Now Custom, Quick Resume off.";
      line.saysModeChanged = true;
      line.saysQuickResumeCleared = true;
      return line;
    }
    line.text = modeTakeoverNote(choice.previousMode);
    line.saysModeChanged = line.text != nullptr;
    return line;
  }

  if (choice.clearedQuickResume) {
    if (caveat) {
      // The case the first version lost: the mode was kept because it already
      // draws the wallpaper (COVER_CUSTOM), the quick-resume flag came off, and
      // the caveat that a book cover still wins inside a book STILL APPLIES.
      // Both go in the line.
      line.text = "Quick Resume off. Book covers win.";
      line.saysQuickResumeCleared = true;
      line.saysCaveat = true;
      return line;
    }
    line.text = "Quick Resume on Timeout turned off.";
    line.saysQuickResumeCleared = true;
    return line;
  }

  if (caveat) {
    line.text = reachHint(reach);
    line.saysCaveat = line.text != nullptr;
    return line;
  }
  return line;
}

Room roomFor(bool queryOk, uint64_t freeBytes, uint64_t floorBytes) {
  if (!queryOk) return Room::Unknown;
  return freeBytes >= floorBytes ? Room::Ok : Room::TooFull;
}

}  // namespace wallpapers

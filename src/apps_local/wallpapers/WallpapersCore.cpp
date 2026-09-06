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

CellAction cellAction(const bool heldLong, const int index, const int activeIndex) {
  if (index < 0) return CellAction::None;
  // FIRST, above every other branch. A hold that the SDK classified as a tap is
  // still a hold, and the thing the user was reaching past is setWallpaper --
  // which has no confirmation and no undo.
  if (heldLong) return CellAction::Sheet;
  if (index == activeIndex) return CellAction::None;
  return CellAction::Set;
}

std::string deleteConsequence(const bool builtIn, const bool isActive) {
  std::string out;
  if (builtIn) {
    out += "A built-in wallpaper. Getting it back means downloading the whole set again, not just this one.";
  } else {
    out += "Your own wallpaper. The card holds the only copy, so this cannot be undone.";
  }
  if (isActive) {
    out += " It stays on your sleep screen until you pick another.";
  }
  return out;
}

Room roomFor(bool queryOk, uint64_t freeBytes, uint64_t floorBytes) {
  if (!queryOk) return Room::Unknown;
  return freeBytes >= floorBytes ? Room::Ok : Room::TooFull;
}

}  // namespace wallpapers

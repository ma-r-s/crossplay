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
    {"durer-eden", "Durer: Adam and Eve", "Adam and Eve"},
    {"durer-horsemen", "Durer: Four Horsemen", "Four Horsemen"},
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

Room roomFor(bool queryOk, uint64_t freeBytes, uint64_t floorBytes) {
  if (!queryOk) return Room::Unknown;
  return freeBytes >= floorBytes ? Room::Ok : Room::TooFull;
}

}  // namespace wallpapers

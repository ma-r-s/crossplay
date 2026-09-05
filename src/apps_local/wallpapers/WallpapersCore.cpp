#include "WallpapersCore.h"

#include <cctype>

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

Room roomFor(bool queryOk, uint64_t freeBytes, uint64_t floorBytes) {
  if (!queryOk) return Room::Unknown;
  return freeBytes >= floorBytes ? Room::Ok : Room::TooFull;
}

}  // namespace wallpapers

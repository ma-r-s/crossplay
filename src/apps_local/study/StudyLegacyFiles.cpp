#include "StudyLegacyFiles.h"

#include <HalStorage.h>

#include <cstdio>

namespace study {

int removeLegacyGlyphLists(const char* deckDir) {
  static constexpr const char* kLists[] = {"headword", "sentence", "latin", "ruby"};
  char path[96];
  int removed = 0;
  for (const char* list : kLists) {
    std::snprintf(path, sizeof(path), "%s/glyphs-%s.txt", deckDir, list);
    if (Storage.exists(path) && Storage.remove(path)) ++removed;
  }
  return removed;
}

}  // namespace study

#include "ShelfState.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace shelf {

namespace {

int clampItem(const long value, const int limit) {
  if (value < 0 || limit <= 0) return 0;
  return value > limit ? limit : static_cast<int>(value);
}

// The second line, trimmed. Returns false when there is no usable title, which
// covers a one-line file (every file written before wake could resume), a
// blank second line, and a title longer than a State can hold.
bool readTitleLine(const char* afterPosition, char* out) {
  const char* nl = std::strchr(afterPosition, '\n');
  if (nl == nullptr) return false;

  const char* start = nl + 1;
  while (*start == ' ' || *start == '\t') ++start;

  const char* end = start;
  while (*end != '\0' && *end != '\n' && *end != '\r') ++end;
  while (end > start && (end[-1] == ' ' || end[-1] == '\t')) --end;

  const size_t length = static_cast<size_t>(end - start);
  if (length == 0 || length > MAX_ITEM_TITLE) return false;

  std::memcpy(out, start, length);
  out[length] = '\0';
  return true;
}

}  // namespace

bool parseState(const char* text, const int folderCount, const int* itemLimits, State& out) {
  if (text == nullptr || itemLimits == nullptr || folderCount <= 0 || folderCount > MAX_FOLDERS) return false;

  const char* cursor = text;
  char* next = nullptr;
  const long folderValue = std::strtol(cursor, &next, 10);
  if (next == cursor) return false;
  cursor = next;

  // Parsed into locals and committed only at the end, so a file that runs out
  // half way leaves the caller's defaults rather than half of them.
  int items[MAX_FOLDERS] = {};
  for (int i = 0; i < folderCount; ++i) {
    const long value = std::strtol(cursor, &next, 10);
    if (next == cursor) return false;
    cursor = next;
    items[i] = clampItem(value, itemLimits[i]);
  }

  char title[MAX_ITEM_TITLE + 1] = {};
  readTitleLine(cursor, title);

  out.lastFolder = folderValue < 0 || folderValue >= folderCount ? -1 : static_cast<int>(folderValue);
  for (int i = 0; i < folderCount; ++i) out.resumeRow[i] = items[i];
  std::memcpy(out.openTitle, title, sizeof(title));
  return true;
}

size_t formatState(const State& state, const int folderCount, char* out, const size_t outSize) {
  if (out == nullptr || outSize == 0 || folderCount <= 0 || folderCount > MAX_FOLDERS) return 0;

  int used = std::snprintf(out, outSize, "%d", state.lastFolder);
  if (used < 0 || static_cast<size_t>(used) >= outSize) return 0;

  for (int i = 0; i < folderCount; ++i) {
    const int written = std::snprintf(out + used, outSize - static_cast<size_t>(used), " %d", state.resumeRow[i]);
    if (written < 0 || static_cast<size_t>(used + written) >= outSize) return 0;
    used += written;
  }

  // No open item writes exactly what every earlier firmware wrote, so the
  // common case does not change the file at all.
  const int tail = state.openTitle[0] == '\0'
                       ? std::snprintf(out + used, outSize - static_cast<size_t>(used), "\n")
                       : std::snprintf(out + used, outSize - static_cast<size_t>(used), "\n%s\n", state.openTitle);
  if (tail < 0 || static_cast<size_t>(used + tail) >= outSize) return 0;
  return static_cast<size_t>(used + tail);
}

}  // namespace shelf

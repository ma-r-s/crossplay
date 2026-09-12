#include "ShelfHidden.h"

#include <cstring>

namespace shelf {

namespace {

// The line between `begin` and `end`, trimmed of spaces, tabs and the carriage
// return a file written on another machine carries. Returns an empty view when
// nothing is left, which is how a blank line disappears.
std::string trimmed(const char* begin, const char* end) {
  while (begin < end && (*begin == ' ' || *begin == '\t' || *begin == '\r')) ++begin;
  while (end > begin && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) --end;
  return std::string(begin, static_cast<size_t>(end - begin));
}

}  // namespace

bool HiddenSet::contains(const char* title) const {
  if (title == nullptr) return false;
  for (const std::string& held : titles_) {
    if (held == title) return true;
  }
  return false;
}

bool HiddenSet::set(const char* title, const bool hidden) {
  if (title == nullptr || *title == '\0') return false;
  for (size_t i = 0; i < titles_.size(); ++i) {
    if (titles_[i] != title) continue;
    if (hidden) return false;
    titles_.erase(titles_.begin() + static_cast<std::ptrdiff_t>(i));
    return true;
  }
  if (!hidden) return false;
  titles_.emplace_back(title);
  return true;
}

void parseHidden(const char* text, HiddenSet& out) {
  out = HiddenSet{};
  if (text == nullptr) return;

  const char* cursor = text;
  size_t taken = 0;
  while (*cursor != '\0' && taken < MAX_HIDDEN) {
    const char* end = cursor;
    while (*end != '\0' && *end != '\n') ++end;
    const std::string title = trimmed(cursor, end);
    // A title too long to be any item's is not truncated to one that might be:
    // the wake-resume learned that lesson on the same card (ShelfState.h).
    if (!title.empty() && title.size() <= MAX_ITEM_TITLE && out.set(title.c_str(), true)) ++taken;
    if (*end == '\0') break;
    cursor = end + 1;
  }
}

std::string formatHidden(const HiddenSet& set) {
  std::string out;
  for (size_t i = 0; i < set.size(); ++i) {
    out += set.at(i);
    out += '\n';
  }
  return out;
}

}  // namespace shelf

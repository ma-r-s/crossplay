#pragma once

// Save for later: the local half, which is the whole feature.
//
// ---------------------------------------------------------------------------
// Entirely local: no account, no token, no service. A read-later shelf built on
// somebody else's API is broken for every user until they have done setup, and
// broken again on the day that API is withdrawn. This one is complete on first
// boot and cannot stop working.
//
// The format is chosen to outlive the code. The index is a plain tab-separated
// file and each article is plain UTF-8 text, so the library is readable in any
// editor and recoverable by hand -- and if this app is ever deleted, the
// articles are still there.
// ---------------------------------------------------------------------------
//
// Freestanding: parsing and formatting only. The Activity owns the SD card.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hn {

struct SavedArticle {
  // Derived from the URL, so saving the same page twice updates one entry
  // rather than growing a duplicate. Also the article's filename.
  std::string id;
  std::string title;
  std::string url;
  uint32_t savedAt = 0;
};

// A stable 32-bit FNV-1a of the URL, in lowercase hex. Short enough for a
// filename on a FAT card and stable across reboots, which a counter would not
// be once an entry in the middle is removed.
std::string savedIdFor(std::string_view url);

// The index as it is written to the card. One header line carrying a format
// version, then one tab-separated line per article.
//
// Tab-separated rather than JSON because this file is the thing that has to
// survive: it is readable in any text editor, recoverable by hand if a write is
// ever interrupted, and needs no parser to inspect. Titles are sanitised of
// tabs and newlines on the way in, so a field can never swallow the next one.
std::string serializeSavedIndex(const std::vector<SavedArticle>& articles);

// Returns false only when the text is not this format at all. A single damaged
// line is skipped rather than failing the whole file: losing one entry beats
// losing the library.
bool parseSavedIndex(std::string_view text, std::vector<SavedArticle>& out);

// Strip anything that would break the row format, and collapse whitespace.
std::string sanitizeField(std::string_view text);

}  // namespace hn

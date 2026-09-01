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

// --- Keeping the conversation --------------------------------------------
//
// A thread is saved the same way an article is, under its OWN key: Hacker
// News's canonical item page. Not the story's link, because these are two
// different pieces of reading and sharing a key would make them one entry --
// saving the thread would silently replace the article, and the reader's mark
// would claim the article was kept when the discussion was.
//
// It also gives the pieces with no link of their own a key at all. An Ask HN
// post is its own text, `Story::url` is empty, and an empty URL is what
// Library::save refuses: those posts could not be kept by any route.
std::string savedThreadUrl(uint32_t storyId);

// What the shelf row says for a saved thread. Two rows carrying one headline,
// one the article and one the discussion, are the same row as far as anyone
// reading the shelf is concerned -- so the discussion says so, in front, where
// a headline cut to the row width still shows it.
std::string savedThreadTitle(std::string_view storyTitle);

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

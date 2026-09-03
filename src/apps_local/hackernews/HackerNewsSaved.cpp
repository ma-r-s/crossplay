#include "HackerNewsSaved.h"

#include <Utf8.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace hn {
namespace {

constexpr char kMagic[] = "hnsaved";
// 2 dropped the two Instapaper columns that version 1 carried.
//
// Bumping is not optional and this is why: read with the version-2 field order,
// a version-1 row hands back its `instapaperId` as the title and its
// `textOnDevice` flag as the URL. Every saved article then displays as "0",
// and because the URL no longer matches, the reader believes nothing is saved
// and saves a second copy on every tap. One unbumped number, two bugs, and
// neither of them looks like a format problem from the outside.
constexpr int kVersion = 2;

std::string field(std::string_view row, size_t& cursor) {
  if (cursor > row.size()) return {};
  const size_t tab = row.find('\t', cursor);
  const size_t end = tab == std::string_view::npos ? row.size() : tab;
  std::string value(row.substr(cursor, end - cursor));
  cursor = tab == std::string_view::npos ? row.size() + 1 : tab + 1;
  return value;
}

uint32_t toUint(const std::string& text) {
  if (text.empty()) return 0;
  return static_cast<uint32_t>(std::strtoul(text.c_str(), nullptr, 10));
}

}  // namespace

std::string savedIdFor(const std::string_view url) {
  // FNV-1a, 32 bit. Chosen over anything cryptographic because the only
  // property needed is that the same URL gives the same name.
  uint32_t hash = 2166136261u;
  for (const char c : url) {
    hash ^= static_cast<uint8_t>(c);
    hash *= 16777619u;
  }
  char out[9];
  std::snprintf(out, sizeof(out), "%08lx", static_cast<unsigned long>(hash));
  return std::string(out);
}

std::string savedThreadUrl(const uint32_t storyId) {
  char url[64];
  std::snprintf(url, sizeof(url), "https://news.ycombinator.com/item?id=%lu", static_cast<unsigned long>(storyId));
  return std::string(url);
}

std::string savedThreadTitle(const std::string_view storyTitle) {
  std::string title = "Comments: ";
  title.append(storyTitle);
  return title;
}

std::string sanitizeField(const std::string_view text) {
  // Flattening only, and deliberately NOT folding. This runs on the URL column
  // as well as the title, and the URL has to come back byte for byte:
  // savedIdFor() hashes it, so a character changed here is an article that can
  // never be unsaved again. The typography fold is applied to the title alone,
  // at both ends -- see serializeSavedIndex and parseSavedIndex below.
  std::string out;
  out.reserve(text.size());
  bool pendingSpace = false;
  for (const char c : text) {
    // Tabs and newlines would end the field or the row, so a title containing
    // one could silently consume the URL after it.
    const bool blank = c == '\t' || c == '\n' || c == '\r' || c == ' ';
    if (blank) {
      pendingSpace = !out.empty();
      continue;
    }
    if (pendingSpace) {
      out.push_back(' ');
      pendingSpace = false;
    }
    out.push_back(c);
  }
  return out;
}

std::string serializeSavedIndex(const std::vector<SavedArticle>& articles) {
  std::string out = kMagic;
  out.push_back(' ');
  out += std::to_string(kVersion);
  out.push_back('\n');
  for (const SavedArticle& article : articles) {
    out += article.id;
    out.push_back('\t');
    out += std::to_string(article.savedAt);
    out.push_back('\t');
    out += sanitizeField(utf8FoldTypography(article.title));
    out.push_back('\t');
    out += sanitizeField(article.url);
    out.push_back('\n');
  }
  return out;
}

// How many tab-separated columns a row actually has.
int countFields(const std::string_view row) {
  int n = 1;
  for (const char c : row) {
    if (c == '\t') ++n;
  }
  return n;
}

bool parseSavedIndex(const std::string_view text, std::vector<SavedArticle>& out) {
  out.clear();
  if (text.compare(0, sizeof(kMagic) - 1, kMagic) != 0) return false;

  size_t i = text.find('\n');
  const std::string_view header = text.substr(0, i == std::string_view::npos ? text.size() : i);
  const int version =
      static_cast<int>(std::strtol(std::string(header.substr(sizeof(kMagic) - 1)).c_str(), nullptr, 10));
  // A version from the future is not something to guess at: a library written
  // by a newer build is left exactly as it is rather than half-read and then
  // overwritten with whatever survived.
  if (version < 1 || version > kVersion) return false;

  if (i == std::string_view::npos) return true;  // a header and nothing saved yet
  ++i;

  while (i < text.size()) {
    const size_t nl = text.find('\n', i);
    const std::string_view row = text.substr(i, (nl == std::string_view::npos ? text.size() : nl) - i);
    i = nl == std::string_view::npos ? text.size() : nl + 1;
    if (row.empty()) continue;

    size_t cursor = 0;
    SavedArticle article;
    article.id = field(row, cursor);
    article.savedAt = toUint(field(row, cursor));
    // Version 1 SOMETIMES carried an Instapaper bookmark id and a
    // have-we-got-the-text flag here, and sometimes did not: Mario's own
    // library, written 2026-08-05 by the build this feature shipped in, is
    // version 1 with four columns. Deciding from the version alone read his
    // title out of a field past the end of the row, got an empty one, and
    // discarded every entry as damage -- so the shelf came up empty for the
    // one person who had anything on it.
    //
    // So count the row rather than trusting the header. Six columns means the
    // two legacy ones are present; four means they never were.
    if (version == 1 && countFields(row) >= 6) {
      field(row, cursor);
      field(row, cursor);
    }
    // Folded on the READ side as well, which is what makes an index written
    // before the fold existed draw correctly instead of keeping its holes for
    // as long as it stays saved. The URL is NOT folded, for the reason
    // sanitizeField gives.
    article.title = utf8FoldTypography(field(row, cursor));
    article.url = field(row, cursor);

    // A row with no id or no title is damage rather than data. Skipping it
    // keeps the rest of the library, which is the whole reason this is a line
    // format instead of one document that either parses or does not.
    if (article.id.empty() || article.title.empty()) continue;
    out.push_back(std::move(article));
  }
  return true;
}

}  // namespace hn

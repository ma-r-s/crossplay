#include "BookmarkUtil.h"

#include <Utf8.h>

#include <algorithm>
#include <string>

std::string BookmarkUtil::getBookmarksDir() { return "/.crosspoint/bookmarks/"; }

std::string BookmarkUtil::getBookmarkPath(const std::string& bookPath) {
  // remove leading slash and replace internal slashes to create a flat filename
  std::string bookName = std::string(bookPath).erase(0, 1);
  std::replace(bookName.begin(), bookName.end(), '/', '_');
  std::replace(bookName.begin(), bookName.end(), '\\', '_');
  const size_t lastDot = bookName.find_last_of('.');
  if (lastDot != std::string::npos) {
    bookName.erase(lastDot);
  }
  bookName += ".json";
  return getBookmarksDir() + bookName;
}

std::string BookmarkUtil::sanitizeBookmarkSummary(std::string summary) {
  // This used to collapse runs of whitespace and then DELETE the newlines that
  // survived, which joined the words either side of every line break: a summary
  // reading "call me\nIshmael" was stored as "call meIshmael". EPUB body text
  // is wrapped, so that fired on most bookmarks rather than on a rare one.
  summary = utf8CollapseWhitespace(summary);
  if (summary.size() > 72) {
    summary.resize(72);
  }
  return summary;
}

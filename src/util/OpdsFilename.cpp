#include "OpdsFilename.h"

#include "StringUtils.h"

namespace {

// sanitizeFilename's own cap. Named here because the budget below is split
// against it, and a split derived from a literal somebody else can change is
// the rot pattern this fork keeps paying for.
constexpr size_t kNameBudget = 100;
constexpr size_t kJoin = 3;  // " - "

// The author half never gets more than this, so the title always has room.
// A book is identified by its title; an author list that crowds the title out
// produces a filename nobody can match to a book.
constexpr size_t kAuthorBudget = 34;

// One name, not a credits list. OPDS joins contributors with ';' and the
// default catalogue returns five for some books -- "Carroll, Lewis;Hargreaves,
// Alice Pleasance Liddell;Winchester, Simon;..." spent the entire budget before
// the title was reached, so the download landed as a list of strangers with no
// book in it.
std::string primaryAuthor(const std::string& author) {
  const size_t sep = author.find(';');
  if (sep == std::string::npos) return author;
  // Trailing space before the separator is common in these feeds.
  size_t end = sep;
  while (end > 0 && author[end - 1] == ' ') --end;
  return author.substr(0, end);
}

}  // namespace

std::string opdsBookFilename(const std::string& author, const std::string& title, OpdsFilenameFormat format) {
  if (format == OpdsFilenameFormat::TitleOnly || author.empty()) {
    return StringUtils::sanitizeFilename(title, kNameBudget) + ".epub";
  }

  // Sanitised separately and budgeted, rather than joined and then cut: cutting
  // the join means whichever component comes second is the one that disappears,
  // and for AuthorTitle that is always the title.
  const std::string namePart = StringUtils::sanitizeFilename(primaryAuthor(author), kAuthorBudget);
  // Saturating, never bare subtraction: these are size_t, and kAuthorBudget
  // growing past kNameBudget - kJoin would wrap the remainder to a huge value
  // and silently restore the unbudgeted behaviour this function exists to
  // prevent. The static_assert keeps the arithmetic honest even so.
  static_assert(kAuthorBudget + kJoin < kNameBudget, "the title must keep a share of the budget");
  const size_t used = namePart.size() + kJoin;
  const size_t titleBudget = used < kNameBudget ? kNameBudget - used : 1;
  const std::string titlePart = StringUtils::sanitizeFilename(title, titleBudget);

  const std::string base =
      format == OpdsFilenameFormat::TitleAuthor ? titlePart + " - " + namePart : namePart + " - " + titlePart;
  return base + ".epub";
}

std::string opdsBookPath(const char* folder, const std::string& author, const std::string& title,
                         const OpdsFilenameFormat format) {
  std::string path;
  path.reserve(96);
  if (folder != nullptr) path += folder;
  path += '/';
  path += opdsBookFilename(author, title, format);
  return path;
}

std::string opdsPathBasename(const std::string& path) {
  // rfind, not find: the folder may itself be nested ("/Books/OPDS/x.epub").
  const size_t slash = path.rfind('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string opdsPathFolder(const std::string& path) {
  const size_t slash = path.rfind('/');
  return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

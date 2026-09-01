// The downloaded book's filename.
//
// Written from what a user needs out of the name -- to find the book again --
// rather than from what the builder does. The case that motivated it shipped a
// filename containing five author names and no title at all.

#include "../../src/util/OpdsFilename.h"

#include <cstdio>
#include <string>

namespace {
int checks = 0, failed = 0;

void expect(const bool ok, const std::string& what) {
  ++checks;
  if (!ok) { ++failed; std::printf("  FAIL: %s\n", what.c_str()); }
}

void contains(const std::string& hay, const std::string& needle, const std::string& what) {
  expect(hay.find(needle) != std::string::npos, what + "  (got \"" + hay + "\")");
}

const std::string kFiveAuthors =
    "Carroll, Lewis;Hargreaves, Alice Pleasance Liddell;Winchester, Simon;Carroll, Lewis;Hargreaves, Alice";
const std::string kAlice = "Alice's Adventures in Wonderland";

void theTitleSurvivesALongAuthorList() {
  const auto name = opdsBookFilename(kFiveAuthors, kAlice, OpdsFilenameFormat::AuthorTitle);
  // The shipped bug: every byte went to names and the title never appeared.
  contains(name, "Alice", "a five-author list must not crowd out the title");
  expect(name.size() <= 105, "stays within the 100-byte budget plus .epub");
  expect(name.rfind(".epub") == name.size() - 5, "ends in .epub");
}

// The author component, as it appears in the finished name.
std::string authorPartOf(const std::string& name) {
  const size_t sep = name.find(" - ");
  return sep == std::string::npos ? std::string() : name.substr(0, sep);
}

void onlyTheFirstAuthorIsKept() {
  const auto name = opdsBookFilename(kFiveAuthors, kAlice, OpdsFilenameFormat::AuthorTitle);
  // Asserted on the SEPARATOR, not on a later name being absent: truncation
  // alone removes "Winchester", so a test that only checked for it passes with
  // the split deleted. A surviving ';' means the list was never split.
  expect(name.find(';') == std::string::npos, "no credits-list separator survives into the name");
  expect(authorPartOf(name) == "Carroll, Lewis", "the author component is exactly the first author");
}

void theOrdinaryCaseIsUnchanged() {
  // What Gutenberg already produced correctly, and must keep producing.
  expect(opdsBookFilename("Austen, Jane", "Pride and Prejudice", OpdsFilenameFormat::AuthorTitle) ==
             "Austen, Jane - Pride and Prejudice.epub",
         "a normal author and title are untouched");
}

void bothOrdersKeepBothParts() {
  const auto ta = opdsBookFilename(kFiveAuthors, kAlice, OpdsFilenameFormat::TitleAuthor);
  contains(ta, "Alice", "TitleAuthor keeps the title");
  contains(ta, "Carroll", "TitleAuthor keeps the author");
}

void titleOnlyIgnoresTheAuthorEntirely() {
  const auto name = opdsBookFilename(kFiveAuthors, kAlice, OpdsFilenameFormat::TitleOnly);
  contains(name, "Alice", "TitleOnly keeps the title");
  expect(name.find("Carroll") == std::string::npos, "TitleOnly drops the author");
}

void aVeryLongSingleAuthorStillLeavesRoom() {
  // No semicolon to split on, so the budget is the only thing protecting the
  // title. Asserted on the author component's SIZE rather than on the title
  // surviving: with the budget removed the remainder underflows to a huge
  // value and the title survives anyway, so the obvious assertion passes by
  // accident on unsigned wraparound.
  const std::string longName(180, 'Z');
  const auto name = opdsBookFilename(longName, kAlice, OpdsFilenameFormat::AuthorTitle);
  expect(authorPartOf(name).size() <= 40, "the author component stays inside its budget");
  contains(name, "Alice", "a single overlong author must not crowd out the title");
  expect(name.size() <= 105, "the whole name stays within budget plus .epub");
}

void anEmptyAuthorDoesNotLeaveADanglingSeparator() {
  const auto name = opdsBookFilename("", kAlice, OpdsFilenameFormat::AuthorTitle);
  expect(name.find(" - ") == std::string::npos, "no ' - ' when there is no author");
  contains(name, "Alice", "title still present with no author");
}

void anEmptyTitleStillProducesAUsableName() {
  const auto name = opdsBookFilename("Austen, Jane", "", OpdsFilenameFormat::AuthorTitle);
  expect(!name.empty() && name != ".epub", "never produces a bare extension");
  expect(name.rfind(".epub") == name.size() - 5, "still ends in .epub");
}
}  // namespace

int main() {
  theTitleSurvivesALongAuthorList();
  onlyTheFirstAuthorIsKept();
  theOrdinaryCaseIsUnchanged();
  bothOrdersKeepBothParts();
  titleOnlyIgnoresTheAuthorEntirely();
  aVeryLongSingleAuthorStillLeavesRoom();
  anEmptyAuthorDoesNotLeaveADanglingSeparator();
  anEmptyTitleStillProducesAUsableName();
  std::printf("opdsfilename: %d checks, %d failed\n", checks, failed);
  return failed == 0 ? 0 : 1;
}

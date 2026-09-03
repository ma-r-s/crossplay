// The downloaded book's filename.
//
// Written from what a user needs out of the name -- to find the book again --
// rather than from what the builder does. The case that motivated it shipped a
// filename containing five author names and no title at all.

#include <cstdio>
#include <string>

#include "../../src/util/OpdsFilename.h"

namespace {
int checks = 0, failed = 0;

void expect(const bool ok, const std::string& what) {
  ++checks;
  if (!ok) {
    ++failed;
    std::printf("  FAIL: %s\n", what.c_str());
  }
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

// --- What the SAVED screen shows ---------------------------------------
//
// The verdict screen after a download reports the name on the card. Failure
// used to speak and success was silent, so a reader could only tell a finished
// download from an abandoned one by going and looking for the file. What makes
// that screen worth anything is that the name it prints is the name that was
// written -- and the filename is NOT the title, so recomposing it from the
// catalog entry would print something the card does not contain.

void theNameShownIsTheNameWritten() {
  // Every awkward shape at once: a credits list, an apostrophe, and a folder.
  const auto path = opdsBookPath("/Books", kFiveAuthors, kAlice, OpdsFilenameFormat::AuthorTitle);
  expect(opdsPathBasename(path) == opdsBookFilename(kFiveAuthors, kAlice, OpdsFilenameFormat::AuthorTitle),
         "the basename of the written path is exactly the composed filename");
  expect(opdsPathFolder(path) == "/Books", "the folder half comes back whole");
}

void theSdRootHasNoFolderHalf() {
  // The default: opdsDownloadFolder is "", so the path is "/Name.epub" and the
  // screen must say SD root rather than print an empty directory.
  const auto path = opdsBookPath("", "Austen, Jane", "Persuasion", OpdsFilenameFormat::AuthorTitle);
  expect(path == "/Austen, Jane - Persuasion.epub", "root paths carry a single leading slash");
  expect(opdsPathFolder(path).empty(), "no folder half at the SD root");
  expect(opdsPathBasename(path) == "Austen, Jane - Persuasion.epub", "the name is still recoverable");
}

void aNestedFolderSplitsAtTheLastSlash() {
  // rfind, not find: a reader who set "/Books/OPDS" would otherwise be told
  // their book is called "OPDS/Austen... .epub" and shown the wrong folder.
  const auto path = opdsBookPath("/Books/OPDS", "Austen, Jane", "Emma", OpdsFilenameFormat::AuthorTitle);
  expect(opdsPathFolder(path) == "/Books/OPDS", "the whole nested folder is the folder half");
  expect(opdsPathBasename(path) == "Austen, Jane - Emma.epub", "the name half holds no separator");
}

void aSlashInTheTitleCannotMoveTheSplit() {
  // The split is only safe because sanitizeFilename turns '/' into '_'. If that
  // ever stopped, this path would gain a separator the reader never asked for
  // and the screen would name a file that does not exist.
  const auto path = opdsBookPath("/Books", "Goethe", "Faust I/II", OpdsFilenameFormat::AuthorTitle);
  expect(opdsPathFolder(path) == "/Books", "a title separator does not become a directory");
  contains(opdsPathBasename(path), "Faust I_II", "the slash is sanitized inside the name");
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
  theNameShownIsTheNameWritten();
  theSdRootHasNoFolderHalf();
  aNestedFolderSplitsAtTheLastSlash();
  aSlashInTheTitleCannotMoveTheSplit();
  std::printf("opdsfilename: %d checks, %d failed\n", checks, failed);
  return failed == 0 ? 0 : 1;
}

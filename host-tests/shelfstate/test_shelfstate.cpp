// The shelf.cfg format, checked without a card.
//
// The cases are written from the format's contract rather than from the parser:
// what an older firmware's one-line file must do, what a title with a space in
// it must do, what a half-written file must do. A test derived from the
// parser's own assumptions cannot falsify them.

#include <cstdio>
#include <cstring>
#include <string>

#include "ShelfState.h"

namespace {

int failures = 0;
int checks = 0;

void check(const bool ok, const std::string& what) {
  ++checks;
  if (!ok) {
    ++failures;
    std::printf("  FAIL: %s\n", what.c_str());
  }
}

void checkEqual(const char* got, const char* want, const std::string& what) {
  check(std::strcmp(got, want) == 0, what + " (got \"" + got + "\", want \"" + want + "\")");
}

void checkEqual(const int got, const int want, const std::string& what) {
  check(got == want, what + " (got " + std::to_string(got) + ", want " + std::to_string(want) + ")");
}

// Two folders, as the registry has today: GAMES with 17 rows, APPS with 4.
constexpr int kFolders = 2;
const int kLimits[shelf::MAX_FOLDERS] = {16, 3, 0, 0};

shelf::State parsed(const char* text, bool& ok) {
  shelf::State state;
  ok = shelf::parseState(text, kFolders, kLimits, state);
  return state;
}

void aOneLineFileHasNoResumeTitle() {
  bool ok = false;
  const shelf::State s = parsed("0 14 1\n", ok);
  check(ok, "one-line file parses");
  checkEqual(s.lastFolder, 0, "one-line file keeps its folder");
  checkEqual(s.resumeRow[0], 14, "one-line file keeps row 0");
  checkEqual(s.resumeRow[1], 1, "one-line file keeps row 1");
  checkEqual(s.openTitle, "", "a file written before wake could resume resumes nothing");
}

void aFileWithNoTrailingNewlineAtAllStillParses() {
  bool ok = false;
  const shelf::State s = parsed("1 3 2", ok);
  check(ok, "file without a trailing newline parses");
  checkEqual(s.lastFolder, 1, "folder survives a missing newline");
  checkEqual(s.openTitle, "", "no newline means no second line");
}

void theSecondLineIsTheOpenItem() {
  bool ok = false;
  const shelf::State s = parsed("0 14 1\nSUDOKU\n", ok);
  check(ok, "two-line file parses");
  checkEqual(s.resumeRow[0], 14, "the position still parses with a title after it");
  checkEqual(s.openTitle, "SUDOKU", "the second line is the open item");
}

void aTitleMayContainSpaces() {
  bool ok = false;
  const shelf::State s = parsed("0 7 0\nSEA SALT\n", ok);
  checkEqual(s.openTitle, "SEA SALT", "a two-word title is one title, not two tokens");

  const shelf::State four = parsed("0 10 0\nCONNECT FOUR\n", ok);
  checkEqual(four.openTitle, "CONNECT FOUR", "CONNECT FOUR survives the space");
}

void carriageReturnsAndPaddingAreTrimmed() {
  bool ok = false;
  checkEqual(parsed("0 1 0\r\nCHESS\r\n", ok).openTitle, "CHESS", "CRLF is trimmed");
  checkEqual(parsed("0 1 0\n  CHESS  \n", ok).openTitle, "CHESS", "padding is trimmed");
}

void aBlankSecondLineIsNoTitle() {
  bool ok = false;
  checkEqual(parsed("0 1 0\n\n", ok).openTitle, "", "a blank second line resumes nothing");
  checkEqual(parsed("0 1 0\n   \n", ok).openTitle, "", "a whitespace second line resumes nothing");
}

void anOverlongTitleIsDroppedNotTruncated() {
  std::string tooLong(shelf::MAX_ITEM_TITLE + 1, 'X');
  bool ok = false;
  const shelf::State s = parsed(("0 1 0\n" + tooLong + "\n").c_str(), ok);
  check(ok, "an overlong title still leaves a usable position");
  // Truncation would produce a title that matches no item anyway, and would
  // look like a real one in a log. Dropping says what happened.
  checkEqual(s.openTitle, "", "a title too long to hold is dropped, not cut short");
}

void aHalfWrittenFileLeavesTheCallerAlone() {
  shelf::State state;
  state.lastFolder = 1;
  state.resumeRow[0] = 9;
  std::snprintf(state.openTitle, sizeof(state.openTitle), "%s", "CHESS");

  // Only two of the three numbers a two-folder registry needs.
  check(!shelf::parseState("0 14", kFolders, kLimits, state), "a half-written line is rejected");
  checkEqual(state.lastFolder, 1, "a rejected parse does not touch the folder");
  checkEqual(state.resumeRow[0], 9, "a rejected parse does not touch the rows");
  checkEqual(state.openTitle, "CHESS", "a rejected parse does not touch the title");

  check(!shelf::parseState("", kFolders, kLimits, state), "an empty file is rejected");
  check(!shelf::parseState("GAMES\n", kFolders, kLimits, state), "a file of text is rejected");
}

void rowsAreClampedToTheRegistryAsItStandsNow() {
  bool ok = false;
  // Written when GAMES had more rows than it has now.
  const shelf::State s = parsed("0 99 99\n", ok);
  // The LAST row, not the first: the row stands for the page the folder was left
  // on, and a folder that shrank under you is nearer its end than its top. Same
  // rule as shelfui::resumeRowFor, which is what turns this row into that page.
  checkEqual(s.resumeRow[0], 16, "a row past the end of GAMES clamps to its last row");
  checkEqual(s.resumeRow[1], 3, "a row past the end of APPS clamps to its last row");

  const shelf::State negative = parsed("0 -5 -5\n", ok);
  checkEqual(negative.resumeRow[0], 0, "a negative row clamps to the top");
}

void anImpossibleFolderMeansNoFolder() {
  bool ok = false;
  checkEqual(parsed("9 0 0\n", ok).lastFolder, -1, "a folder past the end means none");
  checkEqual(parsed("-1 0 0\n", ok).lastFolder, -1, "a negative folder means none");
}

void formattingWithNoOpenItemMatchesTheOldFormat() {
  shelf::State s;
  s.lastFolder = 0;
  s.resumeRow[0] = 14;
  s.resumeRow[1] = 1;

  char out[96] = {};
  const size_t used = shelf::formatState(s, kFolders, out, sizeof(out));
  check(used > 0, "a state with no open item formats");
  // Byte-identical to what every earlier firmware wrote, so the common case
  // does not rewrite the file into a shape an older build cannot read.
  checkEqual(out, "0 14 1\n", "no open item writes exactly the old one-line format");
}

void formattingAndParsingRoundTrip() {
  shelf::State s;
  s.lastFolder = 1;
  s.resumeRow[0] = 3;
  s.resumeRow[1] = 2;
  std::snprintf(s.openTitle, sizeof(s.openTitle), "%s", "HACKER NEWS");

  char out[96] = {};
  check(shelf::formatState(s, kFolders, out, sizeof(out)) > 0, "a state with an open item formats");

  bool ok = false;
  const shelf::State back = parsed(out, ok);
  check(ok, "what formatState wrote, parseState reads");
  checkEqual(back.lastFolder, 1, "round trip keeps the folder");
  checkEqual(back.resumeRow[0], 3, "round trip keeps row 0");
  checkEqual(back.resumeRow[1], 2, "round trip keeps row 1");
  checkEqual(back.openTitle, "HACKER NEWS", "round trip keeps the open item");
}

void formattingRefusesRatherThanTruncating() {
  shelf::State s;
  s.lastFolder = 1;
  s.resumeRow[0] = 3;
  s.resumeRow[1] = 2;
  std::snprintf(s.openTitle, sizeof(s.openTitle), "%s", "KNUCKLEBONES");

  // Room for the position but not the title. A truncated write would leave a
  // file claiming an item nobody can resume.
  char out[10] = {};
  checkEqual(static_cast<int>(shelf::formatState(s, kFolders, out, sizeof(out))), 0,
             "a state that does not fit reports failure instead of writing half");
}

void aFolderCountTheStateCannotHoldIsRefused() {
  shelf::State s;
  char out[96] = {};
  check(!shelf::parseState("0 1 2 3 4 5\n", shelf::MAX_FOLDERS + 1, kLimits, s),
        "parse refuses more folders than a State holds");
  checkEqual(static_cast<int>(shelf::formatState(s, shelf::MAX_FOLDERS + 1, out, sizeof(out))), 0,
             "format refuses more folders than a State holds");
}

}  // namespace

int main() {
  aOneLineFileHasNoResumeTitle();
  aFileWithNoTrailingNewlineAtAllStillParses();
  theSecondLineIsTheOpenItem();
  aTitleMayContainSpaces();
  carriageReturnsAndPaddingAreTrimmed();
  aBlankSecondLineIsNoTitle();
  anOverlongTitleIsDroppedNotTruncated();
  aHalfWrittenFileLeavesTheCallerAlone();
  rowsAreClampedToTheRegistryAsItStandsNow();
  anImpossibleFolderMeansNoFolder();
  formattingWithNoOpenItemMatchesTheOldFormat();
  formattingAndParsingRoundTrip();
  formattingRefusesRatherThanTruncating();
  aFolderCountTheStateCannotHoldIsRefused();

  std::printf("shelfstate: %d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}

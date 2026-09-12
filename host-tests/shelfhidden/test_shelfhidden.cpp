// The shelf-hidden.cfg format, checked without a card.
//
// This file decides which games a person's device has. Everything it can get
// wrong is silent on the device: a title dropped is a game that comes back
// uninvited, a title kept that should not be is a game that vanished, and both
// look to the owner like the firmware losing their settings.
//
// The cases are written from the contract rather than from the parser: what a
// file written by a LATER firmware must do, what a half-written one must do,
// what a file full of junk must do. A test derived from the parser's own
// assumptions cannot falsify them.

#include <cstdio>
#include <string>

#include "ShelfHidden.h"

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

void checkEqual(const size_t got, const size_t want, const std::string& what) {
  check(got == want, what + " (got " + std::to_string(got) + ", want " + std::to_string(want) + ")");
}

shelf::HiddenSet parsed(const char* text) {
  shelf::HiddenSet set;
  shelf::parseHidden(text, set);
  return set;
}

// An empty or absent file is the ordinary state of every device, and it has to
// mean "everything shows". Nothing about it is an error.
void testNothingHiddenIsTheDefault() {
  check(!parsed("").contains("CHESS"), "empty file hides nothing");
  check(!parsed("\n").contains("CHESS"), "a blank line hides nothing");
  check(!parsed(nullptr).contains("CHESS"), "no file at all hides nothing");
  checkEqual(parsed("").size(), 0, "empty file is an empty set");
  check(shelf::formatHidden(shelf::HiddenSet{}).empty(), "an empty set writes an empty file, not a blank line");
}

void testOneTitlePerLine() {
  const shelf::HiddenSet set = parsed("MINESWEEPER\nXKCD\n");
  checkEqual(set.size(), 2, "two lines, two titles");
  check(set.contains("MINESWEEPER"), "first title held");
  check(set.contains("XKCD"), "second title held");
  check(!set.contains("CHESS"), "a title not in the file is not hidden");
  // Titles have spaces in them -- SEA SALT, CONNECT FOUR, HACKER NEWS -- so the
  // line is the unit and the space is not a separator.
  const shelf::HiddenSet spaced = parsed("SEA SALT\nCONNECT FOUR\n");
  check(spaced.contains("SEA SALT"), "a title with a space survives");
  check(spaced.contains("CONNECT FOUR"), "and so does the one after it");
}

void testTheRoundTrip() {
  const char* file = "MINESWEEPER\nSEA SALT\nXKCD\n";
  check(shelf::formatHidden(parsed(file)) == file, "what was read is what is written back");

  // The property that matters more than the literal bytes: parse, write, parse
  // again, and the same things are hidden.
  const shelf::HiddenSet once = parsed(file);
  const shelf::HiddenSet twice = parsed(shelf::formatHidden(once).c_str());
  checkEqual(twice.size(), once.size(), "a round trip holds the same count");
  for (size_t i = 0; i < once.size(); ++i) {
    check(twice.contains(once.at(i).c_str()), "a round trip holds " + once.at(i));
  }
}

// A file written on another machine, or by a hand, or by a firmware that ended
// its last line differently. None of it is worth a warning nobody can act on.
void testTheUntidyFile() {
  const shelf::HiddenSet set = parsed("  MINESWEEPER  \r\n\n\tXKCD\nCHESS");
  checkEqual(set.size(), 3, "whitespace, a blank line, a carriage return and no final newline");
  check(set.contains("MINESWEEPER"), "surrounding spaces trimmed");
  check(set.contains("XKCD"), "a leading tab trimmed");
  check(set.contains("CHESS"), "a last line with no newline is still a line");
  check(!set.contains("  MINESWEEPER  "), "and the untrimmed form is not what is held");

  const shelf::HiddenSet twice = parsed("XKCD\nXKCD\nXKCD\n");
  checkEqual(twice.size(), 1, "a duplicate is one hidden game, not three");
}

// A line longer than any item's title can match no item, so keeping it costs
// memory and buys nothing. Truncating it is the thing that must not happen: a
// truncated title is a DIFFERENT title, and the one it becomes might exist.
void testALineTooLongIsDroppedNotCut() {
  std::string line(shelf::MAX_ITEM_TITLE + 1, 'X');
  const shelf::HiddenSet set = parsed((line + "\nCHESS\n").c_str());
  checkEqual(set.size(), 1, "the over-long line is gone and the good one is not");
  check(set.contains("CHESS"), "the line after it still parsed");
  check(!set.contains(line.substr(0, shelf::MAX_ITEM_TITLE).c_str()), "nothing was truncated into existence");

  std::string exact(shelf::MAX_ITEM_TITLE, 'X');
  check(parsed((exact + "\n").c_str()).contains(exact.c_str()), "a title of exactly the maximum is kept");
}

// A card is a card: it can come back holding anything. What must never happen
// is a corrupt file hiding a game, because the person would have no idea why it
// went, and the fix would be a file they cannot see.
void testJunkHidesNothingReal() {
  const shelf::HiddenSet set = parsed("\x01\x02\x03\n!!!!\n@@@ @@@\n");
  check(!set.contains("CHESS"), "junk does not hide CHESS");
  check(!set.contains("XKCD"), "junk does not hide XKCD");
  check(!set.contains("WIKIPEDIA"), "junk does not hide WIKIPEDIA");

  std::string many;
  for (size_t i = 0; i < shelf::MAX_HIDDEN + 50; ++i) many += "JUNK" + std::to_string(i) + "\n";
  const shelf::HiddenSet capped = parsed(many.c_str());
  check(capped.size() <= shelf::MAX_HIDDEN, "a file of ten thousand lines is bounded");
}

// A title the running firmware knows nothing about is kept, not dropped. That
// is the whole downgrade story: a build without WIKIPEDIA must not be the
// reason the build after it forgets that WIKIPEDIA was hidden.
void testAnUnknownTitleSurvives() {
  const char* file = "SOMETHING FROM 2027\nCHESS\n";
  const shelf::HiddenSet set = parsed(file);
  checkEqual(set.size(), 2, "an unknown title is held like any other");
  check(shelf::formatHidden(set) == file, "and written back out unchanged");
}

// set() reports whether anything CHANGED, and that answer is what keeps a tap
// that hides an already-hidden game from writing to a card with a finite erase
// count.
void testSetSaysWhetherItChangedAnything() {
  shelf::HiddenSet set;
  check(set.set("CHESS", true), "hiding a shown game is a change");
  check(!set.set("CHESS", true), "hiding it again is not");
  checkEqual(set.size(), 1, "and did not add it twice");
  check(set.set("CHESS", false), "showing a hidden game is a change");
  check(!set.set("CHESS", false), "showing a shown one is not");
  checkEqual(set.size(), 0, "and it is gone rather than blanked");

  check(!set.set(nullptr, true), "a null title changes nothing");
  check(!set.set("", true), "and neither does an empty one");
  checkEqual(set.size(), 0, "neither reached the set");
}

// Unhiding takes out one, and only one.
void testShowingOneLeavesTheRest() {
  shelf::HiddenSet set = parsed("CHESS\nXKCD\nSUDOKU\n");
  check(set.set("XKCD", false), "the middle one is removed");
  checkEqual(set.size(), 2, "two left");
  check(set.contains("CHESS"), "the one before it stayed");
  check(set.contains("SUDOKU"), "the one after it stayed");
  check(!set.contains("XKCD"), "and the one asked for went");
  check(shelf::formatHidden(set) == "CHESS\nSUDOKU\n", "the file closes over the gap");
}

// --- rows and items --------------------------------------------------------
//
// The conversion the whole feature turns on, and the reason it is in this file
// rather than beside the registry: the registry cannot be built without a
// device. A ROW is a position in the list a folder draws; an ITEM is a position
// in the registry. Crossing them is silent -- two small ints in the same range
// -- and the symptom is the shelf's oldest: the header says page 2 and the tap
// opens page 1's game.

constexpr int kGames = 6;
const char* const kTitles[kGames] = {"CHESS", "BATTLESHIP", "SOLITAIRE", "SUDOKU", "TRIVIA", "XKCD"};
auto titles = [](const int i) { return kTitles[i]; };

shelf::HiddenSet hiding(const std::initializer_list<const char*> titlesToHide) {
  shelf::HiddenSet set;
  for (const char* title : titlesToHide) set.set(title, true);
  return set;
}

// Nothing hidden is the state every device is in, and there the two units are
// the same number. A conversion that is wrong in general but right here would
// pass every test that only ever hides something.
void testWithNothingHiddenARowIsItsItem() {
  const shelf::HiddenSet none;
  checkEqual(static_cast<size_t>(shelf::shownCountIn(none, kGames, titles)), kGames, "every game is shown");
  for (int i = 0; i < kGames; ++i) {
    check(shelf::shownItemIn(none, kGames, i, titles) == i,
          "row " + std::to_string(i) + " is item " + std::to_string(i));
    check(shelf::shownRowForIn(none, kGames, i, titles) == i,
          "item " + std::to_string(i) + " is row " + std::to_string(i));
  }
  check(shelf::shownItemIn(none, kGames, kGames, titles) == -1, "one past the end has no item");
  check(shelf::shownItemIn(none, kGames, -1, titles) == -1, "a negative row has no item");
}

// Two hidden in the middle: every row after them names an item further along,
// and this is the mapping a tap goes through.
void testHidingShiftsTheRowsThatFollow() {
  const shelf::HiddenSet set = hiding({"BATTLESHIP", "SUDOKU"});
  checkEqual(static_cast<size_t>(shelf::shownCountIn(set, kGames, titles)), 4, "four left");
  check(shelf::shownItemIn(set, kGames, 0, titles) == 0, "row 0 is CHESS");
  check(shelf::shownItemIn(set, kGames, 1, titles) == 2, "row 1 skips BATTLESHIP to SOLITAIRE");
  check(shelf::shownItemIn(set, kGames, 2, titles) == 4, "row 2 skips SUDOKU to TRIVIA");
  check(shelf::shownItemIn(set, kGames, 3, titles) == 5, "row 3 is XKCD");
  check(shelf::shownItemIn(set, kGames, 4, titles) == -1, "and there is no row 4");

  // The round trip, which is the property a resume depends on: an item that is
  // shown comes back as the row that names it.
  for (int item = 0; item < kGames; ++item) {
    if (set.contains(kTitles[item])) continue;
    const int row = shelf::shownRowForIn(set, kGames, item, titles);
    check(shelf::shownItemIn(set, kGames, row, titles) == item,
          std::string(kTitles[item]) + " survives the round trip");
  }
}

// The case the code special-cases and a card will reach: the item the folder
// was standing on is the one that was just hidden.
void testAHiddenItemResumesOnTheNextSurvivor() {
  const shelf::HiddenSet set = hiding({"SOLITAIRE"});
  // SOLITAIRE is item 2; the shown list is CHESS BATTLESHIP SUDOKU TRIVIA XKCD.
  const int row = shelf::shownRowForIn(set, kGames, 2, titles);
  check(row == 2, "the hidden item's row is the next survivor's");
  check(shelf::shownItemIn(set, kGames, row, titles) == 3, "which is SUDOKU");

  // And when everything from there on is hidden, it answers PAST the end on
  // purpose: clamping is resumeRowFor's job, and a clamp here would be a second
  // rule that has to agree with it.
  const shelf::HiddenSet tail = hiding({"SOLITAIRE", "SUDOKU", "TRIVIA", "XKCD"});
  const int past = shelf::shownRowForIn(tail, kGames, 2, titles);
  check(past == shelf::shownCountIn(tail, kGames, titles), "past the end, not pinned");
  check(shelf::shownItemIn(tail, kGames, past, titles) == -1, "and that row has no item");
}

// Hiding everything is allowed. The folder draws its own empty state; what must
// not happen is a row that answers anyway.
void testAnEmptyFolderHasNoRows() {
  const shelf::HiddenSet all = hiding({"CHESS", "BATTLESHIP", "SOLITAIRE", "SUDOKU", "TRIVIA", "XKCD"});
  checkEqual(static_cast<size_t>(shelf::shownCountIn(all, kGames, titles)), 0, "nothing is shown");
  check(shelf::shownItemIn(all, kGames, 0, titles) == -1, "row 0 has no item");
  check(shelf::shownRowForIn(all, kGames, 3, titles) == 0, "and every item is before row 0");
}

}  // namespace

int main() {
  testNothingHiddenIsTheDefault();
  testOneTitlePerLine();
  testTheRoundTrip();
  testTheUntidyFile();
  testALineTooLongIsDroppedNotCut();
  testJunkHidesNothingReal();
  testAnUnknownTitleSurvives();
  testSetSaysWhetherItChangedAnything();
  testShowingOneLeavesTheRest();
  testWithNothingHiddenARowIsItsItem();
  testHidingShiftsTheRowsThatFollow();
  testAHiddenItemResumesOnTheNextSurvivor();
  testAnEmptyFolderHasNoRows();

  std::printf("%d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}

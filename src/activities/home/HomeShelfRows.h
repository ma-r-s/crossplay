#pragma once
// fork-local: where Home's shelf folders (GAMES, APPS) sit in its selector
// index, as pure functions so host-tests/homeshelf can walk every theme.
//
// Home's selector index counts the recent books FIRST, whether a theme draws
// them as cover tiles above the menu or, like RoundedRaff, as the one Continue
// Reading row at the top of it. Then upstream's rows (indexToMenuItem()), then
// the shelf's folders. So a book is subtracted exactly once, in folderAt(),
// and nothing else may count it again: upstreamMenuRows() once added the
// Continue Reading row back on top, which under RoundedRaff with a book on
// the card made GAMES do nothing and APPS open GAMES (report box #647).

namespace homeshelf {

// The rows upstream's indexToMenuItem() walks: Browse Files, Library, File
// Transfer, Settings, plus the library slot (Plugins or OPDS) when there is
// one. Never the recent books, in any theme.
constexpr int upstreamRows(const bool librarySlot) { return 4 + (librarySlot ? 1 : 0); }

// The shelf folder a selector index opens, or -1 when it is a book or one of
// upstream's rows (or past the last folder).
constexpr int folderAt(const int selectorIndex, const int recentBooks, const bool librarySlot, const int folders) {
  const int folder = selectorIndex - recentBooks - upstreamRows(librarySlot);
  return folder >= 0 && folder < folders ? folder : -1;
}

// The selector index of a folder, for putting the cursor back on it.
constexpr int selectorForFolder(const int folder, const int recentBooks, const bool librarySlot) {
  return recentBooks + upstreamRows(librarySlot) + folder;
}

// The selector index of the menu row drawn `row`-th from the top, counting
// from zero. A theme that draws Continue Reading in the menu has the book as
// its row 0; one that draws covers has the books above the menu, so its rows
// start after them.
constexpr int selectorForMenuRow(const int row, const int recentBooks, const bool continueReadingInMenu) {
  return continueReadingInMenu ? row : row + recentBooks;
}

}  // namespace homeshelf

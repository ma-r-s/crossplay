// Home's shelf rows open what they say, in every theme: see run.sh and
// src/activities/home/HomeShelfRows.h.
//
// The menu is modelled the way HomeActivity draws it, and indexed the way its
// touch and button paths index it: a theme with Continue Reading in the menu
// draws the book as row 0, the others draw books as covers above the menu.
// Then upstream's rows, then the shelf's folders.
#include <cstdio>
#include <initializer_list>

#include "HomeShelfRows.h"
#include "theme_rows.h"

static int checks = 0;
static int failures = 0;

static void check(const bool ok, const char* what, const ThemeRows& t, const int books, const bool library,
                  const int row) {
  ++checks;
  if (ok) return;
  ++failures;
  std::printf("FAIL homeshelf  %s, %d book(s), library slot %d, drawn row %d: %s\n", t.name, books, library ? 1 : 0,
              row, what);
}

// The shelf's folders (GAMES, APPS); the arithmetic does not care how many.
constexpr int kFolders = 2;

int main() {
  bool sawContinueInMenu = false;
  for (const ThemeRows& t : kThemes) {
    sawContinueInMenu = sawContinueInMenu || t.continueInMenu;
    for (int books = 0; books <= t.books; ++books) {
      for (const bool library : {false, true}) {
        const int continueRows = t.continueInMenu && books > 0 ? 1 : 0;
        const int upstream = homeshelf::upstreamRows(library);
        const int drawn = continueRows + upstream + kFolders;
        for (int row = 0; row < drawn; ++row) {
          const int selector = homeshelf::selectorForMenuRow(row, books, t.continueInMenu);
          const int folder = homeshelf::folderAt(selector, books, library, kFolders);
          const int expected = row - continueRows - upstream;  // negative: not a folder
          if (row < continueRows) {
            check(selector < books, "the Continue Reading row is not the book", t, books, library, row);
          }
          check(folder == (expected >= 0 ? expected : -1), "the row opens the wrong thing", t, books, library, row);
          if (expected >= 0) {
            // Leaving a folder puts the cursor back on the row that opened it.
            check(homeshelf::selectorForFolder(expected, books, library) == selector,
                  "leaving the folder puts the cursor on another row", t, books, library, row);
          }
        }
      }
    }
  }
  // The case the report was about has to be in the table, or a sync that
  // renames the flag would leave this suite passing on nothing.
  ++checks;
  if (!sawContinueInMenu) {
    ++failures;
    std::printf("FAIL homeshelf  no theme draws Continue Reading in the menu; the RoundedRaff case is not covered\n");
  }
  std::printf("%s %d checks, %d failed\n", failures == 0 ? "PASS" : "FAIL", checks, failures);
  return failures == 0 ? 0 : 1;
}

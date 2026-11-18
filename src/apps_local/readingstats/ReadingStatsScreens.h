#pragma once

// The Reading Stats screens. Freestanding builders in the ChessScreens mould: a
// model in, a drawn frame out, no renderer and no Activity, so host-tests/ui/
// can assert what they drew and what they made tappable.
//
// Two screens:
//
//   main    this week's reading time, the streak, two weeks of minutes a day,
//           and the books finished this year with the day each was finished
//   books   every book finished this year, a page at a time, when the main
//           screen has more than its rows can hold
//
// Each is drawn in TWO PASSES over one frame, because it speaks two faces and a
// target has three slots. buildMain()/buildBooks() draw everything that is the
// app's own -- chrome, numbers, the chart, the dates -- in Jersey, and hand
// back the boxes the titles go in; the activity then binds the system face's
// three cuts and calls drawTitles(). One geometry, two passes, so a title can
// never land anywhere but the row whose date it sits beside.

#include <cstdint>

#include "../ui/ToyboxScreen.h"

namespace rstatsui {

namespace fui = freeink::ui;

enum : fui::ActionId {
  ActionAllBooks = 420,  // the door at the foot of the main screen's list
};

constexpr int kChartCells = 14;

struct Day {
  uint32_t seconds = 0;
  char letter = ' ';  // S M T W T F S
  bool today = false;
  bool thisWeek = false;  // Sunday through today
};

struct BookRow {
  const char* title = nullptr;
  const char* date = nullptr;  // "12 MAR"
};

// The chart's first cell is this many days before today; its last is today.
constexpr int kFirstChartDay = -(kChartCells - 1);

struct MainModel {
  // The clock is not trusted: nothing can be dated, so nothing is shown but how
  // to set it.
  bool clockSet = true;
  // Any session at all in the window. False on a first run, where a row of
  // zeroes would read as a broken counter rather than as a fresh start.
  bool anySession = false;
  int year = 0;
  uint32_t weekSeconds = 0;
  int streak = 0;
  bool streakAtCap = false;
  Day chart[kChartCells];  // oldest first, from firstChartDay()
  // This year's finished books, newest first. The screen shows as many as its
  // rows hold and turns the last row into a door to the rest.
  const BookRow* books = nullptr;
  int bookCount = 0;
};

// Where a title goes, filled by the first pass and drawn by the second.
constexpr int kMaxTitleSlots = 12;
struct TitleSlot {
  fui::Rect box{};
  const char* title = nullptr;
};
struct Titles {
  TitleSlot slots[kMaxTitleSlots];
  int count = 0;
};

void buildMain(toybox::Screen& screen, const MainModel& model, Titles& titles);

struct BooksModel {
  int year = 0;
  const BookRow* books = nullptr;
  int count = 0;
  int first = 0;                    // the page's first row, an index into books
  const char* pageLabel = nullptr;  // "2/3" in the band, or none on one page
};

void buildBooks(toybox::Screen& screen, const BooksModel& model, Titles& titles);
// How many books one page of the books screen holds.
int booksPerPage(const fui::DeviceContext& device);

// The second pass: every title in its box, in whatever three cuts are bound.
void drawTitles(fui::DrawTarget& target, const Titles& titles);

// The box one book row's title gets, for a test asking what fits in it.
int16_t titleWidth(const fui::DeviceContext& device);
int16_t titleHeight();

}  // namespace rstatsui

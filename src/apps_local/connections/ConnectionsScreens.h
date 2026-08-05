#pragma once

// The Connections screens, as freestanding builders over a model.
//
// The tile grid is app surface, not chrome, so it is drawn here directly rather
// than composed from components: sixteen tiles would be sixteen list rows that
// do not behave like list rows. Everything around it (header, status, buttons)
// goes through Screen and inherits the theme.

#include "../ui/ToyboxScreen.h"
#include "ConnectionsCore.h"

namespace connectionsui {

namespace fui = freeink::ui;

enum : fui::ActionId {
  ActionTile = 1,  // value = tile index
  ActionSubmit = 2,
  ActionShuffle = 3,
  ActionDeselect = 4,
  ActionOpenMenu = 5,
  ActionArchiveRow = 6,  // value = row within the visible page
  ActionGetPuzzles = 7,
  ActionNewest = 8,
  ActionCalendarDay = 9,
  ActionCalendarToday = 10,
  ActionCalendarYear = 11,   // value = -1 or +1
  ActionCalendarMonth = 12,  // value = -1 or +1
};

// Grid geometry. Tighter margins than the rest of Toybox on purpose: a tile's
// content is a word that cannot be abbreviated, so every pixel of width buys
// characters. Measured against the whole published archive -- at this width
// 96% of 18255 words set on one line, and the 1.4% that do not are broken
// across lines rather than truncated.
constexpr int kGridMargin = 12;
constexpr int kTileGap = 4;
constexpr int kTilePad = 3;
constexpr int kTileWidth = (480 - 2 * kGridMargin - 3 * kTileGap) / 4;
// Square, exactly. A tile is the game's unit and reads as one; anything off by
// a few pixels looks like a mistake rather than a choice. Four of them plus
// their gaps fit the body with room to spare, so the squareness costs nothing.
constexpr int kTileMaxHeight = kTileWidth;

struct BoardModel {
  const connections::Game* game = nullptr;
  uint32_t date = 0;
  // Briefly shown after a wrong guess, then cleared. nullptr for none.
  const char* toast = nullptr;
};

// Where the four-row block landed, handed from the chrome pass to the tile pass.
struct BoardLayout {
  fui::Rect grid{};
  int rowHeight = 0;
};

// The board is drawn in two passes so the two can speak different faces. The
// chrome pass is the fork's voice (Jersey buttons, Jersey band); the tile pass
// is the game's. The activity rebinds one font slot between them, which is a
// single assignment -- see the note in ToyboxTheme.h.
BoardLayout buildBoardChrome(toybox::Screen& screen, const BoardModel& model);
void buildBoardTiles(toybox::Screen& screen, const BoardModel& model, const BoardLayout& layout);

// Rows of the archive list, filled by the activity from the pack.
struct ArchiveRow {
  char label[16] = {};
  bool played = false;
  bool lost = false;
  uint8_t mistakes = 0;
  bool hardestFirst = false;
};

struct ArchiveModel {
  const ArchiveRow* rows = nullptr;
  int count = 0;
  int selected = 0;
  int topIndex = 0;
  // Shown in the header so you can see how far the archive reaches without
  // scrolling to the end of it.
  int total = 0;
};

void buildArchive(toybox::Screen& screen, const ArchiveModel& model);

// The import screen: one line of state, because there is nothing to do while it
// runs and a progress bar on e-ink costs a refresh per update.
// The import screen. One design, two states, and the transition between them is
// the whole reward: the count is real (it is the number of puzzles parsed so
// far, climbing as they arrive), and when it stops the block inverts to solid
// black. No animation -- on a panel that refreshes once every couple of seconds
// an animation is a lie, but a state change that lands with weight is not.
struct ImportModel {
  const char* detail = "";
  // Puzzles parsed so far. Climbs during the download; this IS the progress.
  int puzzles = 0;
  // Date of the newest puzzle read so far, so you watch it sweep from 2023 to
  // now rather than watching an abstract bar.
  uint32_t reachedDate = 0;
  bool wifiDone = false;
  bool done = false;
  bool failed = false;
};

void buildImport(toybox::Screen& screen, const ImportModel& model);

// A month of the archive. Cells the archive does not cover (before the first
// puzzle, after today, or the blanks either side of the month) are simply
// absent rather than drawn empty.
struct CalendarDay {
  uint8_t day = 0;         // 1-31, or 0 for a blank cell
  bool inArchive = false;  // a puzzle exists for this date
  bool played = false;
  bool lost = false;
  bool finished = false;
  uint8_t mistakes = 0;
};

struct CalendarModel {
  int year = 2026;
  int month = 8;
  // 42 cells: six weeks of seven, Sunday first.
  const CalendarDay* cells = nullptr;
  int cursor = -1;  // index into cells, -1 when driving by touch
  int todayCell = -1;
  int playedThisMonth = 0;
  // Whether each arrow can move. A dead arrow is dimmed rather than hidden: on
  // a 1-bit screen an absent control is hard to tell from a drawing bug, while a
  // greyed one says which end of the archive you have reached.
  bool canPrevYear = true;
  bool canNextYear = true;
  bool canPrevMonth = true;
  bool canNextMonth = true;
};

void buildCalendar(toybox::Screen& screen, const CalendarModel& model);

// What one cell of the sixteen-day grid says.
enum class DayResult : uint8_t {
  Unplayed,
  Won,      // finished, with at least one mistake
  Perfect,  // finished clean
  Lost,
};

struct MenuModel {
  bool hasPuzzles = false;
  uint32_t newestDate = 0;
  int puzzleCount = 0;
  int selected = 0;
  int played = 0;
  int perfect = 0;
  int streak = 0;
  bool inProgress = false;
  bool todayDone = false;
  uint8_t todayMistakes = 0;
  // The last sixteen days, oldest first, so the final cell is always the newest
  // puzzle you hold. Sixteen because that is a Connections board: the menu's
  // one piece of decor is the shape of the game, drawn from your own record.
  DayResult recent[16] = {};
  // True when the newest puzzle on the card is today's. There is then nothing
  // to fetch, so the row says so and does nothing rather than offering a wifi
  // trip that would download 1.3MB to learn there is no news.
  bool upToDate = false;
};

void buildMenu(toybox::Screen& screen, const MenuModel& model);

// "20260802" -> "2 AUG 2026". Written into `out` (at least 16 bytes).
void formatDate(uint32_t date, char* out, int cap);

}  // namespace connectionsui

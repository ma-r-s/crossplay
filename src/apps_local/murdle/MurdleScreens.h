#pragma once

// The Murdle screens, as freestanding builders over plain models.
//
// Everything shaped like chrome goes through toybox::Screen and inherits the
// theme. What is hand-drawn is the game's own surface, and that is exactly two
// things: the deduction grid and the clue list. See ../ui/ToyboxScreen.h for
// why these take a Screen and touch no renderer.
//
// THE ONE THING TO KNOW ABOUT THE GRID. At four categories of four it is 12x12,
// which is 144 tappable cells against an interaction buffer that holds 24. So
// the grid does not register cells at all: it registers itself, and gives back
// the GridLayout it drew with so the activity can turn a tap into a cell using
// the same arithmetic that placed the pixels. That is the fork's hit-testing
// rule (never compute a tappable region a second time) applied to a surface too
// dense to express as components -- not an exception to it.

#include "../ui/ToyboxScreen.h"
#include "MurdleCore.h"

namespace murdleui {

namespace fui = freeink::ui;

enum : fui::ActionId {
  ActionPlay = 1,     // the headline: continue, or start the first case
  ActionNewCase = 2,  // every new case goes through here, never around it
  ActionSettings = 3,
  ActionHowTo = 4,
  ActionBack = 5,
  // value = the face wanted, as Face
  ActionFace = 6,
  // value = -1 or +1
  ActionPage = 7,
  ActionClue = 8,  // value = clue index; strikes it through as a reading aid
  ActionAccuse = 9,
  // value = cat * 8 + item
  ActionPick = 10,
  ActionConfirm = 11,
  ActionKeepLooking = 12,
  ActionDone = 13,
  // value = the tier, absolute
  ActionTier = 14,
  ActionConfirmNew = 15,
  ActionCancel = 16,
  ActionGrid = 17,  // the whole grid; the activity resolves the cell itself
};

// Which face of the case is showing. Not a view: Back leaves the case from
// either of them, and a toggle that sat in the back stack would sometimes mean
// "the other face" and sometimes mean "leave".
// Three faces, each one thing, and the door cycles them: CLUES -> GRID -> INFO.
//
// The cast list used to share the clue face's page stream, so a case ran to
// three or four pages and where the cast stopped and the clues started moved
// with the tier. Mario asked for the split, and for the cast to fit one page
// always -- which it now does, because a suspect is one line like every other
// fixture (see drawCastBlocks) and carries only the dossier columns the case
// actually uses (see suspectAttributes).
enum class Face : uint8_t { Clues, Grid, Info };
constexpr int kFaceCount = 3;

// ---------------------------------------------------------------------------
// The grid

// Everything needed to draw the grid and to read a tap back out of it. Built by
// layoutGrid(), consumed by drawGrid() and by the activity's tap handler, so
// there is one piece of arithmetic and not two that have to agree.
struct GridLayout {
  bool valid = false;
  int groups = 0;  // cats - 1
  int items = 0;
  int16_t cell = 0;
  int16_t originX = 0;  // left edge of the first data column
  int16_t originY = 0;  // top edge of the first data row
  int16_t gutter = 0;   // width of the row-label column to the left of originX
  int16_t headerH = 0;  // height of the column-label band above originY

  // Column groups left to right and row groups top to bottom. The classic
  // staircase: suspects down the left, everything else once across the top and
  // once down the side, so every pair of categories meets in exactly one block.
  uint8_t colCat[murdle::kMaxCats] = {};
  uint8_t rowCat[murdle::kMaxCats] = {};

  // A block exists at (row group r, column group c) only when r + c < groups.
  bool blockLive(const int r, const int c) const { return r + c < groups; }

  // Where a cell sits. Column index is groupC * items + i.
  int16_t cellX(const int col) const { return static_cast<int16_t>(originX + col * cell); }
  int16_t cellY(const int row) const { return static_cast<int16_t>(originY + row * cell); }
};

// A point inside the grid, turned back into the cell it landed on. Lives here,
// beside layoutGrid, because the inverse of a piece of geometry belongs in the
// same file as the geometry -- a second copy in the activity is exactly the
// "computed a tappable region twice" mistake that has cost this fork more bugs
// than anything else. It is also the only way to test it: an activity needs a
// renderer and this does not.
struct GridCell {
  int catA = 0;
  int itemA = 0;
  int catB = 0;
  int itemB = 0;
};

bool cellAt(const GridLayout& layout, int x, int y, GridCell& out);

struct CaseModel {
  const murdle::Puzzle* puzzle = nullptr;
  const murdle::Marks* marks = nullptr;
  Face face = Face::Clues;
  int page = 0;
  uint32_t struck = 0;  // one bit per clue, a reading aid only
  int caseNumber = 1;
  bool solved = false;
  // One line above the grid, or null. The only thing that puts one there is a
  // tap this board refused, and it has to say so: on a panel that takes a
  // second to answer, a tap that changes nothing and says nothing is
  // indistinguishable from a tap that was missed.
  const char* notice = nullptr;
};

// What the case face turned out to be. The page count is in here rather than in
// the model because only the builder can know it: where the clue list breaks
// depends on measured text, and a count computed anywhere else would eventually
// disagree with the split and drop a clue off the end of the last page.
struct CaseReport {
  GridLayout grid;
  int pages = 1;
  // The page actually drawn, clamped. On the grid face this is the page the
  // model came in with, untouched, so the caller can assign it back blindly
  // without losing the reader's place in the clue list.
  int page = 0;
};

// Draws the case and returns what it drew with. `grid.valid` is false on the
// clue face.
CaseReport buildCase(toybox::Screen& screen, const CaseModel& model);

// Where each clue line was drawn on the current page, so a tap can find it.
// Same rule as the grid: the list is too long for the interaction buffer, so it
// hands back its geometry instead of registering a rect per row.
struct ClueLayout {
  int16_t top[murdle::kMaxClues + 1] = {};
  uint8_t index[murdle::kMaxClues] = {};
  int count = 0;
};

// Filled by buildCase on the clue face. Separate call so the model stays a
// plain struct: the activity passes the same model to both.
ClueLayout lastClueLayout();

// ---------------------------------------------------------------------------
// The other screens

struct MenuModel {
  bool hasCase = false;
  bool caseSolved = false;
  murdle::Tier tier = murdle::Tier::Elementary;
  int caseNumber = 0;
  int solvedCount = 0;
  int wrongCount = 0;
  // The open case itself. The front door's job is to get you back into it, so
  // every design below is built out of it rather than out of counters about it.
  const murdle::Puzzle* puzzle = nullptr;
  const murdle::Marks* marks = nullptr;
  int cluesTicked = 0;
  // The last sixteen verdicts, two bits each. Kept for whichever front door
  // wants it; see the note in MurdleScreens.cpp about why it was the wrong
  // ornament for this game.
  uint32_t record = 0;
};

void buildMenu(toybox::Screen& screen, const MenuModel& model);

struct SettingsModel {
  murdle::Tier tier = murdle::Tier::Elementary;
  bool caseOpen = false;
};

void buildSettings(toybox::Screen& screen, const SettingsModel& model);

struct AccuseModel {
  const murdle::Puzzle* puzzle = nullptr;
  // picks[cat] is the chosen item, or kNothingPicked
  static constexpr uint8_t kNothingPicked = 0xFF;
  uint8_t picks[murdle::kMaxCats] = {kNothingPicked, kNothingPicked, kNothingPicked, kNothingPicked};
  bool complete() const;
};

void buildAccuse(toybox::Screen& screen, const AccuseModel& model);

struct VerdictModel {
  const murdle::Puzzle* puzzle = nullptr;
  bool right = false;
  int wrongAccusations = 0;
  uint8_t picks[murdle::kMaxCats] = {};
};

void buildVerdict(toybox::Screen& screen, const VerdictModel& model);

// The confirm sheet the new-case funnel puts up when a case is already open.
void buildConfirmNew(toybox::Screen& screen);

struct HowToModel {
  int page = 0;
};

int howToPages();
void buildHowTo(toybox::Screen& screen, const HowToModel& model);

const char* tierName(murdle::Tier tier);
const char* tierShape(murdle::Tier tier);

}  // namespace murdleui

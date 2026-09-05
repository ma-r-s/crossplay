#include "PicrossScreens.h"

#include <cstdio>

#include "../ui/ToyboxFormat.h"
#include "../ui/ToyboxText.h"

namespace picrossui {

namespace {

// snprintf buffer sizes derived from their formats, not guessed: snprintf cuts
// rather than overruns, so a byte-short buffer is a silently shortened label.
// See ToyboxFormat.h.
constexpr int kMistakeChars = toybox::kIntChars + toybox::literalChars("MISTAKES  ") + 1;
constexpr int kSizeChars = 2 * toybox::kIntChars + toybox::literalChars(" x ") + 1;
constexpr int kGradeChars =
    toybox::kIntChars + toybox::literalChars("SOLVED WITH ") + toybox::literalChars(" MISTAKES") + 1;

// The strip under the header carrying the mistake count and the board size.
// The puzzle NAME is deliberately absent: the name is the answer, so showing it
// while you solve would spoil the picture. It is revealed on the win screen.
constexpr int kStatusStrip = 34;

// A cell never grows past this, so a 5x5 stays a board rather than becoming five
// enormous squares floating in the panel.
constexpr int kMaxCell = 68;

// Air between the clue gutter and the grid. The board frame is stroked OUTSIDE
// the play area (kBoardFrame wide), so without this the last clue in a row ran
// under the frame and the bottom column clue sat on row 0 -- clues bleeding into
// the playfield, which is exactly what Mario rejected. Reserve the frame plus a
// few pixels of visible separation, the way the dungeon board does.
constexpr int kClueGap = toybox::kBoardFrame + 6;

// Clue numbers are at most two digits ("10"), so the gutter slots are sized for
// that plus a little air. Two ladders: the 5x5 boards have room for the UI cut,
// the 10x10 boards use the tile cut so four numbers still fit the gutter.
struct ClueMetrics {
  fui::FontId font;
  toybox::CutMetrics cut;
  int16_t slotH;  // vertical pitch of a stacked column-clue number
  int16_t numW;   // horizontal width of a row-clue number cell
};

ClueMetrics clueMetricsFor(const int n) {
  if (n <= 5) return {toybox::kUiFont, toybox::kUiCut, 30, 28};
  return {toybox::kTileFont, toybox::kTileCut, 16, 18};
}

int maxRunsInRows(const picross::Board& board) {
  int most = 1;
  uint8_t buf[picross::kMaxSize];
  for (int r = 0; r < board.size(); ++r) {
    const int k = board.rowClues(r, buf);
    if (k > most) most = k;
  }
  return most;
}

int maxRunsInCols(const picross::Board& board) {
  int most = 1;
  uint8_t buf[picross::kMaxSize];
  for (int c = 0; c < board.size(); ++c) {
    const int k = board.colClues(c, buf);
    if (k > most) most = k;
  }
  return most;
}

void chrome(toybox::Screen& screen, const char* title, const char* rightLabel) {
  fui::HeaderProps header;
  header.title = title;
  header.rightLabel = rightLabel;
  // rightLabel is drawn with subtitleText, and the theme default subtitle is
  // black -- invisible on the solid black band. Same trap the title falls into
  // one prop over. See DungeonScreens::chrome.
  header.subtitleText = fui::TextStyle{};
  header.subtitleText.font = toybox::kUiFont;
  header.subtitleText.color = fui::Color::White;
  header.subtitleText.align = fui::TextAlign::Right;
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  toybox::headerRule(screen);
}

fui::Rect cellRect(const Layout& layout, const int row, const int col) {
  return fui::makeRect(static_cast<int16_t>(layout.board.x + col * layout.cell),
                       static_cast<int16_t>(layout.board.y + row * layout.cell), layout.cell, layout.cell);
}

// An X drawn as two diagonals. `pad` keeps it clear of the cell edge so it reads
// as a mark rather than as a filled corner; `weight` is thick enough that e-ink
// does not swallow it.
void drawX(toybox::Screen& screen, const fui::Rect& cell, const fui::Color color) {
  const int16_t pad = static_cast<int16_t>(cell.width / 4);
  const uint8_t weight = static_cast<uint8_t>(cell.width >= 44 ? 3 : 2);
  const fui::Paint paint = fui::Paint::solid(color);
  screen.target().line(fui::Point{static_cast<int16_t>(cell.x + pad), static_cast<int16_t>(cell.y + pad)},
                       fui::Point{static_cast<int16_t>(cell.right() - pad), static_cast<int16_t>(cell.bottom() - pad)},
                       weight, paint);
  screen.target().line(fui::Point{static_cast<int16_t>(cell.right() - pad), static_cast<int16_t>(cell.y + pad)},
                       fui::Point{static_cast<int16_t>(cell.x + pad), static_cast<int16_t>(cell.bottom() - pad)},
                       weight, paint);
}

// A CROSSED annotation is a thin X on white paper; a locked MISTAKE is a solid
// black cell with a WHITE X knocked out of it. The mistake always carries the
// weight of a committed cell and the mark cancelling it, so it can never be
// mistaken for the free note the player made -- the point the critic caught in
// the first spec, and the difference that was clearest of the three rendered
// variants (the dither-only mistake read as an unfilled shaded cell).
void drawCell(toybox::Screen& screen, const fui::Rect& cell, const picross::Cell state) {
  switch (state) {
    case picross::Cell::Filled:
      screen.target().fill(cell, fui::Paint::solid(fui::Color::Black));
      break;
    case picross::Cell::Crossed:
      drawX(screen, cell, fui::Color::Black);
      break;
    case picross::Cell::Mistake:
      screen.target().fill(cell, fui::Paint::solid(fui::Color::Black));
      drawX(screen, cell, fui::Color::White);
      break;
    case picross::Cell::Blank:
    default:
      break;
  }
}

// A symmetric "done" chip: a light dot screen centred on (cx, cy) and built
// from chip-local coordinates, so it is identical behind every satisfied clue
// and mirrors left-right and top-bottom about its own centre. Paint::dither
// cannot do this -- its 2x2 pattern is keyed to ABSOLUTE screen coordinates, so
// it lands on a different phase behind each clue and reads as lopsided, the
// defect Mario caught. 1px dots on a 2px lattice, anchored at the centre, are a
// ~25% grey that is mirror-symmetric by construction and phase-stable clue to
// clue.
void drawDoneChip(toybox::Screen& screen, const int16_t cx, const int16_t cy, const int16_t halfW,
                  const int16_t halfH) {
  const fui::Paint ink = fui::Paint::solid(fui::Color::Black);
  for (int16_t dy = 0; dy <= halfH; dy = static_cast<int16_t>(dy + 2)) {
    for (int16_t dx = 0; dx <= halfW; dx = static_cast<int16_t>(dx + 2)) {
      screen.target().fill(fui::makeRect(static_cast<int16_t>(cx + dx), static_cast<int16_t>(cy + dy), 1, 1), ink);
      if (dx) screen.target().fill(fui::makeRect(static_cast<int16_t>(cx - dx), static_cast<int16_t>(cy + dy), 1, 1), ink);
      if (dy) screen.target().fill(fui::makeRect(static_cast<int16_t>(cx + dx), static_cast<int16_t>(cy - dy), 1, 1), ink);
      if (dx && dy)
        screen.target().fill(fui::makeRect(static_cast<int16_t>(cx - dx), static_cast<int16_t>(cy - dy), 1, 1), ink);
    }
  }
}

// A clue number, centred in `box`. When its line is satisfied the number is
// struck through -- honest here only because a wrong fill never becomes a Filled
// cell, so a satisfied count is a solved line and not a lucky miscount (see
// PicrossCore::rowSatisfied; the note there is load-bearing for the free-erase
// switch).
void drawClueNumber(toybox::Screen& screen, const fui::Rect& box, const int value, const ClueMetrics& cm,
                    const bool satisfied) {
  char text[toybox::kIntTextChars];
  std::snprintf(text, sizeof(text), "%d", value);
  fui::TextStyle style;
  style.font = cm.font;
  style.align = fui::TextAlign::Center;

  // inkCentred lands the digit's ink band centred in the box on both axes, so
  // the box centre IS the digit's centre.
  const int16_t cx = static_cast<int16_t>(box.x + box.width / 2);
  const int16_t cy = static_cast<int16_t>(box.y + box.height / 2);

  // A satisfied line's clue is struck through (Mario's pick). A struck lone "1"
  // reads as a "+" only when a centred rule is CONTAINED within the single
  // vertical stroke, the way a plus's crossbar is. Two things stop that and the
  // strike still reads as a cancel: it is centred on the digit (not dropped
  // below it), and it OVERSHOOTS the glyph on both sides -- a plus does not.
  // Chip first, then the digit over it, then the rule over both.
  if (satisfied) {
    const int16_t glyphW = screen.target().measureText(cm.font, text, style).width;
    int16_t chipHalfW = static_cast<int16_t>(glyphW / 2 + 4);
    int16_t chipHalfH = static_cast<int16_t>(cm.cut.inkHeight / 2 + 2);
    // Never spill into the neighbouring stacked clue or the playfield.
    if (chipHalfW > box.width / 2 - 1) chipHalfW = static_cast<int16_t>(box.width / 2 - 1);
    if (chipHalfH > box.height / 2 - 1) chipHalfH = static_cast<int16_t>(box.height / 2 - 1);
    if (chipHalfW > 0 && chipHalfH > 0) drawDoneChip(screen, cx, cy, chipHalfW, chipHalfH);
  }

  screen.target().text(toybox::inkCentred(box, cm.cut), text, style);

  if (satisfied) {
    const int16_t glyphW = screen.target().measureText(cm.font, text, style).width;
    const int16_t overshoot = 3;
    const int16_t half = static_cast<int16_t>(glyphW / 2 + overshoot);
    const int16_t y = static_cast<int16_t>(cy - toybox::kHairline / 2);
    screen.target().fill(
        fui::makeRect(static_cast<int16_t>(cx - half), y, static_cast<int16_t>(half * 2), toybox::kHairline),
        fui::Paint::solid(fui::Color::Black));
  }
}

void drawColClues(toybox::Screen& screen, const picross::Board& board, const Layout& layout, const ClueMetrics& cm) {
  uint8_t buf[picross::kMaxSize];
  for (int c = 0; c < board.size(); ++c) {
    const int k = board.colClues(c, buf);
    const bool satisfied = board.colSatisfied(c);
    const int16_t x = static_cast<int16_t>(layout.board.x + c * layout.cell);
    if (k == 0) {  // an all-empty column shows a single 0 nearest the board
      const fui::Rect box =
          fui::makeRect(x, static_cast<int16_t>(layout.board.y - kClueGap - cm.slotH), layout.cell, cm.slotH);
      drawClueNumber(screen, box, 0, cm, satisfied);
      continue;
    }
    for (int i = 0; i < k; ++i) {
      const int16_t top = static_cast<int16_t>(layout.board.y - kClueGap - (k - i) * cm.slotH);
      const fui::Rect box = fui::makeRect(x, top, layout.cell, cm.slotH);
      drawClueNumber(screen, box, buf[i], cm, satisfied);
    }
  }
}

void drawRowClues(toybox::Screen& screen, const picross::Board& board, const Layout& layout, const ClueMetrics& cm) {
  uint8_t buf[picross::kMaxSize];
  for (int r = 0; r < board.size(); ++r) {
    const int k = board.rowClues(r, buf);
    const bool satisfied = board.rowSatisfied(r);
    const int16_t y = static_cast<int16_t>(layout.board.y + r * layout.cell);
    if (k == 0) {
      const fui::Rect box =
          fui::makeRect(static_cast<int16_t>(layout.board.x - kClueGap - cm.numW), y, cm.numW, layout.cell);
      drawClueNumber(screen, box, 0, cm, satisfied);
      continue;
    }
    for (int i = 0; i < k; ++i) {
      const int16_t left = static_cast<int16_t>(layout.board.x - kClueGap - (k - i) * cm.numW);
      const fui::Rect box = fui::makeRect(left, y, cm.numW, layout.cell);
      drawClueNumber(screen, box, buf[i], cm, satisfied);
    }
  }
}

void drawGrid(toybox::Screen& screen, const picross::Board& board, const Layout& layout) {
  const int n = board.size();
  // White paper under a frame heavier than any line drawn inside it, so the
  // surface reads as one object. kBoardFrame is the weight Toybox reserves.
  screen.target().fill(layout.board, fui::Paint::solid(fui::Color::White));
  screen.target().stroke(layout.board.inset(fui::Insets{-toybox::kBoardFrame, -toybox::kBoardFrame,
                                                        -toybox::kBoardFrame, -toybox::kBoardFrame}),
                         fui::Paint::solid(fui::Color::Black), toybox::kBoardFrame);
  for (int i = 1; i < n; ++i) {
    const int16_t x = static_cast<int16_t>(layout.board.x + i * layout.cell);
    const int16_t y = static_cast<int16_t>(layout.board.y + i * layout.cell);
    screen.target().fill(fui::makeRect(x, layout.board.y, toybox::kHairline, layout.board.height),
                         fui::Paint::solid(fui::Color::Black));
    screen.target().fill(fui::makeRect(layout.board.x, y, layout.board.width, toybox::kHairline),
                         fui::Paint::solid(fui::Color::Black));
  }
  // Major gridlines every five cells, the standard picross counting aid and the
  // one element the winning render borrowed from the others. Only visible on the
  // 10x10 boards; on a 5x5 the fives fall on the frame.
  for (int i = 5; i < n; i += 5) {
    const int16_t x = static_cast<int16_t>(layout.board.x + i * layout.cell - 1);
    const int16_t y = static_cast<int16_t>(layout.board.y + i * layout.cell - 1);
    screen.target().fill(fui::makeRect(x, layout.board.y, toybox::kRule, layout.board.height),
                         fui::Paint::solid(fui::Color::Black));
    screen.target().fill(fui::makeRect(layout.board.x, y, layout.board.width, toybox::kRule),
                         fui::Paint::solid(fui::Color::Black));
  }
  for (int r = 0; r < n; ++r)
    for (int c = 0; c < n; ++c) drawCell(screen, cellRect(layout, r, c), board.cell(r, c));
}

// The mode toggle. FILL and MARK, the active one inverted. Two hit targets
// rather than one, so tapping the mode you already hold is a no-op instead of
// silently flipping you to the other -- and a stray tap on the tool you are
// using cannot cause the wrong thing. The two physical side keys select the
// same two modes, so this is a status readout as much as a control.
void drawModeToggle(toybox::Screen& screen, const fui::Rect& row, const int mode) {
  const char* labels[2] = {"FILL", "MARK"};
  const int values[2] = {ModeFill, ModeMark};
  // One segmented capsule split down the middle, the active half inverted.
  screen.target().fill(row, fui::Paint::solid(fui::Color::White), static_cast<uint8_t>(toybox::kPillRadius));
  screen.target().stroke(row, fui::Paint::solid(fui::Color::Black), toybox::kHairline,
                         static_cast<uint8_t>(toybox::kPillRadius));
  const int16_t half = static_cast<int16_t>(row.width / 2);
  for (int i = 0; i < 2; ++i) {
    const fui::Rect box = fui::makeRect(static_cast<int16_t>(row.x + i * half), row.y, half, row.height);
    const bool on = mode == values[i];
    if (on)
      screen.target().fill(box.inset(fui::Insets{2, 2, 2, 2}), fui::Paint::solid(fui::Color::Black),
                           static_cast<uint8_t>(toybox::kPillRadius - 2));
    fui::TextStyle style;
    style.font = toybox::kUiFont;
    style.align = fui::TextAlign::Center;
    style.color = on ? fui::Color::White : fui::Color::Black;
    screen.target().text(toybox::inkCentred(box, toybox::kUiCut), labels[i], style);
    screen.frame().hit(box, ActionMode, static_cast<int16_t>(values[i]));
  }
}

// A plain action button. RESTART and PUZZLES sit a row above the mode toggle,
// away from the thumb, because RESTART discards the attempt.
void drawActionButton(toybox::Screen& screen, const fui::Rect& box, const char* label, const int value) {
  fui::ButtonProps props;
  props.label = label;
  props.action = ActionButton;
  props.value = static_cast<int16_t>(value);
  props.text = toybox::buttonText(screen.theme());
  props.radius = toybox::kPillRadius;
  props.styles = toybox::rowStyles();
  screen.button(props, box);
}

// The finished picture, drawn clean: black squares only, no lattice and no clue
// gutters. Used by both the win reveal (large) and a solved picker tile (tiny).
void drawPicture(toybox::Screen& screen, const picross::Puzzle& puzzle, const fui::Rect& area) {
  const int n = puzzle.size;
  const int16_t cell = static_cast<int16_t>((area.width < area.height ? area.width : area.height) / n);
  if (cell <= 0) return;
  const int16_t extent = static_cast<int16_t>(cell * n);
  const int16_t ox = static_cast<int16_t>(area.x + (area.width - extent) / 2);
  const int16_t oy = static_cast<int16_t>(area.y + (area.height - extent) / 2);
  for (int r = 0; r < n; ++r) {
    for (int c = 0; c < n; ++c) {
      if ((puzzle.rows[r] & (uint16_t{1} << c)) == 0) continue;
      screen.target().fill(
          fui::makeRect(static_cast<int16_t>(ox + c * cell), static_cast<int16_t>(oy + r * cell), cell, cell),
          fui::Paint::solid(fui::Color::Black));
    }
  }
}

}  // namespace

bool Layout::cellAt(const int x, const int y, int& row, int& col) const {
  if (cell <= 0) return false;
  if (x < board.x || x >= board.right() || y < board.y || y >= board.bottom()) return false;
  col = (x - board.x) / cell;
  row = (y - board.y) / cell;
  return row >= 0 && row < size && col >= 0 && col < size;
}

int PickerLayout::indexAt(const int x, const int y) const {
  if (cell <= 0) return -1;
  const int pitch = cell + gap;
  if (x < grid.x || y < grid.y) return -1;
  const int col = (x - grid.x) / pitch;
  const int row = (y - grid.y) / pitch;
  if (col < 0 || col >= cols || row < 0 || row >= rows) return -1;
  const int index = row * cols + col;
  return index < count ? index : -1;
}

void buildBoard(toybox::Screen& screen, const BoardModel& model, Layout& layout) {
  const picross::Board& board = *model.board;
  const int n = board.size();

  char progress[toybox::kSlashCounterChars];
  std::snprintf(progress, sizeof(progress), "%d/%d", model.solvedCount, model.total);
  chrome(screen, "PICROSS", progress);
  screen.insetContent(fui::Insets{toybox::kGutter, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  // The mode toggle sits at the very bottom, under the thumb. The RESTART /
  // PUZZLES row is above it, so the destructive control is not beside the one
  // the player taps every move.
  const fui::Rect modeRow = screen.takeBottom(toybox::kPillHeight, toybox::kGutter);
  const fui::Rect buttonRow = screen.takeBottom(toybox::kPillHeight, toybox::kGutter);
  const fui::Rect status = screen.takeTop(kStatusStrip, toybox::kGutter);

  // The mistake count, the visible penalty. On the left, in plain words.
  char mistakes[kMistakeChars];
  if (board.mistakes() == 0)
    std::snprintf(mistakes, sizeof(mistakes), "NO MISTAKES");
  else
    std::snprintf(mistakes, sizeof(mistakes), "MISTAKES  %d", board.mistakes());
  fui::TextStyle mistakeStyle;
  mistakeStyle.font = toybox::kUiFont;
  mistakeStyle.align = fui::TextAlign::Left;
  screen.target().text(toybox::inkCentred(status, toybox::kUiCut), mistakes, mistakeStyle);

  char sizeLabel[kSizeChars];
  std::snprintf(sizeLabel, sizeof(sizeLabel), "%d x %d", n, n);
  fui::TextStyle sizeStyle;
  sizeStyle.font = toybox::kUiFont;
  sizeStyle.align = fui::TextAlign::Right;
  screen.target().text(toybox::inkCentred(status, toybox::kUiCut), sizeLabel, sizeStyle);

  const fui::Rect body = screen.body();
  const ClueMetrics cm = clueMetricsFor(n);
  const int16_t rowGutter = static_cast<int16_t>(maxRunsInRows(board) * cm.numW + kClueGap);
  const int16_t colGutter = static_cast<int16_t>(maxRunsInCols(board) * cm.slotH + kClueGap);

  int cell = (body.width - rowGutter) / n;
  const int availCellH = (body.height - colGutter) / n;
  if (availCellH < cell) cell = availCellH;
  if (cell > kMaxCell) cell = kMaxCell;
  if (cell < 1) cell = 1;

  const int boardExtent = cell * n;
  const int totalW = rowGutter + boardExtent;
  const int totalH = colGutter + boardExtent;
  const int16_t ox = static_cast<int16_t>(body.x + (body.width - totalW) / 2);
  const int16_t oy = static_cast<int16_t>(body.y + (body.height - totalH) / 2);

  layout.board = fui::makeRect(static_cast<int16_t>(ox + rowGutter), static_cast<int16_t>(oy + colGutter), boardExtent,
                               boardExtent);
  layout.cell = static_cast<int16_t>(cell);
  layout.size = static_cast<int16_t>(n);

  // One target for the whole grid, registered from the rect the cells were laid
  // out in and resolved back through the same Layout.
  screen.frame().hit(layout.board, ActionBoard, 0);

  drawGrid(screen, board, layout);
  drawColClues(screen, board, layout, cm);
  drawRowClues(screen, board, layout, cm);

  drawModeToggle(screen, modeRow, model.mode);

  const int16_t half = static_cast<int16_t>((buttonRow.width - toybox::kGutter) / 2);
  drawActionButton(screen, fui::makeRect(buttonRow.x, buttonRow.y, half, buttonRow.height), "RESTART", ButtonRestart);
  drawActionButton(
      screen,
      fui::makeRect(static_cast<int16_t>(buttonRow.x + half + toybox::kGutter), buttonRow.y, half, buttonRow.height),
      "PUZZLES", ButtonPuzzles);
}

namespace {

// One picker tile. A solved puzzle reveals its picture; everything else shows
// only its number and size, so the reveal is never spoiled. An in-progress
// puzzle wears a dot so RESUME is findable.
void drawTile(toybox::Screen& screen, const fui::Rect& box, const int index, const picross::Puzzle& puzzle,
              const bool solved, const bool inProgress, const bool selected) {
  screen.target().fill(box, fui::Paint::solid(fui::Color::White), static_cast<uint8_t>(6));
  screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), toybox::kHairline, static_cast<uint8_t>(6));

  if (solved) {
    drawPicture(screen, puzzle, box.inset(fui::Insets{10, 10, 10, 10}));
    // A filled check corner: a small solid wedge says "done" without a glyph.
    screen.target().fill(fui::makeRect(static_cast<int16_t>(box.right() - 14), static_cast<int16_t>(box.y + 4), 10, 10),
                         fui::Paint::solid(fui::Color::Black), static_cast<uint8_t>(5));
  } else {
    char number[toybox::kIntTextChars];
    std::snprintf(number, sizeof(number), "%d", index + 1);
    fui::TextStyle numStyle;
    numStyle.font = toybox::kDisplayFont;
    numStyle.align = fui::TextAlign::Center;
    const fui::Rect numBox = fui::makeRect(box.x, box.y, box.width, static_cast<int16_t>(box.height - 20));
    screen.target().text(toybox::inkCentred(numBox, toybox::kDisplayCut), number, numStyle);

    char size[kSizeChars];
    std::snprintf(size, sizeof(size), "%d x %d", puzzle.size, puzzle.size);
    fui::TextStyle sizeStyle;
    sizeStyle.font = toybox::kSmallFont;
    sizeStyle.align = fui::TextAlign::Center;
    const fui::Rect sizeBox = fui::makeRect(box.x, static_cast<int16_t>(box.bottom() - 20), box.width, 18);
    screen.target().text(toybox::inkCentred(sizeBox, toybox::kTileCut), size, sizeStyle);

    if (inProgress)
      screen.target().fill(fui::makeRect(static_cast<int16_t>(box.x + 6), static_cast<int16_t>(box.y + 6), 8, 8),
                           fui::Paint::solid(fui::Color::Black), static_cast<uint8_t>(4));
  }

  if (selected) {
    // Corner brackets, the same "look here" mark the win screen uses.
    const fui::Rect r = box.inset(fui::Insets{-3, -3, -3, -3});
    const int16_t len = 12;
    const fui::Paint ink = fui::Paint::solid(fui::Color::Black);
    screen.target().fill(fui::makeRect(r.x, r.y, len, 3), ink);
    screen.target().fill(fui::makeRect(r.x, r.y, 3, len), ink);
    screen.target().fill(fui::makeRect(static_cast<int16_t>(r.right() - len), r.y, len, 3), ink);
    screen.target().fill(fui::makeRect(static_cast<int16_t>(r.right() - 3), r.y, 3, len), ink);
    screen.target().fill(fui::makeRect(r.x, static_cast<int16_t>(r.bottom() - 3), len, 3), ink);
    screen.target().fill(fui::makeRect(r.x, static_cast<int16_t>(r.bottom() - len), 3, len), ink);
    screen.target().fill(
        fui::makeRect(static_cast<int16_t>(r.right() - len), static_cast<int16_t>(r.bottom() - 3), len, 3), ink);
    screen.target().fill(
        fui::makeRect(static_cast<int16_t>(r.right() - 3), static_cast<int16_t>(r.bottom() - len), 3, len), ink);
  }
}

}  // namespace

void buildMenu(toybox::Screen& screen, const MenuModel& model, PickerLayout& layout) {
  layout = PickerLayout{};
  char progress[toybox::kSlashCounterChars];
  std::snprintf(progress, sizeof(progress), "%d/%d", model.solvedCount, model.total);
  chrome(screen, "PICROSS", progress);
  screen.insetContent(fui::Insets{toybox::kGutter, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  // The big front-door button: PLAY the next unsolved, or RESUME the one in
  // progress. A player who does not want to choose taps this and gets a puzzle.
  const fui::Rect actions = screen.takeBottom(toybox::kPillHeight, toybox::kGutter);
  {
    fui::ButtonProps play;
    play.label = model.hasProgress ? "RESUME" : "PLAY";
    play.action = ActionButton;
    play.value = ButtonPlay;
    play.text = toybox::buttonText(screen.theme());
    play.radius = toybox::kPillRadius;
    screen.button(play, actions);
  }

  fui::TextStyle hint;
  hint.font = toybox::kSmallFont;
  hint.align = fui::TextAlign::Center;
  screen.target().text(screen.takeTop(22, toybox::kGutter), "TAP A PUZZLE TO PLAY IT", hint);

  const fui::Rect body = screen.body();
  const int count = picross::kPuzzleCount;
  const int16_t cols = 4;
  const int16_t rows = static_cast<int16_t>((count + cols - 1) / cols);
  const int16_t gap = 12;
  int cell = (body.width - (cols - 1) * gap) / cols;
  const int cellH = (body.height - (rows - 1) * gap) / rows;
  if (cellH < cell) cell = cellH;
  if (cell < 1) cell = 1;
  const int16_t gridW = static_cast<int16_t>(cols * cell + (cols - 1) * gap);
  const int16_t gridH = static_cast<int16_t>(rows * cell + (rows - 1) * gap);
  const int16_t left = static_cast<int16_t>(body.x + (body.width - gridW) / 2);
  const int16_t top = static_cast<int16_t>(body.y + (body.height - gridH) / 2);

  layout.grid = fui::makeRect(left, top, gridW, gridH);
  layout.cell = static_cast<int16_t>(cell);
  layout.gap = gap;
  layout.cols = cols;
  layout.rows = rows;
  layout.count = static_cast<int16_t>(count);

  // One target for the whole grid; the layout resolves which tile.
  screen.frame().hit(layout.grid, ActionPick, -1);

  const int16_t pitch = static_cast<int16_t>(cell + gap);
  for (int i = 0; i < count; ++i) {
    const int r = i / cols;
    const int c = i % cols;
    const fui::Rect box = fui::makeRect(static_cast<int16_t>(left + c * pitch), static_cast<int16_t>(top + r * pitch),
                                        static_cast<int16_t>(cell), static_cast<int16_t>(cell));
    const bool solved = model.progress != nullptr && model.progress->isSolved(i);
    drawTile(screen, box, i, picross::kPuzzles[i], solved, i == model.inProgressIndex, i == model.selectedIndex);
  }
}

void buildWin(toybox::Screen& screen, const WinModel& model) {
  char progress[toybox::kSlashCounterChars];
  std::snprintf(progress, sizeof(progress), "%d/%d", model.solvedCount, model.total);
  chrome(screen, "SOLVED", progress);
  screen.insetContent(fui::Insets{toybox::kGutter, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  const fui::Rect actions = screen.takeBottom(toybox::kPillHeight, toybox::kGutter);
  const int16_t half = static_cast<int16_t>((actions.width - toybox::kGutter) / 2);
  {
    fui::ButtonProps next;
    next.label = model.moreToPlay ? "NEXT" : "PUZZLES";
    next.action = ActionButton;
    next.value = model.moreToPlay ? ButtonNext : ButtonPuzzles;
    next.text = toybox::buttonText(screen.theme());
    next.radius = toybox::kPillRadius;
    screen.button(next, fui::makeRect(actions.x, actions.y, half, actions.height));

    fui::ButtonProps back;
    back.label = "PUZZLES";
    back.action = ActionButton;
    back.value = ButtonPuzzles;
    back.text = toybox::buttonText(screen.theme());
    back.radius = toybox::kPillRadius;
    back.styles = toybox::rowStyles();
    screen.button(
        back, fui::makeRect(static_cast<int16_t>(actions.x + half + toybox::kGutter), actions.y, half, actions.height));
  }

  // The grade, at the foot: the reveal's punchline, and the reason to replay.
  const fui::Rect gradeBand = screen.takeBottom(40, toybox::kMargin);
  char grade[kGradeChars];
  if (model.mistakes == 0)
    std::snprintf(grade, sizeof(grade), "PERFECT -- NO MISTAKES");
  else if (model.mistakes == 1)
    std::snprintf(grade, sizeof(grade), "SOLVED WITH 1 MISTAKE");
  else
    std::snprintf(grade, sizeof(grade), "SOLVED WITH %d MISTAKES", model.mistakes);
  fui::TextStyle gradeStyle;
  gradeStyle.font = toybox::kUiFont;
  gradeStyle.align = fui::TextAlign::Center;
  screen.target().text(toybox::inkCentred(gradeBand, toybox::kUiCut), grade, gradeStyle);

  // The revealed name, now safe to show.
  const fui::Rect nameBand = screen.takeBottom(60, toybox::kGutter);
  if (model.cleared != nullptr) {
    fui::TextStyle nameStyle;
    nameStyle.font = toybox::kDisplayFont;
    nameStyle.align = fui::TextAlign::Center;
    const std::string fitted = toybox::fittedTitle(screen.target(), model.cleared->name, nameBand.width, nameStyle);
    screen.target().text(toybox::inkCentred(nameBand, toybox::kDisplayCut), fitted.c_str(), nameStyle);
  }

  // The picture itself, clean, filling what is left -- the payoff.
  if (model.cleared != nullptr) drawPicture(screen, *model.cleared, screen.body());
}

}  // namespace picrossui

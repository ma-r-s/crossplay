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
// A size tab's label, "10x10". No spaces around the x: at four tabs the pill is
// 106px and " x " would put the label at 103px of it. See drawSizeTabs.
constexpr int kSizeChars = 2 * toybox::kIntChars + toybox::literalChars("x") + 1;
constexpr int kGradeChars =
    toybox::kIntChars + toybox::literalChars("SOLVED WITH ") + toybox::literalChars(" MISTAKES") + 1;

// The strip under the header carrying the mistake count. The puzzle NAME is
// deliberately absent: the name is the answer, so showing it while you solve
// would spoil the picture. It is revealed on the win screen.
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
  // No headerRule() here: headerBand() has drawn the rule itself since card
  // #248, so calling it again paints the same line twice. host-tests/chromeguard
  // is the check that says so.
  toybox::headerBand(screen, header);
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

// An asterisk: six spokes from the centre, a vertical and two diagonals. Drawn
// rather than set as a glyph, because the font's '*' sits high on the line (it
// is a superscript mark) and a cell wants it centred, and because at a 30-odd
// pixel cell the drawn one can be given a weight that survives e-ink.
//
// IT OCCUPIES EXACTLY THE X's BOX. Same reach (a quarter of the cell from the
// centre), same 2px weight, so the mistake and the player's own mark differ by
// GLYPH and by nothing else -- which is the distinction Mario drew, and the
// reason the mistake no longer needs weight to be told apart.
//
// The six arms are what separates it from the four-armed X at this size, and
// the arm ANGLES are the part that had to be looked at rather than reasoned
// about. The first version put the diagonals at 63 degrees from horizontal,
// which bunches all three strokes near the vertical: rendered at a 37px cell it
// read as a dense double-dagger, not as an asterisk. Six even arms means 60
// degrees apart, so the diagonals sit at 30 degrees from HORIZONTAL -- dx:dy of
// 7:4, close enough at this size and needing no trigonometry.
void drawAsterisk(toybox::Screen& screen, const fui::Rect& cell, const fui::Color color) {
  const int16_t cx = static_cast<int16_t>(cell.x + cell.width / 2);
  const int16_t cy = static_cast<int16_t>(cell.y + cell.height / 2);
  const int16_t r = static_cast<int16_t>(cell.width / 4);
  if (r <= 0) return;
  const uint8_t weight = static_cast<uint8_t>(cell.width >= 26 ? 2 : 1);
  const fui::Paint paint = fui::Paint::solid(color);
  const int16_t dx = static_cast<int16_t>(r * 7 / 8);
  const int16_t dy = static_cast<int16_t>(r / 2);
  screen.target().line(fui::Point{cx, static_cast<int16_t>(cy - r)}, fui::Point{cx, static_cast<int16_t>(cy + r)},
                       weight, paint);
  screen.target().line(fui::Point{static_cast<int16_t>(cx - dx), static_cast<int16_t>(cy - dy)},
                       fui::Point{static_cast<int16_t>(cx + dx), static_cast<int16_t>(cy + dy)}, weight, paint);
  screen.target().line(fui::Point{static_cast<int16_t>(cx - dx), static_cast<int16_t>(cy + dy)},
                       fui::Point{static_cast<int16_t>(cx + dx), static_cast<int16_t>(cy - dy)}, weight, paint);
}

// Two annotations that mean different things, and they now differ by GLYPH
// rather than by weight:
//
//   X          the PLAYER's own mark: "I have reasoned that this cell is
//              empty". Free, reversible, asserts nothing.
//   asterisk   the GAME's record: "you filled here and you were wrong".
//              Locked and counted.
//
// The mistake used to be a WHITE X on a SOLID BLACK cell, and that is what this
// change undoes. Mario, having played it: "The x is not heavy. What's heavy is
// the x with black background when a mistake is made." A solid cell is what the
// picture is MADE of, so every mistake added a black square to the emerging
// image and a board with a dozen of them showed a picture that was not the
// puzzle's. The mark has to stay visible and countable -- it does, it is the
// only six-armed thing on the board -- but it must not be content.
void drawCell(toybox::Screen& screen, const fui::Rect& cell, const picross::Cell state) {
  switch (state) {
    case picross::Cell::Filled:
      screen.target().fill(cell, fui::Paint::solid(fui::Color::Black));
      break;
    case picross::Cell::Crossed:
      drawX(screen, cell, fui::Color::Black);
      break;
    case picross::Cell::Mistake:
      // No fill behind it. That is the whole of the change.
      drawAsterisk(screen, cell, fui::Color::Black);
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
      if (dx)
        screen.target().fill(fui::makeRect(static_cast<int16_t>(cx - dx), static_cast<int16_t>(cy + dy), 1, 1), ink);
      if (dy)
        screen.target().fill(fui::makeRect(static_cast<int16_t>(cx + dx), static_cast<int16_t>(cy - dy), 1, 1), ink);
      if (dx && dy)
        screen.target().fill(fui::makeRect(static_cast<int16_t>(cx - dx), static_cast<int16_t>(cy - dy), 1, 1), ink);
    }
  }
}

// A clue number, centred in `box`. When its line is satisfied the number is
// crossed out -- honest here only because a wrong fill never becomes a Filled
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

  // A "0" clue is NEVER crossed out. An empty line has nothing to satisfy, so
  // the mark says nothing -- and a struck zero reads as a slashed zero or a
  // theta, which looks like a different symbol rather than a finished clue.
  const bool crossOut = satisfied && value > 0;

  // The cross-out is DIAGONAL, and it has to be. A horizontal rule through the
  // centre of a "1" IS a cross: the digit is a vertical stem, so a bar meeting
  // it at right angles makes a dagger no matter how far it overshoots or what
  // sits behind it -- geometry beats every cosmetic defence, and the earlier
  // centred-and-overshooting horizontal rule still read as a "t". A diagonal
  // still passes through the centre of the number (what Mario asked for) and
  // still reads unmistakably as "crossed out", but it cannot be mistaken for a
  // plus or a dagger because neither has a slanted bar. One consistent 45-degree
  // angle for EVERY struck clue -- a special case for the "1"s would itself look
  // like a bug. Chip first, then the digit, then the cross over both.
  int16_t chipHalfW = 0;
  int16_t chipHalfH = 0;
  if (crossOut) {
    const int16_t glyphW = screen.target().measureText(cm.font, text, style).width;
    chipHalfW = static_cast<int16_t>(glyphW / 2 + 4);
    chipHalfH = static_cast<int16_t>(cm.cut.inkHeight / 2 + 2);
    // Never spill into the neighbouring stacked clue or the playfield.
    if (chipHalfW > box.width / 2 - 1) chipHalfW = static_cast<int16_t>(box.width / 2 - 1);
    if (chipHalfH > box.height / 2 - 1) chipHalfH = static_cast<int16_t>(box.height / 2 - 1);
    if (chipHalfW > 0 && chipHalfH > 0) drawDoneChip(screen, cx, cy, chipHalfW, chipHalfH);
  }

  screen.target().text(toybox::inkCentred(box, cm.cut), text, style);

  if (crossOut && chipHalfW > 0 && chipHalfH > 0) {
    // Square the reach so the angle is the same on every clue, and keep it a
    // pixel inside the chip on both axes: the rule used to poke past the chip's
    // edges, which read as unfinished.
    int16_t reach = chipHalfW < chipHalfH ? chipHalfW : chipHalfH;
    reach = static_cast<int16_t>(reach - 1);
    if (reach > 0) {
      const uint8_t weight = static_cast<uint8_t>(cm.cut.inkHeight >= 20 ? 3 : 2);
      screen.target().line(fui::Point{static_cast<int16_t>(cx - reach), static_cast<int16_t>(cy + reach)},
                           fui::Point{static_cast<int16_t>(cx + reach), static_cast<int16_t>(cy - reach)}, weight,
                           fui::Paint::solid(fui::Color::Black));
    }
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
void drawPicture(toybox::Screen& screen, const picross::Puzzle& puzzle, const fui::Rect& area,
                 const fui::Color color = fui::Color::Black) {
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
          fui::Paint::solid(color));
    }
  }
}

}  // namespace

int stepPage(const int page, const int pageCount, const int delta) {
  if (pageCount <= 1) return 0;
  const int next = page + delta;
  if (next < 0) return 0;
  if (next >= pageCount) return pageCount - 1;
  return next;
}

bool Layout::cellAt(const int x, const int y, int& row, int& col) const {
  if (cell <= 0) return false;
  if (x < board.x || x >= board.right() || y < board.y || y >= board.bottom()) return false;
  col = (x - board.x) / cell;
  row = (y - board.y) / cell;
  return row >= 0 && row < size && col >= 0 && col < size;
}

int PickerLayout::indexAt(const int x, const int y) const {
  if (count <= 0) return -1;
  if (x < grid.x || y < grid.y || x >= grid.right() || y >= grid.bottom()) return -1;
  int local;
  if (rowHeight > 0) {  // a single-column list: one tile per row of `rowHeight`
    const int row = (y - grid.y) / rowHeight;
    if (row < 0) return -1;
    local = row;
  } else {  // a grid resolved by pitch, exactly as it was drawn
    if (cell <= 0) return -1;
    const int pitch = cell + gap;
    const int col = (x - grid.x) / pitch;
    const int row = (y - grid.y) / pitch;
    if (col < 0 || col >= cols || row < 0 || row >= rows) return -1;
    local = row * cols + col;
  }
  if (local < 0 || local >= count) return -1;
  return firstIndex + local;
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

  // No size label here. There is one size, so "10 x 10" told the player nothing
  // they could act on and repeated itself on every screen; Mario's call. The
  // strip is the mistake count alone.

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

// The picker: a paged grid of rounded tiles. Chosen from three rendered variants
// (a SOLID grid, a LIST, and a size-tabbed grid) and a cold review of them. The
// selected / in-progress tile is fully INVERTED -- the fill-is-selected language
// the mode capsule and shelf rows already speak, and the least ambiguous mark
// 1-bit e-ink has. No corner brackets (they clashed with the rounded tiles) and
// no gutter underline (it read as belonging to the tile below).
//
// THE SIZE TABS ARE BACK, because the bank is four tiers again (33 at 5x5, 28
// at 8x8, 29 at 9x9, 109 at 10x10). They answer "puzzles across four sizes"
// with direct access instead of blind paging, and each carries its own solved
// count, so the row doubles as "which tiers still have puzzles left". They were
// removed while the game was 10x10-only and a row of one tab is a control with
// nothing to choose between; four is a control.

namespace {

// The bank is emitted size-sorted, so each size is one contiguous run and the
// groups are recoverable by scanning for the changes. Recovered once, for the
// tabs and for the paging.
//
// THE SLOTS ARE SIZED FROM picross::kSizeGroupCount, which the GENERATOR
// derives from the bank it just wrote. They were a literal 4 with a `break`
// underneath, and that pairing is a silent data-loss bug rather than a bound: a
// bank producing more groups than slots hits the break, and every puzzle after
// it is simply unreachable from the tabs -- nothing drawn wrong, nothing
// logged, nothing to see on the screen. FOUR IS EXACTLY THE BOUNDARY THAT BUG
// SAT ON, and this bank has four groups. Deriving the count means the array
// cannot be too small for a sorted bank; host-tests/picross asserts the
// sortedness AND that the recovered groups account for every puzzle in the
// bank, so the break below is unreachable rather than merely unlikely.
struct SizeGroups {
  int count = 0;
  int size[picross::kSizeGroupCount] = {};
  int start[picross::kSizeGroupCount] = {};
  int len[picross::kSizeGroupCount] = {};
};

SizeGroups sizeGroups() {
  SizeGroups g;
  for (int i = 0; i < picross::kPuzzleCount; ++i) {
    const int s = picross::kPuzzles[i].size;
    if (g.count == 0 || g.size[g.count - 1] != s) {
      if (g.count >= picross::kSizeGroupCount) break;
      g.size[g.count] = s;
      g.start[g.count] = i;
      g.len[g.count] = 0;
      ++g.count;
    }
    ++g.len[g.count - 1];
  }
  return g;
}

// Which group a puzzle index falls in. Used to open the picker on the tab
// holding the selection, so the activity never has to know where a run starts.
int groupOf(const SizeGroups& g, const int index) {
  for (int t = 0; t < g.count; ++t)
    if (index >= g.start[t] && index < g.start[t] + g.len[t]) return t;
  return 0;
}

constexpr int16_t kTabBandHeight = 60;
constexpr int16_t kTabGap = 8;

// HOW MANY SIZE RUNS THE BANK ACTUALLY HAS, counted from the bank at compile
// time, and checked against the number the generator wrote down.
//
// This is the whole mechanism, and it is worth being precise about what it
// catches. The picker recovers its groups by run-scanning for changes of size,
// which is correct only while each size is ONE CONTIGUOUS RUN. An unsorted bank
// -- which is exactly what appending an import to the file produces -- makes
// every alternation a new run: more runs than there are sizes, more groups than
// there are slots, the `break` in sizeGroups() fires, and every puzzle past that
// point is unreachable from the tabs. Nothing draws wrong. Nothing is logged.
//
// Counting the runs here turns that into a build failure. kSizeGroupCount is
// the number of DISTINCT sizes; this is the number of RUNS; they are equal if
// and only if the bank is size-sorted.
constexpr int countSizeRuns() {
  int runs = 0;
  for (int i = 0; i < picross::kPuzzleCount; ++i)
    if (i == 0 || picross::kPuzzles[i].size != picross::kPuzzles[i - 1].size) ++runs;
  return runs;
}

static_assert(picross::kSizeGroupCount >= 1,
              "a bank with no sizes has no puzzles; the generator refuses that long before here");
static_assert(countSizeRuns() == picross::kSizeGroupCount,
              "the bank is not size-sorted: it holds more runs of size than it holds distinct "
              "sizes. sizeGroups() run-scans for changes of size, so the extra runs overflow the "
              "group slots, the break fires, and every puzzle after that point is unreachable "
              "from the picker with nothing drawn wrong and nothing logged. Emit the bank through "
              "gen_picross.sort_by_size rather than appending to it.");

int clampInt(const int v, const int lo, const int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// A right-pointing "play" triangle: the resume affordance, drawn as narrowing
// horizontal bars so it needs no glyph and reads at any size.
void drawPlayGlyph(toybox::Screen& screen, const fui::Rect& box, const fui::Color color) {
  const int h = box.height;
  if (h <= 0 || box.width <= 0) return;
  const int centre = (h - 1) / 2;
  const int half = centre > 0 ? centre : 1;
  const fui::Paint ink = fui::Paint::solid(color);
  for (int i = 0; i < h; ++i) {
    const int dist = i <= centre ? centre - i : i - centre;
    const int w = box.width - (box.width * dist) / half;
    if (w <= 0) continue;
    screen.target().fill(fui::makeRect(box.x, static_cast<int16_t>(box.y + i), static_cast<int16_t>(w), 1), ink);
  }
}

// A row of page dots, the current one filled, each a tap target that jumps to
// its page. Silent when there is only one page.
void drawPageDots(toybox::Screen& screen, const fui::Rect& band, const int pageCount, const int current) {
  if (pageCount <= 1) return;
  const int16_t d = 12;
  const int16_t gap = 14;
  const int16_t total = static_cast<int16_t>(pageCount * d + (pageCount - 1) * gap);
  int16_t x = static_cast<int16_t>(band.x + (band.width - total) / 2);
  const int16_t y = static_cast<int16_t>(band.y + (band.height - d) / 2);
  for (int p = 0; p < pageCount; ++p) {
    const fui::Rect dot = fui::makeRect(x, y, d, d);
    if (p == current)
      screen.target().fill(dot, fui::Paint::solid(fui::Color::Black), static_cast<uint8_t>(d / 2));
    else
      screen.target().stroke(dot, fui::Paint::solid(fui::Color::Black), toybox::kHairline, static_cast<uint8_t>(d / 2));
    screen.frame().hit(
        fui::makeRect(static_cast<int16_t>(x - gap / 2), band.y, static_cast<int16_t>(d + gap), band.height),
        ActionPage, static_cast<int16_t>(p));
    x = static_cast<int16_t>(x + d + gap);
  }
}

// A rounded picker tile. A solved puzzle reveals its finished picture -- which
// is the whole reward and, being a picture rather than a number, is itself the
// "solved" signal, so no extra badge is drawn. Everything else shows its number
// and size. An in-progress puzzle wears a play glyph. `selected` inverts the
// tile to solid black with white content: the picker's one selection language.
void drawTile(toybox::Screen& screen, const fui::Rect& box, const int index, const picross::Puzzle& puzzle,
              const bool solved, const bool inProgress, const bool selected) {
  const fui::Color bg = selected ? fui::Color::Black : fui::Color::White;
  const fui::Color fg = selected ? fui::Color::White : fui::Color::Black;
  screen.target().fill(box, fui::Paint::solid(bg), static_cast<uint8_t>(8));
  screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), toybox::kHairline, static_cast<uint8_t>(8));

  if (solved) {
    drawPicture(screen, puzzle, box.inset(fui::Insets{10, 12, 10, 12}), fg);
  } else {
    char number[toybox::kIntTextChars];
    std::snprintf(number, sizeof(number), "%d", index + 1);
    fui::TextStyle numStyle;
    numStyle.font = toybox::kDisplayFont;
    numStyle.align = fui::TextAlign::Center;
    numStyle.color = fg;
    // The number is centred in the WHOLE tile. It used to sit above a "10 x 10"
    // line and was offset upward to make room for it; with the size gone, an
    // unchanged offset would leave every number riding high for no reason.
    screen.target().text(toybox::inkCentred(box, toybox::kDisplayCut), number, numStyle);
  }

  if (inProgress)
    drawPlayGlyph(screen, fui::makeRect(static_cast<int16_t>(box.x + 7), static_cast<int16_t>(box.y + 7), 9, 11), fg);
}

// How the tile grid fits `body`: four columns, and as many rows as the height
// actually holds.
//
// The rows used to be capped at four and the page at a literal sixteen, sized
// for a body that had a 60px size-tab band above it. With the tabs gone that
// literal would have left a band of dead paper under the last row and paged the
// bank more times than it needs -- a number derived once, then invalidated by a
// layout change that had no reason to know about it. Deriving it here means the
// page count follows the panel.
struct GridGeom {
  int16_t cols = 4;
  int16_t gap = 12;
  int16_t cell = 0;
  int16_t rows = 0;
  int16_t perPage = 0;
};

GridGeom gridGeom(const fui::Rect& body) {
  GridGeom g;
  int cell = (body.width - (g.cols - 1) * g.gap) / g.cols;
  // A tile is a thumbnail of a 10x10 picture, not a poster: past this it is all
  // white space around a small drawing.
  if (cell > 118) cell = 118;
  if (cell < 1) cell = 1;
  g.cell = static_cast<int16_t>(cell);
  int rows = (body.height + g.gap) / (cell + g.gap);
  if (rows < 1) rows = 1;
  g.rows = static_cast<int16_t>(rows);
  g.perPage = static_cast<int16_t>(g.cols * g.rows);
  return g;
}

// Lay the tiles for puzzles [first, first+n) into `body`, filling `layout` so a
// tap resolves back through the same geometry.
void layOutGrid(toybox::Screen& screen, const MenuModel& model, const fui::Rect& body, const GridGeom& g,
                const int first, const int n, const int page, const int pageCount, PickerLayout& layout) {
  const int16_t rows = static_cast<int16_t>((n + g.cols - 1) / g.cols);
  const int16_t pitch = static_cast<int16_t>(g.cell + g.gap);
  const int16_t gridW = static_cast<int16_t>(g.cols * g.cell + (g.cols - 1) * g.gap);
  const int16_t gridH = static_cast<int16_t>(rows * g.cell + (rows - 1) * g.gap);
  const int16_t left = static_cast<int16_t>(body.x + (body.width - gridW) / 2);
  const int16_t top = body.y;

  layout.grid = fui::makeRect(left, top, gridW, gridH);
  layout.cell = g.cell;
  layout.gap = g.gap;
  layout.cols = g.cols;
  layout.rows = rows;
  layout.count = static_cast<int16_t>(n);
  layout.firstIndex = static_cast<int16_t>(first);
  layout.pageCount = static_cast<int16_t>(pageCount);
  layout.pageOnScreen = static_cast<int16_t>(page);
  screen.frame().hit(layout.grid, ActionPick, -1);

  for (int k = 0; k < n; ++k) {
    const int i = first + k;
    const int r = k / g.cols;
    const int c = k % g.cols;
    const fui::Rect box =
        fui::makeRect(static_cast<int16_t>(left + c * pitch), static_cast<int16_t>(top + r * pitch), g.cell, g.cell);
    const bool solved = model.progress != nullptr && model.progress->isSolved(i);
    drawTile(screen, box, i, picross::kPuzzles[i], solved, i == model.inProgressIndex, i == model.selectedIndex);
  }
}

// The size tabs. Each is a rounded pill carrying its size and its own solved
// count; the active one is filled. Tapping a tab switches groups.
//
// THE LABEL IS "10x10", NOT "10 x 10", and the spaces are not a style choice.
// At four tabs the band gives each pill 106px, and "10 x 10" MEASURES 103px in
// the body cut -- one and a half pixels of air either side of a shape with a
// 20px corner radius. It was the label while there were three tabs and it does
// not survive a fourth. Closing the spaces takes it to 83px and gives the pill
// eleven pixels of padding. Measured with tools_local/picross/name_fit.py
// against the real cut, not judged from a render.
//
// The pill's vertical budget, from its top edge: 8px padding, the 25px label
// ink, a 7px gap, the 13px count ink, 7px padding -- 60 in all. It was 48, which
// left three pixels above and below the text, and kPillRadius rounds the corners
// into exactly those pixels, so the two lines read as jammed against the bubble.
// Grow the pill rather than shrink the type.
//
// `layout` is filled as this draws, the same discipline the grid keeps: the
// width a tab was actually given is a number a test can check against
// kTabMinWidth, and a hit rect per tab is what makes "every tier is reachable"
// checkable rather than assumed.
void drawSizeTabs(toybox::Screen& screen, const MenuModel& model, const SizeGroups& g, const fui::Rect& band,
                  const int active, PickerLayout& layout) {
  if (g.count <= 0) return;
  const int16_t tabW = static_cast<int16_t>((band.width - (g.count - 1) * kTabGap) / g.count);
  layout.tabCount = static_cast<int16_t>(g.count);
  layout.tabWidth = tabW;
  layout.tabOnScreen = static_cast<int16_t>(active);
  for (int t = 0; t < g.count; ++t) {
    const fui::Rect tb = fui::makeRect(static_cast<int16_t>(band.x + t * (tabW + kTabGap)), band.y, tabW, band.height);
    const bool on = t == active;
    screen.target().fill(tb, fui::Paint::solid(on ? fui::Color::Black : fui::Color::White),
                         static_cast<uint8_t>(toybox::kPillRadius));
    screen.target().stroke(tb, fui::Paint::solid(fui::Color::Black), toybox::kHairline,
                           static_cast<uint8_t>(toybox::kPillRadius));
    int solvedHere = 0;
    if (model.progress != nullptr)
      for (int i = g.start[t]; i < g.start[t] + g.len[t]; ++i)
        if (model.progress->isSolved(i)) ++solvedHere;

    const fui::Color fg = on ? fui::Color::White : fui::Color::Black;
    char label[kSizeChars];
    std::snprintf(label, sizeof(label), "%dx%d", g.size[t], g.size[t]);
    fui::TextStyle ts;
    ts.font = toybox::kUiFont;
    ts.align = fui::TextAlign::Center;
    ts.color = fg;
    const fui::Rect labelBox = fui::makeRect(tb.x, tb.y, tb.width, static_cast<int16_t>(tb.height - 18));
    screen.target().text(toybox::inkCentred(labelBox, toybox::kUiCut), label, ts);

    char count[toybox::kSlashCounterChars];
    std::snprintf(count, sizeof(count), "%d/%d", solvedHere, g.len[t]);
    fui::TextStyle cs;
    cs.font = toybox::kSmallFont;
    cs.align = fui::TextAlign::Center;
    cs.color = fg;
    const fui::Rect countBox = fui::makeRect(tb.x, static_cast<int16_t>(tb.bottom() - 22), tb.width, 18);
    screen.target().text(toybox::inkCentred(countBox, toybox::kTileCut), count, cs);

    screen.frame().hit(tb, ActionTab, static_cast<int16_t>(t));
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

  // The tabs come off the TOP, before the dots and the grid take what is left,
  // so the grid's height is what actually remains rather than a number written
  // for a layout that has since moved.
  const SizeGroups groups = sizeGroups();
  const fui::Rect tabBand = screen.takeTop(kTabBandHeight, toybox::kGutter);
  const fui::Rect dotBand = screen.takeBottom(26, toybox::kGutter);

  const GridGeom g = gridGeom(screen.body());
  const int perPage = g.perPage;

  // followSelection is how the picker OPENS on the puzzle PLAY would start. It
  // resolves BOTH the tab and the page, and that is the point: the activity
  // knows neither how many tiles fit a page nor where one size run ends, and a
  // second copy of either is a copy that goes wrong the next time the layout or
  // the bank moves. That is not hypothetical -- the page was once computed in
  // the activity by dividing by a literal 16, and it went silently wrong the
  // moment this band's height changed.
  const int selected = clampInt(model.selectedIndex, 0, picross::kPuzzleCount - 1);
  const int tab = model.followSelection ? groupOf(groups, selected) : clampInt(model.sizeTab, 0, groups.count - 1);

  const int len = groups.count > 0 ? groups.len[tab] : 0;
  const int pageCount = len > 0 ? (len + perPage - 1) / perPage : 1;
  const int page = model.followSelection ? clampInt((selected - groups.start[tab]) / perPage, 0, pageCount - 1)
                                         : clampInt(model.page, 0, pageCount - 1);

  drawSizeTabs(screen, model, groups, tabBand, tab, layout);

  const int firstInGroup = page * perPage;
  const int first = groups.start[tab] + firstInGroup;
  const int n = len - firstInGroup < perPage ? len - firstInGroup : perPage;
  layOutGrid(screen, model, screen.body(), g, first, n, page, pageCount, layout);
  drawPageDots(screen, dotBand, pageCount, page);
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

  // NO DESIGNER CREDIT HERE, and that is a decision rather than an oversight.
  // It used to read "PUZZLE BY <name>" under the reveal. Mario, having seen it:
  // "it just looks bad". The credit obligation is unchanged and is met in
  // assets_local/picross/PROVENANCE.md, which carries the full per-puzzle
  // mapping, is generated from the bank and is checked against it -- see the
  // note at the top of PicrossPuzzles.h. The designer strings are no longer in
  // the firmware at all, so there is nothing here to draw even if somebody
  // wanted to.

  // The revealed name, now safe to show -- and the WHOLE reward, which is why
  // the picker and the board hide it.
  //
  // An UNNAMED puzzle draws no band at all rather than an empty one: the names
  // are being written by hand, so a bank is normally part-named, and a blank
  // 52px gap over every unnamed picture would read as a name that failed to
  // render (see the a-silent-screen-reads-as-a-crash memory). No band means the
  // picture simply gets the space.
  const bool named = model.cleared != nullptr && model.cleared->name != nullptr && model.cleared->name[0] != '\0';
  if (named) {
    const fui::Rect nameBand = screen.takeBottom(52, toybox::kGutter);
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

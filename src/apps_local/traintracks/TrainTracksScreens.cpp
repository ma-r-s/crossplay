#include "TrainTracksScreens.h"

#include <algorithm>
#include <cstdio>

#include "../ui/ToyboxFormat.h"

namespace traintracksui {

namespace {

// ---------------------------------------------------------------------------
// The composition.
//
// The grid is as wide as the panel allows once the row clues, the two gate
// tabs and the board frame have their room, and no wider than kMaxCell. On the
// 480px portrait panel that is a 40px cell at 10x10, 50 at 8x8, and the cap at
// 6x6. Everything else is placed from the grid outward.
// ---------------------------------------------------------------------------

constexpr int16_t kSide = 6;      // page margin left and right of the board; buttons keep kMargin
constexpr int16_t kClueW = 38;    // row-clue gutter: "10" is 33px in the UI cut
constexpr int16_t kClueH = 34;    // column-clue band
constexpr int16_t kTab = 10;      // the stations live in this band outside the frame
constexpr int16_t kMaxCell = 64;  // a 6x6 should still read as a board, not as tiles
constexpr int16_t kStatusH = 34;
// Between the gate band under the board and the status line: without it a
// bottom station hung into the line's first letters.
constexpr int16_t kStatusGap = 10;
constexpr int16_t kThumbRow = 72;  // UNDO / REDO, sized for a thumb

constexpr int16_t kFrameW = toybox::kFrame;

fui::Paint black() { return fui::Paint::solid(fui::Color::Black); }
fui::Paint white() { return fui::Paint::solid(fui::Color::White); }

// The header band with the offset rule under it, as the other games wear it. A
// local copy rather than a shared helper, for the reason LinkScreens gives: a
// copy is cheaper than a header dependency between apps.
void toyboxChrome(toybox::Screen& screen, const char* title, const char* rightLabel) {
  fui::HeaderProps header;
  header.title = title;
  header.rightLabel = rightLabel;
  // rightLabel is drawn with subtitleText, and the theme's default for it is
  // black, on the black band.
  header.subtitleText = fui::TextStyle{};
  header.subtitleText.font = toybox::kUiFont;
  header.subtitleText.color = fui::Color::White;
  header.subtitleText.align = fui::TextAlign::Right;
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

int16_t gridSide(const Geometry& g) { return static_cast<int16_t>(g.n * g.cell); }

// ---------------------------------------------------------------------------
// One square.

struct Pt {
  int16_t x;
  int16_t y;
};

Pt centreOf(const fui::Rect& box) {
  return Pt{static_cast<int16_t>(box.x + box.width / 2), static_cast<int16_t>(box.y + box.height / 2)};
}

Pt toward(const Pt p, const int dir, const int16_t distance) {
  return Pt{static_cast<int16_t>(p.x + tt::kDCol[dir] * distance),
            static_cast<int16_t>(p.y + tt::kDRow[dir] * distance)};
}

void line(toybox::Screen& screen, const Pt a, const Pt b, const uint8_t width) {
  screen.target().line(fui::Point{a.x, a.y}, fui::Point{b.x, b.y}, width, black());
}

// A sleeper across the track at `at`, perpendicular to `dir`.
void tie(toybox::Screen& screen, const Pt at, const int dir, const int16_t length) {
  const bool vertical = dir == tt::kNorth || dir == tt::kSouth;
  const int16_t half = static_cast<int16_t>(length / 2);
  const int16_t w = toybox::kRule;
  if (vertical) {
    screen.target().fill(
        fui::makeRect(static_cast<int16_t>(at.x - half), static_cast<int16_t>(at.y - w / 2), length, w), black());
  } else {
    screen.target().fill(
        fui::makeRect(static_cast<int16_t>(at.x - w / 2), static_cast<int16_t>(at.y - half), w, length), black());
  }
}

// The rail through a square, from its connection mask. Every track square
// the board draws has exactly two connections (inferShapes); the other forms
// below are a fallback, so a malformed mask still draws something honest.
//
//   two opposite connections   a straight, with two sleepers
//   two adjacent connections   a quarter-circle curve, with two sleepers
//   one                        a stub toward it, ending in a dot
//   none                       a small solid block: track, not yet joined
//   three or four              a larger open block: ambiguous until more
//                              marks resolve it, so no shape is guessed
void drawRail(toybox::Screen& screen, const fui::Rect& box, const uint8_t mask) {
  const Pt c = centreOf(box);
  const int16_t half = static_cast<int16_t>(box.width / 2);
  const int16_t tieLen = static_cast<int16_t>(box.width * 45 / 100);
  const int count = tt::popcount4(mask);

  if (count >= 3) {
    const int16_t inset = static_cast<int16_t>(box.width / 4);
    screen.target().stroke(
        fui::makeRect(static_cast<int16_t>(box.x + inset), static_cast<int16_t>(box.y + inset),
                      static_cast<int16_t>(box.width - 2 * inset), static_cast<int16_t>(box.height - 2 * inset)),
        black(), toybox::kRule);
    return;
  }
  if (count == 0) {
    const int16_t side = static_cast<int16_t>(box.width / 4);
    screen.target().fill(
        fui::makeRect(static_cast<int16_t>(c.x - side / 2), static_cast<int16_t>(c.y - side / 2), side, side), black());
    return;
  }

  int dirs[2] = {-1, -1};
  int k = 0;
  for (int d = 0; d < 4; ++d) {
    if (mask & tt::dirBit(d)) dirs[k++] = d;
  }

  if (count == 1) {
    line(screen, c, toward(c, dirs[0], half), toybox::kRule);
    tie(screen, toward(c, dirs[0], static_cast<int16_t>(half / 2)), dirs[0], tieLen);
    const int16_t dot = 9;
    screen.target().fill(
        fui::makeRect(static_cast<int16_t>(c.x - dot / 2), static_cast<int16_t>(c.y - dot / 2), dot, dot), black());
    return;
  }

  if (dirs[1] == tt::opposite(dirs[0])) {
    line(screen, toward(c, dirs[0], half), toward(c, dirs[1], half), toybox::kRule);
    tie(screen, toward(c, dirs[0], static_cast<int16_t>(half / 2)), dirs[0], tieLen);
    tie(screen, toward(c, dirs[1], static_cast<int16_t>(half / 2)), dirs[1], tieLen);
    return;
  }

  // A curve: a quarter circle of radius half a square, centred on the corner
  // the two connections share, drawn as short chords. Clipping a stroked circle
  // would be simpler, but the renderer does not clip rounded-rect strokes.
  // cos/sin of 0, 15, ... 90 degrees, in thousandths.
  static constexpr int16_t kCos[7] = {1000, 966, 866, 707, 500, 259, 0};
  const Pt corner = toward(toward(c, dirs[0], half), dirs[1], half);
  auto arcPoint = [&](const int step, const int16_t radius) {
    // From the edge midpoint on dirs[0]'s side (step 0) round to dirs[1]'s.
    const int along1 = radius * kCos[step] / 1000;
    const int along0 = radius * kCos[6 - step] / 1000;
    return Pt{static_cast<int16_t>(corner.x - tt::kDCol[dirs[1]] * along1 - tt::kDCol[dirs[0]] * along0),
              static_cast<int16_t>(corner.y - tt::kDRow[dirs[1]] * along1 - tt::kDRow[dirs[0]] * along0)};
  };
  for (int step = 0; step < 6; ++step) line(screen, arcPoint(step, half), arcPoint(step + 1, half), toybox::kRule);
  // Two sleepers, a third and two thirds of the way round, along the radius so
  // they stay square to the rail. Without them a winding route stops reading
  // as track. One at the midpoint was rendered too; two won, to be confirmed
  // on the panel for busyness at Hard's 40px.
  const int16_t inner = static_cast<int16_t>(half - tieLen / 2);
  const int16_t outer = static_cast<int16_t>(half + tieLen / 2);
  line(screen, arcPoint(2, inner), arcPoint(2, outer), toybox::kRule);
  line(screen, arcPoint(4, inner), arcPoint(4, outer), toybox::kRule);
}

// A cross is drawn heavier than the rails so it pairs with the question mark,
// the other mark the player works the puzzle out with.
void drawCross(toybox::Screen& screen, const fui::Rect& box) {
  constexpr uint8_t kStroke = 5;
  const Pt c = centreOf(box);
  const int16_t arm = static_cast<int16_t>(box.width * 16 / 100);
  line(screen, Pt{static_cast<int16_t>(c.x - arm), static_cast<int16_t>(c.y - arm)},
       Pt{static_cast<int16_t>(c.x + arm), static_cast<int16_t>(c.y + arm)}, kStroke);
  line(screen, Pt{static_cast<int16_t>(c.x - arm), static_cast<int16_t>(c.y + arm)},
       Pt{static_cast<int16_t>(c.x + arm), static_cast<int16_t>(c.y - arm)}, kStroke);
}

// Track whose piece the player has left undecided: a question mark at the
// clue size on every board, which reads at Hard's 40px squares and leaves the
// corner where CHECK's notch goes clear. The display cut on Easy's 64px
// squares outweighed the cross beside it.
void drawUnknown(toybox::Screen& screen, const fui::Rect& box) {
  fui::TextStyle mark;
  mark.align = fui::TextAlign::Center;
  mark.font = toybox::kBodyFont;
  screen.target().text(toybox::inkCentred(box, toybox::kUiCut), "?", mark);
}

// ---------------------------------------------------------------------------
// The board.

void drawClue(toybox::Screen& screen, const fui::Rect& box, const int clue, const tt::ClueState state) {
  char text[12];
  std::snprintf(text, sizeof(text), "%d", clue);
  fui::TextStyle digit;
  digit.font = toybox::kBodyFont;
  digit.align = fui::TextAlign::Center;
  // Boxed when met, inverted when over: two states that read at a glance and
  // need no grey, which this panel does not have for type.
  // The chip is placed around the digits' ink, not the box, so the space
  // inside its 2px border is the same above and below. The renderer draws the
  // cut one row above where the metrics put its ink, so the digits move down
  // that row; centring on the box had left their tops touching the border.
  fui::Rect textRect = toybox::inkCentred(box, toybox::kUiCut);
  textRect.y = static_cast<int16_t>(textRect.y + 1);
  const int16_t inkTop = static_cast<int16_t>(textRect.y + toybox::kUiCut.ascender - toybox::kUiCut.inkHeight - 1);
  const int16_t inkHeight = toybox::kUiCut.inkHeight;
  constexpr int16_t kInset = 4;  // border and gap
  const int16_t chipW = clue >= 10 ? 42 : 32;
  const int16_t chipH = static_cast<int16_t>(inkHeight + 2 * kInset);
  const fui::Rect chip = fui::makeRect(static_cast<int16_t>(box.x + (box.width - chipW) / 2),
                                       static_cast<int16_t>(inkTop - kInset), chipW, chipH);
  if (state == tt::ClueState::Over) {
    screen.target().fill(chip, black());
    digit.color = fui::Color::White;
  } else if (state == tt::ClueState::Met) {
    screen.target().stroke(chip, black(), 2);
  }
  screen.target().text(textRect, text, digit);
}

void drawGate(toybox::Screen& screen, const Geometry& g, const tt::Gate& gate) {
  const fui::Rect box = cellRect(g, gate.row, gate.col);
  const int s = gate.side;
  const Pt mid = toward(centreOf(box), s, static_cast<int16_t>(g.cell / 2));
  // The opening: the frame is broken where the track leaves the board.
  const int16_t gap = static_cast<int16_t>(g.cell / 2);
  const bool vertical = s == tt::kNorth || s == tt::kSouth;
  const Pt frameMid = toward(mid, s, static_cast<int16_t>(kFrameW / 2));
  if (vertical) {
    screen.target().fill(fui::makeRect(static_cast<int16_t>(frameMid.x - gap / 2),
                                       static_cast<int16_t>(s == tt::kNorth ? mid.y - kFrameW : mid.y), gap, kFrameW),
                         white());
  } else {
    screen.target().fill(fui::makeRect(static_cast<int16_t>(s == tt::kWest ? mid.x - kFrameW : mid.x),
                                       static_cast<int16_t>(frameMid.y - gap / 2), kFrameW, gap),
                         white());
  }
  // The station: a ringed dot on the opening, as a subway map marks a stop. It
  // sits over the frame and the end of the rail, so the line arrives at it.
  // Both ends look the same: the line can be laid from either.
  const int16_t r = static_cast<int16_t>(kTab + kFrameW / 2 - 1);
  const Pt c = toward(mid, s, static_cast<int16_t>(kFrameW / 2));
  screen.target().fill(fui::makeRect(static_cast<int16_t>(c.x - r), static_cast<int16_t>(c.y - r),
                                     static_cast<int16_t>(2 * r + 1), static_cast<int16_t>(2 * r + 1)),
                       black(), static_cast<uint8_t>(r));
  const int16_t hole = static_cast<int16_t>(r - toybox::kRule - 1);
  screen.target().fill(fui::makeRect(static_cast<int16_t>(c.x - hole), static_cast<int16_t>(c.y - hole),
                                     static_cast<int16_t>(2 * hole + 1), static_cast<int16_t>(2 * hole + 1)),
                       white(), static_cast<uint8_t>(hole));
}

// CHECK's mark: a solid notch in the top-right corner. A quarter of the square
// on a side, so it stays clear of a curve that turns through that corner: the
// curve's sleepers reach to within about 0.27 of the square of the corner,
// and a third-of-a-square notch touched them.
void drawWrong(toybox::Screen& screen, const fui::Rect& box) {
  const int16_t k = static_cast<int16_t>(box.width / 4);
  const int16_t right = static_cast<int16_t>(box.x + box.width - 1);
  screen.target().triangle(fui::Point{static_cast<int16_t>(right - k), static_cast<int16_t>(box.y + 1)},
                           fui::Point{right, static_cast<int16_t>(box.y + 1)},
                           fui::Point{right, static_cast<int16_t>(box.y + 1 + k)}, black());
}

// A run along one of the square's grid lines, `from` to `to` along it (0 at
// the top or left corner), drawn kRule thick and centred on the line.
void edgeRun(toybox::Screen& screen, const fui::Rect& box, const int side, const int16_t from, const int16_t to) {
  const int16_t half = toybox::kRule / 2;
  const int16_t length = static_cast<int16_t>(to - from);
  switch (side) {
    case tt::kNorth:
      screen.target().fill(
          fui::makeRect(static_cast<int16_t>(box.x + from), static_cast<int16_t>(box.y - half), length, toybox::kRule),
          black());
      break;
    case tt::kSouth:
      screen.target().fill(fui::makeRect(static_cast<int16_t>(box.x + from),
                                         static_cast<int16_t>(box.y + box.height - half), length, toybox::kRule),
                           black());
      break;
    case tt::kWest:
      screen.target().fill(
          fui::makeRect(static_cast<int16_t>(box.x - half), static_cast<int16_t>(box.y + from), toybox::kRule, length),
          black());
      break;
    default:
      screen.target().fill(fui::makeRect(static_cast<int16_t>(box.x + box.width - half),
                                         static_cast<int16_t>(box.y + from), toybox::kRule, length),
                           black());
      break;
  }
}

// HINT's mark: corner brackets on the square's own grid lines. Track only ever meets
// those lines at the middle of a side, so a mark kept off the midpoints never
// touches the piece it marks; a curve's sleepers come no nearer a corner than
// about a quarter of the square.
void drawHintMark(toybox::Screen& screen, const fui::Rect& box) {
  const int16_t w = box.width;
  const int16_t arm = static_cast<int16_t>(w / 5);
  for (int side = 0; side < 4; ++side) {
    edgeRun(screen, box, side, -1, arm);
    edgeRun(screen, box, side, static_cast<int16_t>(w - arm), static_cast<int16_t>(w + 2));
  }
}

struct Marks {
  const uint8_t* wrong = nullptr;
  int hintCell = -1;
  int holdCell = -1;
  bool clues = true;
  // Solved: the track set is the puzzle's one answer, so every square draws
  // the answer's own shape. Inference alone leaves a tightly wound route as
  // open blocks, because marks say which squares are track, not how they join.
  bool solved = false;
};

// The frame and the hairlines alone: a board with nothing on it yet.
void drawFrame(toybox::Screen& screen, const Geometry& g) {
  const int16_t side = gridSide(g);
  // Frame knocked out: fill solid and paint the playing surface back over it.
  screen.target().fill(
      fui::makeRect(static_cast<int16_t>(g.gridX - kFrameW), static_cast<int16_t>(g.gridY - kFrameW),
                    static_cast<int16_t>(side + 2 * kFrameW), static_cast<int16_t>(side + 2 * kFrameW)),
      black());
  screen.target().fill(fui::makeRect(g.gridX, g.gridY, side, side), white());
}

void drawHairlines(toybox::Screen& screen, const Geometry& g) {
  const int16_t side = gridSide(g);
  for (int k = 1; k < g.n; ++k) {
    const int16_t offset = static_cast<int16_t>(k * g.cell);
    screen.target().fill(fui::makeRect(static_cast<int16_t>(g.gridX + offset), g.gridY, toybox::kHairline, side),
                         black());
    screen.target().fill(fui::makeRect(g.gridX, static_cast<int16_t>(g.gridY + offset), side, toybox::kHairline),
                         black());
  }
}

// While a hold is deciding, the board's frame changes too, since the square
// itself is under the thumb.
void drawHoldFrame(toybox::Screen& screen, const Geometry& g) {
  // A ring kRule thick, 3px outside the frame, in the station band where the
  // thumb does not cover it.
  const int16_t out = static_cast<int16_t>(kFrameW + 3 + toybox::kRule);
  const int16_t span = static_cast<int16_t>(gridSide(g) + 2 * out);
  const int16_t left = static_cast<int16_t>(g.gridX - out);
  const int16_t top = static_cast<int16_t>(g.gridY - out);
  const int16_t far = static_cast<int16_t>(span - toybox::kRule);
  screen.target().fill(fui::makeRect(left, top, span, toybox::kRule), black());
  screen.target().fill(fui::makeRect(left, static_cast<int16_t>(top + far), span, toybox::kRule), black());
  screen.target().fill(fui::makeRect(left, top, toybox::kRule, span), black());
  screen.target().fill(fui::makeRect(static_cast<int16_t>(left + far), top, toybox::kRule, span), black());
}

void drawSurface(toybox::Screen& screen, const Geometry& g, const tt::Puzzle& p, const tt::Board& b,
                 const Marks& marks) {
  drawFrame(screen, g);
  if (marks.holdCell >= 0) drawHoldFrame(screen, g);

  uint8_t shapes[tt::kMaxCells];
  if (marks.solved) {
    for (int i = 0; i < tt::kMaxCells; ++i) shapes[i] = p.solution[i];
  } else {
    tt::inferShapes(b, p, shapes);
  }

  for (int r = 0; r < g.n; ++r) {
    for (int c = 0; c < g.n; ++c) {
      const int i = tt::cellIndex(r, c);
      const fui::Rect box = cellRect(g, r, c);
      // A given is the board's own material: the dithered ground Sudoku gives
      // its clues, with its real shape drawn on it.
      if (p.given[i]) screen.target().fill(box, fui::Paint::dither(fui::Color::LightGray));
      if (b.cell[i] == tt::kTrack && b.pin[i] == tt::kPinUnknown && !marks.solved) {
        drawUnknown(screen, box);
      } else if (b.cell[i] == tt::kTrack) {
        drawRail(screen, box, shapes[i]);
      } else if (b.cell[i] == tt::kCross) {
        drawCross(screen, box);
      }
      if (marks.wrong != nullptr && marks.wrong[i]) drawWrong(screen, box);
      if (marks.hintCell == i) drawHintMark(screen, box);
      // A held square: its own grid lines drawn at the hint's weight all the
      // way round, while the hold is still deciding.
      if (marks.holdCell == i) {
        for (int side = 0; side < 4; ++side) edgeRun(screen, box, side, -1, static_cast<int16_t>(box.width + 2));
      }
    }
  }

  // Hairlines after the squares, so no square can overdraw a line.
  drawHairlines(screen, g);

  drawGate(screen, g, p.start);
  drawGate(screen, g, p.end);

  if (!marks.clues) return;
  for (int k = 0; k < g.n; ++k) {
    const fui::Rect rowBox = fui::makeRect(g.rowClueX, static_cast<int16_t>(g.gridY + k * g.cell), kClueW, g.cell);
    drawClue(screen, rowBox, p.rowClue[k], tt::clueState(tt::rowCount(b, p, k), p.rowClue[k]));
    const fui::Rect colBox = fui::makeRect(static_cast<int16_t>(g.gridX + k * g.cell), g.colClueY, g.cell, kClueH);
    drawClue(screen, colBox, p.colClue[k], tt::clueState(tt::colCount(b, p, k), p.colClue[k]));
  }
}

// A board of `n` squares centred in `room`, with or without its clue gutters.
// The play screen uses boardGeometry; this is for the pictures elsewhere.
Geometry fitGeometry(const fui::Rect& room, const int n, const bool clues) {
  Geometry g{};
  g.n = n;
  const int16_t left = clues ? kClueW : 0;
  const int16_t top = clues ? kClueH : 0;
  const int16_t pad = static_cast<int16_t>(kTab + kFrameW);
  const int16_t across = static_cast<int16_t>(room.width - left - 2 * pad);
  const int16_t down = static_cast<int16_t>(room.height - top - 2 * pad);
  g.cell = static_cast<int16_t>((across < down ? across : down) / n);
  if (g.cell > kMaxCell) g.cell = kMaxCell;
  const int16_t side = static_cast<int16_t>(n * g.cell);
  g.rowClueX = static_cast<int16_t>(room.x + (room.width - (left + 2 * pad + side)) / 2);
  g.gridX = static_cast<int16_t>(g.rowClueX + left + pad);
  g.colClueY = static_cast<int16_t>(room.y + (room.height - (top + 2 * pad + side)) / 2);
  g.gridY = static_cast<int16_t>(g.colClueY + top + pad);
  return g;
}

// "CLEAN WIN", "2 HINTS", "1 CHECK", "2 HINTS, 1 CHECK".
void helpText(const uint16_t hints, const uint16_t checks, char* out, const int size) {
  const unsigned h = hints;
  const unsigned c = checks;
  if (h == 0 && c == 0) {
    std::snprintf(out, static_cast<size_t>(size), "CLEAN WIN");
  } else if (c == 0) {
    std::snprintf(out, static_cast<size_t>(size), "%u HINT%s", h, h == 1 ? "" : "S");
  } else if (h == 0) {
    std::snprintf(out, static_cast<size_t>(size), "%u CHECK%s", c, c == 1 ? "" : "S");
  } else {
    std::snprintf(out, static_cast<size_t>(size), "%u HINT%s, %u CHECK%s", h, h == 1 ? "" : "S", c, c == 1 ? "" : "S");
  }
}

// The result on a solved board: the time large, the tally under it, centred
// in the band the rail's top row leaves empty. Plain ink on white, so NEW
// PUZZLE stays the only solid shape below the board: three renders put a
// solid capsule or block here, and either read as a second button.
void drawResult(toybox::Screen& screen, const Geometry& g, const BoardModel& model) {
  char clock[16];
  formatClock(model.elapsedMs, clock, sizeof(clock));
  char help[32];
  helpText(model.hints, model.checks, help, sizeof(help));
  const int16_t width = static_cast<int16_t>(screen.device().width - 2 * kSide);
  const int16_t top = g.statusY;
  const int16_t height = static_cast<int16_t>(g.controlsY + toybox::kPillHeight - top);
  // Stacked by ink, half the time's ink apart, the pair centred in the band.
  const int16_t gap = toybox::kDisplayCut.inkHeight / 2;
  const int16_t stack = static_cast<int16_t>(toybox::kDisplayCut.inkHeight + gap + toybox::kUiCut.inkHeight);
  const int16_t inkTop = static_cast<int16_t>(top + (height - stack) / 2);
  fui::TextStyle big;
  big.font = toybox::kDisplayFont;
  big.align = fui::TextAlign::Center;
  fui::TextStyle small = big;
  small.font = toybox::kBodyFont;
  const fui::Rect timeBand = fui::makeRect(kSide, inkTop, width, toybox::kDisplayCut.inkHeight);
  const fui::Rect helpBand = fui::makeRect(kSide, static_cast<int16_t>(inkTop + toybox::kDisplayCut.inkHeight + gap),
                                           width, toybox::kUiCut.inkHeight);
  screen.target().text(toybox::inkCentred(timeBand, toybox::kDisplayCut), clock, big);
  // Never cut: a long tally steps down to the small cut rather than lose words.
  if (screen.target().measureText(small.font, help, small).width > width) {
    small.font = toybox::kTileFont;
    screen.target().text(toybox::inkCentred(helpBand, toybox::kTileCut), help, small);
    return;
  }
  screen.target().text(toybox::inkCentred(helpBand, toybox::kUiCut), help, small);
}

void drawStatus(toybox::Screen& screen, const Geometry& g, const BoardModel& model) {
  char text[64] = "";
  if (model.solved) {
    drawResult(screen, g, model);
    return;
  }
  if (model.notice != nullptr) {
    std::snprintf(text, sizeof(text), "%s", model.notice);
  }
  // Blank while solving: the clues already say how each line stands.
  if (text[0] == '\0') return;
  fui::TextStyle body;
  body.font = toybox::kBodyFont;
  body.align = fui::TextAlign::Center;
  const fui::Rect band =
      fui::makeRect(kSide, g.statusY, static_cast<int16_t>(screen.device().width - 2 * kSide), kStatusH);
  // Never cut: a long tally steps down to the small cut rather than lose words.
  if (screen.target().measureText(body.font, text, body).width > band.width) {
    body.font = toybox::kTileFont;
    screen.target().text(toybox::inkCentred(band, toybox::kTileCut), text, body);
    return;
  }
  screen.target().text(toybox::inkCentred(band, toybox::kUiCut), text, body);
}

// Registers a button, and if it sits on the bottom margin, lets its tap area
// run on down to the panel's edge. A control against an edge should own the
// edge, since a finger aimed at it cannot overshoot; and on the Picco, taps
// aimed at the bottom row read 781 to 794 against a drawn 732 to 784, so
// without this its bottom row needed several tries. Only the hit area grows.
void placeButton(toybox::Screen& screen, fui::ButtonProps props, const fui::Rect& rect) {
  const int16_t edge = screen.device().height;
  if (rect.bottom() >= edge - toybox::kMargin - 1 && rect.bottom() < edge) {
    props.hitPadding.bottom = static_cast<int16_t>(edge - rect.bottom());
  }
  screen.button(props, rect);
}

void button(toybox::Screen& screen, const char* label, const fui::ActionId action, const bool enabled,
            const fui::Rect& rect, const int16_t value = 0) {
  fui::ButtonProps props;
  props.label = label;
  props.styles = enabled ? toybox::rowStyles() : toybox::disabledStepperStyles();
  if (enabled) {
    props.action = action;
    props.value = value;
  }
  placeButton(screen, props, rect);
}

void drawControls(toybox::Screen& screen, const Geometry& g, const BoardModel& model) {
  // The buttons keep the page margin the front door's do. Only the board runs
  // nearly to the edge, because every pixel of it is grid.
  const int16_t left = toybox::kMargin;
  const int16_t width = static_cast<int16_t>(screen.device().width - 2 * left);
  const int16_t gap = 8;
  const int16_t top = g.controlsY;
  const int16_t thumbs = static_cast<int16_t>(top + toybox::kPillHeight + gap);

  if (model.solved) {
    // Where the thumb rests on the rail, so the finished board does not move.
    // The one way on; Back reaches the same screen.
    fui::ButtonProps next;
    next.label = "NEW PUZZLE";
    next.action = ActionNewGame;
    placeButton(screen, next, fui::makeRect(left, thumbs, width, kThumbRow));
    return;
  }

  const bool live = model.board != nullptr;
  const bool undo = live && tt::canUndo(*model.board);
  const bool redo = live && tt::canRedo(*model.board);

  // The thumb rail: CHECK / HINT / MENU, then UNDO and REDO tall on the bottom
  // margin, where the thumb rests and where the moves come from most often.
  const int16_t small = static_cast<int16_t>((width - 2 * gap) / 3);
  button(screen, "CHECK", ActionCheck, live, fui::makeRect(left, top, small, toybox::kPillHeight));
  button(screen, "HINT", ActionHint, live,
         fui::makeRect(static_cast<int16_t>(left + small + gap), top, small, toybox::kPillHeight));
  button(screen, "MENU", ActionMenu, true,
         fui::makeRect(static_cast<int16_t>(left + 2 * (small + gap)), top, small, toybox::kPillHeight));
  const int16_t big = static_cast<int16_t>((width - gap) / 2);
  button(screen, "UNDO", ActionUndo, undo, fui::makeRect(left, thumbs, big, kThumbRow));
  button(screen, "REDO", ActionRedo, redo,
         fui::makeRect(static_cast<int16_t>(left + big + gap), thumbs, big, kThumbRow));
}

// ---------------------------------------------------------------------------
// The front door.

// A game at a glance, bracketed as the chess board is: the app's own material
// carrying the player's own data. `game` is null only before the first puzzle
// has ever been dealt, and then it is the empty board an Easy deal starts on.
void drawOrnament(toybox::Screen& screen, const fui::Rect& room, const tt::Game* game, const int emptySize,
                  const bool brackets = true) {
  const int n = game != nullptr ? game->puzzle.size : emptySize;
  const int16_t reach = brackets ? 14 : 0;
  const fui::Rect inner =
      fui::makeRect(static_cast<int16_t>(room.x + reach), static_cast<int16_t>(room.y + reach),
                    static_cast<int16_t>(room.width - 2 * reach), static_cast<int16_t>(room.height - 2 * reach));
  const Geometry g = fitGeometry(inner, n, false);
  const int16_t side = gridSide(g);
  const int16_t pad = static_cast<int16_t>(kTab + kFrameW);
  if (brackets) {
    toybox::bracket(
        screen,
        fui::makeRect(static_cast<int16_t>(g.gridX - pad - reach), static_cast<int16_t>(g.gridY - pad - reach),
                      static_cast<int16_t>(side + 2 * (pad + reach)), static_cast<int16_t>(side + 2 * (pad + reach))),
        20, toybox::kRule);
  }
  if (game != nullptr) {
    Marks marks;
    marks.clues = false;
    marks.solved = game->solved;
    drawSurface(screen, g, game->puzzle, game->board, marks);
    return;
  }
  drawFrame(screen, g);
  drawHairlines(screen, g);
}

// EASY / MEDIUM / HARD, the selected one inverted, as one row of `band`.
void levelRow(toybox::Screen& screen, const fui::Rect& band, const tt::Level selected) {
  const int16_t gap = 8;
  const int16_t each = static_cast<int16_t>((band.width - 2 * gap) / 3);
  for (int k = 0; k < tt::kLevelCount; ++k) {
    const tt::Level level = static_cast<tt::Level>(k);
    fui::ButtonProps props;
    props.label = levelName(level);
    props.action = ActionLevel;
    props.value = static_cast<int16_t>(k);
    props.styles = level == selected ? toybox::invertedStyles() : toybox::rowStyles();
    placeButton(screen, props,
                fui::makeRect(static_cast<int16_t>(band.x + k * (each + gap)), band.y, each, band.height));
  }
}

void textLine(toybox::Screen& screen, const fui::Rect& band, const char* text) {
  fui::TextStyle body;
  body.font = toybox::kBodyFont;
  body.align = fui::TextAlign::Left;
  screen.target().text(toybox::inkCentred(band, toybox::kUiCut), text, body);
}

// A state line, a rule, and the record line under it, reserved: per-level
// records come with stats, and the space is held now so they arrive without
// moving anything.
void stateAndRecord(toybox::Screen& screen, const char* state) {
  textLine(screen, screen.takeTop(34), state);
  const fui::Rect rule = screen.takeTop(toybox::kRule + 12);
  screen.target().fill(fui::makeRect(rule.x, static_cast<int16_t>(rule.y + 6), rule.width, toybox::kRule), black());
  screen.takeTop(26);
}

// PLAY PUZZLE. While the puzzle it would deal is still being laid it says so,
// dimmed, rather than the screen looking frozen.
void playButton(toybox::Screen& screen, const fui::Rect& rect, const bool ready) {
  fui::ButtonProps play;
  if (ready) {
    play.label = "PLAY PUZZLE";
    play.action = ActionPlayPreview;
  } else {
    play.label = "LAYING TRACK";
    play.styles = toybox::disabledStepperStyles();
  }
  placeButton(screen, play, rect);
}

}  // namespace

// ---------------------------------------------------------------------------
// Public.

const char* levelName(const tt::Level level) {
  switch (level) {
    case tt::Level::Easy:
      return "EASY";
    case tt::Level::Medium:
      return "MEDIUM";
    case tt::Level::Hard:
      return "HARD";
  }
  return "";
}

void formatClock(const uint32_t ms, char* out, const int size) {
  const uint32_t seconds = ms / 1000;
  const uint32_t hours = seconds / 3600;
  if (hours > 0) {
    std::snprintf(out, static_cast<size_t>(size), "%u:%02u:%02u", static_cast<unsigned>(hours),
                  static_cast<unsigned>((seconds / 60) % 60), static_cast<unsigned>(seconds % 60));
    return;
  }
  std::snprintf(out, static_cast<size_t>(size), "%u:%02u", static_cast<unsigned>(seconds / 60),
                static_cast<unsigned>(seconds % 60));
}

Geometry boardGeometry(const fui::DeviceContext& device, const int n) {
  Geometry g{};
  g.n = n;
  const int16_t width = device.width;
  const int16_t height = device.height;
  const int16_t room = static_cast<int16_t>(width - 2 * kSide - kClueW - 2 * kTab - 2 * kFrameW);
  g.cell = static_cast<int16_t>(room / n);
  if (g.cell > kMaxCell) g.cell = kMaxCell;
  const int16_t side = static_cast<int16_t>(n * g.cell);
  const int16_t block = static_cast<int16_t>(kClueW + 2 * kTab + 2 * kFrameW + side);
  g.rowClueX = static_cast<int16_t>(kSide + (width - 2 * kSide - block) / 2);
  g.gridX = static_cast<int16_t>(g.rowClueX + kClueW + kTab + kFrameW);

  const int16_t top = static_cast<int16_t>(toybox::kChromeHeight + toybox::kGutter);
  const int16_t bottom = static_cast<int16_t>(height - toybox::kMargin);
  g.colClueY = top;
  g.gridY = static_cast<int16_t>(top + kClueH + kTab + kFrameW);
  g.statusY = static_cast<int16_t>(g.gridY + side + kFrameW + kTab + kStatusGap);
  g.controlsY = static_cast<int16_t>(bottom - kThumbRow - 8 - toybox::kPillHeight);
  return g;
}

fui::Rect cellRect(const Geometry& g, const int row, const int col) {
  return fui::makeRect(static_cast<int16_t>(g.gridX + col * g.cell), static_cast<int16_t>(g.gridY + row * g.cell),
                       g.cell, g.cell);
}

bool cellAt(const Geometry& g, const int x, const int y, int& row, int& col) {
  if (x < g.gridX || y < g.gridY) return false;
  const int c = (x - g.gridX) / g.cell;
  const int r = (y - g.gridY) / g.cell;
  if (c >= g.n || r >= g.n) return false;
  row = r;
  col = c;
  return true;
}

void buildBoard(toybox::Screen& screen, const BoardModel& model) {
  // "TRAIN TRACKS" beside a level does not fit the display cut, and the band
  // would step the title down to the small one on this screen alone. So the
  // name stands alone, as MINESWEEPER and KNUCKLEBONES do; the board's size
  // says the level, and SOLVED, which is short, carries it.
  toyboxChrome(screen, model.solved ? "SOLVED" : "TRAIN TRACKS", model.solved ? levelName(model.level) : nullptr);
  if (model.puzzle == nullptr || model.board == nullptr) return;
  const Geometry g = boardGeometry(screen.device(), model.puzzle->size);
  Marks marks;
  marks.solved = model.solved;
  // A finished board wears no marks: it is the picture of what you did.
  if (!model.solved) {
    marks.wrong = model.wrong;
    marks.hintCell = model.hintCell;
    marks.holdCell = model.holdCell;
  }
  drawSurface(screen, g, *model.puzzle, *model.board, marks);
  drawStatus(screen, g, model);
  drawControls(screen, g, model);
}

// The front door while a game is unfinished: the game, and the two things to
// do about it. With nothing unfinished the new-puzzle screen is the front door
// instead.
void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  const tt::Game* game = model.game;
  // The name alone, as on the board; the level moves into the state line.
  toyboxChrome(screen, "TRAIN TRACKS", nullptr);
  char state[40] = "IN PROGRESS";
  if (game != nullptr) {
    char clock[16];
    formatClock(game->elapsedMs, clock, sizeof(clock));
    std::snprintf(state, sizeof(state), "%s IN PROGRESS, %s", levelName(game->level), clock);
  }
  stateAndRecord(screen, state);

  button(screen, "NEW PUZZLE", ActionNewGame, true, screen.takeBottom(toybox::kPillHeight, 8));
  fui::ButtonProps play;
  play.label = "CONTINUE";
  play.action = ActionPlay;
  placeButton(screen, play, screen.takeBottom(kThumbRow, toybox::kGutter));
  const fui::Rect room = screen.body();
  drawOrnament(screen, room, game, tt::spec(tt::Level::Easy).size);
  if (game != nullptr) screen.frame().hit(room, ActionPlay, 0);
}

// Pick a difficulty, see the puzzle it dealt, play it. The preview fills the
// page; the choice and the go sit on the bottom margin, where the thumb is.
void buildNewGame(toybox::Screen& screen, const NewGameModel& model) {
  toyboxChrome(screen, "NEW PUZZLE", nullptr);
  const bool ready = model.preview != nullptr;
  button(screen, "HOW TO PLAY", ActionHowTo, true, screen.takeBottom(toybox::kPillHeight, 8));
  playButton(screen, screen.takeBottom(kThumbRow, 8), ready);
  levelRow(screen, screen.takeBottom(toybox::kPillHeight, toybox::kGutter), model.selected);
  const fui::Rect room = screen.body();
  // No brackets: this is the puzzle itself, not an ornament standing for it.
  drawOrnament(screen, room, model.preview, tt::spec(model.selected).size, false);
  if (ready) screen.frame().hit(room, ActionPlayPreview, 0);
}

// ---------------------------------------------------------------------------
// How to play.

namespace {

// A lesson is a sentence set at the UI cut above the board and a second one
// under it, each broken by hand where it reads best rather than where the
// width runs out, and a 4x4 board drawn by the same code as the real one, with
// the same given on every page. Faces are 16
// characters, row by row: '.' empty, 'T' track, 'x' cross, '?' track with its
// piece undecided, 'G' a given.
struct Lesson {
  const char* lines[3];  // above the board; nullptr after the last
  const char* under[2];  // below it, at the same size; nullptr after the last
  const char* face;
};

const Lesson kLessons[] = {
    {{"CONNECT THE STATIONS", "WITH A CONTINUOUS SET", "OF TRAIN TRACKS"},
     {"NUMBERS INDICATE TRACKS", "PER COLUMN OR ROW"},
     ".TT."
     "TGT."
     "..TT"
     "...."},
    {{"TAP TO LAY TRACK,", "AGAIN TO CHANGE IT", nullptr},
     {"HOLD TO CLEAR", nullptr},
     ".TT."
     "TG.."
     "...."
     "...."},
    {{"HOLD EMPTY BOX", "TO CROSS OUT", nullptr},
     {"TAP AGAIN TO MARK ?", "AS POSSIBLE TRACK"},
     // Crossed where the numbers say: column 1's track is the station square,
     // and row 4 has none.
     "x?T."
     "TG?."
     "x..."
     "xxxx"},
    {{"OUTLINED NUMBER", "MEANS MET", nullptr},
     {"FILLED NUMBER", "MEANS TOO MANY"},
     ".TT."
     "TGT."
     "...."
     ".T.."},
};
constexpr int kLessonCount = static_cast<int>(sizeof(kLessons) / sizeof(kLessons[0]));

// The example: in from the west at row 1, out to the east at row 2.
void lessonPuzzle(tt::Puzzle& p, tt::Board& b, const char* face) {
  const uint8_t path[] = {
      static_cast<uint8_t>(tt::cellIndex(1, 0)), static_cast<uint8_t>(tt::cellIndex(1, 1)),
      static_cast<uint8_t>(tt::cellIndex(0, 1)), static_cast<uint8_t>(tt::cellIndex(0, 2)),
      static_cast<uint8_t>(tt::cellIndex(1, 2)), static_cast<uint8_t>(tt::cellIndex(2, 2)),
      static_cast<uint8_t>(tt::cellIndex(2, 3)),
  };
  tt::buildFromPath(p, 4, path, static_cast<int>(sizeof(path)), tt::kWest, tt::kEast);
  b = tt::Board{};
  for (int k = 0; k < 16; ++k) {
    const int i = tt::cellIndex(k / 4, k % 4);
    switch (face[k]) {
      case 'G':
        p.given[i] = p.solution[i];
        b.cell[i] = tt::kTrack;
        break;
      case 'T':
        b.cell[i] = tt::kTrack;
        break;
      case 'x':
        b.cell[i] = tt::kCross;
        break;
      case '?':
        b.cell[i] = tt::kTrack;
        b.pin[i] = tt::kPinUnknown;
        break;
      default:
        break;
    }
  }
}

}  // namespace

int howToPages() { return kLessonCount; }

void buildHowTo(toybox::Screen& screen, const HowToModel& model) {
  const int page = model.page < 0 ? 0 : (model.page >= kLessonCount ? kLessonCount - 1 : model.page);
  const Lesson& lesson = kLessons[page];
  char progress[toybox::kOfCounterChars];
  std::snprintf(progress, sizeof(progress), "%d OF %d", page + 1, kLessonCount);
  toyboxChrome(screen, "HOW TO PLAY", progress);

  // Taken before the page's own drawing, so no branch can skip the way forward.
  fui::ButtonProps next;
  next.label = page + 1 < kLessonCount ? "NEXT" : "GOT IT";
  next.action = ActionHowToNext;
  placeButton(screen, next, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));

  // The sentences, one line per row in the UI cut, all stepping down to the
  // small cut together if any line would not fit: two sizes on one page would
  // read as two kinds of thing.
  fui::TextStyle body;
  body.font = toybox::kBodyFont;
  body.align = fui::TextAlign::Center;
  const int16_t width = screen.body().width;
  bool fits = true;
  for (const Lesson& any : kLessons) {
    for (const char* line : any.lines) {
      if (line != nullptr && screen.target().measureText(body.font, line, body).width > width) fits = false;
    }
    for (const char* line : any.under) {
      if (line != nullptr && screen.target().measureText(body.font, line, body).width > width) fits = false;
    }
  }
  if (!fits) body.font = toybox::kTileFont;
  const toybox::CutMetrics& cut = fits ? toybox::kUiCut : toybox::kTileCut;

  // Every page reserves the same room above and below its board, sized for
  // the fullest page, so the board stands still while the pages turn and only
  // the marks on it change.
  int16_t above = 0;
  int16_t below = 0;
  for (const Lesson& any : kLessons) {
    int16_t lines = 0;
    for (const char* line : any.lines) lines = static_cast<int16_t>(lines + (line != nullptr));
    int16_t under = 0;
    for (const char* line : any.under) under = static_cast<int16_t>(under + (line != nullptr));
    above = std::max<int16_t>(above, static_cast<int16_t>(lines * cut.lineHeight));
    below = std::max<int16_t>(below, static_cast<int16_t>(under * cut.lineHeight));
  }

  screen.takeTop(toybox::kGutter);
  const fui::Rect top = screen.takeTop(above);
  const fui::Rect foot = screen.takeBottom(below, toybox::kGutter);
  int16_t y = top.y;
  for (const char* line : lesson.lines) {
    if (line == nullptr) break;
    screen.target().text(toybox::inkCentred(fui::makeRect(top.x, y, top.width, cut.lineHeight), cut), line, body);
    y = static_cast<int16_t>(y + cut.lineHeight);
  }
  // Under the board, the second sentence from the top of the reserve.
  y = foot.y;
  for (const char* line : lesson.under) {
    if (line == nullptr) break;
    screen.target().text(toybox::inkCentred(fui::makeRect(foot.x, y, foot.width, cut.lineHeight), cut), line, body);
    y = static_cast<int16_t>(y + cut.lineHeight);
  }

  tt::Puzzle puzzle;
  tt::Board board;
  lessonPuzzle(puzzle, board, lesson.face);
  screen.takeTop(toybox::kGutter);
  const Geometry g = fitGeometry(screen.body(), 4, true);
  drawSurface(screen, g, puzzle, board, Marks{});
}

}  // namespace traintracksui

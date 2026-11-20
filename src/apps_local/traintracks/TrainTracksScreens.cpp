#include "TrainTracksScreens.h"

#include <cstdio>

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

constexpr int16_t kSide = 6;      // page margin left and right of the board
constexpr int16_t kClueW = 38;    // row-clue gutter: "10" is 33px in the UI cut
constexpr int16_t kClueH = 34;    // column-clue band
constexpr int16_t kTab = 10;      // gate arrows live in this band outside the frame
constexpr int16_t kMaxCell = 64;  // a 6x6 should still read as a board, not as tiles
constexpr int16_t kStatusH = 34;
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

// The rail through a square, from its connection mask.
//
//   two opposite connections   a straight, with two sleepers
//   two adjacent connections   a quarter-circle curve
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
  // No sleeper on a curve: a short bar across a diagonal reads as the cross
  // that marks a square empty.
  for (int step = 0; step < 6; ++step) line(screen, arcPoint(step, half), arcPoint(step + 1, half), toybox::kRule);
}

void drawCross(toybox::Screen& screen, const fui::Rect& box) {
  const Pt c = centreOf(box);
  const int16_t arm = static_cast<int16_t>(box.width * 16 / 100);
  line(screen, Pt{static_cast<int16_t>(c.x - arm), static_cast<int16_t>(c.y - arm)},
       Pt{static_cast<int16_t>(c.x + arm), static_cast<int16_t>(c.y + arm)}, toybox::kRule);
  line(screen, Pt{static_cast<int16_t>(c.x - arm), static_cast<int16_t>(c.y + arm)},
       Pt{static_cast<int16_t>(c.x + arm), static_cast<int16_t>(c.y - arm)}, toybox::kRule);
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
  const int16_t chipW = clue >= 10 ? 40 : 30;
  const int16_t chipH = 30;
  const fui::Rect chip = fui::makeRect(static_cast<int16_t>(box.x + (box.width - chipW) / 2),
                                       static_cast<int16_t>(box.y + (box.height - chipH) / 2), chipW, chipH);
  if (state == tt::ClueState::Over) {
    screen.target().fill(chip, black());
    digit.color = fui::Color::White;
  } else if (state == tt::ClueState::Met) {
    screen.target().stroke(chip, black(), 2);
  }
  screen.target().text(toybox::inkCentred(box, toybox::kUiCut), text, digit);
}

void drawGate(toybox::Screen& screen, const Geometry& g, const tt::Gate& gate, const bool entering) {
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
  // The arrow, in the tab band outside the frame: the start points in, the end
  // points out, so the two gates say which way the line runs.
  const Pt inner = toward(mid, s, kFrameW);
  const Pt outer = toward(mid, s, static_cast<int16_t>(kFrameW + kTab));
  const Pt tip = entering ? inner : outer;
  const Pt base = entering ? outer : inner;
  const int16_t wing = 8;
  const int perp = (s + 1) & 3;
  const Pt a = toward(base, perp, wing);
  const Pt b = toward(base, perp, static_cast<int16_t>(-wing));
  screen.target().triangle(fui::Point{tip.x, tip.y}, fui::Point{a.x, a.y}, fui::Point{b.x, b.y}, black());
}

void drawBoard(toybox::Screen& screen, const Geometry& g, const BoardModel& model) {
  const tt::Puzzle& p = *model.puzzle;
  const tt::Board& b = *model.board;
  const int16_t side = gridSide(g);

  // Frame knocked out: fill solid and paint the playing surface back over it.
  screen.target().fill(
      fui::makeRect(static_cast<int16_t>(g.gridX - kFrameW), static_cast<int16_t>(g.gridY - kFrameW),
                    static_cast<int16_t>(side + 2 * kFrameW), static_cast<int16_t>(side + 2 * kFrameW)),
      black());
  screen.target().fill(fui::makeRect(g.gridX, g.gridY, side, side), white());

  uint8_t shapes[tt::kMaxCells];
  tt::inferShapes(b, p, shapes);

  for (int r = 0; r < g.n; ++r) {
    for (int c = 0; c < g.n; ++c) {
      const int i = tt::cellIndex(r, c);
      const fui::Rect box = cellRect(g, r, c);
      // A given is the board's own material: the dithered ground Sudoku gives
      // its clues, with its real shape drawn on it.
      if (p.given[i]) screen.target().fill(box, fui::Paint::dither(fui::Color::LightGray));
      if (b.cell[i] == tt::kTrack) {
        drawRail(screen, box, shapes[i]);
      } else if (b.cell[i] == tt::kCross) {
        drawCross(screen, box);
      }
    }
  }

  // Hairlines after the squares, so no square can overdraw a line.
  for (int k = 1; k < g.n; ++k) {
    const int16_t offset = static_cast<int16_t>(k * g.cell);
    screen.target().fill(fui::makeRect(static_cast<int16_t>(g.gridX + offset), g.gridY, toybox::kHairline, side),
                         black());
    screen.target().fill(fui::makeRect(g.gridX, static_cast<int16_t>(g.gridY + offset), side, toybox::kHairline),
                         black());
  }

  drawGate(screen, g, p.start, true);
  drawGate(screen, g, p.end, false);

  for (int k = 0; k < g.n; ++k) {
    const fui::Rect rowBox = fui::makeRect(g.rowClueX, static_cast<int16_t>(g.gridY + k * g.cell), kClueW, g.cell);
    drawClue(screen, rowBox, p.rowClue[k], tt::clueState(tt::rowCount(b, p, k), p.rowClue[k]));
    const fui::Rect colBox = fui::makeRect(static_cast<int16_t>(g.gridX + k * g.cell), g.colClueY, g.cell, kClueH);
    drawClue(screen, colBox, p.colClue[k], tt::clueState(tt::colCount(b, p, k), p.colClue[k]));
  }
}

void drawStatus(toybox::Screen& screen, const Geometry& g, const BoardModel& model) {
  char text[40];
  if (model.generating) {
    std::snprintf(text, sizeof(text), "LAYING TRACK");
  } else if (model.solved) {
    std::snprintf(text, sizeof(text), "SOLVED");
  } else {
    int laid = 0;
    for (int i = 0; i < tt::kMaxCells; ++i) laid += model.board->cell[i] == tt::kTrack;
    std::snprintf(text, sizeof(text), "%d OF %d LAID", laid, tt::totalTrack(*model.puzzle));
  }
  fui::TextStyle body;
  body.font = toybox::kBodyFont;
  body.align = fui::TextAlign::Center;
  const fui::Rect band =
      fui::makeRect(kSide, g.statusY, static_cast<int16_t>(screen.device().width - 2 * kSide), kStatusH);
  screen.target().text(toybox::inkCentred(band, toybox::kUiCut), text, body);
}

void button(toybox::Screen& screen, const char* label, const fui::ActionId action, const bool enabled,
            const fui::Rect& rect) {
  fui::ButtonProps props;
  props.label = label;
  props.styles = enabled ? toybox::rowStyles() : toybox::disabledStepperStyles();
  if (enabled) props.action = action;
  screen.button(props, rect);
}

void drawControls(toybox::Screen& screen, const Geometry& g, const BoardModel& model) {
  const bool live = !model.generating && model.board != nullptr;
  const bool undo = live && tt::canUndo(*model.board);
  const bool redo = live && tt::canRedo(*model.board);
  const bool play = live && !model.solved;
  const int16_t width = static_cast<int16_t>(screen.device().width - 2 * kSide);
  const int16_t gap = 8;

  // The thumb rail: CHECK / HINT / MENU, then UNDO and REDO tall on the bottom
  // margin, where the thumb rests and where the moves come from most often.
  const int16_t small = static_cast<int16_t>((width - 2 * gap) / 3);
  const int16_t top = g.controlsY;
  button(screen, "CHECK", ActionCheck, play, fui::makeRect(kSide, top, small, toybox::kPillHeight));
  button(screen, "HINT", ActionHint, play,
         fui::makeRect(static_cast<int16_t>(kSide + small + gap), top, small, toybox::kPillHeight));
  button(screen, "MENU", ActionMenu, true,
         fui::makeRect(static_cast<int16_t>(kSide + 2 * (small + gap)), top, small, toybox::kPillHeight));
  const int16_t big = static_cast<int16_t>((width - gap) / 2);
  const int16_t thumbs = static_cast<int16_t>(top + toybox::kPillHeight + gap);
  button(screen, "UNDO", ActionUndo, undo, fui::makeRect(kSide, thumbs, big, kThumbRow));
  button(screen, "REDO", ActionRedo, redo,
         fui::makeRect(static_cast<int16_t>(kSide + big + gap), thumbs, big, kThumbRow));
}

}  // namespace

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
  g.statusY = static_cast<int16_t>(g.gridY + side + kFrameW + kTab);
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
  toyboxChrome(screen, "TRAIN TRACKS", levelName(model.level));
  if (model.puzzle == nullptr || model.board == nullptr) return;
  // While generating, the puzzle is not there yet; the level says how big it
  // will be, so the controls are already where they will stay.
  const int n = model.generating ? tt::spec(model.level).size : model.puzzle->size;
  const Geometry g = boardGeometry(screen.device(), n);
  if (!model.generating) drawBoard(screen, g, model);
  drawStatus(screen, g, model);
  drawControls(screen, g, model);
}

}  // namespace traintracksui

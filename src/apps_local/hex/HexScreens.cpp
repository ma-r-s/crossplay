#include "HexScreens.h"

#include <cmath>
#include <cstdio>

#include "../link/LinkScreens.h"

namespace hexui {

namespace {

// A hexagon, as six vertices around a centre:
//
//        v4 ____ v5              flat top and bottom, points left and right.
//         /      \               Flat-top is not a style choice: it is what
//     v3 <        > v0           makes the rhombus run corner to corner down a
//         \______/               PORTRAIT panel. The conventional pointy-top
//        v2      v1              drawing is width-bound, so on 480x800 it
//                                wastes most of the glass -- the same board
// comes out at a 28px cell drawn wide against 48px drawn this way.
//
// The board stays axis-aligned. A ~57 degree rotation is the true best fit and
// buys about twelve per cent, which does not pay for the stair-stepping every
// edge would pick up on a one-bit panel.
constexpr int kVertexCount = 6;

void hexagonVertices(const Layout& layout, const int16_t cx, const int16_t cy, fui::Point out[kVertexCount]) {
  const int16_t a = layout.a;
  const int16_t h = layout.h;
  out[0] = fui::Point{static_cast<int16_t>(cx + 2 * a), cy};
  out[1] = fui::Point{static_cast<int16_t>(cx + a), static_cast<int16_t>(cy + h)};
  out[2] = fui::Point{static_cast<int16_t>(cx - a), static_cast<int16_t>(cy + h)};
  out[3] = fui::Point{static_cast<int16_t>(cx - 2 * a), cy};
  out[4] = fui::Point{static_cast<int16_t>(cx - a), static_cast<int16_t>(cy - h)};
  out[5] = fui::Point{static_cast<int16_t>(cx + a), static_cast<int16_t>(cy - h)};
}

// Four triangles fanned from v0. `triangle()` is the only arbitrary-shape fill
// a screen builder has -- there is no circle and no n-gon in DrawTarget -- so a
// hexagon is composed the way toybox::disc composes a circle out of fills.
void fillHexagon(toybox::Screen& screen, const fui::Point v[kVertexCount], const fui::Paint& paint) {
  for (int i = 1; i + 1 < kVertexCount; ++i) screen.target().triangle(v[0], v[i], v[i + 1], paint);
}

void outlineHexagon(toybox::Screen& screen, const fui::Point v[kVertexCount], const int16_t weight) {
  const fui::Paint ink = fui::Paint::solid(fui::Color::Black);
  for (int i = 0; i < kVertexCount; ++i) {
    screen.target().line(v[i], v[(i + 1) % kVertexCount], static_cast<uint8_t>(weight), ink);
  }
}

// How far a border strip reaches outside the board. A quarter of the vertical
// pitch, which is about a sixth of a cell: enough to read as an edge, and small
// enough that the whole board plus its four strips still fits the panel at the
// largest hexagon the width allows.
int16_t borderDepth(const int16_t h) { return static_cast<int16_t>(h / 4 + 2); }

int16_t isqrt16(const int value) {
  if (value <= 0) return 0;
  int root = 1;
  while ((root + 1) * (root + 1) <= value) ++root;
  return static_cast<int16_t>(root);
}

// A stone, drawn the way the design language says a light shape has to be: the
// silhouette knocked out in ink first, then the face, or the hexagon's own
// outline shows through a white stone and the two colours stop being two
// colours. Verbatim Go's idiom, so a Hex stone and a Go stone are the same
// object on the same shelf.
void stone(toybox::Screen& screen, const int16_t cx, const int16_t cy, const int16_t radius, const uint8_t colour) {
  if (radius < 3) return;
  toybox::disc(screen, cx, cy, radius, fui::Color::Black);
  toybox::disc(screen, cx, cy, static_cast<int16_t>(radius - 3),
               colour == hex::kBlack ? fui::Color::Black : fui::Color::White);
}

// The mark on the stone just played, and on the stones of a winning chain. On a
// board of identical discs there is no other way to see what moved.
void stoneMark(toybox::Screen& screen, const int16_t cx, const int16_t cy, const int16_t radius, const uint8_t colour) {
  const fui::Color ink = colour == hex::kBlack ? fui::Color::White : fui::Color::Black;
  const int16_t outer = static_cast<int16_t>(radius * 8 / 22 + 2);
  const int16_t inner = static_cast<int16_t>(radius * 5 / 22 + 1);
  toybox::disc(screen, cx, cy, outer, ink);
  toybox::disc(screen, cx, cy, inner, colour == hex::kBlack ? fui::Color::Black : fui::Color::White);
}

// Which player owns the border an off-board neighbour sits past. Black owns the
// two ROW borders and White the two COLUMN ones, so the direction that ran out
// of range is the whole answer -- and at a corner, where both ran out, the row
// is taken first so the two strips meet rather than overlap.
uint8_t borderOwner(const int cell, const int dir) {
  const int row = hex::rowOf(cell) + hex::kNeighbourRow[dir];
  if (row < 0 || row >= hex::kSize) return hex::kBlack;
  return hex::kWhite;
}

// The strip outside one border edge: the edge itself, pushed outward by `depth`
// along the line to the neighbour that is not there. Black's strips are solid
// ink and White's are paper with a rail along the outside, which is the same
// filled-versus-outlined pair the stones use -- so a player reads which edges
// are theirs from the same language as the piece in their hand.
void borderStrip(toybox::Screen& screen, const Layout& layout, const int cell, const int dir, const int16_t depth) {
  int16_t cx = 0;
  int16_t cy = 0;
  cellCentre(layout, cell, cx, cy);
  fui::Point v[kVertexCount];
  hexagonVertices(layout, cx, cy, v);

  const fui::Point from = v[dir];
  const fui::Point to = v[(dir + 1) % kVertexCount];
  // The outward direction IS the vector to the missing neighbour's centre, so
  // the strip cannot drift away from the edge it belongs to.
  const double dx = 3.0 * layout.a * hex::kNeighbourCol[dir];
  const double dy = 2.0 * layout.h * hex::kNeighbourRow[dir] + static_cast<double>(layout.h) * hex::kNeighbourCol[dir];
  const double length = std::sqrt(dx * dx + dy * dy);
  const int16_t ox = static_cast<int16_t>(dx * depth / length);
  const int16_t oy = static_cast<int16_t>(dy * depth / length);
  const fui::Point outFrom{static_cast<int16_t>(from.x + ox), static_cast<int16_t>(from.y + oy)};
  const fui::Point outTo{static_cast<int16_t>(to.x + ox), static_cast<int16_t>(to.y + oy)};

  const bool black = borderOwner(cell, dir) == hex::kBlack;
  const fui::Paint paint = fui::Paint::solid(black ? fui::Color::Black : fui::Color::White);
  screen.target().triangle(from, to, outTo, paint);
  screen.target().triangle(from, outTo, outFrom, paint);
  if (black) return;
  // Only the OUTER rail. The inner one is the hexagon's own outline, drawn
  // after every strip, so two adjacent white border cells join into one
  // continuous band instead of a ladder with a rung between them.
  screen.target().line(outFrom, outTo, static_cast<uint8_t>(toybox::kHairline), fui::Paint::solid(fui::Color::Black));
}

void drawBoard(toybox::Screen& screen, const Layout& layout, const hex::Game& game, const uint8_t* chain,
               const bool markLast) {
  const int16_t depth = borderDepth(layout.h);
  for (int cell = 0; cell < hex::kCells; ++cell) {
    for (int dir = 0; dir < 6; ++dir) {
      if (hex::neighbour(cell, dir) != hex::kNoCell) continue;
      borderStrip(screen, layout, cell, dir, depth);
    }
  }

  const int16_t radius = stoneRadius(layout);
  for (int cell = 0; cell < hex::kCells; ++cell) {
    int16_t cx = 0;
    int16_t cy = 0;
    cellCentre(layout, cell, cx, cy);
    fui::Point v[kVertexCount];
    hexagonVertices(layout, cx, cy, v);
    // Knocked out in paper before it is stroked, which is the rule for every
    // light shape here: the cell then owns its own pixels whatever was drawn
    // underneath -- the front door's rule under the miniature, the border strip
    // that stops exactly on this edge -- rather than letting it show through.
    fillHexagon(screen, v, fui::Paint::solid(fui::Color::White));
    outlineHexagon(screen, v, toybox::kHairline);
    const uint8_t here = game.at(cell);
    if (hex::isStone(here)) stone(screen, cx, cy, radius, here);
  }

  // Drawn in a pass of their own, after every cell: a mark sits proud of its
  // hexagon and would otherwise be overdrawn by whichever neighbour rendered
  // later, which is the broken-rectangle bug chess's selection frame paid for.
  for (int cell = 0; cell < hex::kCells; ++cell) {
    const uint8_t here = game.at(cell);
    if (!hex::isStone(here)) continue;
    const bool inChain = chain != nullptr && hex::marked(chain, cell);
    const bool isLast = markLast && chain == nullptr && game.lastMove == cell;
    if (!inChain && !isLast) continue;
    int16_t cx = 0;
    int16_t cy = 0;
    cellCentre(layout, cell, cx, cy);
    stoneMark(screen, cx, cy, radius, here);
  }
}

// One player's card, in the triangle of paper the rhombus leaves at its corner.
//
// That space is the whole reason this layout is worth its arithmetic: a
// parallelogram in a box leaves two big notches, and the two things a Hex
// player has to know -- which colour they are and which pair of edges that
// colour is joining -- fit in them exactly. The seat to move is inverted, the
// other is outlined, which is the same "this one of these" the shelf's page
// marks and the settings rows already use.
void seatCard(toybox::Screen& screen, const fui::Rect& box, const uint8_t colour, const char* who, const bool toMove) {
  if (toMove) {
    screen.target().fill(box, fui::Paint::solid(fui::Color::Black));
  } else {
    screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), toybox::kRule);
  }
  const int16_t cy = static_cast<int16_t>(box.y + box.height / 2);
  // On the inverted card the stone is drawn the other way round: black ink on
  // black paper is nothing at all. Whenever a drawn element moves, re-check
  // what is behind it.
  const int16_t stoneX = static_cast<int16_t>(box.x + 30);
  toybox::disc(screen, stoneX, cy, 17, toMove ? fui::Color::White : fui::Color::Black);
  toybox::disc(screen, stoneX, cy, 14,
               colour == hex::kBlack ? (toMove ? fui::Color::White : fui::Color::Black)
                                     : (toMove ? fui::Color::Black : fui::Color::White));

  const int16_t textLeft = static_cast<int16_t>(box.x + 56);
  const int16_t textWidth = static_cast<int16_t>(box.width - 56 - 10);
  fui::TextStyle name;
  name.font = toybox::kUiFont;
  name.align = fui::TextAlign::Left;
  name.color = toMove ? fui::Color::White : fui::Color::Black;
  screen.target().text(
      toybox::inkCentred(fui::makeRect(textLeft, box.y, textWidth, static_cast<int16_t>(box.height / 2)),
                         toybox::kUiCut),
      who, name);

  fui::TextStyle edges = name;
  edges.font = toybox::kTileFont;
  screen.target().text(toybox::inkCentred(fui::makeRect(textLeft, static_cast<int16_t>(box.y + box.height / 2),
                                                        textWidth, static_cast<int16_t>(box.height / 2)),
                                          toybox::kTileCut),
                       colour == hex::kBlack ? "TOP TO BOTTOM" : "LEFT TO RIGHT", edges);
}

void toyboxChrome(toybox::Screen& screen, const char* title, const char* rightLabel = nullptr) {
  fui::HeaderProps header;
  header.title = title;
  header.rightLabel = rightLabel;
  // rightLabel is drawn with subtitleText, and the theme's default is black on
  // the black band -- invisible, and indistinguishable from never having been
  // set. Jaipur paid for this discovery and every band since has copied the fix.
  header.subtitleText = fui::TextStyle{};
  header.subtitleText.font = toybox::kUiFont;
  header.subtitleText.color = fui::Color::White;
  header.subtitleText.align = fui::TextAlign::Right;
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

}  // namespace

// The two cards, in the notches the board leaves. Returned rather than drawn
// here so the board and the result screen place them identically.
//
// The notch is a TRIANGLE, so how far left a rect may start depends on how TALL
// it is: row 0's cell `c` has ink from `21c - strip` downward at the shipped
// size, so a card 4h high clears everything left of column five and the taller
// button stack below does not. That is why the buttons get a rect of their own
// rather than borrowing this one.
constexpr int16_t kCardColumns = 17;
// The result screen's two doors are stacked, and each row is as wide as its own
// height allows rather than both taking the narrower of the two. Row 0's cell
// `c` has ink from `21c - strip` downward, so a band 52 pixels tall clears
// everything left of column three while a band reaching 112 clears only column
// six: PLAY AGAIN gets the wide top row and DONE the short one under it.
//
// Both rows the narrow width elided the label to "PLAY AG...", which a
// screenshot caught and no assertion would have -- the button drew, it was
// tappable, and it said the wrong thing.
constexpr int16_t kWideButtonColumns = 23;
constexpr int16_t kButtonColumns = 15;

fui::Rect theirCardRect(const Layout& layout) {
  const int16_t width = static_cast<int16_t>(layout.a * kCardColumns);
  const int16_t height = static_cast<int16_t>(layout.h * 4);
  return fui::makeRect(static_cast<int16_t>(layout.left + layout.a * 34 - width), layout.top, width, height);
}

fui::Rect yourCardRect(const Layout& layout) {
  const int16_t width = static_cast<int16_t>(layout.a * kCardColumns);
  const int16_t height = static_cast<int16_t>(layout.h * 4);
  return fui::makeRect(layout.left, static_cast<int16_t>(layout.top + layout.h * 32 - height), width, height);
}

fui::Rect againButtonRect(const Layout& layout) {
  const int16_t width = static_cast<int16_t>(layout.a * kWideButtonColumns);
  return fui::makeRect(static_cast<int16_t>(layout.left + layout.a * 34 - width), layout.top, width,
                       toybox::kPillHeight);
}

fui::Rect doneButtonRect(const Layout& layout) {
  const int16_t width = static_cast<int16_t>(layout.a * kButtonColumns);
  return fui::makeRect(static_cast<int16_t>(layout.left + layout.a * 34 - width),
                       static_cast<int16_t>(layout.top + toybox::kPillHeight + 8), width, toybox::kPillHeight);
}

Layout boardLayout(const fui::DeviceContext& device) {
  Layout layout;
  const int16_t availableWidth = static_cast<int16_t>(device.width - toybox::kMargin * 2);
  const int16_t availableHeight =
      static_cast<int16_t>(device.height - toybox::kChromeHeight - toybox::kGutter - toybox::kMargin);

  // The box is 34a by 32h with h = sqrt(3) * a, so the board is HEIGHT-bound in
  // portrait: 34/32 * sqrt(3) is about 1.84, against the panel's 1.67. Take the
  // width's answer and then shrink until the height's is satisfied, rather than
  // solving it once in floating point -- the loop runs at most a handful of
  // times and cannot round the wrong way.
  //
  // The BORDER STRIPS are part of the fit, on all four sides. They are drawn
  // OUTSIDE the box -- that is what makes them read as the edge a player is
  // joining rather than as the first row of cells -- so a fit that measured the
  // box alone put the top strip one pixel inside the header's gutter, which the
  // chrome probe in host-tests/ui catches and nothing on the panel would.
  int16_t a = static_cast<int16_t>(availableWidth / 34);
  if (a < 2) a = 2;
  int16_t h = static_cast<int16_t>((a * 1732 + 500) / 1000);
  const auto fits = [&](const int16_t side, const int16_t half) {
    const int16_t margin = static_cast<int16_t>(borderDepth(half) + toybox::kHairline);
    return side * 34 + margin * 2 <= availableWidth && half * 32 + margin * 2 <= availableHeight;
  };
  while (a > 2 && !fits(a, h)) {
    --a;
    h = static_cast<int16_t>((a * 1732 + 500) / 1000);
  }
  layout.a = a;
  layout.h = h;
  const int16_t margin = static_cast<int16_t>(borderDepth(h) + toybox::kHairline);
  layout.left = static_cast<int16_t>((device.width - a * 34) / 2);
  const int16_t top = static_cast<int16_t>(toybox::kChromeHeight + toybox::kGutter);
  layout.top = static_cast<int16_t>(top + margin + (availableHeight - h * 32 - margin * 2) / 2);
  return layout;
}

void cellCentre(const Layout& layout, const int cell, int16_t& cx, int16_t& cy) {
  const int row = hex::rowOf(cell);
  const int col = hex::colOf(cell);
  cx = static_cast<int16_t>(layout.left + layout.a * 2 + layout.a * 3 * col);
  cy = static_cast<int16_t>(layout.top + layout.h + layout.h * 2 * row + layout.h * col);
}

bool cellAt(const Layout& layout, const int x, const int y, int& cell) {
  // Pixel to fractional axial, then cube rounding -- the standard inverse, and
  // the only one that gives every point of the plane to the hexagon it is
  // actually inside. Rounding row and column independently claims the rhombus
  // of four centres instead of the hexagon, which puts a tap up to a third of a
  // cell away from the stone it places near every edge.
  const double px = static_cast<double>(x - (layout.left + layout.a * 2));
  const double py = static_cast<double>(y - (layout.top + layout.h));
  const double col = px / (3.0 * layout.a);
  const double row = py / (2.0 * layout.h) - col / 2.0;

  double rq = std::floor(col + 0.5);
  double rr = std::floor(row + 0.5);
  double rs = std::floor(-col - row + 0.5);
  const double dq = std::fabs(rq - col);
  const double dr = std::fabs(rr - row);
  const double ds = std::fabs(rs - (-col - row));
  if (dq > dr && dq > ds) {
    rq = -rr - rs;
  } else if (dr > ds) {
    rr = -rq - rs;
  }

  const int foundCol = static_cast<int>(rq);
  const int foundRow = static_cast<int>(rr);
  if (!hex::onBoard(foundRow, foundCol)) return false;
  cell = hex::cellAt(foundRow, foundCol);
  return true;
}

int16_t stoneRadius(const Layout& layout) {
  // The hexagon's inscribed circle: `h` to the flat edges, half the distance to
  // a neighbour's centre for the slanted ones. The smaller of the two, less the
  // ring, so a stone never touches the cell it sits in.
  const int16_t across = static_cast<int16_t>(isqrt16(9 * layout.a * layout.a + layout.h * layout.h) / 2);
  const int16_t inscribed = layout.h < across ? layout.h : across;
  return static_cast<int16_t>(inscribed - 2);
}

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  toyboxChrome(screen, "HEX");

  char record[48];
  std::snprintf(record, sizeof(record), "%d PLAYED   %d WON", model.wins + model.losses, model.wins);
  const fui::Rect line = screen.takeTop(26);
  fui::TextStyle small;
  small.font = toybox::kTileFont;
  small.align = fui::TextAlign::Left;
  screen.target().text(line, model.wins + model.losses > 0 ? record : "NO GAMES YET", small);
  screen.target().fill(fui::makeRect(line.x, static_cast<int16_t>(line.bottom() + 6), line.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));

  fui::ListItem rows[static_cast<int>(MenuRow::Count)] = {};
  rows[static_cast<int>(MenuRow::Play)].label = model.inProgress ? "RESUME GAME" : "PLAY";
  rows[static_cast<int>(MenuRow::Play)].actionValue = static_cast<int16_t>(MenuRow::Play);
  rows[static_cast<int>(MenuRow::PlayNearby)].label = "PLAY NEARBY";
  rows[static_cast<int>(MenuRow::PlayNearby)].subtitle = model.nearbyName;
  rows[static_cast<int>(MenuRow::PlayNearby)].actionValue = static_cast<int16_t>(MenuRow::PlayNearby);
  rows[static_cast<int>(MenuRow::Settings)].label = "SETTINGS";
  rows[static_cast<int>(MenuRow::Settings)].actionValue = static_cast<int16_t>(MenuRow::Settings);

  const int selected = model.selected < 0 ? 0 : model.selected;
  fui::ListProps list;
  list.items = rows;
  list.count = static_cast<uint16_t>(MenuRow::Count);
  list.selectedIndex = static_cast<int16_t>(selected);
  list.action = ActionMenuRow;
  const int count = static_cast<int>(MenuRow::Count);
  const int16_t listHeight =
      static_cast<int16_t>(count * toybox::kRowHeight + (count - 1) * toybox::kGutter / 2 + toybox::kGutter);
  const fui::Rect content = screen.contentRect();
  const fui::Rect listBand =
      fui::makeRect(content.x, static_cast<int16_t>(content.bottom() - listHeight), content.width, listHeight);
  screen.list(list, listHeight, fui::LayoutAnchor::Bottom);
  toybox::iconAtRowRight(screen, listBand, static_cast<int>(MenuRow::PlayNearby), 0, linkui::nearbyMark(),
                         selected == static_cast<int>(MenuRow::PlayNearby));

  // Ornament made of the app's own material carrying the app's own data: the
  // game in PROGRESS when there is one, the last one finished when there is
  // not, and an EMPTY board on a device that has played neither.
  //
  // Empty rather than nothing, which is what this screen drew first and what Go
  // still draws: a front door with a four hundred pixel hole in the middle of
  // it reads as a screen that failed to load, and the shape of the board is the
  // one thing about Hex a stranger has to see before the rules mean anything.
  hex::Game picture{};
  hex::reset(picture);
  if (model.boardCells != nullptr) {
    for (int i = 0; i < hex::kCellBytes; ++i) picture.cell[i] = model.boardCells[i];
  }

  // The caption is part of the block, not something squeezed in after it: a
  // miniature sized to the space and THEN given a line underneath is a line
  // drawn over the first row of the list.
  constexpr int16_t kCaptionHeight = 28;
  const int16_t areaTop = static_cast<int16_t>(line.bottom() + 6 + toybox::kRule + toybox::kGutter);
  const int16_t room = static_cast<int16_t>(listBand.y - areaTop - toybox::kGutter - kCaptionHeight);

  Layout mini;
  mini.a = 7;
  while (mini.a > 2 && static_cast<int>((mini.a * 1732 + 500) / 1000) * 32 > room) --mini.a;
  mini.h = static_cast<int16_t>((mini.a * 1732 + 500) / 1000);
  mini.left = static_cast<int16_t>((screen.device().width - mini.a * 34) / 2);
  mini.top = static_cast<int16_t>(areaTop + (room - mini.h * 32) / 2);
  drawBoard(screen, mini, picture, nullptr, false);

  char caption[64];
  if (model.inProgress) {
    std::snprintf(caption, sizeof(caption), "IN PROGRESS   MOVE %d", model.moveNumber);
  } else if (model.boardCells != nullptr) {
    std::snprintf(caption, sizeof(caption), "LAST GAME: %s", model.lastWon ? "WON" : "LOST");
  } else {
    // The rules, in the space the record would fill later. It is one sentence,
    // which is the whole reason this game is on the shelf.
    std::snprintf(caption, sizeof(caption), "JOIN YOUR TWO EDGES BEFORE THEY JOIN THEIRS");
  }
  fui::TextStyle cap;
  cap.font = toybox::kTileFont;
  cap.align = fui::TextAlign::Center;
  screen.target().text(
      fui::makeRect(content.x, static_cast<int16_t>(mini.top + mini.h * 32 + 4), content.width, kCaptionHeight),
      caption, cap);
}

void buildSettings(toybox::Screen& screen, const SettingsModel& model) {
  toyboxChrome(screen, "SETTINGS");

  fui::ListItem rows[static_cast<int>(SettingsRow::Count)] = {};
  rows[static_cast<int>(SettingsRow::Opponent)].label = "OPPONENT";
  rows[static_cast<int>(SettingsRow::Opponent)].value =
      model.opponent == hex::Opponent::Computer ? "COMPUTER" : "2 PLAYERS";
  rows[static_cast<int>(SettingsRow::Opponent)].actionValue = static_cast<int16_t>(SettingsRow::Opponent);

  rows[static_cast<int>(SettingsRow::Level)].label = "LEVEL";
  // Dimmed rather than gone when two people share the device: a control that
  // vanishes takes its space with it and the list jumps under the finger.
  rows[static_cast<int>(SettingsRow::Level)].value =
      model.opponent == hex::Opponent::Computer ? hex::levelName(model.level) : "--";
  rows[static_cast<int>(SettingsRow::Level)].enabled = model.opponent == hex::Opponent::Computer;
  rows[static_cast<int>(SettingsRow::Level)].actionValue = static_cast<int16_t>(SettingsRow::Level);

  rows[static_cast<int>(SettingsRow::PlayAs)].label = "YOU PLAY";
  rows[static_cast<int>(SettingsRow::PlayAs)].value = model.opponent != hex::Opponent::Computer ? "--"
                                                      : model.playAs == hex::kBlack             ? "BLACK"
                                                                                                : "WHITE";
  rows[static_cast<int>(SettingsRow::PlayAs)].enabled = model.opponent == hex::Opponent::Computer;
  rows[static_cast<int>(SettingsRow::PlayAs)].actionValue = static_cast<int16_t>(SettingsRow::PlayAs);

  const int selected = model.selected < 0 ? 0 : model.selected;
  fui::ListProps list;
  list.items = rows;
  list.count = static_cast<uint16_t>(SettingsRow::Count);
  list.selectedIndex = static_cast<int16_t>(selected);
  list.action = ActionSettingsRow;
  const int count = static_cast<int>(SettingsRow::Count);
  const int16_t listHeight =
      static_cast<int16_t>(count * toybox::kRowHeight + (count - 1) * toybox::kGutter / 2 + toybox::kGutter);
  const fui::Rect content = screen.contentRect();
  const fui::Rect listBand = fui::makeRect(content.x, content.y, content.width, listHeight);
  screen.list(list, listHeight, fui::LayoutAnchor::Top);

  // What the rows mean, said once, below them rather than inside them: a
  // subtitle on a value row is set at the title cut and about twenty characters
  // is all there is, which is not enough to say anything true.
  const char* explain =
      model.opponent != hex::Opponent::Computer
          ? "TWO PLAYERS, ONE DEVICE. THE BOARD IS DRAWN THE SAME WAY UP FOR BOTH OF YOU, SO PASS IT ACROSS."
      : model.level == hex::Level::Easy
          ? "IT PLAYS QUICKLY AND WILL NOT SEE A CONNECTION COMING, THOUGH IT NEVER MISSES A WINNING STONE."
      : model.level == hex::Level::Normal
          ? "IT KNOWS THE BRIDGE, SO IT DEFENDS A LINK YOU HAVE NOT FINISHED BUILDING YET."
          : "IT THINKS FOR AS LONG AS IT IS ALLOWED, WHICH IS UNDER FIVE SECONDS A MOVE.";
  fui::TextStyle body;
  body.font = toybox::kTileFont;
  body.align = fui::TextAlign::Left;
  body.maxLines = 4;
  screen.target().text(
      fui::makeRect(content.x, static_cast<int16_t>(listBand.bottom() + toybox::kGutter * 2), content.width, 120),
      explain, body);

  // Black moves first and there is no swap, so which colour you take is the one
  // setting on this screen that changes the game rather than the opponent.
  if (model.opponent != hex::Opponent::Computer) return;
  fui::TextStyle note = body;
  note.maxLines = 2;
  screen.target().text(
      fui::makeRect(content.x, static_cast<int16_t>(listBand.bottom() + toybox::kGutter * 2 + 130), content.width, 60),
      "BLACK PLAYS FIRST AND KEEPS THE ADVANTAGE THAT COMES WITH IT: THERE IS NO SWAP RULE HERE.", note);
}

void buildBoard(toybox::Screen& screen, const BoardModel& model) {
  char right[16];
  if (model.thinking) {
    std::snprintf(right, sizeof(right), "THINKING");
  } else {
    std::snprintf(right, sizeof(right), "%u", static_cast<unsigned>(model.game.moveNumber));
  }
  toyboxChrome(screen, "HEX", right);

  const Layout layout = boardLayout(screen.device());
  drawBoard(screen, layout, model.game, nullptr, true);

  const uint8_t yours = model.seat;
  const uint8_t theirs = hex::other(yours);
  const bool sharedDevice = model.sharedDevice;
  const char* yourName = sharedDevice ? (yours == hex::kBlack ? "BLACK" : "WHITE") : "YOU";
  const char* theirName = sharedDevice ? (theirs == hex::kBlack ? "BLACK" : "WHITE")
                                       : (model.opponentName != nullptr ? model.opponentName : "THEM");
  seatCard(screen, theirCardRect(layout), theirs, theirName, model.game.toMove == theirs && !hex::over(model.game));
  seatCard(screen, yourCardRect(layout), yours, yourName, model.game.toMove == yours && !hex::over(model.game));
}

void buildResult(toybox::Screen& screen, const ResultModel& model) {
  const uint8_t won = model.game.winner;
  const bool youWon = won == model.seat;
  const char* headline =
      model.sharedDevice ? (won == hex::kBlack ? "BLACK WINS" : "WHITE WINS") : (youWon ? "YOU WIN" : "THEY WIN");
  char moves[16];
  std::snprintf(moves, sizeof(moves), "%u", static_cast<unsigned>(model.game.moveNumber));
  toyboxChrome(screen, headline, moves);

  const Layout layout = boardLayout(screen.device());
  drawBoard(screen, layout, model.game, model.chain, false);

  const uint8_t yours = model.seat;
  const char* yourName = model.sharedDevice ? (yours == hex::kBlack ? "BLACK" : "WHITE") : "YOU";
  // Inverted when this seat WON. There is no turn left to say anything about,
  // and the header has already named the winner, so the card carries the one
  // thing a finished Hex board still has to explain: which pair of edges the
  // connection on the panel was joining.
  seatCard(screen, yourCardRect(layout), yours, yourName, won == yours);

  // The two doors go in the notch the opponent's card had. The board is the
  // whole panel by design, so a band reserved for buttons would cost every cell
  // a pixel -- and the rhombus leaves this corner empty either way.
  fui::ButtonProps again;
  again.label = "PLAY AGAIN";
  again.action = ActionAgain;
  again.borderEdges = fui::EdgesNone;
  screen.button(again, againButtonRect(layout));

  fui::ButtonProps done;
  done.label = "DONE";
  done.action = ActionDone;
  done.borderEdges = fui::EdgesNone;
  screen.button(done, doneButtonRect(layout));
}

}  // namespace hexui

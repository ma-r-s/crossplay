#include "HexScreens.h"

#include <cmath>
#include <cstdint>
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

void drawBoard(toybox::Screen& screen, const Layout& layout, const BorderStrip* strips, const int stripCount,
               const hex::Game& game, const uint8_t* chain, const bool markLast) {
  // The band that says whose edge is whose. Black's strips are solid ink and
  // White's are paper with a rail along the outside, which is the same
  // filled-versus-outlined pair the stones use -- so a player reads which edges
  // are theirs from the same language as the piece in their hand.
  //
  // The strips are MITRED (see borderStrips), so the band is one shape round
  // the board. Drawn as independent bars it had a notch at every joint of the
  // staircase, which is every other pixel of the two slanted sides. The caller
  // builds them, once a screen: the front door needs them again for its words,
  // and two copies of ninety strips is stack the render task does not have.
  const fui::Paint paper = fui::Paint::solid(fui::Color::White);
  const fui::Paint ink = fui::Paint::solid(fui::Color::Black);
  for (int i = 0; i < stripCount; ++i) {
    const BorderStrip& strip = strips[i];
    if (strip.owner != hex::kWhite) continue;
    screen.target().triangle(strip.from, strip.to, strip.outTo, paper);
    screen.target().triangle(strip.from, strip.outTo, strip.outFrom, paper);
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

  // Black's band goes on AFTER the cells. Every cell is knocked out in paper,
  // and a band drawn first had its inner edge nibbled by each hexagon's fill,
  // which is the ragged inside of the slanted strips. The band starts exactly on
  // the board's outline, so covering that hairline is covering its own edge.
  for (int i = 0; i < stripCount; ++i) {
    const BorderStrip& strip = strips[i];
    if (strip.owner != hex::kBlack) continue;
    screen.target().triangle(strip.from, strip.to, strip.outTo, ink);
    screen.target().triangle(strip.from, strip.outTo, strip.outFrom, ink);
  }
  // White's rail, along the mitred outer edge, so it reads as one line round
  // the side rather than a zigzag of separate strokes. The inner rail is the
  // hexagons' own outline.
  for (int i = 0; i < stripCount; ++i) {
    const BorderStrip& strip = strips[i];
    if (strip.owner != hex::kWhite) continue;
    screen.target().line(strip.outFrom, strip.outTo, static_cast<uint8_t>(toybox::kHairline), ink);
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
  const char* edgesText = colour == hex::kBlack ? "TOP TO BOTTOM" : "LEFT TO RIGHT";
  const SeatCardLayout at = seatCardLayout(screen.target(), box, who, edgesText);
  // On the inverted card the stone is drawn the other way round: black ink on
  // black paper is nothing at all. Whenever a drawn element moves, re-check
  // what is behind it.
  toybox::disc(screen, at.stoneX, at.stoneY, kCardStoneRadius, toMove ? fui::Color::White : fui::Color::Black);
  toybox::disc(screen, at.stoneX, at.stoneY, static_cast<int16_t>(kCardStoneRadius - 3),
               colour == hex::kBlack ? (toMove ? fui::Color::White : fui::Color::Black)
                                     : (toMove ? fui::Color::Black : fui::Color::White));

  fui::TextStyle name;
  name.font = toybox::kUiFont;
  name.align = fui::TextAlign::Left;
  name.color = toMove ? fui::Color::White : fui::Color::Black;
  screen.target().text(at.name, who, name);

  fui::TextStyle edges = name;
  edges.font = toybox::kTileFont;
  screen.target().text(at.edges, edgesText, edges);
}

// One line of words in a notch beside the miniature.
struct NotchLine {
  const char* text;
  fui::FontId font;
  const toybox::CutMetrics* cut;
};

// How far the board's band reaches, left or right, within a run of rows: the
// edge a line of words beside the miniature has to keep clear of. Sampled along
// every strip's four sides, because the band's outline between two rows is a
// slanted edge, not a vertex.
int16_t bandReach(const BorderStrip* strips, const int count, const int16_t y0, const int16_t y1,
                  const bool rightmost) {
  int16_t reach = rightmost ? INT16_MIN : INT16_MAX;
  for (int i = 0; i < count; ++i) {
    const fui::Point corners[4] = {strips[i].from, strips[i].to, strips[i].outTo, strips[i].outFrom};
    for (int k = 0; k < 4; ++k) {
      const fui::Point a = corners[k];
      const fui::Point b = corners[(k + 1) % 4];
      for (int step = 0; step <= 16; ++step) {
        const int16_t x = static_cast<int16_t>(a.x + (b.x - a.x) * step / 16);
        const int16_t y = static_cast<int16_t>(a.y + (b.y - a.y) * step / 16);
        if (y < y0 || y > y1) continue;
        if (rightmost ? x > reach : x < reach) reach = x;
      }
    }
  }
  return reach;
}

// Words stacked in one of the miniature's notches, flush against `edge` --
// right-aligned to it, or left-aligned from it -- with the block's first cap
// line at `y` (anchorTop) or its last baseline at `y`. Each line keeps
// kNotchClear from the band; a line in the UI cut that will not fit its row
// steps down to the tile cut rather than run into the board.
constexpr int16_t kNotchClear = 12;
constexpr int16_t kNotchLineGap = 8;
void notchWords(toybox::Screen& screen, const BorderStrip* strips, const int count, const NotchLine* lines, const int n,
                const int16_t edge, const bool right, const int16_t y, const bool anchorTop) {
  int16_t block = static_cast<int16_t>((n - 1) * kNotchLineGap);
  for (int i = 0; i < n; ++i) block = static_cast<int16_t>(block + lines[i].cut->inkHeight);
  int16_t capTop = anchorTop ? y : static_cast<int16_t>(y - block);
  for (int i = 0; i < n; ++i) {
    NotchLine line = lines[i];
    fui::TextStyle style;
    style.font = line.font;
    style.align = right ? fui::TextAlign::Right : fui::TextAlign::Left;
    const int16_t ink = line.cut->inkHeight;
    const int16_t reach = bandReach(strips, count, static_cast<int16_t>(capTop - kNotchClear),
                                    static_cast<int16_t>(capTop + ink + kNotchClear), right);
    int16_t room = 0;
    if (right) {
      room = reach == INT16_MIN ? static_cast<int16_t>(edge) : static_cast<int16_t>(edge - reach - kNotchClear);
    } else {
      room = reach == INT16_MAX ? static_cast<int16_t>(screen.device().width - edge)
                                : static_cast<int16_t>(reach - kNotchClear - edge);
    }
    if (screen.target().measureText(line.font, line.text, style).width > room && line.cut != &toybox::kTileCut) {
      line.font = toybox::kTileFont;
      line.cut = &toybox::kTileCut;
      style.font = line.font;
    }
    if (room > 0) {
      const fui::Rect box =
          fui::makeRect(right ? static_cast<int16_t>(edge - room) : edge, capTop, room, line.cut->inkHeight);
      screen.target().text(toybox::inkCentred(box, *line.cut), line.text, style);
    }
    capTop = static_cast<int16_t>(capTop + ink + kNotchLineGap);
  }
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

SeatCardLayout seatCardLayout(const fui::DrawTarget& target, const fui::Rect& box, const char* who,
                              const char* edgesText) {
  fui::TextStyle nameStyle;
  nameStyle.font = toybox::kUiFont;
  fui::TextStyle edgesStyle;
  edgesStyle.font = toybox::kTileFont;
  const int16_t nameWidth = target.measureText(nameStyle.font, who, nameStyle).width;
  const int16_t edgesWidth = target.measureText(edgesStyle.font, edgesText, edgesStyle).width;

  // The stone, a gap, and the wider of the two lines: one group, centred in the
  // card both ways. Pinned to fixed offsets from the left it sat hard against
  // one side with a strip of empty card on the other, and the two lines, each
  // centred in its own half, left the name riding high over the stone.
  constexpr int16_t kGap = 12;
  constexpr int16_t kInset = 8;
  int16_t textWidth = nameWidth > edgesWidth ? nameWidth : edgesWidth;
  const int16_t room = static_cast<int16_t>(box.width - 2 * kInset - 2 * kCardStoneRadius - kGap);
  if (textWidth > room) textWidth = room;
  const int16_t groupWidth = static_cast<int16_t>(2 * kCardStoneRadius + kGap + textWidth);
  const int16_t left = static_cast<int16_t>(box.x + (box.width - groupWidth) / 2);

  SeatCardLayout at;
  at.stoneX = static_cast<int16_t>(left + kCardStoneRadius);
  at.stoneY = static_cast<int16_t>(box.y + box.height / 2);
  const int16_t textLeft = static_cast<int16_t>(left + 2 * kCardStoneRadius + kGap);
  // Wider than the text so a rounding difference between measuring and drawing
  // cannot elide it; left-aligned, so the slack falls to the right of the ink.
  const int16_t textBox = static_cast<int16_t>(box.right() - kInset - textLeft);

  // The two cap bands and the space between them, centred as a block on the
  // stone's middle, which is the card's.
  constexpr int16_t kLineGap = 8;
  const int16_t block = static_cast<int16_t>(toybox::kUiCut.inkHeight + kLineGap + toybox::kTileCut.inkHeight);
  const int16_t top = static_cast<int16_t>(box.y + (box.height - block) / 2);
  at.name = toybox::inkCentred(fui::makeRect(textLeft, top, textBox, toybox::kUiCut.inkHeight), toybox::kUiCut);
  at.edges = toybox::inkCentred(fui::makeRect(textLeft, static_cast<int16_t>(top + toybox::kUiCut.inkHeight + kLineGap),
                                              textBox, toybox::kTileCut.inkHeight),
                                toybox::kTileCut);
  return at;
}

int borderStrips(const Layout& layout, BorderStrip out[kMaxBorderStrips]) {
  const int16_t depth = borderDepth(layout.h);
  // Each strip's outward offset, kept unrounded: the mitre is an intersection
  // of two offset edges, and rounding the offsets first is what let
  // neighbouring bars miss each other. Floats, because this runs on the render
  // task's stack and a pixel does not need a double.
  float offX[kMaxBorderStrips];
  float offY[kMaxBorderStrips];
  int count = 0;
  for (int cell = 0; cell < hex::kCells && count < kMaxBorderStrips; ++cell) {
    for (int dir = 0; dir < 6 && count < kMaxBorderStrips; ++dir) {
      if (hex::neighbour(cell, dir) != hex::kNoCell) continue;
      int16_t cx = 0;
      int16_t cy = 0;
      cellCentre(layout, cell, cx, cy);
      fui::Point v[kVertexCount];
      hexagonVertices(layout, cx, cy, v);
      // The outward direction IS the vector to the missing neighbour's centre,
      // so the strip cannot drift away from the edge it belongs to.
      const float dx = 3.0f * layout.a * hex::kNeighbourCol[dir];
      const float dy =
          2.0f * layout.h * hex::kNeighbourRow[dir] + static_cast<float>(layout.h) * hex::kNeighbourCol[dir];
      const float length = std::sqrt(dx * dx + dy * dy);
      BorderStrip& strip = out[count];
      strip.from = v[dir];
      strip.to = v[(dir + 1) % kVertexCount];
      strip.owner = borderOwner(cell, dir);
      offX[count] = dx * depth / length;
      offY[count] = dy * depth / length;
      strip.outFrom = fui::Point{static_cast<int16_t>(std::lround(strip.from.x + offX[count])),
                                 static_cast<int16_t>(std::lround(strip.from.y + offY[count]))};
      strip.outTo = fui::Point{static_cast<int16_t>(std::lround(strip.to.x + offX[count])),
                               static_cast<int16_t>(std::lround(strip.to.y + offY[count]))};
      ++count;
    }
  }

  // Every border edge runs from one vertex to the next in the same turning
  // order, so the strip that continues strip i is the one that STARTS where i
  // ends. Both outer edges are cut back, or run on, to the point where they
  // cross: a convex joint gains the corner a pair of bars left empty and a
  // concave one loses the overlap. The vertices are integers computed the same
  // way on both sides, so the comparison is exact.
  for (int i = 0; i < count; ++i) {
    for (int j = 0; j < count; ++j) {
      if (j == i || out[j].from.x != out[i].to.x || out[j].from.y != out[i].to.y) continue;
      const float ax = out[i].from.x + offX[i];
      const float ay = out[i].from.y + offY[i];
      const float d1x = static_cast<float>(out[i].to.x - out[i].from.x);
      const float d1y = static_cast<float>(out[i].to.y - out[i].from.y);
      const float cx = out[j].from.x + offX[j];
      const float cy = out[j].from.y + offY[j];
      const float d2x = static_cast<float>(out[j].to.x - out[j].from.x);
      const float d2y = static_cast<float>(out[j].to.y - out[j].from.y);
      const float denom = d1x * d2y - d1y * d2x;
      float px = 0.5f * (ax + d1x + cx);
      float py = 0.5f * (ay + d1y + cy);
      if (std::fabs(denom) > 1e-6f) {
        const float t = ((cx - ax) * d2y - (cy - ay) * d2x) / denom;
        px = ax + t * d1x;
        py = ay + t * d1y;
      }
      const fui::Point mitre{static_cast<int16_t>(std::lround(px)), static_cast<int16_t>(std::lround(py))};
      out[i].outTo = mitre;
      out[j].outFrom = mitre;
      break;
    }
  }
  return count;
}

// The two cards, in the notches the board leaves. Returned rather than drawn
// here so the board and the result screen place them identically.
//
// The notch is a TRIANGLE, so how far left a rect may start depends on how TALL
// it is: row 0's cell `c` has ink from `21c - strip` downward at the shipped
// size, so a card 4h high clears everything left of column five.
constexpr int16_t kCardColumns = 17;

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

// The result screen's two doors take the two cards' places: PLAY AGAIN where
// their card was, top edge on the board's top, and DONE where yours was, bottom
// edge on the board's bottom. A finished game has no turn to show, so the cards
// have nothing left to say, and each door is a pill in the widest part of its
// notch -- the part furthest from the board.
//
// Stacked together in the top notch, as they first were, the lower door ran
// within a few pixels of the staircase: the notch narrows as it gets taller.
fui::Rect againButtonRect(const Layout& layout) {
  const fui::Rect card = theirCardRect(layout);
  return fui::makeRect(card.x, card.y, card.width, toybox::kPillHeight);
}

fui::Rect doneButtonRect(const Layout& layout) {
  const fui::Rect card = yourCardRect(layout);
  return fui::makeRect(card.x, static_cast<int16_t>(card.bottom() - toybox::kPillHeight), card.width,
                       toybox::kPillHeight);
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

  // The miniature takes the whole space above the list, and its words go in the
  // two notches the rhombus leaves -- the record top right, where their card
  // sits during play, and the state of the game bottom left, where yours does.
  // The words first sat in a line across the top and a line under the
  // miniature, pressed against its bottom corner, while the paper either side
  // of it stood empty: the notches ARE the room this screen has.
  const int16_t areaTop = content.y;
  const int16_t areaBottom = static_cast<int16_t>(listBand.y - toybox::kGutter);
  Layout mini;
  for (mini.a = 8; mini.a > 2; --mini.a) {
    mini.h = static_cast<int16_t>((mini.a * 1732 + 500) / 1000);
    const int16_t margin = static_cast<int16_t>(borderDepth(mini.h) + toybox::kHairline);
    if (mini.h * 32 + margin * 2 <= areaBottom - areaTop && mini.a * 34 + margin * 2 <= content.width) break;
  }
  mini.h = static_cast<int16_t>((mini.a * 1732 + 500) / 1000);
  mini.left = static_cast<int16_t>((screen.device().width - mini.a * 34) / 2);
  mini.top = static_cast<int16_t>(areaTop + (areaBottom - areaTop - mini.h * 32) / 2);
  BorderStrip strips[kMaxBorderStrips];
  const int stripCount = borderStrips(mini, strips);
  drawBoard(screen, mini, strips, stripCount, picture, nullptr, false);
  const NotchLine uiLine{nullptr, toybox::kUiFont, &toybox::kUiCut};
  const NotchLine tileLine{nullptr, toybox::kTileFont, &toybox::kTileCut};

  char played[24];
  char won[24];
  NotchLine record[2] = {tileLine, uiLine};
  int recordLines = 1;
  if (model.wins + model.losses > 0) {
    std::snprintf(played, sizeof(played), "%d PLAYED", model.wins + model.losses);
    std::snprintf(won, sizeof(won), "%d WON", model.wins);
    record[0] = uiLine;
    record[0].text = played;
    record[1].text = won;
    recordLines = 2;
  } else {
    record[0].text = "NO GAMES YET";
  }
  notchWords(screen, strips, stripCount, record, recordLines, content.right(), true, mini.top, true);

  char move[24];
  NotchLine state[2] = {tileLine, uiLine};
  if (model.inProgress) {
    std::snprintf(move, sizeof(move), "MOVE %d", model.moveNumber);
    state[0].text = "IN PROGRESS";
    state[1].text = move;
  } else if (model.boardCells != nullptr) {
    state[0].text = "LAST GAME";
    state[1].text = model.lastWon ? "WON" : "LOST";
  } else {
    // The rules, for a device that has never played. They are one sentence,
    // which is the whole reason this game is on the shelf.
    state[0].text = "JOIN YOUR TWO EDGES";
    state[1] = tileLine;
    state[1].text = "BEFORE THEY JOIN THEIRS";
  }
  notchWords(screen, strips, stripCount, state, 2, content.x, false, static_cast<int16_t>(mini.top + mini.h * 32),
             false);
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
  screen.list(list, listHeight, fui::LayoutAnchor::Top);

  // Nothing under the rows. Two paragraphs used to explain each level and the
  // first-move advantage; they read as a wall of capitals under three rows that
  // already say what they are, and the board says the rest the moment a game
  // starts: the cards name each colour's edges and the one to move is inverted.
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
  BorderStrip strips[kMaxBorderStrips];
  const int stripCount = borderStrips(layout, strips);
  drawBoard(screen, layout, strips, stripCount, model.game, nullptr, true);

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
  BorderStrip strips[kMaxBorderStrips];
  const int stripCount = borderStrips(layout, strips);
  drawBoard(screen, layout, strips, stripCount, model.game, model.chain, false);

  // No seat card: the game is over, the band has named the winner and the
  // marked chain shows the connection. The notches hold the two doors instead
  // (see againButtonRect). The board is the whole panel by design, so a band
  // reserved for buttons would cost every cell a pixel.
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

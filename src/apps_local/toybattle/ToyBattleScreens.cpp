#include "ToyBattleScreens.h"

#include <cstdio>

// Two board treatments, built together so they can be photographed side by side
// and one chosen. Options described in prose get judged wrong; options rendered
// at native size get judged.
//
// Slabs, chosen over rings by looking at both at native size. The losing
// variant is deleted rather than left behind a flag.

namespace tbui {
namespace {

namespace tb = toybattle;

// The prompt line sits between the rule and the board, and the board starts
// below it. They shared a band at first and the question was drawn over the
// top row of bases.
constexpr int16_t kPromptTop = toybox::kHeaderHeight + 10;
constexpr int16_t kBoardTop = kPromptTop + 26;
constexpr int16_t kRackTile = 54;
// Taller than it is wide: a numeral over a mark needs the height, and the
// square tile was cropping both.
constexpr int16_t kRackTall = 70;
constexpr int16_t kRackHeight = kRackTall + 8;
constexpr int16_t kCapsuleTop = 800 - toybox::kMargin - toybox::kPillHeight;
constexpr int16_t kRackTop = kCapsuleTop - toybox::kGutter - kRackHeight;
// The counts live in the action bar, not in a band of their own: `DRAW 2` was
// 372px wide for a label needing about 110, and that slack is free where a
// band above the rack cost the board 38px.
constexpr int16_t kCountsRow = 16;

// The board owns everything between the header and the rack.
constexpr int16_t kBoardHeight = kRackTop - toybox::kGutter - kBoardTop;

constexpr int16_t kSlot = 52;
constexpr uint8_t kPathWeight = 5;

int16_t boardLeft(const fui::DeviceContext& device) { return static_cast<int16_t>((device.width - 448) / 2); }

const char* pip(const tb::Troop kind) {
  static const char* const kPips[] = {"*", "1", "2", "3", "4", "5", "6", "7"};
  return kPips[static_cast<int>(kind)];
}

void centred(toybox::Screen& screen, const fui::Rect box, const char* text, const fui::FontId font, const bool white) {
  fui::TextStyle style;
  style.font = font;
  style.align = fui::TextAlign::Center;
  style.color = white ? fui::Color::White : fui::Color::Black;
  const int16_t h = screen.target().lineHeight(font);
  screen.target().text(fui::makeRect(box.x, static_cast<int16_t>(box.y + (box.height - h) / 2), box.width, h), text,
                       style);
}

// --- the marks -------------------------------------------------------------
//
// A design language, because eight troops and seven kinds of special base is
// more than anyone holds in their head, and a player who has to remember what
// a 5 does is playing the manual rather than the game.
//
// Two rules carry it:
//
//   The SILHOUETTE says whether a base restricts what may be placed there. A
//   base with square corners has a rule about its contents; every other base is
//   rounded. Silhouette rather than a mark, because it survives having a troop
//   standing on top of it.
//
//   A BADGE says what a base does after a troop lands. Filled disc, glyph
//   knocked out white, pinned to the top-right so it never sits where the
//   troop's number goes.
//
// The same glyphs appear under the numbers on the rack, so the mark a player
// learns on their own cards is the mark they read on the board.

void glyph(toybox::Screen& screen, const fui::Point at, const int16_t size, const tb::Special what, const bool white) {
  const fui::Paint ink = fui::Paint::solid(white ? fui::Color::White : fui::Color::Black);
  const int16_t h = size / 2;
  const auto tri = [&](const int dx, const int dy) {
    // A triangle pointing along (dx, dy): home, away, or sideways.
    if (dy < 0) {
      screen.target().triangle(fui::Point{static_cast<int16_t>(at.x), static_cast<int16_t>(at.y - h)},
                               fui::Point{static_cast<int16_t>(at.x - h), static_cast<int16_t>(at.y + h)},
                               fui::Point{static_cast<int16_t>(at.x + h), static_cast<int16_t>(at.y + h)}, ink);
    } else if (dy > 0) {
      screen.target().triangle(fui::Point{static_cast<int16_t>(at.x), static_cast<int16_t>(at.y + h)},
                               fui::Point{static_cast<int16_t>(at.x - h), static_cast<int16_t>(at.y - h)},
                               fui::Point{static_cast<int16_t>(at.x + h), static_cast<int16_t>(at.y - h)}, ink);
    } else {
      screen.target().triangle(fui::Point{static_cast<int16_t>(at.x + h), static_cast<int16_t>(at.y)},
                               fui::Point{static_cast<int16_t>(at.x - h), static_cast<int16_t>(at.y - h)},
                               fui::Point{static_cast<int16_t>(at.x - h), static_cast<int16_t>(at.y + h)}, ink);
    }
    (void)dx;
  };
  const auto bar = [&](const int16_t y, const int16_t w, const int16_t t) {
    screen.target().fill(fui::makeRect(static_cast<int16_t>(at.x - w / 2), y, w, t), ink);
  };

  switch (what) {
    case tb::Special::Recall:  // comes home to your rack
      tri(0, -1);
      break;
    case tb::Special::Draw:  // arrives from the reserve
      tri(0, 1);
      break;
    case tb::Special::Exhume:  // up, but out of the ground
      tri(0, -1);
      bar(static_cast<int16_t>(at.y + h), size, 2);
      break;
    case tb::Special::Shove:  // pushed sideways
      tri(1, 0);
      break;
    case tb::Special::Suppress:  // held down, so a solid block
      screen.target().fill(fui::makeRect(static_cast<int16_t>(at.x - h), static_cast<int16_t>(at.y - h + 1), size,
                                         static_cast<int16_t>(size - 2)),
                           ink, 2);
      break;
    case tb::Special::Gate:  // only some values, so a narrowed way through
      bar(static_cast<int16_t>(at.y - h), size, 2);
      bar(static_cast<int16_t>(at.y + h - 2), size, 2);
      break;
    case tb::Special::Nullify:  // effects struck out
      screen.target().line(fui::Point{static_cast<int16_t>(at.x - h), static_cast<int16_t>(at.y - h)},
                           fui::Point{static_cast<int16_t>(at.x + h), static_cast<int16_t>(at.y + h)}, 3, ink);
      screen.target().line(fui::Point{static_cast<int16_t>(at.x + h), static_cast<int16_t>(at.y - h)},
                           fui::Point{static_cast<int16_t>(at.x - h), static_cast<int16_t>(at.y + h)}, 3, ink);
      break;
    case tb::Special::None:
      break;
  }
}

// What a troop does, in the same alphabet. Blank for the two that do nothing,
// because a mark meaning "no mark" is worse than the space.
void troopMark(toybox::Screen& screen, const fui::Point at, const int16_t size, const tb::Troop kind) {
  const fui::Paint ink = fui::Paint::solid(fui::Color::Black);
  const int16_t h = size / 2;
  switch (kind) {
    case tb::Troop::Skully:  // two arrive
      glyph(screen, fui::Point{static_cast<int16_t>(at.x - h + 1), at.y}, size, tb::Special::Draw, false);
      glyph(screen, fui::Point{static_cast<int16_t>(at.x + h - 1), at.y}, size, tb::Special::Draw, false);
      break;
    case tb::Troop::Star:  // one arrives
      glyph(screen, at, size, tb::Special::Draw, false);
      break;
    case tb::Troop::Capn:  // and another after it
      screen.target().stroke(
          fui::makeRect(static_cast<int16_t>(at.x - h - 1), static_cast<int16_t>(at.y - h), size, size), ink, 2, 2);
      screen.target().fill(fui::makeRect(static_cast<int16_t>(at.x + 1), static_cast<int16_t>(at.y - h + 2),
                                         static_cast<int16_t>(size - 4), static_cast<int16_t>(size - 4)),
                           ink, 2);
      break;
    case tb::Troop::Jumbo:  // one leaves, struck out
      glyph(screen, at, size, tb::Special::Nullify, false);
      break;
    case tb::Troop::Hook:  // goes anywhere, so sideways past everything
      glyph(screen, at, size, tb::Special::Shove, false);
      break;
    case tb::Troop::XB42:  // reaches into their rack
      toybox::ring(screen, at.x, at.y, h, 2, fui::Color::Black, fui::Color::White);
      toybox::disc(screen, at.x, at.y, 2, fui::Color::Black);
      break;
    case tb::Troop::Kwak:
    case tb::Troop::Roxy:
      break;
  }
}

// The badge a special base wears. Pinned top-right, clear of the numeral.
void baseBadge(toybox::Screen& screen, const fui::Rect box, const tb::Special what) {
  if (what == tb::Special::None || what == tb::Special::Gate || what == tb::Special::Nullify) return;
  const int16_t r = 9;
  const fui::Point at{static_cast<int16_t>(box.right() - 2), static_cast<int16_t>(box.y + 2)};
  toybox::disc(screen, at.x, at.y, r, fui::Color::Black);
  glyph(screen, at, 8, what, true);
}

// One base, drawn where `slotCenter` says it is.
void drawSlot(toybox::Screen& screen, const fui::Point at, const tb::Game& game, const int slot, const bool candidate) {
  const tb::Terrain& b = game.board();
  const int16_t half = kSlot / 2;
  const fui::Rect box =
      fui::makeRect(static_cast<int16_t>(at.x - half), static_cast<int16_t>(at.y - half), kSlot, kSlot);
  const bool isHq = b.isHq(slot);
  const int holder = b.isBase(slot) ? game.occupantSeat(slot) : tb::kNoSeat;
  const tb::Special special = game.specialBases ? b.specialAt(slot) : tb::Special::None;
  // Square corners mean this SLOT has a rule about what may be placed on it.
  // A silhouette says it even with a troop standing on top; a mark would not.
  //
  // Asking `specialAt` alone was not enough: it returns None for an H.Q. by
  // construction, so Tropical Pool's two gated H.Q. drew round -- the one thing
  // on the board you must bring a 6 or a 7 to, looking like the two you can
  // take with anything. A gate is per SLOT, so the silhouette is too.
  const bool gated = game.specialBases && b.gate[slot] != 0;
  const bool restricts = gated || special == tb::Special::Gate || special == tb::Special::Nullify;
  const uint8_t corner = restricts ? 0 : 8;

  // A dithered ground with a black edge. The dither is in the fill because
  // there is no grey ink on this device.
  //
  // A special base wears a heavier edge as well as its badge: the badge says
  // which one, the weight says "this base does something" from across the
  // board, before you have looked closely enough to read a glyph.
  const uint8_t edge = isHq ? 5 : (special != tb::Special::None ? 6 : 3);
  screen.target().fill(box, fui::Paint::dither(holder == tb::kNoSeat ? fui::Color::LightGray : fui::Color::DarkGray),
                       corner);
  screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), edge, corner);

  if (isHq) {
    centred(screen, box, b.hqOwner(slot) == 0 ? "H" : "E", toybox::kUiFont, false);
    return;
  }

  if (holder != tb::kNoSeat) {
    const tb::Troop kind = game.occupantTroop(slot);
    // The holder is told by inversion rather than by a second shape: yours is
    // knocked out of black, theirs sits on the ground.
    if (holder == 0) {
      screen.target().fill(fui::makeRect(static_cast<int16_t>(box.x + 6), static_cast<int16_t>(box.y + 6),
                                         static_cast<int16_t>(box.width - 12), static_cast<int16_t>(box.height - 12)),
                           fui::Paint::solid(fui::Color::Black), 6);
    }
    centred(screen, box, pip(kind), toybox::kUiFont, holder == 0);

    // Stack depth as pips down the right edge: the buried troops are the whole
    // reason this game is not draughts.
    const int depth = game.stackDepth(slot);
    for (int i = 1; i < depth && i < 4; ++i) {
      screen.target().fill(
          fui::makeRect(static_cast<int16_t>(box.right() - 3), static_cast<int16_t>(box.y + 4 + (i - 1) * 7), 3, 5),
          fui::Paint::solid(fui::Color::Black));
    }
  }

  baseBadge(screen, box, special);

  if (candidate) {
    // Mark the base, not the troop standing on it.
    toybox::bracket(screen,
                    fui::makeRect(static_cast<int16_t>(box.x - 5), static_cast<int16_t>(box.y - 5),
                                  static_cast<int16_t>(box.width + 10), static_cast<int16_t>(box.height + 10)),
                    12, 4);
  }
}

}  // namespace

// --- geometry ---------------------------------------------------------------

int16_t slotRadius() { return kSlot / 2 + 6; }

fui::Point boardPoint(const fui::DeviceContext& device, const uint16_t nx, const uint16_t ny) {
  // The one place normalised board coordinates become pixels. Bases, H.Q. and
  // medal anchors all come through here, so a medal cannot land somewhere the
  // same arithmetic would not put a base.
  const int16_t left = boardLeft(device);
  const int16_t inset = kSlot / 2 + 4;
  const int16_t usableW = static_cast<int16_t>(448 - inset * 2);
  const int16_t usableH = static_cast<int16_t>(kBoardHeight - inset * 2);
  return fui::Point{static_cast<int16_t>(left + inset + nx * usableW / 1000),
                    static_cast<int16_t>(kBoardTop + inset + ny * usableH / 1000)};
}

fui::Point slotCenter(const fui::DeviceContext& device, const tb::Terrain& board, const int slot) {
  // Straight from the terrain. The coordinates are already balanced: the
  // tracing tool aligns the rows and columns, mirrors the board about its own
  // midline where the terrain is symmetric, and bakes the result into the
  // table, so the device never pays for the algorithm.
  return boardPoint(device, board.x[slot], board.y[slot]);
}

int slotAt(const fui::DeviceContext& device, const tb::Terrain& board, const int x, const int y) {
  const int16_t r = slotRadius();
  for (int slot = 0; slot < board.slotCount(); ++slot) {
    const fui::Point p = slotCenter(device, board, slot);
    const int dx = x - p.x, dy = y - p.y;
    if (dx * dx + dy * dy <= r * r) return slot;
  }
  return -1;
}

fui::Rect rackTile(const fui::DeviceContext& device, const int position) {
  const int16_t span = static_cast<int16_t>(tb::kTroopKinds * kRackTile);
  const int16_t left = static_cast<int16_t>((device.width - span) / 2);
  return fui::makeRect(static_cast<int16_t>(left + position * kRackTile), kRackTop, kRackTile, kRackTall);
}

int handKindAt(const tb::Game& game, const int seat, const int position) {
  // The hand laid out low to high, one slot per troop. Stable ordering, so a
  // tile does not jump under the thumb when an unrelated troop is drawn.
  int at = 0;
  for (int kind = 0; kind < tb::kTroopKinds; ++kind) {
    for (int held = 0; held < game.rack[seat][kind]; ++held) {
      if (at == position) return kind;
      ++at;
    }
  }
  return -1;
}

int rackAt(const fui::DeviceContext& device, const tb::Game& game, const int seat, const int x, const int y) {
  for (int position = 0; position < tb::kTroopKinds; ++position) {
    const fui::Rect r = rackTile(device, position);
    if (x >= r.x && x < r.right() && y >= r.y && y < r.bottom()) return handKindAt(game, seat, position);
  }
  return -1;
}

// --- screens ----------------------------------------------------------------

const char* specialBlurb(const tb::Special what) {
  switch (what) {
    case tb::Special::Recall:
      return "CALLS ONE OF YOUR OTHER TROOPS HOME";
    case tb::Special::Draw:
      return "DRAWS ONE FROM YOUR RESERVE";
    case tb::Special::Shove:
      return "SHOVES A TROOP NEXT TO IT ONE BASE";
    case tb::Special::Exhume:
      return "TAKES ONE OF YOURS BACK OFF THE DISCARD";
    case tb::Special::Suppress:
      return "PINS A TROOP ON THEIR RACK FOR A TURN";
    case tb::Special::Gate:
      return "ONLY THE PRINTED VALUES MAY LAND HERE";
    case tb::Special::Nullify:
      return "TROOP EFFECTS DO NOT WORK HERE";
    case tb::Special::None:
      return "";
  }
  return "";
}

void drawTroopMark(toybox::Screen& screen, const fui::Point at, const int16_t size, const tb::Troop kind) {
  troopMark(screen, at, size, kind);
}

void drawSpecialGlyph(toybox::Screen& screen, const fui::Point at, const int16_t size, const tb::Special what,
                      const bool white) {
  glyph(screen, at, size, what, white);
}

const char* troopBlurb(const tb::Troop kind) {
  switch (kind) {
    case tb::Troop::Kwak:
      return "JOKER: COVERS ANY, COVERED BY ANY";
    case tb::Troop::Skully:
      return "DRAWS TWO FROM YOUR RESERVE";
    case tb::Troop::Capn:
      return "LETS YOU PLACE A SECOND TROOP";
    case tb::Troop::Jumbo:
      return "REMOVES A TROOP NEXT TO IT";
    case tb::Troop::Hook:
      return "LANDS ANYWHERE, PATH OR NOT";
    case tb::Troop::XB42:
      return "SHOOTS A TROOP OFF THEIR RACK";
    case tb::Troop::Star:
      return "DRAWS ONE FROM YOUR RESERVE";
    case tb::Troop::Roxy:
      return "THE STRONGEST. NO EFFECT";
  }
  return "";
}

const char* refusalBlurb(const tb::Refusal why) {
  switch (why) {
    case tb::Refusal::None:
      return "";
    case tb::Refusal::NotYours:
      return "YOU ARE NOT HOLDING THAT";
    case tb::Refusal::Pinned:
      return "PINNED: IT SITS OUT THIS TURN";
    case tb::Refusal::NoPath:
      return "NO PATH TO THERE FROM YOUR H.Q.";
    case tb::Refusal::TooWeak:
      return "TOO WEAK TO COVER THAT ONE";
    case tb::Refusal::OwnHq:
      return "THAT IS YOUR OWN H.Q.";
    case tb::Refusal::Gated:
      return "THIS BASE TAKES OTHER VALUES";
    case tb::Refusal::Nullified:
      return "EFFECTS DO NOT WORK ON THAT BASE";
    case tb::Refusal::NotATarget:
      return "NOT ONE OF THE CHOICES";
    case tb::Refusal::NothingAsked:
      return "";
  }
  return "";
}

// The briefing: every special base on this terrain, drawn with the badge it
// wears on the board so the mark and the meaning are learned together.
void buildBrief(toybox::Screen& screen, const BriefModel& model) {
  fui::HeaderProps header;
  header.title = model.board ? model.board->name : "TERRAIN";
  header.borderEdges = fui::EdgesNone;
  screen.header(header);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
  if (!model.board) return;

  fui::TextStyle body;
  body.font = toybox::kSmallFont;
  body.align = fui::TextAlign::Left;

  if (!model.specialBases) {
    screen.target().text(screen.takeTop(60),
                         "SPECIAL BASES ARE SWITCHED OFF FOR THIS GAME. EVERY BASE IS AN "
                         "ORDINARY ONE.",
                         body);
    return;
  }

  // One row per kind actually on this board, counted so it says how many.
  int seen[8] = {};
  for (int base = 0; base < model.board->baseCount; ++base) {
    ++seen[static_cast<int>(model.board->specialAt(base))];
  }

  bool any = false;
  for (int k = 1; k < 8; ++k) {
    if (!seen[k]) continue;
    any = true;
    const tb::Special what = static_cast<tb::Special>(k);
    const fui::Rect row = screen.takeTop(52, toybox::kGutter);

    const int16_t badge = 17;
    const fui::Point at{static_cast<int16_t>(row.x + badge), static_cast<int16_t>(row.y + row.height / 2)};
    toybox::disc(screen, at.x, at.y, badge, fui::Color::Black);
    glyph(screen, at, 15, what, true);

    char line[96];
    std::snprintf(line, sizeof(line), "%d x  %s", seen[k], specialBlurb(what));
    screen.target().text(fui::makeRect(static_cast<int16_t>(row.x + badge * 2 + toybox::kGutter), row.y,
                                       static_cast<int16_t>(row.width - badge * 2 - toybox::kGutter), row.height),
                         line, body);
  }
  if (!any) {
    screen.target().text(screen.takeTop(52), "THIS TERRAIN HAS NO SPECIAL BASES.", body);
  }

  // The bar's counts, explained where the other marks are explained.
  {
    const fui::Rect row = screen.takeTop(30, toybox::kGutter);
    screen.target().text(row, "IN THE BAR: TILE = TROOPS IN HAND, TRIANGLE = LEFT TO DRAW, CROSS = OUT OF THE "
                              "GAME. TOP ROW IS THEIRS.",
                         body);
  }

  // A rule, then the troops. Two alphabets on one screen need telling apart:
  // above it is what the ground does, below it is what your own troops do.
  const fui::Rect gap = screen.takeTop(toybox::kRule + toybox::kGutter * 2, toybox::kGutter);
  screen.target().fill(fui::makeRect(gap.x, static_cast<int16_t>(gap.y + toybox::kGutter), gap.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));

  for (int k = 0; k < tb::kTroopKinds; ++k) {
    const tb::Troop troop = static_cast<tb::Troop>(k);
    const fui::Rect row = screen.takeTop(40, 2);
    const int16_t mid = static_cast<int16_t>(row.y + row.height / 2);

    // The card's own face, small: the numeral it carries and the mark under it
    // on the rack, side by side here so the two are learned together.
    const fui::Rect tile =
        fui::makeRect(row.x, static_cast<int16_t>(row.y + 2), 30, static_cast<int16_t>(row.height - 4));
    screen.target().stroke(tile, fui::Paint::solid(fui::Color::Black), 2, 6);
    centred(screen, tile, pip(troop), toybox::kSmallFont, false);
    troopMark(screen, fui::Point{static_cast<int16_t>(row.x + 52), mid}, 12, troop);

    screen.target().text(
        fui::makeRect(static_cast<int16_t>(row.x + 74), row.y, static_cast<int16_t>(row.width - 74), row.height),
        troopBlurb(troop), body);
  }
}

void buildResult(toybox::Screen& screen, const ResultModel& model) {
  const tb::Game& game = model.game;
  const bool won = game.winner == model.seat;

  fui::HeaderProps header;
  header.title = won ? "YOU WIN" : "YOU LOSE";
  header.borderEdges = fui::EdgesNone;
  screen.header(header);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  // How it ended, because the three endings are genuinely different games and
  // "you lose" alone does not say which one you just played.
  const char* how = "";
  switch (game.endedBy()) {
    case tb::Ending::HqCaptured:
      how = won ? "YOU TOOK THEIR H.Q." : "THEY TOOK YOUR H.Q.";
      break;
    case tb::Ending::MedalsObjective:
      how = won ? "YOU REACHED THE MEDALS" : "THEY REACHED THE MEDALS";
      break;
    case tb::Ending::Stuck:
      how = "NOBODY COULD MOVE. MEDALS DECIDED IT";
      break;
    case tb::Ending::None:
      break;
  }
  fui::TextStyle body;
  body.font = toybox::kSmallFont;
  body.align = fui::TextAlign::Center;
  screen.target().text(screen.takeTop(28, toybox::kGutter), how, body);

  char tally[40];
  std::snprintf(tally, sizeof(tally), "MEDALS %d - %d", game.medals[model.seat], game.medals[model.seat ^ 1]);
  fui::TextStyle big;
  big.font = toybox::kDisplayFont;
  big.align = fui::TextAlign::Center;
  screen.target().text(screen.takeTop(60, toybox::kGutter), tally, big);

  fui::ButtonProps done;
  done.label = "DONE";
  done.action = ActionDone;
  screen.button(done, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));

  fui::ButtonProps again;
  again.label = "PLAY AGAIN";
  again.action = ActionAgain;
  screen.button(again, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));
}


void buildBoard(toybox::Screen& screen, const BoardModel& model) {
  const tb::Game& game = model.game;
  const tb::Terrain& b = game.board();

  fui::HeaderProps header;
  header.title = "TOY BATTLE";
  header.borderEdges = fui::EdgesNone;
  screen.header(header);

  const fui::DeviceContext device = screen.device();

  screen.target().fill(fui::makeRect(0, toybox::kHeaderHeight + 4, device.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));

  // Medals ride in the black band beside the title, where the eye already goes
  // and where they cost no body space. They were on their own line under the
  // rule and Mario could not find them.
  char medals[20];
  std::snprintf(medals, sizeof(medals), "%d-%d OF %d", game.medals[model.seat], game.medals[model.seat ^ 1],
                b.medalsObjective);
  fui::TextStyle band;
  band.font = toybox::kUiFont;
  band.align = fui::TextAlign::Right;
  band.color = fui::Color::White;
  // Centred on the band, not on a guessed y: the counter sits beside the title
  // and has to share its middle.
  const int16_t bandLine = screen.target().lineHeight(toybox::kUiFont);
  screen.target().text(fui::makeRect(0, static_cast<int16_t>((toybox::kHeaderHeight - bandLine) / 2),
                                     static_cast<int16_t>(device.width - toybox::kMargin), bandLine),
                       medals, band);

  // The line the question lives on, now that the foot of the screen is
  // controls rather than commentary.
  fui::TextStyle prompt;
  prompt.font = toybox::kSmallFont;
  prompt.align = fui::TextAlign::Center;
  screen.target().text(
      fui::makeRect(toybox::kMargin, kPromptTop, static_cast<int16_t>(device.width - toybox::kMargin * 2), 22),
      model.prompt, prompt);

  // Paths first, so the bases sit on top of them.
  for (int e = 0; e < b.edgeCount; ++e) {
    const fui::Point a = slotCenter(device, b, b.edges[e].a);
    const fui::Point z = slotCenter(device, b, b.edges[e].b);
    screen.target().line(a, z, kPathWeight, fui::Paint::solid(fui::Color::Black));
  }

  // Medals, sitting in the region they belong to. Without these the board is a
  // graph rather than a game: the whole second victory condition is invisible.
  //
  // The anchor is baked into the terrain rather than averaged here. Averaging
  // the fence bases is what this used to do, and it put every thin region's
  // medals hard against the column that fences it -- and on a board whose two
  // halves were traced by hand, it put the left ones and the right ones in
  // visibly different places. See Region::x in ToyBattleCore.h.
  for (int r = 0; r < b.regionCount; ++r) {
    if (game.regionsTaken & (1u << r)) continue;  // banked, and gone from the board
    const fui::Point at = boardPoint(device, b.regions[r].x, b.regions[r].y);
    const int count = b.regions[r].medals;
    const int16_t pipR = 7;
    const int16_t span = static_cast<int16_t>(count * (pipR * 2 + 3) - 3);
    // A white plate knocked out from under the cluster, because an anchor can
    // still land on a path when a region is narrow enough.
    const fui::Rect plate =
        fui::makeRect(static_cast<int16_t>(at.x - span / 2 - 5), static_cast<int16_t>(at.y - pipR - 4),
                      static_cast<int16_t>(span + 10), static_cast<int16_t>(pipR * 2 + 8));
    screen.target().fill(plate, fui::Paint::solid(fui::Color::White), 8);
    for (int i = 0; i < count; ++i) {
      toybox::disc(screen, static_cast<int16_t>(at.x - span / 2 + pipR + i * (pipR * 2 + 3)), at.y, pipR,
                   fui::Color::Black);
    }
  }

  const uint64_t candidates = toybattle::candidateSlots(game, model.draft);
  for (int slot = 0; slot < b.slotCount(); ++slot) {
    drawSlot(screen, slotCenter(device, b, slot), game, slot, (candidates & (uint64_t{1} << slot)) != 0);
  }

  // The rack. A troop you cannot play dims rather than disappearing.
  const uint8_t offer = toybattle::candidateTroops(game, model.draft);
  const tb::Draft& draft = model.draft;
  const bool chosenPending = toybattle::pending(game, draft) != toybattle::Ask::Troop;
  for (int position = 0; position < tb::kTroopKinds; ++position) {
    const fui::Rect tile = rackTile(device, position);
    const fui::Rect inner = fui::makeRect(static_cast<int16_t>(tile.x + 3), static_cast<int16_t>(tile.y + 3),
                                          static_cast<int16_t>(tile.width - 6), static_cast<int16_t>(tile.height - 6));
    const int kind = handKindAt(game, model.seat, position);

    // An empty slot is drawn, not skipped: the rack is eight places and seeing
    // how many are free is how you know whether you can draw.
    if (kind < 0) {
      screen.target().stroke(inner, fui::Paint::dither(fui::Color::LightGray), 2, 8);
      continue;
    }

    const tb::Troop troop = static_cast<tb::Troop>(kind);
    const bool live = (offer & (1u << kind)) != 0;
    const bool chosen = chosenPending && draft.move.stepCount > draft.step && draft.move.steps[draft.step].kind == kind;

    // Three grounds, because there are three states and two of them are not the
    // same kind of "no". A troop with nowhere legal to go is white; a troop
    // Battlefield pointed at is FROZEN, and that is something done to you rather
    // than a shape of the board, so it gets the darker of the only two dithers
    // this device has. Tapping it still says why -- PINNED: IT SITS OUT THIS
    // TURN -- but a state you have to tap to discover is a state you do not know
    // you are in when you are planning.
    const bool frozen = game.frozenKind[model.seat] == static_cast<uint8_t>(kind);
    screen.target().fill(inner,
                         frozen ? fui::Paint::dither(fui::Color::DarkGray)
                                : live ? fui::Paint::dither(fui::Color::LightGray)
                                       : fui::Paint::solid(fui::Color::White),
                         8);
    screen.target().stroke(inner, fui::Paint::solid(fui::Color::Black), chosen ? 5 : 2, 8);

    // Number on top, what it does underneath. Eight troops is more than anyone
    // holds in their head, and the mark here is the same one the board wears,
    // so learning it once covers both.
    //
    // Kwak and Roxy do nothing, so there is no mark to sit under and their
    // numeral takes the whole tile.
    const bool marked = troop != tb::Troop::Kwak && troop != tb::Troop::Roxy;
    if (!marked) {
      centred(screen, inner, pip(troop), toybox::kUiFont, false);
    } else {
      centred(screen, fui::makeRect(inner.x, static_cast<int16_t>(inner.y + 9), inner.width, 26), pip(troop),
              toybox::kUiFont, false);
      troopMark(screen,
                fui::Point{static_cast<int16_t>(inner.x + inner.width / 2), static_cast<int16_t>(inner.bottom() - 17)},
                12, troop);
    }
  }

  // Three regions, and the left one is always the same width so nothing
  // reflows under a thumb: one wide button, or two narrow ones.
  const int16_t barY = kCapsuleTop;
  const int16_t full = static_cast<int16_t>(device.width - toybox::kMargin * 2);
  const int16_t briefW = 52;
  // The counts need 100px. Everything else is the action, which is what a
  // thumb is aiming at.
  constexpr int16_t kCountsW = 88;
  const int16_t actionW = static_cast<int16_t>(full - briefW - kCountsW - toybox::kGutter * 2);
  const int16_t countsX = static_cast<int16_t>(toybox::kMargin + actionW + toybox::kGutter);
  const int16_t briefX = static_cast<int16_t>(toybox::kMargin + full - briefW);
  const int16_t halfW = static_cast<int16_t>((actionW - toybox::kGutter) / 2);

  const toybattle::Ask ask = toybattle::pending(game, model.draft);
  const bool picking = model.draft.move.stepCount > model.draft.step || model.draft.slotChosen;

  const auto place = [&](const char* label, const fui::ActionId action, const bool live, const int16_t x,
                         const int16_t w) {
    fui::ButtonProps props;
    props.label = label;
    props.action = live ? action : fui::NO_ACTION;
    // A control that cannot act dims rather than disappearing.
    if (!live) props.styles = toybox::disabledButtonStyles();
    screen.button(props, fui::makeRect(x, barY, w, toybox::kPillHeight));
  };

  if (!model.yourTurn) {
    place("WAIT", ActionSkip, false, toybox::kMargin, actionW);
  } else if (ask == toybattle::Ask::Troop && !picking) {
    place("DRAW 2", ActionDraw, model.canDraw, toybox::kMargin, actionW);
  } else if (ask == toybattle::Ask::Troop || ask == toybattle::Ask::Slot) {
    place("CANCEL", ActionCancel, true, toybox::kMargin, actionW);
  } else {
    const bool targeted = ask == toybattle::Ask::JumboVictim || ask == toybattle::Ask::RecallFrom ||
                          ask == toybattle::Ask::ShoveFrom || ask == toybattle::Ask::ShoveTo ||
                          ask == toybattle::Ask::ExhumeKind;
    place("SKIP", ActionSkip, true, toybox::kMargin, halfW);
    place(targeted ? "BACK" : "TAKE", targeted ? ActionCancel : ActionTake, true,
          static_cast<int16_t>(toybox::kMargin + halfW + toybox::kGutter), halfW);
  }
  place("?", ActionBrief, true, briefX, briefW);

  // Everything a player could count for themselves at a table: the piles are
  // visible as heights, the discard is face up, and their tiles can be counted
  // even though their faces cannot be read.
  //
  // Tile is troops in hand, triangle is still to draw, cross is discarded and
  // gone for good. Both rows carry all three.
  {
    const int me = model.seat, them = model.seat ^ 1;
    int out[2] = {0, 0};
    for (int seat = 0; seat < tb::kSeats; ++seat) {
      for (int k = 0; k < tb::kTroopKinds; ++k) out[seat] += game.discarded[seat][k];
    }
    fui::TextStyle cell;
    cell.font = toybox::kSmallFont;
    cell.align = fui::TextAlign::Center;

    // No headings, no row labels, no words. The marks already exist -- a down
    // triangle is "comes from the reserve" on every card that draws, a cross is
    // "removed" on Jumbo and on a nullified base -- and the rows need no
    // labelling because the board says which end is which: their H.Q. is at the
    // top, yours at the bottom, and your own rack is the row directly above.
    //
    // 88px against 194 for the spelled-out version, and 232 for the table that
    // started above the rack.
    //
    // The hand gets a narrower column than the other two because it is the one
    // number that cannot reach double figures -- the rack holds eight. Given
    // the same pitch, its single digit left fourteen pixels of slack and the
    // group read as detached from the row rather than as the start of it.
    constexpr int16_t kHandW = 24;
    constexpr int16_t kPitch = 34;
    const int16_t top = static_cast<int16_t>(barY + (toybox::kPillHeight - kCountsRow * 2) / 2);

    const auto count = [&](const int16_t x, const int16_t y, const int value, const tb::Special mark) {
      const int16_t mid = static_cast<int16_t>(y + kCountsRow / 2);
      if (mark == tb::Special::None) {
        // The hand: a solid tile. An outline at this size is a zero, and the
        // two overlapping tiles that fixed that were a shape nobody reads at
        // 10px. Filled and rectangular is the only version that is a tile at a
        // glance -- the count beside it does the counting.
        screen.target().fill(fui::makeRect(x, static_cast<int16_t>(mid - 5), 7, 11),
                             fui::Paint::solid(fui::Color::Black));
      } else {
        glyph(screen, fui::Point{static_cast<int16_t>(x + 4), mid}, 9, mark, false);
      }
      char field[8];
      std::snprintf(field, sizeof(field), "%d", value);
      screen.target().text(fui::makeRect(static_cast<int16_t>(x + 10), y, 20, kCountsRow), field, cell);
    };

    cell.align = fui::TextAlign::Left;
    for (int row = 0; row < 2; ++row) {
      const int seat = row == 0 ? them : me;
      const int16_t y = static_cast<int16_t>(top + row * kCountsRow);
      // Yours is on the rack below as well, but a row missing its first column
      // reads as a bug rather than as an economy.
      count(countsX, y, game.rackSize(seat), tb::Special::None);
      count(static_cast<int16_t>(countsX + kHandW), y, game.reserveRemaining(seat), tb::Special::Draw);
      count(static_cast<int16_t>(countsX + kHandW + kPitch), y, out[seat], tb::Special::Nullify);
    }
  }



}

}  // namespace tbui

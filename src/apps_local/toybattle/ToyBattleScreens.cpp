#include "ToyBattleScreens.h"

#include <cstdio>

#include "ToyBattleMenus.h"

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
    case tb::Troop::Capn: {
      // Go again: a shaft with a riser at its far end and a head pointing back
      // the way you came. Drawn from the same triangle the rest of the alphabet
      // uses, so it is one mark rather than a picture.
      const int16_t bar = static_cast<int16_t>(at.y + h / 2);
      screen.target().fill(fui::makeRect(static_cast<int16_t>(at.x - h / 2), bar, static_cast<int16_t>(h + h / 2), 2),
                           ink);
      screen.target().fill(fui::makeRect(static_cast<int16_t>(at.x + h - 2), static_cast<int16_t>(at.y - h), 2,
                                         static_cast<int16_t>(h + h / 2 + 2)),
                           ink);
      screen.target().triangle(fui::Point{static_cast<int16_t>(at.x - h), static_cast<int16_t>(bar + 1)},
                               fui::Point{static_cast<int16_t>(at.x - h / 3), static_cast<int16_t>(bar - h / 2 + 1)},
                               fui::Point{static_cast<int16_t>(at.x - h / 3), static_cast<int16_t>(bar + h / 2 + 2)},
                               ink);
      break;
    }
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
//
// Every kind wears one. Gate and Nullify used to be silhouette-only, on the
// theory that square corners said enough -- but the ? card lists all seven with
// their badges, so the two rules the board did not mark were the two a player
// looked up and then could not find. Silhouette and badge answer different
// questions: the corners say "there is a rule about what may land here", the
// badge says WHICH rule.
void baseBadge(toybox::Screen& screen, const fui::Rect box, const tb::Special what) {
  if (what == tb::Special::None) return;
  const int16_t r = 9;
  const fui::Point at{static_cast<int16_t>(box.right() - 2), static_cast<int16_t>(box.y + 2)};
  toybox::disc(screen, at.x, at.y, r, fui::Color::Black);
  glyph(screen, at, 8, what, true);
}

// What a gate admits, in as few characters as fit under a troop. Written as a
// range when the numbers run consecutively, which every gate on every printed
// board so far does: 1-2, 3-5, 6-7. A joker is a star in front, because the
// joker has no printed value and La Croisette's gate takes one.
void gateLabel(const uint8_t admits, char* out, const int size) {
  int lo = -1, hi = -1, count = 0;
  for (int k = 1; k < tb::kTroopKinds; ++k) {
    if (!(admits & (1u << k))) continue;
    if (lo < 0) lo = k;
    hi = k;
    ++count;
  }
  const bool joker = (admits & 1u) != 0;
  const bool run = count > 1 && (hi - lo + 1) == count;
  int n = 0;
  if (joker && n + 1 < size) out[n++] = '*';
  if (count == 0) {
    // A gate that admits only the joker. Nothing on a printed board does this,
    // but the star alone says it rather than leaving an empty strip.
  } else if (run) {
    if (n + 3 < size) {
      out[n++] = static_cast<char>('0' + lo);
      out[n++] = '-';
      out[n++] = static_cast<char>('0' + hi);
    }
  } else {
    for (int k = 1; k < tb::kTroopKinds && n + 1 < size; ++k) {
      if (admits & (1u << k)) out[n++] = static_cast<char>('0' + k);
    }
  }
  out[n] = '\0';
}

// One base, drawn where `slotCenter` says it is.
void drawSlot(toybox::Screen& screen, const fui::Point at, const tb::Game& game, const int slot, const bool candidate,
              const int viewer) {
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
  // Square corners mean "there is a rule about what may LAND here", which is
  // true of a gate and false of a nullifier: a nullifier takes any troop and
  // refuses only its effect. It was drawn square anyway, which told the player
  // to bring a particular value to a base that takes every value.
  const bool restricts = gated || special == tb::Special::Gate;
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

  // A gated slot says what it admits on a tab just under it, on a white plate
  // like the medals wear. Inside the slot it does not fit: the slot is 52px, the
  // small cut is nearly twenty of them, and the strip clipped its own digits
  // against a six-pixel border. Under it, the whole slot stays available for the
  // troop, and the plate keeps the values legible where a path runs beneath.
  //
  // It has to survive a troop standing on the slot, for the reason the
  // silhouette does: a gate you have to remember is a gate you misplay.
  if (gated) {
    char admits[10];
    gateLabel(b.gate[slot], admits, static_cast<int>(sizeof(admits)));
    fui::TextStyle values;
    values.font = toybox::kTileFont;
    values.align = fui::TextAlign::Center;
    const int16_t lineH = screen.target().lineHeight(toybox::kTileFont);
    const int16_t tabW = 46;
    const int16_t tabH = static_cast<int16_t>(lineH + 4);
    // Below the slot, unless there is no room below. The lowest row of a board
    // sits within a few pixels of the rack -- the board's own bottom bound --
    // and a tab hung under it reached six pixels into the first card in hand.
    // Above is the same distance from the slot and nothing else lives there.
    const bool roomBelow = box.bottom() - 2 + tabH <= kBoardTop + kBoardHeight;
    const fui::Rect tab = fui::makeRect(
        static_cast<int16_t>(at.x - tabW / 2),
        roomBelow ? static_cast<int16_t>(box.bottom() - 2) : static_cast<int16_t>(box.y - tabH + 2), tabW, tabH);
    screen.target().fill(tab, fui::Paint::solid(fui::Color::White), 5);
    screen.target().stroke(tab, fui::Paint::solid(fui::Color::Black), 2, 5);
    screen.target().text(fui::makeRect(tab.x, static_cast<int16_t>(tab.y + 2), tab.width, lineH), admits, values);
  }

  if (isHq) {
    // H is YOURS and E is theirs, from the seat looking at the screen. This
    // read `hqOwner(slot) == 0`, which is only the same thing while you are
    // always seat 0 -- playing second labelled your own H.Q. as the enemy's,
    // on the boards where taking the other side is the whole point.
    centred(screen, box, b.hqOwner(slot) == viewer ? "H" : "E", toybox::kUiFont, false);
    return;
  }

  if (holder != tb::kNoSeat) {
    const tb::Troop kind = game.occupantTroop(slot);
    // The holder is told by inversion rather than by a second shape: yours is
    // knocked out of black, theirs sits on the ground.
    // Yours knocked out of black, theirs on the ground -- from the VIEWER's
    // seat. This was `holder == 0`, so playing second drew the enemy's troops
    // as yours and yours as theirs: the same seat-0 assumption as the H.Q.
    // letter, in the shape that would have been hardest to notice, because the
    // board still looks like a board.
    const bool ours = holder == viewer;
    if (ours) {
      screen.target().fill(fui::makeRect(static_cast<int16_t>(box.x + 6), static_cast<int16_t>(box.y + 6),
                                         static_cast<int16_t>(box.width - 12), static_cast<int16_t>(box.height - 12)),
                           fui::Paint::solid(fui::Color::Black), 6);
    }
    centred(screen, box, pip(kind), toybox::kUiFont, ours);

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
  // (see slotCenter: the seat's half-turn is applied by the caller)
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

fui::Point slotCenter(const fui::DeviceContext& device, const tb::Terrain& board, const int slot, const int seat) {
  // Straight from the terrain. The coordinates are already balanced: the
  // tracing tool aligns the rows and columns, mirrors the board about its own
  // midline where the terrain is symmetric, and bakes the result into the
  // table, so the device never pays for the algorithm.
  //
  // The seat's half-turn is applied HERE and nowhere else. Playing seat 1 on a
  // board whose coordinates put seat 0 at the bottom would otherwise mean
  // playing upside down -- your own H.Q. at the top, your rack at the far end
  // of the board from it -- and turning it round is the whole reason a side is
  // worth offering. Because drawing, hit-testing, path drawing and the medal
  // anchors all come through this one function, they cannot disagree about
  // which way up the board is.
  const uint16_t x = seat == 0 ? board.x[slot] : static_cast<uint16_t>(1000 - board.x[slot]);
  const uint16_t y = seat == 0 ? board.y[slot] : static_cast<uint16_t>(1000 - board.y[slot]);
  return boardPoint(device, x, y);
}

int slotAt(const fui::DeviceContext& device, const tb::Terrain& board, const int x, const int y, const int seat) {
  const int16_t r = slotRadius();
  for (int slot = 0; slot < board.slotCount(); ++slot) {
    const fui::Point p = slotCenter(device, board, slot, seat);
    const int dx = x - p.x, dy = y - p.y;
    if (dx * dx + dy * dy <= r * r) return slot;
  }
  return -1;
}

// THE CARD FACE. The numeral over the mark, exactly as the rack draws it.
//
// One function rather than one per screen, because the rack is the card a
// player actually holds and taps: any reference drawn a second way is a second
// object to learn. The rules deck used to draw its own -- a 62x78 tile with the
// numeral in a 30px box and the mark hard-coded at y+56 -- and every mark on
// that screen landed on top of its own number.
//
// Proportions are the rack's own 48x64 inner tile, scaled, so this is the same
// card at any size rather than a redrawing of it.
void troopCardFace(toybox::Screen& screen, const fui::Rect& card, const tb::Troop troop, const fui::Paint ground,
                   const uint8_t edge) {
  screen.target().fill(card, ground, 8);
  screen.target().stroke(card, fui::Paint::solid(fui::Color::Black), edge, 8);

  // Kwak and Roxy do nothing, so there is no mark to sit under and the numeral
  // takes the whole card. A mark meaning "no mark" is worse than the space.
  const bool marked = troop != tb::Troop::Kwak && troop != tb::Troop::Roxy;
  if (!marked) {
    centred(screen, card, pip(troop), toybox::kUiFont, false);
    return;
  }
  const int16_t h = card.height;
  centred(
      screen,
      fui::makeRect(card.x, static_cast<int16_t>(card.y + h * 9 / 64), card.width, static_cast<int16_t>(h * 26 / 64)),
      pip(troop), toybox::kUiFont, false);
  troopMark(
      screen,
      fui::Point{static_cast<int16_t>(card.x + card.width / 2), static_cast<int16_t>(card.bottom() - h * 17 / 64)},
      static_cast<int16_t>(card.width * 12 / 48), troop);
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

int discardKindAt(const tb::Game& game, const tb::Draft& draft, const int seat, const int position) {
  (void)seat;  // the flow answers for whoever's turn it is, which is the mover
  int at = 0;
  for (int kind = 0; kind < tb::kTroopKinds; ++kind) {
    // What is LEFT after earlier links of a Cap'n chain took theirs, not what
    // the discard held when the turn began. Offering a troop that is already
    // spoken for is how the second grave in a chain became a dead end.
    for (int gone = 0; gone < tb::discardLeft(game, draft, kind); ++gone) {
      if (at == position) return kind;
      ++at;
    }
  }
  return -1;
}

// What the rack ROW is showing right now. Normally your hand; during the
// Cursed Cemetery question it is your DISCARD, because that is what the
// question is about and there is nowhere else on the screen to put it.
//
// One function, called by the drawing AND by the hit test, for the same reason
// slotCenter is: the alternative is two places that agree until they do not.
// Before this existed the row always drew the hand and the tap always called
// answerTroop, so the Exhume prompt asked "RAISE ONE FROM THE DISCARD?" while
// offering nothing that could answer it. Mario found that by playing.
int rowKindAt(const tb::Game& game, const tb::Draft& draft, const int seat, const int position) {
  if (tb::pending(game, draft) == tb::Ask::ExhumeKind) return discardKindAt(game, draft, seat, position);
  // The hand as it stands AFTER the placements already committed in this draft.
  // A troop you have put on the board must not still be sitting in your rack
  // while the game asks about its effect: the board and the rack would show the
  // same card twice, and you are left working out from memory what you did.
  //
  // Here rather than at the call sites because this function is what the
  // drawing AND the hit test both go through; split, they could disagree about
  // which tile holds what.
  const tb::Game shown = tb::projected(game, draft);
  return handKindAt(shown, seat, position);
}

int rackAt(const fui::DeviceContext& device, const tb::Game& game, const tb::Draft& draft, const int seat, const int x,
           const int y) {
  for (int position = 0; position < tb::kTroopKinds; ++position) {
    const fui::Rect r = rackTile(device, position);
    if (x >= r.x && x < r.right() && y >= r.y && y < r.bottom()) return rowKindAt(game, draft, seat, position);
  }
  return -1;
}

// --- screens ----------------------------------------------------------------

const char* specialBlurb(const tb::Special what) {
  switch (what) {
    case tb::Special::Recall:
      return "CALLS ANOTHER OF YOURS HOME";
    case tb::Special::Draw:
      return "DRAWS ONE FROM YOUR RESERVE";
    case tb::Special::Shove:
      return "SHOVES A NEIGHBOUR ONE BASE";
    case tb::Special::Exhume:
      return "TAKES ONE OF YOURS OFF THE DISCARD";
    case tb::Special::Suppress:
      return "PINS ONE ON THEIR RACK FOR A TURN";
    case tb::Special::Gate:
      return "ONLY PRINTED VALUES MAY LAND HERE";
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

// THE TROOP REFERENCE. Every card, its real face, and what it does.
//
// One page rather than a footer under the map's special bases: crammed under
// them it got a 30px box with the numeral in it and the mark parked off to one
// side, which is not the card -- the card is the numeral OVER the mark, and
// that is the object the player is holding. On the four-kind maps the list also
// reached within one row of the bottom edge, so the reference nobody can read
// was also the reference that nearly did not fit.
//
// Drawn into whatever box it is handed, so the in-game ? card and the rules
// deck show the same screen rather than two descriptions of one.
void troopReference(toybox::Screen& screen, const fui::Rect& box, const int columns) {
  fui::TextStyle body;
  body.font = toybox::kSmallFont;
  body.align = fui::TextAlign::Left;
  body.maxLines = 3;

  const int cols = columns < 1 ? 1 : columns;
  const int rows = (tb::kTroopKinds + cols - 1) / cols;
  const int16_t colW = static_cast<int16_t>((box.width - toybox::kGutter * (cols - 1)) / cols);
  const int16_t rowH = static_cast<int16_t>(box.height / rows);

  // The rack's own proportions: 48 wide to 64 tall. A card at another ratio is
  // a different object, so the height is what varies and the width follows.
  int16_t cardH = static_cast<int16_t>(rowH - 6);
  if (cardH > 64) cardH = 64;  // the rack's own size; bigger is a different object
  const int16_t cardW = static_cast<int16_t>(cardH * 48 / 64);

  for (int k = 0; k < tb::kTroopKinds; ++k) {
    const tb::Troop troop = static_cast<tb::Troop>(k);
    const int col = k / rows, row = k % rows;
    const fui::Rect cell = fui::makeRect(static_cast<int16_t>(box.x + col * (colW + toybox::kGutter)),
                                         static_cast<int16_t>(box.y + row * rowH), colW, rowH);
    troopCardFace(screen, fui::makeRect(cell.x, static_cast<int16_t>(cell.y + (rowH - cardH) / 2), cardW, cardH), troop,
                  fui::Paint::dither(fui::Color::LightGray), 2);
    const int16_t textX = static_cast<int16_t>(cell.x + cardW + toybox::kGutter);
    screen.target().text(fui::makeRect(textX, cell.y, static_cast<int16_t>(cell.right() - textX), rowH),
                         troopBlurb(troop), body);
  }
}

// The briefing: what this map's special bases do, what the counts in the bar
// mean, and every troop with its own card face and what it does. One screen.
//
// It was two for a day and Mario did not want two. The thing that buys the one
// page back is the troop grid: eight full-width rows plus a four-kind map's
// specials come to 721px in a 672px content rect, and four rows of two come to
// 304. The card gets BIGGER doing it, because the height a row may spend is
// what doubled.
void buildBrief(toybox::Screen& screen, const BriefModel& model) {
  const char* title = model.board ? model.board->name : "TERRAIN";
  // "CURSED CEMETERY" came out of the display cut as "CURSED CEMETER" -- not a
  // wrapped line and not an ellipsis, just a name with its last letter gone,
  // which reads as a misspelling rather than as an overflow. The map names are
  // data and the longest is fifteen characters today. The ladder that fixes it
  // is toybox::headerBand's now, so every band in the fork has it and this
  // screen carries no copy: host-tests/fittedtitle walks all ten maps.
  fui::HeaderProps header;
  header.title = title;
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
  if (!model.board) return;

  fui::TextStyle body;
  body.font = toybox::kSmallFont;
  body.align = fui::TextAlign::Left;

  if (!model.specialBases) {
    fui::TextStyle wrapped = body;
    wrapped.maxLines = 3;
    const char* off = "SPECIAL BASES ARE SWITCHED OFF FOR THIS GAME. EVERY BASE IS AN ORDINARY ONE.";
    const fui::Rect room = screen.contentRect();
    screen.target().text(screen.takeTop(fui::measureWrappedText(screen.target(), off, wrapped, room.width).height), off,
                         wrapped);
    return;
  }

  // One row per kind actually on this board, counted so it says how many.
  int seen[8] = {};
  for (int base = 0; base < model.board->baseCount; ++base) {
    ++seen[static_cast<int>(model.board->specialAt(base))];
  }

  // The map's list is the part that varies -- nought to four kinds -- so it is
  // the part that gets a ceiling, and the troops take everything left under the
  // rule. Reserving a fixed band for the troops instead left a hole in the
  // middle of every map that has one special kind, which reads as a missing
  // element rather than as air.
  constexpr int16_t kTroopFloor = 300;

  bool any = false;
  int kinds = 0;
  for (int k = 1; k < 8; ++k) {
    if (seen[k]) ++kinds;
  }
  // Four kinds is the most any printed board carries. The rows share what is
  // left of the space above the troops rather than claiming a fixed 52 each.
  const fui::Rect whole = screen.contentRect();
  const int16_t legendBand = 30, ruleBand = toybox::kRule + toybox::kGutter * 2;
  const int16_t forList =
      static_cast<int16_t>(whole.height - kTroopFloor - legendBand - ruleBand - toybox::kGutter * 3);
  int16_t specialRow = kinds > 0 ? static_cast<int16_t>(forList / kinds) : 0;
  if (specialRow > 52) specialRow = 52;
  if (specialRow < 34) specialRow = 34;

  for (int k = 1; k < 8; ++k) {
    if (!seen[k]) continue;
    any = true;
    const tb::Special what = static_cast<tb::Special>(k);
    const fui::Rect row = screen.takeTop(specialRow, 4);

    const int16_t badge = static_cast<int16_t>(specialRow / 3 + 1);
    const fui::Point at{static_cast<int16_t>(row.x + badge), static_cast<int16_t>(row.y + row.height / 2)};
    toybox::disc(screen, at.x, at.y, badge, fui::Color::Black);
    glyph(screen, at, static_cast<int16_t>(badge - 2), what, true);

    char line[96];
    std::snprintf(line, sizeof(line), "%d x %s", seen[k], specialBlurb(what));
    screen.target().text(fui::makeRect(static_cast<int16_t>(row.x + badge * 2 + toybox::kGutter), row.y,
                                       static_cast<int16_t>(row.width - badge * 2 - toybox::kGutter), row.height),
                         line, body);
  }
  if (!any) {
    screen.target().text(screen.takeTop(52), "THIS TERRAIN HAS NO SPECIAL BASES.", body);
  }

  // The bar's counts, explained where the other marks are explained -- and
  // explained with the marks themselves rather than with words for them.
  //
  // It used to be one sentence naming each mark ("TILE = TROOPS IN HAND,
  // TRIANGLE = LEFT TO DRAW, ..."), which is 110 characters into a 448px row at
  // maxLines 1: the panel showed "IN THE BAR: TILE = TROOPS IN HAND, TRIANGLE =
  // LEFT ..." and the two marks it had not reached yet were the two nobody
  // knows. Wrapping it would have cost two more rows the card does not have
  // (La Croisette's four kinds already reach within a row of the bottom), so
  // the sentence is gone instead. Three marks and six words fit on one line
  // with room to spare, and a legend that draws the mark teaches the mark --
  // which the sentence never did, since it only ever named it.
  {
    const fui::Rect row = screen.takeTop(30, toybox::kGutter);
    const int16_t mid = static_cast<int16_t>(row.y + row.height / 2);
    const int16_t pitch = static_cast<int16_t>(row.width / 3);
    struct Item {
      tb::Special mark;  // None means the hand's solid tile
      const char* label;
    };
    static constexpr Item kLegend[] = {
        {tb::Special::None, "IN HAND"},
        {tb::Special::Draw, "TO DRAW"},
        {tb::Special::Nullify, "GONE"},
    };
    for (int i = 0; i < 3; ++i) {
      const int16_t x = static_cast<int16_t>(row.x + i * pitch);
      // The same two shapes the action bar draws, at the same proportions: a
      // filled tile for the hand, the glyph alphabet for the other two.
      if (kLegend[i].mark == tb::Special::None) {
        screen.target().fill(fui::makeRect(x, static_cast<int16_t>(mid - 6), 8, 13),
                             fui::Paint::solid(fui::Color::Black));
      } else {
        glyph(screen, fui::Point{static_cast<int16_t>(x + 5), mid}, 11, kLegend[i].mark, false);
      }
      screen.target().text(
          fui::makeRect(static_cast<int16_t>(x + 18), row.y, static_cast<int16_t>(pitch - 18), row.height),
          kLegend[i].label, body);
    }
  }

  // A rule, then the troops. Two alphabets on one screen need telling apart:
  // above it is what the ground does, below it is what your own troops do.
  const fui::Rect gap = screen.takeTop(ruleBand, toybox::kGutter);
  screen.target().fill(fui::makeRect(gap.x, static_cast<int16_t>(gap.y + toybox::kGutter), gap.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));

  troopReference(screen, screen.contentRect(), 2);
}

void buildResult(toybox::Screen& screen, const ResultModel& model) {
  const tb::Game& game = model.game;
  const bool won = game.winner == model.seat;

  fui::HeaderProps header;
  header.title = won ? "YOU WIN" : "YOU LOSE";
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
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
  screen.target().text(toybox::inkCentred(screen.takeTop(60, toybox::kGutter), toybox::kDisplayCut), tally, big);

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
  // What the board LOOKS like mid-move: the troop you have just placed is
  // standing on it while the game asks what its effect should do. Every
  // question below still goes through `game` and the draft -- only the drawing
  // reads this.
  const tb::Game shown = tb::projected(game, model.draft);
  const tb::Terrain& b = game.board();

  fui::HeaderProps header;
  header.title = "TOY BATTLE";
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);

  const fui::DeviceContext device = screen.device();

  toybox::headerRule(screen);

  // Medals ride in the black band beside the title, where the eye already goes
  // and where they cost no body space. They were on their own line under the
  // rule and Mario could not find them.
  char medals[20];
  std::snprintf(medals, sizeof(medals), "%d-%d OF %d", shown.medals[model.seat], shown.medals[model.seat ^ 1],
                b.medalsObjective);
  // A medals win happened in the tally, not on a base, so that is where it is
  // marked: the readout inverts to a solid plate. The design language spends
  // solid black on a surface that repaints once, and this one never repaints
  // again.
  const bool medalWin = game.currentPhase() != tb::Phase::Playing && game.endedBy() == tb::Ending::MedalsObjective;

  fui::TextStyle band;
  band.font = toybox::kUiFont;
  band.align = fui::TextAlign::Right;
  band.color = fui::Color::White;
  // Centred on the band, not on a guessed y: the counter sits beside the title
  // and has to share its middle.
  const int16_t bandLine = screen.target().lineHeight(toybox::kUiFont);
  const fui::Rect medalBox = fui::makeRect(0, toybox::bandCenterY(screen, bandLine),
                                           static_cast<int16_t>(device.width - toybox::kMargin), bandLine);
  if (medalWin) {
    // Knocked out of the black band: a white plate with the tally in ink.
    const int16_t w = screen.target().measureText(toybox::kUiFont, medals, band).width;
    screen.target().fill(
        fui::makeRect(static_cast<int16_t>(medalBox.right() - w - 10), static_cast<int16_t>(medalBox.y - 5),
                      static_cast<int16_t>(w + 20), static_cast<int16_t>(bandLine + 10)),
        fui::Paint::solid(fui::Color::White), 6);
    band.color = fui::Color::Black;
  }
  screen.target().text(medalBox, medals, band);

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
    const fui::Point a = slotCenter(device, b, b.edges[e].a, model.seat);
    const fui::Point z = slotCenter(device, b, b.edges[e].b, model.seat);
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
    if (shown.regionsTaken & (1u << r)) continue;  // banked, and gone from the board
    const fui::Point at = model.seat == 0 ? boardPoint(device, b.regions[r].x, b.regions[r].y)
                                          : boardPoint(device, static_cast<uint16_t>(1000 - b.regions[r].x),
                                                       static_cast<uint16_t>(1000 - b.regions[r].y));
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
    drawSlot(screen, slotCenter(device, b, slot, model.seat), shown, slot, (candidates & (uint64_t{1} << slot)) != 0,
             model.seat);
  }

  // WHY it ended, on the board rather than only in a sentence.
  //
  // An H.Q. capture is a place, so it gets marked in place: the last placement
  // of the game IS the troop that took it -- `apply` ends the game on the step
  // that lands there -- so there is no search and no second way of deciding
  // which slot it was.
  if (game.currentPhase() != tb::Phase::Playing && game.endedBy() == tb::Ending::HqCaptured &&
      game.placementCount > 0) {
    const int taken = game.placeSlot[game.placementCount - 1];
    const fui::Point at = slotCenter(device, b, taken, model.seat);
    const int16_t arm = static_cast<int16_t>(kSlot / 2 + 10);
    toybox::bracket(screen,
                    fui::makeRect(static_cast<int16_t>(at.x - arm), static_cast<int16_t>(at.y - arm),
                                  static_cast<int16_t>(arm * 2), static_cast<int16_t>(arm * 2)),
                    14, 5);
  }

  // The rack. A troop you cannot play dims rather than disappearing.
  const uint8_t offer = toybattle::candidateTroops(game, model.draft);
  const tb::Draft& draft = model.draft;
  const bool chosenPending = toybattle::pending(game, draft) != toybattle::Ask::Troop;
  for (int position = 0; position < tb::kTroopKinds; ++position) {
    const fui::Rect tile = rackTile(device, position);
    const fui::Rect inner = fui::makeRect(static_cast<int16_t>(tile.x + 3), static_cast<int16_t>(tile.y + 3),
                                          static_cast<int16_t>(tile.width - 6), static_cast<int16_t>(tile.height - 6));
    const int kind = rowKindAt(game, model.draft, model.seat, position);

    // An empty slot is drawn, not skipped: the rack is eight places and seeing
    // how many are free is how you know whether you can draw.
    if (kind < 0) {
      screen.target().stroke(inner, fui::Paint::dither(fui::Color::LightGray), 2, 8);
      continue;
    }

    const tb::Troop troop = static_cast<tb::Troop>(kind);
    // During the Exhume question every tile on the row is a legal answer, so
    // they all read live. `offer` is about placing from the hand and says
    // nothing about raising from the discard.
    const bool raising = toybattle::pending(game, model.draft) == toybattle::Ask::ExhumeKind;
    const bool live = raising || (offer & (1u << kind)) != 0;
    const bool chosen = chosenPending && draft.move.stepCount > draft.step && draft.move.steps[draft.step].kind == kind;

    // Three grounds, because there are three states and two of them are not the
    // same kind of "no". A troop with nowhere legal to go is white; a troop
    // Battlefield pointed at is FROZEN, and that is something done to you rather
    // than a shape of the board, so it gets the darker of the only two dithers
    // this device has. Tapping it still says why -- PINNED: IT SITS OUT THIS
    // TURN -- but a state you have to tap to discover is a state you do not know
    // you are in when you are planning.
    const bool frozen = !raising && game.frozenKind[model.seat] == static_cast<uint8_t>(kind);
    // Number on top, what it does underneath -- the card face, drawn by the one
    // function every screen that shows a card calls.
    troopCardFace(screen, inner, troop,
                  frozen ? fui::Paint::dither(fui::Color::DarkGray)
                  : live ? fui::Paint::dither(fui::Color::LightGray)
                         : fui::Paint::solid(fui::Color::White),
                  chosen ? 5 : 2);
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

  if (game.currentPhase() != tb::Phase::Playing) {
    // The game is over and the board is still up. One way on, loud, because it
    // is the only thing left to do -- and the ? stays beside it, since reading
    // what a base did is a fair thing to want at the end of a game.
    fui::ButtonProps done;
    done.label = "HOW IT ENDED";
    done.action = ActionResult;
    done.styles = toybox::invertedStyles();
    done.text.font = toybox::kUiFont;
    done.text.align = fui::TextAlign::Center;
    done.text.color = fui::Color::White;
    done.radius = toybox::kPillRadius;
    screen.button(done, fui::makeRect(toybox::kMargin, barY, actionW, toybox::kPillHeight));
  } else if (!model.yourTurn) {
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
      for (int k = 0; k < tb::kTroopKinds; ++k) out[seat] += shown.discarded[seat][k];
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
      screen.target().text(
          toybox::inkCentred(fui::makeRect(static_cast<int16_t>(x + 10), y, 20, kCountsRow), toybox::kTileCut), field,
          cell);
    };

    cell.align = fui::TextAlign::Left;
    for (int row = 0; row < 2; ++row) {
      const int seat = row == 0 ? them : me;
      const int16_t y = static_cast<int16_t>(top + row * kCountsRow);
      // Yours is on the rack below as well, but a row missing its first column
      // reads as a bug rather than as an economy.
      count(countsX, y, shown.rackSize(seat), tb::Special::None);
      count(static_cast<int16_t>(countsX + kHandW), y, shown.reserveRemaining(seat), tb::Special::Draw);
      count(static_cast<int16_t>(countsX + kHandW + kPitch), y, out[seat], tb::Special::Nullify);
    }
  }
}

}  // namespace tbui

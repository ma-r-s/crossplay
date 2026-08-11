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
  // Square corners mean this base has a rule about what may be placed on it.
  // A silhouette says it even with a troop standing on top; a mark would not.
  const bool restricts = special == tb::Special::Gate || special == tb::Special::Nullify;
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

fui::Point slotCenter(const fui::DeviceContext& device, const tb::Terrain& board, const int slot) {
  // Straight from the terrain. The coordinates are already balanced: the
  // tracing tool derives an even layout from the graph and bakes the result
  // into the table, so the device never pays for the algorithm.
  const int16_t left = boardLeft(device);
  const int16_t inset = kSlot / 2 + 4;
  const int16_t usableW = static_cast<int16_t>(448 - inset * 2);
  const int16_t usableH = static_cast<int16_t>(kBoardHeight - inset * 2);
  return fui::Point{static_cast<int16_t>(left + inset + board.x[slot] * usableW / 1000),
                    static_cast<int16_t>(kBoardTop + inset + board.y[slot] * usableH / 1000)};
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

fui::Rect rackTile(const fui::DeviceContext& device, const int kind) {
  const int16_t span = static_cast<int16_t>(tb::kTroopKinds * kRackTile);
  const int16_t left = static_cast<int16_t>((device.width - span) / 2);
  return fui::makeRect(static_cast<int16_t>(left + kind * kRackTile), kRackTop, kRackTile, kRackTall);
}

int rackAt(const fui::DeviceContext& device, const int x, const int y) {
  for (int kind = 0; kind < tb::kTroopKinds; ++kind) {
    const fui::Rect r = rackTile(device, kind);
    if (x >= r.x && x < r.right() && y >= r.y && y < r.bottom()) return kind;
  }
  return -1;
}

// --- screens ----------------------------------------------------------------

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  fui::HeaderProps header;
  header.title = "TOY BATTLE";
  header.borderEdges = fui::EdgesNone;
  screen.header(header);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  static const fui::ListItem kRows[] = {{"PLAY"}, {"HOW TO PLAY"}};
  fui::ListProps list;
  list.items = kRows;
  list.count = static_cast<uint16_t>(MenuRow::Count);
  list.selectedIndex = static_cast<int16_t>(model.selected);
  list.action = ActionMenuRow;
  screen.list(list, static_cast<int16_t>(toybox::kRowHeight * static_cast<int>(MenuRow::Count) + 8));
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
  screen.target().text(fui::makeRect(0, 30, static_cast<int16_t>(device.width - toybox::kMargin), 26), medals, band);

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
  // A region's medals go at the centre of the bases that fence it. On a
  // two-base region that centre lands on the path between them, so each
  // cluster knocks a white plate out from under itself first.
  for (int r = 0; r < b.regionCount; ++r) {
    if (game.regionsTaken & (1u << r)) continue;  // banked, and gone from the board
    int32_t sx = 0, sy = 0, n = 0;
    for (int base = 0; base < b.baseCount; ++base) {
      if (!(b.regions[r].bases & (uint32_t{1} << base))) continue;
      const fui::Point p = slotCenter(device, b, base);
      sx += p.x;
      sy += p.y;
      ++n;
    }
    if (!n) continue;
    int16_t cx = static_cast<int16_t>(sx / n);
    int16_t cy = static_cast<int16_t>(sy / n);
    if (n == 2) {
      // Two bases fence the water between two bridges, and their midpoint is
      // exactly on the path joining them -- medals there read as a severed
      // connection. Step sideways off it.
      int first = -1, second = -1;
      for (int base = 0; base < b.baseCount; ++base) {
        if (!(b.regions[r].bases & (uint32_t{1} << base))) continue;
        (first < 0 ? first : second) = base;
      }
      const fui::Point a = slotCenter(device, b, first);
      const fui::Point z = slotCenter(device, b, second);
      const int16_t dx = static_cast<int16_t>(z.x - a.x), dy = static_cast<int16_t>(z.y - a.y);
      const int len = dx * dx + dy * dy > 0 ? static_cast<int>(dx * dx + dy * dy) : 1;
      int16_t scale = 1;
      while (scale * scale * 4 < len) ++scale;  // integer hypot, near enough at this size
      cx = static_cast<int16_t>(cx - dy * 26 / (scale > 0 ? scale * 2 : 1));
      cy = static_cast<int16_t>(cy + dx * 26 / (scale > 0 ? scale * 2 : 1));
    }
    const int count = b.regions[r].medals;
    const int16_t pipR = 7;
    const int16_t span = static_cast<int16_t>(count * (pipR * 2 + 3) - 3);
    const fui::Rect plate = fui::makeRect(static_cast<int16_t>(cx - span / 2 - 5), static_cast<int16_t>(cy - pipR - 4),
                                          static_cast<int16_t>(span + 10), static_cast<int16_t>(pipR * 2 + 8));
    screen.target().fill(plate, fui::Paint::solid(fui::Color::White), 8);
    for (int i = 0; i < count; ++i) {
      toybox::disc(screen, static_cast<int16_t>(cx - span / 2 + pipR + i * (pipR * 2 + 3)), cy, pipR,
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
  for (int kind = 0; kind < tb::kTroopKinds; ++kind) {
    const fui::Rect tile = rackTile(device, kind);
    const fui::Rect inner = fui::makeRect(static_cast<int16_t>(tile.x + 3), static_cast<int16_t>(tile.y + 3),
                                          static_cast<int16_t>(tile.width - 6), static_cast<int16_t>(tile.height - 6));
    const int held = game.rack[model.seat][kind];
    const bool live = (offer & (1u << kind)) != 0;
    const bool chosen = chosenPending && draft.move.stepCount > draft.step && draft.move.steps[draft.step].kind == kind;

    if (held == 0) {
      screen.target().stroke(inner, fui::Paint::dither(fui::Color::LightGray), 2, 8);
      continue;
    }
    screen.target().fill(inner, live ? fui::Paint::dither(fui::Color::LightGray) : fui::Paint::solid(fui::Color::White),
                         8);
    screen.target().stroke(inner, fui::Paint::solid(fui::Color::Black), chosen ? 5 : 2, 8);
    // Number on top, what it does underneath. Eight troops is more than anyone
    // holds in their head, and the mark here is the same one the board wears,
    // so learning it once covers both.
    centred(screen, fui::makeRect(inner.x, static_cast<int16_t>(inner.y - 2), inner.width, 24),
            pip(static_cast<tb::Troop>(kind)), toybox::kUiFont, false);
    troopMark(screen,
              fui::Point{static_cast<int16_t>(inner.x + inner.width / 2), static_cast<int16_t>(inner.bottom() - 11)},
              12, static_cast<tb::Troop>(kind));

    // How many you hold, as pips down the left edge. "x2" as text sat on top of
    // the numeral: there is no room for two pieces of type on a 48px tile.
    for (int i = 1; i < held && i < 3; ++i) {
      screen.target().fill(
          fui::makeRect(static_cast<int16_t>(inner.x + 3), static_cast<int16_t>(inner.y + 4 + (i - 1) * 7), 4, 4),
          fui::Paint::solid(fui::Color::Black), 2);
    }
  }

  // The foot of the board is whatever the position actually offers, and every
  // button says what tapping it does. A turn is "place a troop" or "draw two",
  // and until this existed the second had no way in at all -- the old capsule
  // narrated the game without ever being the way to play it.
  const int16_t barY = kCapsuleTop;
  const int16_t full = static_cast<int16_t>(device.width - toybox::kMargin * 2);
  const int16_t half = static_cast<int16_t>((full - toybox::kGutter) / 2);
  const toybattle::Ask ask = toybattle::pending(game, model.draft);
  const bool picking = model.draft.move.stepCount > model.draft.step || model.draft.slotChosen;

  const auto place = [&](const char* label, const fui::ActionId action, const bool live, const int16_t x,
                         const int16_t w) {
    fui::ButtonProps props;
    props.label = label;
    props.action = live ? action : fui::NO_ACTION;
    // A control that cannot act dims rather than disappearing, so the row
    // never reflows under the thumb.
    if (!live) props.styles = toybox::disabledButtonStyles();
    screen.button(props, fui::makeRect(x, barY, w, toybox::kPillHeight));
  };

  if (!model.yourTurn) {
    place("THEIR MOVE", ActionSkip, false, toybox::kMargin, full);
  } else if (ask == toybattle::Ask::Troop && !picking) {
    place("DRAW 2", ActionDraw, model.canDraw, toybox::kMargin, full);
  } else if (ask == toybattle::Ask::Troop || ask == toybattle::Ask::Slot) {
    place("CANCEL", ActionCancel, true, toybox::kMargin, full);
  } else {
    // Every remaining question is optional, so it always has two answers.
    const bool targeted = ask == toybattle::Ask::JumboVictim || ask == toybattle::Ask::RecallFrom ||
                          ask == toybattle::Ask::ShoveFrom || ask == toybattle::Ask::ShoveTo ||
                          ask == toybattle::Ask::ExhumeKind;
    place("SKIP", ActionSkip, true, toybox::kMargin, half);
    place(targeted ? "CANCEL" : "TAKE IT", targeted ? ActionCancel : ActionTake, true,
          static_cast<int16_t>(toybox::kMargin + half + toybox::kGutter), half);
  }
}

}  // namespace tbui

#include "ToyBattleScreens.h"

#include <cstdio>

// Three board treatments, built together so they can be photographed side by
// side and one chosen. Options described in prose get judged wrong; options
// rendered at native size get judged.
//
//   1 SLABS  bases as chunky dithered slabs, thick paths, rack in one row.
//   2 DISCS  bases as light rings, thin paths, rack in one row.
//   3 TALL   slabs again, but the board takes the height back off the rack,
//            which becomes two rows of larger tiles.
//
// Build with: PLATFORMIO_BUILD_FLAGS="-DTOYBATTLE_BOARD=2" ./scripts_local/sim-shot.sh ...
#ifndef TOYBATTLE_BOARD
#define TOYBATTLE_BOARD 1
#endif

namespace tbui {
namespace {

namespace tb = toybattle;

constexpr int16_t kBoardTop = toybox::kHeaderHeight + toybox::kGutter * 2;  // 100

#if TOYBATTLE_BOARD == 3
constexpr int16_t kRackRows = 2;
constexpr int16_t kRackTile = 92;
#else
constexpr int16_t kRackRows = 1;
constexpr int16_t kRackTile = 54;
#endif

constexpr int16_t kRackHeight = kRackTile * kRackRows + 8;
constexpr int16_t kCapsuleTop = 800 - toybox::kMargin - toybox::kPillHeight;
constexpr int16_t kRackTop = kCapsuleTop - toybox::kGutter - kRackHeight;

// The board owns everything between the header and the rack.
constexpr int16_t kBoardHeight = kRackTop - toybox::kGutter - kBoardTop;

#if TOYBATTLE_BOARD == 2
constexpr int16_t kSlot = 46;
constexpr uint8_t kPathWeight = 3;
#else
constexpr int16_t kSlot = 52;
constexpr uint8_t kPathWeight = 5;
#endif

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

// One base, drawn where `slotCenter` says it is.
void drawSlot(toybox::Screen& screen, const fui::Point at, const tb::Game& game, const int slot, const bool candidate) {
  const tb::Terrain& b = game.board();
  const int16_t half = kSlot / 2;
  const fui::Rect box =
      fui::makeRect(static_cast<int16_t>(at.x - half), static_cast<int16_t>(at.y - half), kSlot, kSlot);
  const bool isHq = b.isHq(slot);
  const int holder = b.isBase(slot) ? game.occupantSeat(slot) : tb::kNoSeat;

#if TOYBATTLE_BOARD == 2
  // DISCS: a ring, filled only when somebody holds it. Lightest ink of the
  // three, which matters most on the surface that repaints every move.
  toybox::ring(screen, at.x, at.y, half, 3, fui::Color::Black,
               holder == tb::kNoSeat ? fui::Color::White : fui::Color::LightGray);
  if (isHq) toybox::ring(screen, at.x, at.y, static_cast<int16_t>(half - 7), 3, fui::Color::Black, fui::Color::White);
#else
  // SLABS: a dithered ground with a black edge. The dither is in the fill
  // because there is no grey ink on this device.
  screen.target().fill(box, fui::Paint::dither(holder == tb::kNoSeat ? fui::Color::LightGray : fui::Color::DarkGray),
                       8);
  screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), isHq ? 5 : 3, 8);
#endif

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
  // The terrain stores 0..1000 on both axes; the board rect is what turns that
  // into pixels, and it is the only place the mapping happens.
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
  const int16_t perRow = static_cast<int16_t>(tb::kTroopKinds / kRackRows);
  const int16_t row = static_cast<int16_t>(kind / perRow);
  const int16_t col = static_cast<int16_t>(kind % perRow);
  const int16_t span = static_cast<int16_t>(perRow * kRackTile);
  const int16_t left = static_cast<int16_t>((device.width - span) / 2);
  return fui::makeRect(static_cast<int16_t>(left + col * kRackTile), static_cast<int16_t>(kRackTop + row * kRackTile),
                       kRackTile, kRackTile);
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

  // Medals on their own line under the rule. They started in the black band
  // beside the title and collided with it -- the title is long and the band is
  // not a place to put anything that grows.
  char medals[28];
  std::snprintf(medals, sizeof(medals), "MEDALS %d - %d   TO WIN %d", game.medals[model.seat],
                game.medals[model.seat ^ 1], b.medalsObjective);
  fui::TextStyle report;
  report.font = toybox::kSmallFont;
  report.align = fui::TextAlign::Center;
  screen.target().text(fui::makeRect(toybox::kMargin, toybox::kHeaderHeight + 12,
                                     static_cast<int16_t>(device.width - toybox::kMargin * 2), 20),
                       medals, report);

  // Paths first, so the bases sit on top of them.
  for (int e = 0; e < b.edgeCount; ++e) {
    const fui::Point a = slotCenter(device, b, b.edges[e].a);
    const fui::Point z = slotCenter(device, b, b.edges[e].b);
    screen.target().line(a, z, kPathWeight, fui::Paint::solid(fui::Color::Black));
  }

  const uint64_t candidates = toybattle::candidateSlots(game, model.draft);
  for (int slot = 0; slot < b.slotCount(); ++slot) {
    drawSlot(screen, slotCenter(device, b, slot), game, slot, (candidates & (uint64_t{1} << slot)) != 0);
  }

  // The rack. A troop you cannot play dims rather than disappearing.
  const uint8_t offer = toybattle::candidateTroops(game, model.draft);
  const tb::Draft& draft = model.draft;
  const bool picking = toybattle::pending(game, draft) != toybattle::Ask::Troop;
  for (int kind = 0; kind < tb::kTroopKinds; ++kind) {
    const fui::Rect tile = rackTile(device, kind);
    const fui::Rect inner = fui::makeRect(static_cast<int16_t>(tile.x + 3), static_cast<int16_t>(tile.y + 3),
                                          static_cast<int16_t>(tile.width - 6), static_cast<int16_t>(tile.height - 6));
    const int held = game.rack[model.seat][kind];
    const bool live = (offer & (1u << kind)) != 0;
    const bool chosen = picking && draft.move.stepCount > draft.step && draft.move.steps[draft.step].kind == kind;

    if (held == 0) {
      screen.target().stroke(inner, fui::Paint::dither(fui::Color::LightGray), 2, 8);
      continue;
    }
    screen.target().fill(inner, live ? fui::Paint::dither(fui::Color::LightGray) : fui::Paint::solid(fui::Color::White),
                         8);
    screen.target().stroke(inner, fui::Paint::solid(fui::Color::Black), chosen ? 5 : 2, 8);
    centred(screen, fui::makeRect(inner.x, inner.y, inner.width, static_cast<int16_t>(inner.height - 12)),
            pip(static_cast<tb::Troop>(kind)), toybox::kDisplayFont, false);

    if (held > 1) {
      char n[4];
      std::snprintf(n, sizeof(n), "x%d", held);
      fui::TextStyle count;
      count.font = toybox::kSmallFont;
      count.align = fui::TextAlign::Center;
      screen.target().text(fui::makeRect(inner.x, static_cast<int16_t>(inner.bottom() - 16), inner.width, 14), n,
                           count);
    }
  }

  // The capsule names the one thing the board is waiting for.
  fui::ButtonProps capsule;
  capsule.label = model.capsule;
  capsule.action = model.capsuleLive ? ActionCapsule : fui::NO_ACTION;
  screen.button(capsule, fui::makeRect(toybox::kMargin, kCapsuleTop,
                                       static_cast<int16_t>(device.width - toybox::kMargin * 2), toybox::kPillHeight));
}

}  // namespace tbui

#include "ToyBattleMenus.h"

#include <cstdio>

#include "../link/LinkScreens.h"
#include "ToyBattleScreens.h"

namespace tbui {
namespace {

namespace tb = toybattle;

// The fork's signature chrome: the 76px band, a 4px gap, then a 3px rule. The
// right label is drawn with subtitleText and has to be set white by hand --
// unset, the theme substitutes black type onto a black band, which is invisible
// and indistinguishable from never having been set at all.
void chrome(toybox::Screen& screen, const char* title, const char* rightLabel = "") {
  fui::HeaderProps header;
  header.title = title;
  header.rightLabel = rightLabel;
  header.subtitleText = fui::TextStyle{};
  header.subtitleText.font = toybox::kUiFont;
  header.subtitleText.color = fui::Color::White;
  header.subtitleText.align = fui::TextAlign::Right;
  header.borderEdges = fui::EdgesNone;
  screen.header(header);
  const fui::Rect band = screen.device().screen();
  screen.target().fill(fui::makeRect(0, toybox::kHeaderHeight + 4, band.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

fui::TextStyle styled(const fui::FontId font, const fui::TextAlign align,
                      const fui::Color color = fui::Color::Black) {
  fui::TextStyle style;
  style.font = font;
  style.align = align;
  style.color = color;
  return style;
}

// The list height every game in this fork computes the same way. Written out
// rather than guessed, because a list that disagrees with its own band by six
// pixels per row is how a fourth row lands under the bottom margin.
int16_t listHeightFor(const int count) {
  return static_cast<int16_t>(count * toybox::kRowHeight + (count - 1) * toybox::kGutter / 2 + toybox::kGutter);
}

int medalsOn(const tb::Terrain& terrain) {
  int total = 0;
  for (int r = 0; r < terrain.regionCount; ++r) total += terrain.regions[r].medals;
  return total;
}

}  // namespace

const char* skillName(const tb::Skill skill) {
  switch (skill) {
    case tb::Skill::Recruit:
      return "RECRUIT";
    case tb::Skill::Sergeant:
      return "SERGEANT";
    case tb::Skill::General:
      return "GENERAL";
  }
  return "";
}

// What each rung actually is, in the player's terms rather than the search's.
const char* skillBlurb(const tb::Skill skill) {
  switch (skill) {
    case tb::Skill::Recruit:
      return "TAKES WHAT IS THERE";
    case tb::Skill::Sergeant:
      return "PLAYS FOR POSITION";
    case tb::Skill::General:
      return "READS YOUR REPLY";
  }
  return "";
}

// ---------------------------------------------------------------------------
// The board, small
// ---------------------------------------------------------------------------

void miniBoard(toybox::Screen& screen, const fui::Rect& box, const tb::Terrain& terrain, const tb::Game* game,
               const uint64_t spotlight) {
  const int slots = terrain.slotCount();
  if (slots <= 0) return;

  // Node size from the room actually available, not from a two-way guess. A
  // fifteen-base lattice in a 100px thumbnail drew its nodes into each other and
  // read as a blob; the divisor is what keeps neighbours apart at any size.
  const int16_t shorter = static_cast<int16_t>(box.width < box.height ? box.width : box.height);
  int16_t node = static_cast<int16_t>(shorter / 8);
  if (node < 5) node = 5;
  if (node > 18) node = 18;
  // The H.Q. ring is drawn 3px OUTSIDE its node, so the padding has to clear the
  // decoration and not just the node -- otherwise the topmost H.Q. fuses with
  // whatever sits above the board, which is exactly what the rule under the
  // record line did on the front door.
  const int16_t pad = static_cast<int16_t>(node / 2 + 6);
  const int16_t left = static_cast<int16_t>(box.x + pad);
  const int16_t top = static_cast<int16_t>(box.y + pad);
  const int16_t usableW = static_cast<int16_t>(box.width - pad * 2);
  const int16_t usableH = static_cast<int16_t>(box.height - pad * 2);
  if (usableW <= 0 || usableH <= 0) return;

  const auto px = [&](const int slot) {
    return static_cast<int16_t>(left + (static_cast<int32_t>(terrain.x[slot]) * usableW) / 1000);
  };
  const auto py = [&](const int slot) {
    return static_cast<int16_t>(top + (static_cast<int32_t>(terrain.y[slot]) * usableH) / 1000);
  };

  const fui::Paint ink = fui::Paint::solid(fui::Color::Black);

  // Paths first, so the nodes sit on top of them rather than being cut by them.
  for (int e = 0; e < terrain.edgeCount; ++e) {
    const int a = terrain.edges[e].a, b = terrain.edges[e].b;
    screen.target().line(fui::Point{px(a), py(a)}, fui::Point{px(b), py(b)}, 2, ink);
  }

  for (int slot = 0; slot < slots; ++slot) {
    const int16_t cx = px(slot), cy = py(slot);
    const fui::Rect cell = fui::makeRect(static_cast<int16_t>(cx - node / 2), static_cast<int16_t>(cy - node / 2),
                                         node, node);
    const bool isHq = terrain.isHq(slot);
    const int held = game != nullptr && terrain.isBase(slot) ? game->occupantSeat(slot) : tb::kNoSeat;

    // Square corners mean the base restricts what may be placed on it, the same
    // silhouette rule the full board uses -- but only while it is big enough to
    // read as a decision. Below that it is one node that does not match the
    // others, which reads as a stray mark rather than as a rule.
    const int16_t radius = (node >= 14 && terrain.gate[slot] != 0) ? 0 : 4;

    if (held == 0) {
      screen.target().fill(cell, ink, radius);
    } else if (held == 1) {
      screen.target().fill(cell, fui::Paint::dither(fui::Color::DarkGray), radius);
      screen.target().stroke(cell, ink, 2, radius);
    } else {
      screen.target().fill(cell, fui::Paint::solid(fui::Color::White), radius);
      screen.target().stroke(cell, ink, 2, radius);
    }

    if (isHq) {
      // An H.Q. is not a base and never a stepping stone, so it is drawn as
      // something else rather than as a base wearing a letter.
      screen.target().stroke(fui::makeRect(static_cast<int16_t>(cell.x - 3), static_cast<int16_t>(cell.y - 3),
                                           static_cast<int16_t>(node + 6), static_cast<int16_t>(node + 6)),
                             ink, 2, 3);
    }
    if (spotlight & (uint64_t{1} << slot)) {
      const int16_t arm = static_cast<int16_t>(node / 2 + 5);
      toybox::bracket(screen,
                      fui::makeRect(static_cast<int16_t>(cx - arm), static_cast<int16_t>(cy - arm),
                                    static_cast<int16_t>(arm * 2), static_cast<int16_t>(arm * 2)),
                      6, 3);
    }
  }
}

// ---------------------------------------------------------------------------
// The front door
// ---------------------------------------------------------------------------

int shellRowCount(const MenuModel& model) {
  return model.hasSave ? static_cast<int>(ShellRow::Count) : static_cast<int>(ShellRow::Count) - 1;
}

ShellRow shellRowAt(const MenuModel& model, const int visibleIndex) {
  const int count = shellRowCount(model);
  int clamped = visibleIndex;
  if (clamped < 0) clamped = 0;
  if (clamped >= count) clamped = count - 1;
  // With nothing to continue, the first row is PLAY and everything shifts up.
  return static_cast<ShellRow>(model.hasSave ? clamped : clamped + 1);
}

namespace {

const char* shellRowLabel(const ShellRow row) {
  switch (row) {
    case ShellRow::Continue:
      return "CONTINUE";
    case ShellRow::Play:
      return "PLAY";
    case ShellRow::Nearby:
      // The wording every link game in the fork uses. Not a place to be
      // original: a player who learns it once should read it everywhere.
      return "PLAY NEARBY";
    case ShellRow::HowTo:
      return "HOW TO PLAY";
    case ShellRow::Count:
      break;
  }
  return "";
}

// The record line, written rather than printed. A player who has never played
// should get a sentence, not three noughts.
void recordLine(toybox::Screen& screen, const MenuModel& model, const fui::Rect& line) {
  char record[64];
  if (model.played == 0) {
    std::snprintf(record, sizeof(record), "NO BATTLES YET");
  } else {
    std::snprintf(record, sizeof(record), "%d PLAYED   %d WON", model.played, model.won);
  }
  screen.target().text(line, record, styled(toybox::kTileFont, fui::TextAlign::Left));
}

void menuRows(toybox::Screen& screen, const MenuModel& model, const fui::Rect& listBand) {
  const int count = shellRowCount(model);
  fui::ListItem rows[static_cast<int>(ShellRow::Count)] = {};
  for (int i = 0; i < count; ++i) {
    const ShellRow row = shellRowAt(model, i);
    rows[i].label = shellRowLabel(row);
    rows[i].actionValue = static_cast<int16_t>(i);
    // Only CONTINUE carries a value, and it is the game you would go back to.
    if (row == ShellRow::Continue) rows[i].value = model.saveDetail;
  }

  fui::ListProps list;
  list.items = rows;
  list.count = static_cast<uint16_t>(count);
  list.selectedIndex = static_cast<int16_t>(model.selected < 0 ? 0 : model.selected);
  list.action = ActionShellRow;
  screen.list(list, listHeightFor(count), fui::LayoutAnchor::Bottom);

  for (int i = 0; i < count; ++i) {
    if (shellRowAt(model, i) != ShellRow::Nearby) continue;
    toybox::iconAtRowRight(screen, listBand, i, 0, linkui::nearbyMark(), i == model.selected);
  }
}

}  // namespace

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  chrome(screen, "TOY BATTLE");
  const tb::Terrain& terrain = tb::terrainAt(model.options.terrain);

  // The documented band order: record, rule, ornament, then the doors
  // bottom-anchored with the likeliest one loudest.
  const fui::Rect line = screen.takeTop(26);
  recordLine(screen, model, line);
  screen.target().fill(fui::makeRect(line.x, static_cast<int16_t>(line.bottom() + 6), line.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));

  const fui::Rect content = screen.contentRect();
  const int count = shellRowCount(model);
  const int16_t listH = listHeightFor(count);
  const fui::Rect listBand =
      fui::makeRect(content.x, static_cast<int16_t>(content.bottom() - listH), content.width, listH);

  const int16_t areaTop = static_cast<int16_t>(line.bottom() + 6 + toybox::kRule);
  const fui::Rect ornament =
      fui::makeRect(content.x, areaTop, content.width, static_cast<int16_t>(listBand.y - areaTop - toybox::kMargin));

  // The ornament is the board you would be playing, carrying the position you
  // would be resuming. Made of the app's own material and showing the app's own
  // data, so a screenshot of it is not the same on everyone's device.
  char caption[64];
  std::snprintf(caption, sizeof(caption), "%s   %d TO WIN, %d ON THE BOARD", terrain.name,
                terrain.medalsObjective, medalsOn(terrain));
  const fui::Rect capBox =
      fui::makeRect(ornament.x, static_cast<int16_t>(ornament.bottom() - 24), ornament.width, 24);
  miniBoard(screen,
            fui::makeRect(ornament.x, ornament.y, ornament.width, static_cast<int16_t>(ornament.height - 30)),
            terrain, model.preview, 0);
  screen.target().text(capBox, caption, styled(toybox::kTileFont, fui::TextAlign::Center));

  menuRows(screen, model, listBand);
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

int setupRowCount(const SetupModel& model) {
  // Against a person there is no opponent to choose. A row that is there and
  // means nothing is worse than a row that is not there.
  return model.forLink ? static_cast<int>(SetupRow::Count) - 1 : static_cast<int>(SetupRow::Count);
}

SetupRow setupRowAt(const SetupModel& model, const int visibleIndex) {
  const int count = setupRowCount(model);
  int clamped = visibleIndex;
  if (clamped < 0) clamped = 0;
  if (clamped >= count) clamped = count - 1;
  if (!model.forLink) return static_cast<SetupRow>(clamped);
  return clamped == 0 ? SetupRow::Map : SetupRow::Bases;
}

void buildSetup(toybox::Screen& screen, const SetupModel& model) {
  chrome(screen, model.forLink ? "NEARBY GAME" : "NEW GAME");
  const tb::Terrain& terrain = tb::terrainAt(model.options.terrain);
  const fui::Rect content = screen.contentRect();

  // The way out is taken first, so nothing below can grow into it and no branch
  // can skip it. Its label is the confirmation.
  fui::ButtonProps start;
  start.label = "START";
  start.action = ActionStart;
  start.styles = toybox::invertedStyles();
  start.text = styled(toybox::kDisplayFont, fui::TextAlign::Center, fui::Color::White);
  start.radius = 10;
  const fui::Rect startBox =
      fui::makeRect(content.x, static_cast<int16_t>(content.bottom() - 76), content.width, 76);
  screen.button(start, startBox);

  // The label-and-value list every settings screen in this fork uses. A tap
  // cycles the value in place; the map row opens a screen, because eight maps
  // do not cycle.
  const int count = setupRowCount(model);
  fui::ListItem rows[static_cast<int>(SetupRow::Count)] = {};
  char mapValue[40];
  std::snprintf(mapValue, sizeof(mapValue), "%s", terrain.name);
  for (int i = 0; i < count; ++i) {
    const SetupRow row = setupRowAt(model, i);
    rows[i].actionValue = static_cast<int16_t>(i);
    switch (row) {
      case SetupRow::Map:
        rows[i].label = "MAP";
        rows[i].value = mapValue;
        break;
      case SetupRow::Opponent:
        rows[i].label = "OPPONENT";
        rows[i].value = skillName(model.options.skill);
        break;
      case SetupRow::Bases:
        rows[i].label = "SPECIAL BASES";
        rows[i].value = model.options.specialBases ? "ON" : "OFF";
        break;
      case SetupRow::Count:
        break;
    }
  }
  fui::ListProps list;
  list.items = rows;
  list.count = static_cast<uint16_t>(count);
  list.selectedIndex = static_cast<int16_t>(model.selected);
  list.action = ActionSetupRow;
  screen.list(list, listHeightFor(count));

  // The rest of the screen shows what was chosen, which is the only thing a
  // settings screen on this device can usefully do with its slack.
  const fui::Rect body = screen.body();
  const int16_t below = static_cast<int16_t>(startBox.y - body.y - toybox::kMargin);
  if (below > 60) {
    char note[64];
    std::snprintf(note, sizeof(note), "%d MEDALS TO WIN, %d ON THE BOARD", terrain.medalsObjective,
                  medalsOn(terrain));
    miniBoard(screen, fui::makeRect(body.x, body.y, body.width, static_cast<int16_t>(below - 28)), terrain, nullptr,
              0);
    screen.target().text(
        fui::makeRect(body.x, static_cast<int16_t>(body.y + below - 26), body.width, 24), note,
        styled(toybox::kTileFont, fui::TextAlign::Center));
  }
}

// ---------------------------------------------------------------------------
// The map list
// ---------------------------------------------------------------------------

// Dots for which page you are on, the same vocabulary the rules pages use.
// Drawn only when there is more than one, because a single dot says nothing.
namespace {
void pageDots(toybox::Screen& screen, const fui::Rect& box, const int page, const int pages) {
  if (pages < 2) return;
  const int16_t dot = 12, gap = 10;
  const int16_t total = static_cast<int16_t>(pages * dot + (pages - 1) * gap);
  const int16_t left = static_cast<int16_t>(box.x + (box.width - total) / 2);
  const int16_t cy = static_cast<int16_t>(box.y + box.height / 2);
  for (int i = 0; i < pages; ++i) {
    const int16_t cx = static_cast<int16_t>(left + i * (dot + gap) + dot / 2);
    if (i == page) {
      toybox::disc(screen, cx, cy, 6, fui::Color::Black);
    } else {
      toybox::ring(screen, cx, cy, 6, toybox::kHairline, fui::Color::Black, fui::Color::White);
    }
  }
}

// The card is 104 tall because that is what makes a map's shape readable. How
// many fit follows from that rather than the other way round: sizing the card
// to the map count instead made nine of them small enough to be useless, which
// is solving the wrong problem.
constexpr int16_t kMapCard = 104;
constexpr int16_t kDotsBand = 30;
}  // namespace

int mapsPerPage() {
  // The band the dots occupy is always reserved, even on a one-page list. It
  // costs a little white space there and it keeps this function from depending
  // on its own answer.
  const int16_t room = static_cast<int16_t>(800 - toybox::kHeaderHeight - 4 - toybox::kRule - toybox::kGutter * 3 -
                                            toybox::kMargin - kDotsBand);
  const int fits = (room + toybox::kGutter) / (kMapCard + toybox::kGutter);
  return fits < 1 ? 1 : fits;
}

int mapPages() { return (tb::kPlayableTerrainCount + mapsPerPage() - 1) / mapsPerPage(); }

void buildMapPick(toybox::Screen& screen, const MapPickModel& model) {
  const int perPage = mapsPerPage();
  const int pages = mapPages();
  int page = model.page;
  if (page < 0) page = 0;
  if (page >= pages) page = pages - 1;

  char counter[16];
  std::snprintf(counter, sizeof(counter), pages > 1 ? "%d/%d" : "%d MAPS",
                pages > 1 ? page + 1 : tb::kPlayableTerrainCount, pages);
  chrome(screen, "MAPS", counter);

  const fui::Rect content = screen.contentRect();
  const fui::Rect dots =
      fui::makeRect(content.x, static_cast<int16_t>(content.bottom() - kDotsBand), content.width, kDotsBand);
  pageDots(screen, dots, page, pages);

  const int first = page * perPage;
  for (int i = 0; i < perPage && first + i < tb::kPlayableTerrainCount; ++i) {
    // The card's value is the ENGINE's index, so the skip is applied once,
    // here, and every screen downstream keeps speaking in real terrain ids.
    const int index = tb::playableTerrainAt(first + i);
    const tb::Terrain& terrain = tb::terrainAt(index);
    const fui::Rect card = fui::makeRect(content.x, static_cast<int16_t>(content.y + i * (kMapCard + toybox::kGutter)),
                                         content.width, kMapCard);

    // Every card looks the same. Nothing here is "current": a tap picks the map
    // and leaves, so an inverted card would be a cursor, and the one thing this
    // screen must not have is a cursor.
    fui::ButtonProps pick;
    pick.label = "";
    pick.action = ActionMapRow;
    pick.value = static_cast<int16_t>(index);
    pick.styles = toybox::rowStyles();
    pick.radius = 8;
    screen.button(pick, card);

    screen.target().text(fui::makeRect(static_cast<int16_t>(card.x + toybox::kGutter),
                                       static_cast<int16_t>(card.y + 22),
                                       static_cast<int16_t>(card.width - 132), 28),
                         terrain.name, styled(toybox::kUiFont, fui::TextAlign::Left));
    char sub[56];
    std::snprintf(sub, sizeof(sub), "%d BASES   WIN AT %d OF %d", terrain.baseCount, terrain.medalsObjective,
                  medalsOn(terrain));
    screen.target().text(fui::makeRect(static_cast<int16_t>(card.x + toybox::kGutter),
                                       static_cast<int16_t>(card.y + 58),
                                       static_cast<int16_t>(card.width - 132), 24),
                         sub, styled(toybox::kTileFont, fui::TextAlign::Left));

    miniBoard(screen,
              fui::makeRect(static_cast<int16_t>(card.right() - 108), static_cast<int16_t>(card.y + 8), 100,
                            static_cast<int16_t>(kMapCard - 16)),
              terrain, nullptr, 0);
  }
}

// ---------------------------------------------------------------------------
// The rules
// ---------------------------------------------------------------------------

namespace {

// One idea per page, taught on the real board. The first version of this in
// another game was abstract diagrams with captions, and the note it left behind
// is the reason these are not: a diagram is a second thing to learn before you
// can learn the game.
//
// Every caption is written to fit four lines at kUiFont -- about twenty-two
// characters a line, so eighty-eight is the ceiling. Past it the component
// truncates with no ellipsis and nothing in the log.
struct Page {
  const char* title;
  const char* caption;
  // Which slots to ring, as a mask. Castle Field's numbering: bases 0..14,
  // then the two H.Q.
  uint64_t spotlight;
  bool showTroops;
};

constexpr uint64_t kHqs = (uint64_t{1} << 15) | (uint64_t{1} << 16);
constexpr uint64_t kOneBase = uint64_t{1} << 7;
constexpr uint64_t kChain = (uint64_t{1} << 16) | (uint64_t{1} << 12) | (uint64_t{1} << 7);
constexpr uint64_t kRegion = (uint64_t{1} << 5) | (uint64_t{1} << 6) | (uint64_t{1} << 7) | (uint64_t{1} << 8);

constexpr Page kPages[] = {
    {"THE GOAL", "TAKE THE MEDALS THIS MAP IS WORTH, OR PUT ONE TROOP ON THEIR H.Q.", kHqs, false},
    {"YOUR TURN", "EACH TURN: PLACE ONE TROOP, OR DRAW TWO. THAT IS THE WHOLE CHOICE.", 0, false},
    {"WHERE IT GOES", "ONTO AN EMPTY BASE, ONE OF YOURS, OR AN ENEMY TROOP WEAKER THAN IT.", kOneBase, false},
    {"THE LINE HOME", "A BASE ONLY COUNTS IF YOU CAN WALK BACK TO YOUR H.Q. ON BASES YOU HOLD.", kChain, false},
    {"REGIONS", "HOLD EVERY BASE AROUND A REGION AND ITS MEDALS ARE YOURS FOR GOOD.", kRegion, false},
    {"THE TROOPS", "EVERY TROOP HAS A NUMBER AND A MARK. THE MARK IS WHAT IT DOES.", 0, true},
    {"SPECIAL BASES", "SOME BASES DO SOMETHING. TAP THE ? IN A GAME TO SEE WHAT THIS MAP'S DO.", 0, false},
};
constexpr int kPageCount = static_cast<int>(sizeof(kPages) / sizeof(kPages[0]));

// The eight troops in a strip, drawn with the same marks the rack uses.
void troopStrip(toybox::Screen& screen, const fui::Rect& box) {
  const int16_t tile = 62;
  const int16_t tall = 78;
  const int16_t gap = 6;
  const int16_t total = static_cast<int16_t>(4 * tile + 3 * gap);
  const int16_t left = static_cast<int16_t>(box.x + (box.width - total) / 2);
  const int16_t top = static_cast<int16_t>(box.y + (box.height - (tall * 2 + gap)) / 2);
  static const char* const kPips[] = {"*", "1", "2", "3", "4", "5", "6", "7"};

  for (int i = 0; i < 8; ++i) {
    const int col = i % 4, row = i / 4;
    const fui::Rect cell = fui::makeRect(static_cast<int16_t>(left + col * (tile + gap)),
                                         static_cast<int16_t>(top + row * (tall + gap)), tile, tall);
    screen.target().stroke(cell, fui::Paint::solid(fui::Color::Black), 2, 8);
    screen.target().text(fui::makeRect(cell.x, static_cast<int16_t>(cell.y + 8), cell.width, 30), kPips[i],
                         styled(toybox::kDisplayFont, fui::TextAlign::Center));
    drawTroopMark(screen, fui::Point{static_cast<int16_t>(cell.x + cell.width / 2),
                                     static_cast<int16_t>(cell.y + 56)},
                  14, static_cast<tb::Troop>(i));
  }
}

// The picture for a page: the real board, or the troops.
void pagePicture(toybox::Screen& screen, const fui::Rect& box, const Page& page) {
  if (page.showTroops) {
    troopStrip(screen, box);
    return;
  }
  miniBoard(screen, box, tb::terrainAt(static_cast<int>(tb::TerrainId::CastleField)), nullptr, page.spotlight);
}


}  // namespace

int howToPages() { return kPageCount; }

void buildHowTo(toybox::Screen& screen, const HowToModel& model) {
  const int page = (model.page < 0 || model.page >= kPageCount) ? 0 : model.page;
  const Page& content = kPages[page];

  char counter[16];
  std::snprintf(counter, sizeof(counter), "%d/%d", page + 1, kPageCount);

  // Dungeon Diagrams' shape. The page title is the header title, the picture
  // and its caption are centred together as one block, and the two pills
  // relabel rather than disappearing.
  chrome(screen, content.title, counter);
  const fui::Rect actions = screen.takeBottom(toybox::kPillHeight, toybox::kGutter);
  const int16_t width = static_cast<int16_t>((actions.width - toybox::kGutter) / 2);
  const char* labels[2] = {page == 0 ? "BACK" : "PREV", page + 1 == kPageCount ? "PLAY" : "NEXT"};
  const fui::ActionId ids[2] = {ActionPagePrev, ActionPageNext};
  for (int i = 0; i < 2; ++i) {
    fui::ButtonProps props;
    props.label = labels[i];
    props.action = ids[i];
    // The way forward is the loud one. Both drawn as outlines gave the page no
    // primary, and a deck of seven pages is exactly where that is felt.
    const bool forward = i == 1;
    props.text = forward ? styled(toybox::kUiFont, fui::TextAlign::Center, fui::Color::White)
                         : toybox::buttonText(screen.theme());
    props.styles = forward ? toybox::invertedStyles() : toybox::rowStyles();
    props.radius = toybox::kPillRadius;
    screen.button(props, fui::makeRect(static_cast<int16_t>(actions.x + i * (width + toybox::kGutter)), actions.y,
                                       width, actions.height));
  }

  const fui::Rect body = screen.body();
  constexpr int16_t kCaption = 132;
  // Picture and caption together, filling the body rather than floating in it.
  // The picture used to be capped at 360 and the pair then centred in a taller
  // body, which left a hand's width of white under the header and crowded the
  // caption from above -- the page read as a board that had drifted.
  const int16_t picH = static_cast<int16_t>(body.height - kCaption - toybox::kMargin);
  pagePicture(screen, fui::makeRect(body.x, body.y, body.width, picH), content);

  fui::TextStyle caption = styled(toybox::kUiFont, fui::TextAlign::Center);
  caption.maxLines = 4;
  screen.target().text(
      fui::makeRect(body.x, static_cast<int16_t>(body.y + picH + toybox::kMargin), body.width, kCaption),
      content.caption, caption);
}

}  // namespace tbui

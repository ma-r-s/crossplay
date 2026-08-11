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

const char* lookName(const Look look) {
  switch (look) {
    case Look::FrontDoor:
      return "FRONT DOOR";
    case Look::Slab:
      return "SLAB";
    case Look::Briefing:
      return "BRIEFING";
  }
  return "";
}

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

  // Node size from the room available, clamped so a fifteen-base map stays
  // legible and a small one does not turn into paddles.
  const int16_t node = static_cast<int16_t>(box.width < 300 ? 14 : 18);
  const int16_t pad = static_cast<int16_t>(node / 2 + 2);
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

    // Square corners mean the base restricts what may be placed on it, which is
    // the same silhouette rule the full board uses.
    const int16_t radius = (terrain.isBase(slot) && terrain.gate[slot] != 0) ? 0 : 4;

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

  if (model.look == Look::Slab) {
    // Everything is a slab, the same material the troops and bases are made of
    // on the board. No list component: each door is its own object, and the one
    // you are most likely to want is the only filled one.
    const fui::Rect content = screen.contentRect();
    const int count = shellRowCount(model);
    const int16_t slabH = 74;
    const int16_t gap = 10;
    const int16_t total = static_cast<int16_t>(count * slabH + (count - 1) * gap);
    const int16_t topOfSlabs = static_cast<int16_t>(content.bottom() - total);

    miniBoard(screen, fui::makeRect(content.x, content.y, content.width,
                                    static_cast<int16_t>(topOfSlabs - content.y - toybox::kMargin)),
              terrain, model.preview, 0);

    for (int i = 0; i < count; ++i) {
      const ShellRow row = shellRowAt(model, i);
      const fui::Rect slab =
          fui::makeRect(content.x, static_cast<int16_t>(topOfSlabs + i * (slabH + gap)), content.width, slabH);
      const bool loud = i == 0;
      fui::ButtonProps door;
      door.label = "";
      door.action = ActionShellRow;
      door.value = static_cast<int16_t>(i);
      door.styles = loud ? toybox::invertedStyles() : toybox::rowStyles();
      door.radius = 8;
      screen.button(door, slab);

      // The label is drawn over the slab rather than through it, because
      // CONTINUE needs a second line saying what it continues and a button
      // draws one.
      const fui::Color inkColor = loud ? fui::Color::White : fui::Color::Black;
      const bool twoLine = row == ShellRow::Continue && model.saveDetail[0] != '\0';
      const int16_t nameH = screen.target().lineHeight(toybox::kDisplayFont);
      const int16_t subH = screen.target().lineHeight(toybox::kTileFont);
      const int16_t block = static_cast<int16_t>(twoLine ? nameH + subH : nameH);
      const int16_t textTop = static_cast<int16_t>(slab.y + (slabH - block) / 2);
      screen.target().text(fui::makeRect(static_cast<int16_t>(slab.x + toybox::kGutter), textTop,
                                         static_cast<int16_t>(slab.width - toybox::kGutter * 2), nameH),
                           shellRowLabel(row), styled(toybox::kDisplayFont, fui::TextAlign::Left, inkColor));
      if (twoLine) {
        screen.target().text(fui::makeRect(static_cast<int16_t>(slab.x + toybox::kGutter),
                                           static_cast<int16_t>(textTop + nameH),
                                           static_cast<int16_t>(slab.width - toybox::kGutter * 2), subH),
                             model.saveDetail, styled(toybox::kTileFont, fui::TextAlign::Left, inkColor));
      }
      if (row == ShellRow::Nearby) {
        toybox::iconAtRowRight(screen, slab, 0, 0, linkui::nearbyMark(), loud);
      }
    }
    return;
  }

  if (model.look == Look::Briefing) {
    // The setup is always on screen, so starting is one tap and changing it is
    // a deliberate second one. The loudest thing is the battle you are about to
    // have, not the word PLAY.
    const fui::Rect line = screen.takeTop(26);
    recordLine(screen, model, line);
    screen.target().fill(fui::makeRect(line.x, static_cast<int16_t>(line.bottom() + 6), line.width, toybox::kRule),
                         fui::Paint::solid(fui::Color::Black));

    const fui::Rect content = screen.contentRect();
    const int count = shellRowCount(model) - 1;  // PLAY is the big button instead
    const int16_t listH = listHeightFor(count);
    const int16_t startH = 86;

    // The briefing block: what you are about to play, in three lines.
    const int16_t blockTop = static_cast<int16_t>(line.bottom() + 6 + toybox::kRule + toybox::kMargin);
    const int16_t blockH = 128;
    const fui::Rect block = fui::makeRect(content.x, blockTop, content.width, blockH);
    screen.target().stroke(block, fui::Paint::solid(fui::Color::Black), toybox::kHairline, 8);
    char detail[64];
    std::snprintf(detail, sizeof(detail), "%d MEDALS TO WIN, %d ON THE BOARD", terrain.medalsObjective,
                  medalsOn(terrain));
    char rest[64];
    std::snprintf(rest, sizeof(rest), "%s   BASES %s", skillName(model.options.skill),
                  model.options.specialBases ? "ON" : "OFF");
    const int16_t pad = toybox::kGutter;
    screen.target().text(fui::makeRect(static_cast<int16_t>(block.x + pad), static_cast<int16_t>(block.y + 14),
                                       static_cast<int16_t>(block.width - pad * 2), 34),
                         terrain.name, styled(toybox::kDisplayFont, fui::TextAlign::Left));
    screen.target().text(fui::makeRect(static_cast<int16_t>(block.x + pad), static_cast<int16_t>(block.y + 54),
                                       static_cast<int16_t>(block.width - pad * 2), 26),
                         detail, styled(toybox::kTileFont, fui::TextAlign::Left));
    screen.target().text(fui::makeRect(static_cast<int16_t>(block.x + pad), static_cast<int16_t>(block.y + 84),
                                       static_cast<int16_t>(block.width - pad * 2), 26),
                         rest, styled(toybox::kTileFont, fui::TextAlign::Left));
    screen.frame().hit(block, ActionOpenMaps, 0);

    // The picture takes whatever is between the block and the doors.
    const int16_t doorsTop = static_cast<int16_t>(content.bottom() - listH - toybox::kGutter - startH);
    miniBoard(screen,
              fui::makeRect(content.x, static_cast<int16_t>(block.bottom() + toybox::kGutter), content.width,
                            static_cast<int16_t>(doorsTop - block.bottom() - toybox::kGutter * 2)),
              terrain, model.preview, 0);

    fui::ButtonProps start;
    start.label = model.hasSave ? "CONTINUE" : "START";
    start.action = ActionShellRow;
    start.value = static_cast<int16_t>(model.hasSave ? 0 : 0);
    start.styles = toybox::invertedStyles();
    start.text = styled(toybox::kDisplayFont, fui::TextAlign::Center, fui::Color::White);
    start.radius = 10;
    screen.button(start, fui::makeRect(content.x, doorsTop, content.width, startH));

    // The quieter doors, minus the one the big button already is.
    fui::ListItem rows[static_cast<int>(ShellRow::Count)] = {};
    int n = 0;
    for (int i = 0; i < shellRowCount(model); ++i) {
      const ShellRow row = shellRowAt(model, i);
      if (row == (model.hasSave ? ShellRow::Continue : ShellRow::Play)) continue;
      rows[n].label = shellRowLabel(row);
      rows[n].actionValue = static_cast<int16_t>(i);
      ++n;
    }
    fui::ListProps list;
    list.items = rows;
    list.count = static_cast<uint16_t>(n);
    list.selectedIndex = -1;
    list.action = ActionShellRow;
    screen.list(list, listHeightFor(n), fui::LayoutAnchor::Bottom);
    return;
  }

  // FRONT DOOR: the documented band order, done plainly. Record, rule,
  // ornament, then the doors bottom-anchored with the likeliest one loudest.
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

  if (model.look == Look::Slab) {
    // Every value visible at once, nothing to cycle. Costs height and buys the
    // question a player actually has, which is what the choices ARE.
    int16_t y = content.y;

    screen.target().text(fui::makeRect(content.x, y, content.width, 24), "MAP",
                         styled(toybox::kTileFont, fui::TextAlign::Left));
    y = static_cast<int16_t>(y + 28);
    const fui::Rect mapSlab = fui::makeRect(content.x, y, content.width, 92);
    fui::ButtonProps mapPick;
    mapPick.label = "";
    mapPick.action = ActionOpenMaps;
    mapPick.styles = toybox::rowStyles();
    mapPick.radius = 8;
    screen.button(mapPick, mapSlab);
    miniBoard(screen,
              fui::makeRect(static_cast<int16_t>(mapSlab.right() - 96), static_cast<int16_t>(mapSlab.y + 6), 88, 80),
              terrain, nullptr, 0);
    char sub[48];
    std::snprintf(sub, sizeof(sub), "%d MEDALS TO WIN", terrain.medalsObjective);
    {
      const int16_t nameH = screen.target().lineHeight(toybox::kDisplayFont);
      const int16_t subH = screen.target().lineHeight(toybox::kTileFont);
      const int16_t textW = static_cast<int16_t>(mapSlab.width - 120);
      const int16_t textTop = static_cast<int16_t>(mapSlab.y + (mapSlab.height - nameH - subH) / 2);
      screen.target().text(
          fui::makeRect(static_cast<int16_t>(mapSlab.x + toybox::kGutter), textTop, textW, nameH), terrain.name,
          styled(toybox::kDisplayFont, fui::TextAlign::Left));
      screen.target().text(fui::makeRect(static_cast<int16_t>(mapSlab.x + toybox::kGutter),
                                         static_cast<int16_t>(textTop + nameH), textW, subH),
                           sub, styled(toybox::kTileFont, fui::TextAlign::Left));
    }
    y = static_cast<int16_t>(mapSlab.bottom() + toybox::kMargin);

    if (!model.forLink) {
      screen.target().text(fui::makeRect(content.x, y, content.width, 24), "OPPONENT",
                           styled(toybox::kTileFont, fui::TextAlign::Left));
      y = static_cast<int16_t>(y + 28);
      const int16_t rowH = 62;
      for (int i = 0; i < tb::kSkillCount; ++i) {
        const tb::Skill skill = static_cast<tb::Skill>(i);
        const bool current = skill == model.options.skill;
        const fui::Rect row = fui::makeRect(content.x, static_cast<int16_t>(y + i * (rowH + 8)), content.width, rowH);
        fui::ButtonProps pick;
        pick.label = "";
        pick.action = ActionPickSkill;
        pick.value = static_cast<int16_t>(i);
        pick.styles = current ? toybox::invertedStyles() : toybox::rowStyles();
        pick.radius = 8;
        screen.button(pick, row);
        const fui::Color ink = current ? fui::Color::White : fui::Color::Black;
        const int16_t nameH = screen.target().lineHeight(toybox::kUiFont);
        const int16_t blurbH = screen.target().lineHeight(toybox::kTileFont);
        const int16_t textW = static_cast<int16_t>(row.width - toybox::kGutter * 2);
        const int16_t textTop = static_cast<int16_t>(row.y + (row.height - nameH - blurbH) / 2);
        screen.target().text(fui::makeRect(static_cast<int16_t>(row.x + toybox::kGutter), textTop, textW, nameH),
                             skillName(skill), styled(toybox::kUiFont, fui::TextAlign::Left, ink));
        screen.target().text(fui::makeRect(static_cast<int16_t>(row.x + toybox::kGutter),
                                           static_cast<int16_t>(textTop + nameH), textW, blurbH),
                             skillBlurb(skill), styled(toybox::kTileFont, fui::TextAlign::Left, ink));
      }
      y = static_cast<int16_t>(y + tb::kSkillCount * (rowH + 8) + toybox::kGutter);
    }

    const fui::Rect basesRow = fui::makeRect(content.x, y, content.width, 62);
    fui::ButtonProps bases;
    bases.label = "";
    bases.action = ActionPickBases;
    bases.styles = toybox::rowStyles();
    bases.radius = 8;
    screen.button(bases, basesRow);
    {
      const int16_t lineH = screen.target().lineHeight(toybox::kUiFont);
      const int16_t lineTop = static_cast<int16_t>(basesRow.y + (basesRow.height - lineH) / 2);
      const int16_t valueW = 80;
      screen.target().text(
          fui::makeRect(static_cast<int16_t>(basesRow.x + toybox::kGutter), lineTop,
                        static_cast<int16_t>(basesRow.width - valueW - toybox::kGutter * 2), lineH),
          "SPECIAL BASES", styled(toybox::kUiFont, fui::TextAlign::Left));
      screen.target().text(
          fui::makeRect(static_cast<int16_t>(basesRow.right() - valueW - toybox::kGutter), lineTop, valueW, lineH),
          model.options.specialBases ? "ON" : "OFF", styled(toybox::kUiFont, fui::TextAlign::Right));
    }
    return;
  }

  if (model.look == Look::Briefing) {
    // One block that reads as a sentence about the game you are about to play,
    // with each clause tappable in place. Changing a setting never leaves the
    // screen, so there is nothing to come back from.
    int16_t y = static_cast<int16_t>(content.y);
    const int16_t rowH = 88;
    const char* labels[3] = {"MAP", "OPPONENT", "SPECIAL BASES"};
    const fui::ActionId actions[3] = {ActionOpenMaps, ActionPickSkill, ActionPickBases};
    char values[3][48];
    std::snprintf(values[0], sizeof(values[0]), "%s", terrain.name);
    std::snprintf(values[1], sizeof(values[1]), "%s", skillName(model.options.skill));
    std::snprintf(values[2], sizeof(values[2]), "%s", model.options.specialBases ? "ON" : "OFF");
    char notes[3][48];
    std::snprintf(notes[0], sizeof(notes[0]), "%d MEDALS TO WIN, %d ON THE BOARD", terrain.medalsObjective,
                  medalsOn(terrain));
    std::snprintf(notes[1], sizeof(notes[1]), "%s", skillBlurb(model.options.skill));
    std::snprintf(notes[2], sizeof(notes[2]), "%s", model.options.specialBases ? "BASES DO SOMETHING"
                                                                              : "PLAIN BASES, AS THE RULEBOOK ALLOWS");

    for (int i = 0; i < 3; ++i) {
      if (model.forLink && i == 1) continue;
      const fui::Rect row = fui::makeRect(content.x, y, content.width, rowH);
      screen.frame().hit(row, actions[i], 0);
      screen.target().fill(fui::makeRect(row.x, static_cast<int16_t>(row.bottom() - toybox::kHairline), row.width,
                                         toybox::kHairline),
                           fui::Paint::solid(fui::Color::Black));
      const int16_t labelH = screen.target().lineHeight(toybox::kTileFont);
      const int16_t valueH = screen.target().lineHeight(toybox::kDisplayFont);
      const int16_t stack = static_cast<int16_t>(labelH + valueH + labelH);
      const int16_t textTop = static_cast<int16_t>(row.y + (row.height - stack) / 2);
      screen.target().text(fui::makeRect(row.x, textTop, row.width, labelH), labels[i],
                           styled(toybox::kTileFont, fui::TextAlign::Left));
      screen.target().text(fui::makeRect(row.x, static_cast<int16_t>(textTop + labelH), row.width, valueH),
                           values[i], styled(toybox::kDisplayFont, fui::TextAlign::Left));
      screen.target().text(fui::makeRect(row.x, static_cast<int16_t>(textTop + labelH + valueH), row.width, labelH),
                           notes[i], styled(toybox::kTileFont, fui::TextAlign::Left));
      y = static_cast<int16_t>(y + rowH + toybox::kGutter);
    }

    miniBoard(screen,
              fui::makeRect(content.x, y, content.width, static_cast<int16_t>(startBox.y - y - toybox::kMargin)),
              terrain, nullptr, 0);
    return;
  }

  // FRONT DOOR: the label-and-value list every settings screen in this fork
  // uses. A tap cycles the value in place; the map row opens a screen because
  // eight maps do not cycle.
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

void buildMapPick(toybox::Screen& screen, const MapPickModel& model) {
  char counter[16];
  std::snprintf(counter, sizeof(counter), "%d MAPS", tb::kTerrainCount);
  chrome(screen, "MAPS", counter);

  const fui::Rect content = screen.contentRect();
  // A card per map rather than a row, because the thing you are choosing
  // between is a shape, and its name is the least useful thing about it.
  const int16_t cardH = 104;
  const int16_t gap = toybox::kGutter;
  const int perScreen = (content.height + gap) / (cardH + gap);
  const int first = model.topRow;

  for (int i = 0; i < perScreen && first + i < tb::kTerrainCount; ++i) {
    const int index = first + i;
    const tb::Terrain& terrain = tb::terrainAt(index);
    const bool current = index == model.selected;
    const fui::Rect card =
        fui::makeRect(content.x, static_cast<int16_t>(content.y + i * (cardH + gap)), content.width, cardH);

    fui::ButtonProps pick;
    pick.label = "";
    pick.action = ActionMapRow;
    pick.value = static_cast<int16_t>(index);
    pick.styles = current ? toybox::invertedStyles() : toybox::rowStyles();
    pick.radius = 8;
    screen.button(pick, card);

    const fui::Color ink = current ? fui::Color::White : fui::Color::Black;
    screen.target().text(fui::makeRect(static_cast<int16_t>(card.x + toybox::kGutter),
                                       static_cast<int16_t>(card.y + 22),
                                       static_cast<int16_t>(card.width - 132), 28),
                         terrain.name, styled(toybox::kUiFont, fui::TextAlign::Left, ink));
    char sub[56];
    std::snprintf(sub, sizeof(sub), "%d BASES   WIN AT %d OF %d", terrain.baseCount, terrain.medalsObjective,
                  medalsOn(terrain));
    screen.target().text(fui::makeRect(static_cast<int16_t>(card.x + toybox::kGutter),
                                       static_cast<int16_t>(card.y + 58),
                                       static_cast<int16_t>(card.width - 132), 24),
                         sub, styled(toybox::kTileFont, fui::TextAlign::Left, ink));

    // The board itself, on the right. A selected card inverts, and a mini board
    // drawn in black on that ground would disappear -- so it gets its own white
    // panel rather than being drawn straight onto the slab.
    const fui::Rect panel =
        fui::makeRect(static_cast<int16_t>(card.right() - 108), static_cast<int16_t>(card.y + 8), 100,
                      static_cast<int16_t>(cardH - 16));
    if (current) screen.target().fill(panel, fui::Paint::solid(fui::Color::White), 6);
    miniBoard(screen, panel, terrain, nullptr, 0);
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

void pageDots(toybox::Screen& screen, const fui::Rect& box, const int page, const int pages) {
  const int16_t dot = 14, gap = 10;
  const int16_t total = static_cast<int16_t>(pages * dot + (pages - 1) * gap);
  const int16_t left = static_cast<int16_t>(box.x + (box.width - total) / 2);
  const int16_t cy = static_cast<int16_t>(box.y + box.height / 2);
  for (int i = 0; i < pages; ++i) {
    const int16_t cx = static_cast<int16_t>(left + i * (dot + gap) + dot / 2);
    if (i == page) {
      toybox::disc(screen, cx, cy, 7, fui::Color::Black);
    } else {
      toybox::ring(screen, cx, cy, 7, toybox::kHairline, fui::Color::Black, fui::Color::White);
    }
  }
}

}  // namespace

int howToPages(Look) { return kPageCount; }

void buildHowTo(toybox::Screen& screen, const HowToModel& model) {
  const int page = (model.page < 0 || model.page >= kPageCount) ? 0 : model.page;
  const Page& content = kPages[page];

  char counter[16];
  std::snprintf(counter, sizeof(counter), "%d/%d", page + 1, kPageCount);

  if (model.look == Look::Slab) {
    // Insider's pacing: the page title is the loud thing on the page, the whole
    // body is one target, and the dots say where you are.
    chrome(screen, "HOW TO PLAY", counter);
    const fui::Rect body = screen.body();
    screen.frame().hit(body, ActionPageNext, 0);

    // The title band is a full line height. Anything placed at a guessed offset
    // lands on top of the display cut, which draws taller than it measures.
    constexpr int16_t kTitleBand = 46;
    constexpr int16_t kCaption = 132;
    screen.target().text(fui::makeRect(body.x, body.y, body.width, kTitleBand), content.title,
                         styled(toybox::kDisplayFont, fui::TextAlign::Center));

    const fui::Rect footer =
        fui::makeRect(body.x, static_cast<int16_t>(body.bottom() - 30), body.width, 30);
    pageDots(screen, footer, page, kPageCount);
    screen.target().text(fui::makeRect(body.x, static_cast<int16_t>(footer.y - 34), body.width, 22),
                         page + 1 == kPageCount ? "TAP TO FINISH" : "TAP TO CONTINUE",
                         styled(toybox::kTileFont, fui::TextAlign::Center));

    const int16_t picTop = static_cast<int16_t>(body.y + kTitleBand + 12);
    const int16_t picH = static_cast<int16_t>(footer.y - 34 - toybox::kMargin - picTop - kCaption);
    pagePicture(screen, fui::makeRect(body.x, picTop, body.width, picH), content);

    fui::TextStyle caption = styled(toybox::kUiFont, fui::TextAlign::Center);
    caption.maxLines = 4;
    screen.target().text(fui::makeRect(body.x, static_cast<int16_t>(picTop + picH), body.width, kCaption),
                         content.caption, caption);
    return;
  }

  if (model.look == Look::Briefing) {
    // Denser: the title in the band, so the page gets its whole height, and the
    // caption set left as a line of a manual rather than centred as a slogan.
    chrome(screen, content.title, counter);
    const fui::Rect actions = screen.takeBottom(toybox::kPillHeight, toybox::kGutter);
    const int16_t width = static_cast<int16_t>((actions.width - toybox::kGutter) / 2);
    const char* labels[2] = {page == 0 ? "BACK" : "PREV", page + 1 == kPageCount ? "PLAY" : "NEXT"};
    const fui::ActionId ids[2] = {ActionPagePrev, ActionPageNext};
    for (int i = 0; i < 2; ++i) {
      fui::ButtonProps props;
      props.label = labels[i];
      props.action = ids[i];
      props.text = toybox::buttonText(screen.theme());
      props.styles = i == 1 ? toybox::invertedStyles() : toybox::rowStyles();
      props.radius = toybox::kPillRadius;
      screen.button(props, fui::makeRect(static_cast<int16_t>(actions.x + i * (width + toybox::kGutter)), actions.y,
                                         width, actions.height));
    }

    const fui::Rect rest = screen.body();
    const int16_t kCaption = static_cast<int16_t>(screen.target().lineHeight(toybox::kUiFont) * 4);
    fui::TextStyle caption = styled(toybox::kUiFont, fui::TextAlign::Left);
    caption.maxLines = 4;
    screen.target().text(fui::makeRect(rest.x, rest.y, rest.width, kCaption), content.caption, caption);
    screen.target().fill(fui::makeRect(rest.x, static_cast<int16_t>(rest.y + kCaption), rest.width, toybox::kRule),
                         fui::Paint::solid(fui::Color::Black));
    pagePicture(screen,
                fui::makeRect(rest.x, static_cast<int16_t>(rest.y + kCaption + toybox::kMargin), rest.width,
                              static_cast<int16_t>(rest.height - kCaption - toybox::kMargin * 2)),
                content);
    return;
  }

  // FRONT DOOR: Dungeon Diagrams' shape. The page title is the header title,
  // the picture and its caption are centred together as one block, and the two
  // pills relabel rather than disappearing.
  chrome(screen, content.title, counter);
  const fui::Rect actions = screen.takeBottom(toybox::kPillHeight, toybox::kGutter);
  const int16_t width = static_cast<int16_t>((actions.width - toybox::kGutter) / 2);
  const char* labels[2] = {page == 0 ? "BACK" : "PREV", page + 1 == kPageCount ? "PLAY" : "NEXT"};
  const fui::ActionId ids[2] = {ActionPagePrev, ActionPageNext};
  for (int i = 0; i < 2; ++i) {
    fui::ButtonProps props;
    props.label = labels[i];
    props.action = ids[i];
    props.text = toybox::buttonText(screen.theme());
    props.styles = toybox::rowStyles();
    props.radius = toybox::kPillRadius;
    screen.button(props, fui::makeRect(static_cast<int16_t>(actions.x + i * (width + toybox::kGutter)), actions.y,
                                       width, actions.height));
  }

  const fui::Rect body = screen.body();
  constexpr int16_t kCaption = 132;
  // Picture and caption centred TOGETHER. Centring the picture in what is left
  // after pinning the caption to the bottom puts a hand's width of white
  // between them, and the page reads as two unrelated things.
  const int16_t picH = static_cast<int16_t>(body.height - kCaption > 360 ? 360 : body.height - kCaption);
  const int16_t block = static_cast<int16_t>(picH + toybox::kMargin + kCaption);
  const int16_t top = static_cast<int16_t>(body.y + (body.height - block) / 2);
  pagePicture(screen, fui::makeRect(body.x, top, body.width, picH), content);

  fui::TextStyle caption = styled(toybox::kUiFont, fui::TextAlign::Center);
  caption.maxLines = 4;
  screen.target().text(
      fui::makeRect(body.x, static_cast<int16_t>(top + picH + toybox::kMargin), body.width, kCaption),
      content.caption, caption);
}

}  // namespace tbui

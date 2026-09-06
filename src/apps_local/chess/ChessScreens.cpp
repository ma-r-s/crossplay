#include "ChessScreens.h"

#include "../link/LinkScreens.h"

namespace chessui {

namespace {

// Four row sets rather than one with holes punched in it. Level and Play As are
// meaningless when two people share the device; New Game and Take Back are
// meaningless when there is no board in front of you.
constexpr MenuRow kBoardVsComputer[] = {MenuRow::NewGame, MenuRow::TakeBack, MenuRow::Opponent,
                                        MenuRow::Level,   MenuRow::PlayAs,   MenuRow::Hints};
// Both human modes show the same rows: level and colour mean nothing when the
// other player is a person in front of you.
constexpr MenuRow kBoardVsPerson[] = {MenuRow::NewGame, MenuRow::TakeBack, MenuRow::Opponent, MenuRow::Hints};
constexpr MenuRow kMenuVsComputer[] = {MenuRow::Opponent, MenuRow::Level, MenuRow::PlayAs, MenuRow::Hints};
constexpr MenuRow kMenuVsPerson[] = {MenuRow::Opponent, MenuRow::Hints};

struct RowSet {
  const MenuRow* rows;
  int count;
};

RowSet rowSet(const SettingsModel& model) {
  const bool computer = model.opponent == Opponent::Computer;
  if (model.fromMenu) {
    if (computer) return {kMenuVsComputer, static_cast<int>(sizeof(kMenuVsComputer) / sizeof(kMenuVsComputer[0]))};
    return {kMenuVsPerson, static_cast<int>(sizeof(kMenuVsPerson) / sizeof(kMenuVsPerson[0]))};
  }
  if (computer) return {kBoardVsComputer, static_cast<int>(sizeof(kBoardVsComputer) / sizeof(kBoardVsComputer[0]))};
  return {kBoardVsPerson, static_cast<int>(sizeof(kBoardVsPerson) / sizeof(kBoardVsPerson[0]))};
}

}  // namespace

int visibleRows(const SettingsModel& model) { return rowSet(model).count; }

MenuRow rowAt(const SettingsModel& model, const int visibleIndex) {
  const RowSet set = rowSet(model);
  const int clamped = visibleIndex < 0 ? 0 : (visibleIndex >= set.count ? set.count - 1 : visibleIndex);
  return set.rows[clamped];
}

const char* rowLabel(const MenuRow row) {
  switch (row) {
    case MenuRow::NewGame:
      return "NEW GAME";
    case MenuRow::TakeBack:
      return "TAKE BACK";
    case MenuRow::Level:
      return "LEVEL";
    case MenuRow::PlayAs:
      return "PLAY AS";
    case MenuRow::Opponent:
      return "OPPONENT";
    case MenuRow::Hints:
      return "MOVE HINTS";
    default:
      return "";
  }
}

const char* rowValue(const SettingsModel& model, const MenuRow row) {
  switch (row) {
    case MenuRow::TakeBack:
      return model.canTakeBack ? "" : "NONE";
    case MenuRow::Level:
      return model.level == 0 ? "EASY" : (model.level == 1 ? "NORMAL" : "HARD");
    case MenuRow::PlayAs:
      return model.humanPlaysWhite ? "WHITE" : "BLACK";
    case MenuRow::Opponent:
      switch (model.opponent) {
        case Opponent::Computer:
          return "COMPUTER";
        case Opponent::PassAndPlay:
          return "P&P";
        case Opponent::FaceToFace:
          return "F2F";
      }
      return "";
    case MenuRow::Hints:
      return model.showHints ? "ON" : "OFF";
    default:
      return "";
  }
}

namespace {

// The header band, its offset rule, and the content inset every Toybox screen
// shares. Returns nothing: it leaves `screen` with the body as its content.
void toyboxChrome(toybox::Screen& screen, const char* title) {
  fui::HeaderProps header;
  header.title = title;
  header.borderEdges = fui::EdgesNone;
  // Everything else (title style, side padding, band height, minimum touch
  // size) comes from the theme through Screen.
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);

  // Toybox's doubled line: a thinner rule set off from the band rather than
  // flush against it. Drawn straight to the target because it is one fill, not
  // a control, and it deliberately sits in the gap the header left behind.

  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

}  // namespace

void buildSettings(toybox::Screen& screen, const SettingsModel& model) {
  toyboxChrome(screen, "SETTINGS");

  // The close button is anchored to the bottom margin before the rows fill the
  // rest, so the rows never grow into it.
  fui::ButtonProps close;
  // The label is the confirmation: you can see the setting has changed, you can
  // see what leaving is about to cost you, and -- the part that was wrong --
  // you can see where you are about to end up. Saying BACK TO BOARD on a screen
  // that returns to the menu is a lie the flow fix alone did not remove.
  close.label = model.restartPending ? "START NEW GAME" : (model.fromMenu ? "BACK TO MENU" : "BACK TO BOARD");
  close.action = ActionCloseSettings;
  close.borderEdges = fui::EdgesNone;
  screen.button(close, screen.takeBottom(toybox::kPillHeight));

  // A list, not a stack of settingRows. Both draw the same label-and-value row,
  // but Screen::list() substitutes the theme into it (row height, gap, side
  // padding, selection style) while Screen::settingRow() only does layout, and
  // the list virtualizes for free if this menu ever outgrows the screen.
  fui::ListItem rows[static_cast<int>(MenuRow::Count)] = {};
  const int count = visibleRows(model);
  for (int i = 0; i < count; ++i) {
    const MenuRow row = rowAt(model, i);
    rows[i].label = rowLabel(row);
    rows[i].value = rowValue(model, row);
    rows[i].actionValue = static_cast<int16_t>(i);
  }

  fui::ListProps list;
  list.items = rows;
  list.count = static_cast<uint16_t>(count);
  list.selectedIndex = static_cast<int16_t>(model.selected);
  list.action = ActionMenuRow;
  screen.list(list);
}

fui::Rect buildBoardChrome(toybox::Screen& screen, const BoardModel& model) {
  toyboxChrome(screen, "CHESS");

  fui::ButtonProps status;
  status.label = model.status;
  // Registering no action is what makes a mid-game capsule inert: with
  // NO_ACTION the component draws it and adds nothing to the hit table, so
  // there is no tappable region to drift out of step with the label.
  status.action = model.gameOver ? static_cast<fui::ActionId>(ActionPlayAgain) : fui::NO_ACTION;
  status.borderEdges = fui::EdgesNone;
  screen.button(status, linkui::withOpponentFace(screen, screen.takeBottom(toybox::kPillHeight), model.theirName));

  return screen.body();
}

int startRows(const StartModel& model) {
  return model.hasSavedGame ? static_cast<int>(StartRow::Count) : static_cast<int>(StartRow::Count) - 1;
}

StartRow startRowAt(const StartModel& model, const int visibleIndex) {
  const int count = startRows(model);
  const int clamped = visibleIndex < 0 ? 0 : (visibleIndex >= count ? count - 1 : visibleIndex);
  // With no game to continue, the first row is NEW GAME and everything shifts.
  return static_cast<StartRow>(model.hasSavedGame ? clamped : clamped + 1);
}

const char* startRowLabel(const StartRow row) {
  switch (row) {
    case StartRow::Continue:
      return "CONTINUE";
    case StartRow::NewGame:
      return "NEW GAME";
    case StartRow::PlayNearby:
      // The words matter more than they look. "Click where it says multiplayer"
      // only works if something says it, and NEARBY is what it is: somebody in
      // the room, not a server and not a friends list.
      return "PLAY NEARBY";
    case StartRow::Settings:
      return "SETTINGS";
    default:
      return "";
  }
}

fui::Rect buildStartMenu(toybox::Screen& screen, const StartModel& model) {
  toyboxChrome(screen, "CHESS");

  fui::ListItem rows[static_cast<int>(StartRow::Count)] = {};
  const int count = startRows(model);
  for (int i = 0; i < count; ++i) {
    const StartRow row = startRowAt(model, i);
    rows[i].label = startRowLabel(row);
    // Only CONTINUE carries a value, and it is the position you left.
    if (row == StartRow::Continue) rows[i].value = model.continueDetail;
    rows[i].actionValue = static_cast<int16_t>(i);
  }

  fui::ListProps list;
  list.items = rows;
  list.count = static_cast<uint16_t>(count);
  list.selectedIndex = static_cast<int16_t>(model.selected);
  list.action = ActionStartRow;
  // Anchored to the bottom margin, so what is left above is one zone for the
  // board rather than slack scattered around the rows.
  const int16_t listHeight =
      static_cast<int16_t>(count * toybox::kRowHeight + (count - 1) * toybox::kGutter / 2 + toybox::kGutter);
  // The band the list is about to occupy, taken from the same content rect and
  // height it will use. Needed to put the multiplayer mark on PLAY NEARBY.
  const fui::Rect content = screen.contentRect();
  const fui::Rect listBand =
      fui::makeRect(content.x, static_cast<int16_t>(content.bottom() - listHeight), content.width, listHeight);
  screen.list(list, listHeight, fui::LayoutAnchor::Bottom);

  // One symbol wherever two devices talk to each other, so it is learned once
  // and recognised everywhere. See linkui::nearbyMark().
  for (int i = 0; i < count; ++i) {
    if (startRowAt(model, i) != StartRow::PlayNearby) continue;
    toybox::iconAtRowRight(screen, listBand, i, 0, linkui::nearbyMark(), i == model.selected);
  }

  return screen.body();
}

}  // namespace chessui

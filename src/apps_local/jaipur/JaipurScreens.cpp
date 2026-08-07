#include "JaipurScreens.h"

#include <cstdio>

#include "../link/LinkScreens.h"

namespace jaipurui {

namespace {

// The header band and its offset rule, as every Toybox screen wears them. A
// local copy rather than a shared helper, for the reason LinkScreens gives: a
// copy is cheaper than a header dependency between apps.
void toyboxChrome(toybox::Screen& screen, const char* title) {
  fui::HeaderProps header;
  header.title = title;
  header.borderEdges = fui::EdgesNone;
  screen.header(header);

  const fui::Rect band = screen.device().screen();
  screen.target().fill(fui::makeRect(0, toybox::kHeaderHeight + 4, band.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));

  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

// A small left-aligned line. The alignment is named even though the component
// would apply it anyway: a style whose only field is FONT_SLOT_SMALL reads as
// unset, and the theme quietly puts the full-size style back.
void smallLine(toybox::Screen& screen, const fui::Rect& where, const char* text,
               const fui::TextAlign align = fui::TextAlign::Left) {
  fui::TextStyle style;
  style.font = toybox::kTileFont;
  style.align = align;
  screen.target().text(where, text, style);
}

}  // namespace

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
      // The same words chess and battleship use. "Tap where it says
      // multiplayer" only works if something says it, and NEARBY is what it is.
      return "PLAY NEARBY";
    case StartRow::HowToPlay:
      return "HOW TO PLAY";
    default:
      return "";
  }
}

fui::Rect buildStartMenu(toybox::Screen& screen, const StartModel& model) {
  toyboxChrome(screen, "JAIPUR");

  char record[64];
  std::snprintf(record, sizeof(record), "%d PLAYED   %d WON", model.played, model.won);
  const fui::Rect line = screen.takeTop(26);
  smallLine(screen, line, record);
  screen.target().fill(fui::makeRect(line.x, static_cast<int16_t>(line.bottom() + 6), line.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));

  fui::ListItem rows[static_cast<int>(StartRow::Count)] = {};
  const int count = startRows(model);
  for (int i = 0; i < count; ++i) {
    const StartRow row = startRowAt(model, i);
    rows[i].label = startRowLabel(row);
    if (row == StartRow::Continue) rows[i].value = model.continueDetail;
    rows[i].actionValue = static_cast<int16_t>(i);
  }

  fui::ListProps list;
  list.items = rows;
  list.count = static_cast<uint16_t>(count);
  list.selectedIndex = static_cast<int16_t>(model.selected);
  list.action = ActionStartRow;
  const int16_t listHeight =
      static_cast<int16_t>(count * toybox::kRowHeight + (count - 1) * toybox::kGutter / 2 + toybox::kGutter);
  const fui::Rect content = screen.contentRect();
  const fui::Rect listBand =
      fui::makeRect(content.x, static_cast<int16_t>(content.bottom() - listHeight), content.width, listHeight);
  screen.list(list, listHeight, fui::LayoutAnchor::Bottom);

  // One symbol wherever two devices talk to each other, so it is learned once
  // and recognised everywhere.
  for (int i = 0; i < count; ++i) {
    if (startRowAt(model, i) != StartRow::PlayNearby) continue;
    toybox::iconAtRowRight(screen, listBand, i, linkui::nearbyMark(), i == model.selected);
  }

  return screen.body();
}

fui::Rect buildBoardChrome(toybox::Screen& screen, const BoardModel& model) {
  toyboxChrome(screen, "JAIPUR");

  // The narration sits at the top, under the rule: it is read once and then the
  // eye belongs to the market. One element acts, one element reports.
  const fui::Rect line = screen.takeTop(26, toybox::kGutter / 2);
  smallLine(screen, line, model.report);

  fui::ButtonProps status;
  status.label = model.status;
  // Registering no action is what makes the capsule inert while it is only
  // reporting: with NO_ACTION the component draws it and adds nothing to the
  // hit table, so there is no tappable region to drift out of step with the
  // label. It is a button exactly when it says something you can press.
  // Three things the capsule can be, in the order they take precedence: the
  // round has ended and there are scores to see, the selection is a legal move,
  // or it is only reporting.
  status.action = model.roundOver ? ActionScores : (model.canCommit ? ActionCommit : fui::NO_ACTION);
  status.borderEdges = fui::EdgesNone;
  if (!model.roundOver && !model.canCommit) status.styles = toybox::disabledButtonStyles();
  screen.button(status, linkui::withOpponentFace(screen, screen.takeBottom(toybox::kPillHeight), model.theirName));

  return screen.body();
}

fui::Rect buildRoundOver(toybox::Screen& screen, const RoundModel& model) {
  char title[32];
  std::snprintf(title, sizeof(title), "ROUND %d", model.round);
  toyboxChrome(screen, model.matchOver ? "JAIPUR" : title);

  char waiting[40];
  fui::ButtonProps go;
  if (model.waitingOnThem) {
    // Named, because "WAITING" alone reads as the app being busy rather than as
    // a person being asked for something.
    if (model.theirShortName != nullptr && model.theirShortName[0] != '\0') {
      std::snprintf(waiting, sizeof(waiting), "%s DEALS", model.theirShortName);
    } else {
      std::snprintf(waiting, sizeof(waiting), "THEY DEAL");
    }
    go.label = waiting;
    go.action = fui::NO_ACTION;
    go.styles = toybox::disabledButtonStyles();
  } else {
    go.label = model.matchOver ? "PLAY AGAIN" : "NEXT ROUND";
    go.action = model.matchOver ? ActionPlayAgain : ActionContinue;
  }
  go.borderEdges = fui::EdgesNone;
  screen.button(go, linkui::withOpponentFace(screen, screen.takeBottom(toybox::kPillHeight), model.theirName));

  return screen.body();
}

}  // namespace jaipurui

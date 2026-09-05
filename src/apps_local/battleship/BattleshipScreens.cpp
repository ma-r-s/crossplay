#include "BattleshipScreens.h"

#include <cstdio>

#include "../link/LinkScreens.h"

namespace bshipui {

namespace {

// The header band and its offset rule, as every Toybox screen wears them. A
// fourth local copy rather than a shared helper, for the reason LinkScreens
// gives: a copy is cheaper than a header dependency between apps.
void toyboxChrome(toybox::Screen& screen, const char* title) {
  fui::HeaderProps header;
  header.title = title;
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);

  toybox::headerRule(screen);

  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
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
      // The same words chess uses, and for the same reason: "tap where it says
      // multiplayer" only works if something says it, and NEARBY is what it is.
      return "PLAY NEARBY";
    default:
      return "";
  }
}

fui::Rect buildStartMenu(toybox::Screen& screen, const StartModel& model) {
  toyboxChrome(screen, "BATTLESHIP");

  // Your record, in one line, above a rule. Small: it is worth having but it is
  // not why you opened the app.
  char record[64];
  std::snprintf(record, sizeof(record), "%d PLAYED   %d WON   STREAK %d", model.played, model.won, model.streak);
  const fui::Rect line = screen.takeTop(26);
  fui::TextStyle recordStyle;
  recordStyle.font = toybox::kTileFont;
  recordStyle.align = fui::TextAlign::Left;
  screen.target().text(line, record, recordStyle);
  screen.target().fill(fui::makeRect(line.x, static_cast<int16_t>(line.bottom() + 6), line.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));

  fui::ListItem rows[static_cast<int>(StartRow::Count)] = {};
  const int count = startRows(model);
  for (int i = 0; i < count; ++i) {
    const StartRow row = startRowAt(model, i);
    rows[i].label = startRowLabel(row);
    // Only CONTINUE carries a value, and it is the game you left.
    // No value on CONTINUE. "14 SHOTS, 2 SUNK" beside the word was a label
    // wearing a receipt; the artwork above says how the game stands, in marks.
    rows[i].actionValue = static_cast<int16_t>(i);
  }

  fui::ListProps list;
  list.items = rows;
  list.count = static_cast<uint16_t>(count);
  list.selectedIndex = static_cast<int16_t>(model.selected);
  list.action = ActionStartRow;
  // Anchored to the bottom margin, so what is left above is one zone for the
  // artwork rather than slack scattered around the rows.
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

fui::Rect buildPlaceChrome(toybox::Screen& screen, const PlaceModel& model) {
  // "PLACE YOUR FLEET" came out as "PLACE YOUR FLEE": the display cut is wide
  // and the band does not scroll, so a title has to be short enough to survive
  // it. Shrink to fit is for content; chrome is written to fit.
  toyboxChrome(screen, "YOUR FLEET");

  // Two controls, side by side rather than stacked: stacked pills fuse into one
  // black slab and read as a single control, and this screen has room for
  // neither the height nor the confusion.
  const fui::Rect footer = screen.takeBottom(toybox::kPillHeight);
  const int16_t half = static_cast<int16_t>((footer.width - toybox::kGutter) / 2);

  fui::ButtonProps shuffle;
  shuffle.label = "SHUFFLE";
  shuffle.action = model.canEdit ? static_cast<fui::ActionId>(ActionShuffle) : fui::NO_ACTION;
  shuffle.borderEdges = fui::EdgesNone;
  if (!model.canEdit) shuffle.styles = toybox::disabledButtonStyles();
  screen.button(shuffle, fui::makeRect(footer.x, footer.y, half, footer.height));

  fui::ButtonProps ready;
  ready.label = "READY";
  ready.action = model.canEdit ? static_cast<fui::ActionId>(ActionReady) : fui::NO_ACTION;
  ready.borderEdges = fui::EdgesNone;
  if (!model.canEdit) ready.styles = toybox::disabledButtonStyles();
  screen.button(ready, fui::makeRect(static_cast<int16_t>(footer.right() - half), footer.y, half, footer.height));

  // The instruction sits under the rule rather than over the buttons: it is
  // read once, at the top, and then the eye belongs to the grid.
  const fui::Rect line = screen.takeTop(26, toybox::kGutter / 2);
  fui::TextStyle style;
  style.font = toybox::kTileFont;
  style.align = fui::TextAlign::Left;
  screen.target().text(line, model.status, style);

  return screen.body();
}

fui::Rect buildBoardChrome(toybox::Screen& screen, const BoardModel& model) {
  toyboxChrome(screen, "BATTLESHIP");

  // Taken before the capsule so the two can never argue about the space, and
  // drawn at the top because the eye goes there first and then to the grid.
  const fui::Rect line = screen.takeTop(26, toybox::kGutter / 2);
  fui::TextStyle reportStyle;
  reportStyle.font = toybox::kTileFont;
  reportStyle.align = fui::TextAlign::Left;
  screen.target().text(line, model.report, reportStyle);

  fui::ButtonProps status;
  status.label = model.status;
  // Registering no action is what makes the capsule inert while it is only
  // reporting: with NO_ACTION the component draws it and adds nothing to the
  // hit table, so there is no tappable region to drift out of step with the
  // label. It is a button exactly when it says something you can press.
  status.action = model.gameOver ? static_cast<fui::ActionId>(ActionPlayAgain)
                                 : (model.canFire ? static_cast<fui::ActionId>(ActionFire) : fui::NO_ACTION);
  status.borderEdges = fui::EdgesNone;
  // While it is only reporting (TAP A TARGET) it is a status line, not a
  // disabled control, so it keeps the solid capsule -- the same treatment
  // chess's inert mid-game status takes from the default button style. It must
  // NOT borrow disabledButtonStyles() here: that dither is a sparse pattern of
  // black pixels, low-contrast to read and, being sparse, exactly what a
  // partial refresh leaves residue from, which ghosted the one control on the
  // opening screen you most need to read. NO_ACTION, not the style, is what
  // keeps it inert; the label alone carries the state, TAP A TARGET vs FIRE AT
  // C4, which is what a two-state control on this panel needs anyway.
  screen.button(status, linkui::withOpponentFace(screen, screen.takeBottom(toybox::kPillHeight), model.theirName));

  return screen.body();
}

}  // namespace bshipui

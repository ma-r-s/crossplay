#pragma once

// A shelf folder's screen, as a freestanding builder. One builder draws every
// folder: GAMES and APPS differ only by their title and their rows, which is
// the whole point of them being the same kind of thing. See ToyboxScreen.h for
// why screens are written this way.

#include <Icon.h>

#include "ui/ToyboxScreen.h"

namespace shelfui {

namespace fui = freeink::ui;

enum : fui::ActionId { ActionOpen = 1, ActionOpenPlayer = 2 };

struct MenuModel {
  // Drawn at the right edge of each row, in the same order as `items`. Right
  // rather than left so every icon lines up on one axis and the labels start
  // flush with the header above them.
  const freeink::Icon* const* icons = nullptr;
  // The folder's own name, drawn in the header.
  const char* title = "";
  // Drawn beside the title. Home shows these rows with upstream's folder icon,
  // in upstream's list, in upstream's language; this is where the folder gets
  // to say what kind of folder it is.
  const freeink::Icon* mark = nullptr;
  const fui::ListItem* items = nullptr;
  int count = 0;
  int selected = 0;
  int topIndex = 0;
  // This device's name, shown to anyone it plays with. It lives here rather
  // than inside any game because it belongs to the device: a DS asked once and
  // every game used it.
  //
  // The footer it draws is a door, not a control. It used to reroll the name in
  // place, which meant the only way to see your own name was also the only way
  // to lose it, and there was no way to choose one -- you pulled the lever until
  // something acceptable came out. Tapping it now opens PLAYER, where the name
  // comes apart into three words you can steer. That screen is the fork's
  // System Settings, and this bar is its entrance.
  //
  // The face beside it is derived from the name and stored nowhere; see
  // player/PlayerAvatar.h.
  //
  // Null when this folder does not show it; the footer disappears with it.
  const char* playerName = nullptr;
};

// Where the list will scroll to, given a selection. Split out from the builder
// so the activity can keep the value it owns, and so a test can check that a
// selection below the fold actually scrolls into view rather than being styled
// on a row that is never drawn.
int topIndexFor(const fui::Rect& body, const fui::ThemeTokens& tokens, int selected, int topIndex, int count);

// The body rect the list occupies, which is what topIndexFor needs to know how
// many rows fit. Shared with the builder so the two cannot disagree.
fui::Rect listBand(const fui::DeviceContext& device, bool hasDeviceName);

void buildMenu(toybox::Screen& screen, const MenuModel& model);

}  // namespace shelfui

#pragma once

// A shelf folder's screen, as a freestanding builder. One builder draws every
// folder: GAMES and APPS differ only by their title and their rows, which is
// the whole point of them being the same kind of thing. See ToyboxScreen.h for
// why screens are written this way.

#include "ui/ToyboxScreen.h"

namespace shelfui {

namespace fui = freeink::ui;

enum : fui::ActionId { ActionOpen = 1, ActionRerollName = 2 };

struct MenuModel {
  // The folder's own name, drawn in the header.
  const char* title = "";
  const fui::ListItem* items = nullptr;
  int count = 0;
  int selected = 0;
  int topIndex = 0;
  // This device's name, shown to anyone it plays with. It lives here rather
  // than inside any game because it belongs to the device: a DS asked once and
  // every game used it. The footer is also the only place it can be changed,
  // which is why the hub is the fork's System Settings.
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

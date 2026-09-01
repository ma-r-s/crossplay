#pragma once

// A shelf folder's screen, as a freestanding builder. One builder draws every
// folder: GAMES and APPS differ only by their title and their rows, which is
// the whole point of them being the same kind of thing. See ToyboxScreen.h for
// why screens are written this way.

#include <Icon.h>

#include "ui/ToyboxScreen.h"

namespace shelfui {

namespace fui = freeink::ui;

enum : fui::ActionId { ActionOpen = 1, ActionOpenPlayer = 2, ActionGoToPage = 3 };

// How many rows a page holds, and how many pages there are.
//
// Returned together because the two are circular: the page bar only exists when
// there is more than one page, and it costs a row, which can be what creates the
// second page. Resolved once, here, so no caller can compute half of it.
struct Paging {
  int rowsPerPage;
  int pageCount;
};

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
  // The current page's items, and only those: the caller slices, so `items[0]`
  // is the top row on screen and `count` is what this page holds, which is
  // short on the last one.
  //
  // Sliced rather than handed the whole folder with a topIndex, because the list
  // component clamps topIndex to count - visible so the screen always ends up
  // full (list.h:164). That is right for scrolling and wrong for paging: page
  // two of twelve would have shown items four to eleven, repeating half of page
  // one. A page is a short list, so it is passed as one, and the component never
  // learns that paging exists.
  //
  // `actionValue` still carries the absolute index, so a tap reports which game
  // it is rather than which row.
  const fui::ListItem* items = nullptr;
  int count = 0;
  // Page-relative, matching `items`. -1 draws no cursor at all.
  int selected = 0;
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
  // Which page is showing, and how many there are. A pageCount of 1 draws no
  // page bar at all, so a folder that fits keeps every row it has.
  //
  // The bar is pips rather than prev/next arrows, and that is not decoration.
  // Arrows are up to pageCount-1 taps to reach the far end; pips are always one,
  // which matters most on the panel that is slowest to redraw. They also say
  // where you are, which arrows do not. A right chevron was the obvious glyph
  // for "next" and is exactly what could not be used: on this device a right
  // chevron already means "opens", and it is the only affordance the player bar
  // has.
  int page = 0;
  int pageCount = 1;
};

// How the folder's items divide into pages. See Paging above for why both
// numbers come back at once.
Paging pagingFor(const fui::DeviceContext& device, const fui::ThemeTokens& tokens, bool hasDeviceName, int count);

// Which page a selection is on. The list pages rather than scrolling, so the
// top row of a page is always a multiple of rowsPerPage and never lands
// mid-page: an e-ink panel repaints the whole screen either way, and a page
// that always starts in the same place is one a thumb can learn.
//
// Split out from the builder so the activity can keep the value it owns, and so
// a test can check that a selection below the fold actually brings its page into
// view rather than being styled on a row that is never drawn.
int pageFor(int selected, int rowsPerPage);

// How many pages `itemCount` rows need. Beside pageFor because it is the same
// piece of knowledge, and the activity needs it to page with the side keys
// without reaching for the whole Paging struct (which needs a device and a
// token set it does not have in loop()).
int pageCountFor(int itemCount, int rowsPerPage);

// The row that stands for `page`: its first one.
//
// A folder remembers the page it was left on, and it remembers it as a ROW,
// because a row survives things a page number does not. Rows per page is a
// property of the panel and the chrome -- the player bar costs one, the page bar
// costs another -- so a stored page number means a different set of games the
// first time a token moves, silently. A stored row is the same game either way,
// and pageFor() puts it back on whichever page now holds it.
//
// pageFor(rowForPage(p, n), n) == p for every page of every folder, which is the
// property that makes "come back to the page I was on" true rather than likely.
int rowForPage(int page, int rowsPerPage);

// The row a folder actually resumes on, given what it remembered and how many
// items it holds NOW.
//
// The two disagree when the registry has shrunk since: a game removed by a
// firmware update, and a card that outlives the firmware that wrote it. When
// the remembered row is past the end this pins it to the LAST row, so the
// folder opens on its LAST page.
//
// Last rather than first, deliberately. What was remembered was "near the end of
// this folder", and the nearest surviving place to that is the end, not the top;
// falling back to page one would throw away the only thing that was stored.
// Naming it here rather than leaving it to a clamp because a clamp that cannot
// produce the last page is exactly what made this area unpredictable before, and
// an unnamed rule cannot be tested.
int resumeRowFor(int rememberedRow, int itemCount);

// The page one step of `delta` away, wrapping at both ends. The one place a
// page key, a swipe and a pip tap all agree on what "next" means, so no route
// can move by a different amount than another -- one input, one page, and the
// same page whichever input it was.
//
// Wraps because there is no cursor to run off the end of, and a page key that
// stops working at the last page reads as a broken key.
int pageStep(int page, int pageCount, int delta);

// The body rect the list occupies. `hasPages` is what the page bar costs it, and
// it is a separate argument rather than derived because pagingFor has to ask
// this question both ways round to resolve the circularity. Shared with the
// builder so the two cannot disagree.
fui::Rect listBand(const fui::DeviceContext& device, bool hasDeviceName, bool hasPages);

void buildMenu(toybox::Screen& screen, const MenuModel& model);

}  // namespace shelfui

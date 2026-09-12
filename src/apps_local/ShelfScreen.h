#pragma once

// A shelf folder's screen, as a freestanding builder. One builder draws every
// folder: GAMES and APPS differ only by their title and their rows, which is
// the whole point of them being the same kind of thing. See ToyboxScreen.h for
// why screens are written this way.

#include <Icon.h>

#include "ui/ToyboxScreen.h"

namespace shelfui {

namespace fui = freeink::ui;

enum : fui::ActionId {
  ActionOpen = 1,
  ActionOpenPlayer = 2,
  ActionGoToPage = 3,
  // The corner chip, in both directions: one control, one action, so the
  // screen cannot be in a state where the way in is drawn and the way out is
  // not. The activity holds which way round it is.
  ActionChoose = 4,
  // A row while choosing. A different action from ActionOpen rather than the
  // same one read differently, because the two do opposite things to the same
  // pixels and a mode read from a second place is how a tap opens a game the
  // user was trying to hide.
  ActionToggleShown = 5,
};

// The one word this screen spends on the mode, and it is spent on the way OUT.
//
// There is no button for the way IN: the whole header band is the target, and
// the folder's mark sits in it as the thing that does not look like a title. A
// permanent EDIT chip was the first design and Mario turned it down -- it is a
// control that shouts on every visit for a thing done once, on a shelf whose
// whole job is the list underneath it.
//
// The exit still has to be visible, because a mode you cannot see the way out
// of is a trap, and DONE is only ever drawn inside the mode.
extern const char* const kDoneChip;

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
  // Drawn beside the title, at the right of the band. Home shows these rows with
  // upstream's folder icon, in upstream's list, in upstream's language; this is
  // where the folder gets to say what kind of folder it is -- and it is what a
  // finger aims at to open the chooser, though the whole band answers.
  const freeink::Icon* mark = nullptr;
  // One per row, in the same order as `items`: true when that item is on the
  // list. NON-NULL IS THE MODE. A screen with checks draws a box at the left
  // of every row, puts DONE in the corner where the mark was, and sends taps to
  // ActionToggleShown instead of ActionOpen -- all of it from this one field,
  // because a mode carried in two places is a mode that can disagree with
  // itself, and here the disagreement opens a game while you are trying to
  // hide it.
  const bool* checks = nullptr;
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
  // `actionValue` carries the item's index in the WHOLE list -- the folder's
  // shown items while browsing, its every item while choosing -- so a tap
  // reports which entry it is rather than which row of the slice. It is not the
  // registry index: the activity turns one into the other in a single place
  // (ShelfFolderActivity::itemAtRow), because a screen that knows both units
  // is a screen that can open the game below the one you tapped.
  const fui::ListItem* items = nullptr;
  int count = 0;
  // There is no selection field, and that is the design. Navigation here is
  // touch: the two side keys page, and `frontButtonConfirm` is an unassigned
  // pin, so nothing can move an inverted row and nothing can act on one. Which
  // page you are on is said by the pips, which is a fact about the list rather
  // than a cursor over it.
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
  // The bar is marks rather than prev/next arrows, and that is not decoration.
  // Arrows are up to pageCount-1 taps to reach the far end; a mark is always
  // one, which matters most on the panel that is slowest to redraw. They also
  // say where you are, which arrows do not. A right chevron was the obvious
  // glyph for "next" and is exactly what could not be used: on this device a
  // right chevron already means "opens", and it is the only affordance the
  // player bar has.
  //
  // Each mark carries its page NUMBER, and the current one is a filled slab.
  // They were 10px squares, filled for here and outlined for elsewhere, and a
  // cold tester called them "the size of a full stop": at that size the
  // difference between the two states is the only thing saying where you are,
  // and it is smaller than the ink of one letter. The list resumes on the page
  // it was left on, so the row in position two is a different game on each
  // visit -- which makes "which page is this" the one question the screen has
  // to answer before any tap is safe.
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
// a test can check that a row below the fold actually brings its page into view
// rather than being asked for on a page that does not hold it.
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
// stops working at the last page reads as a broken key. That argument holds
// where the screen has no legible page indicator to explain the stop; the shelf
// has one and takes pageStepClamped instead. Kept because Hacker News's story
// list steps through this.
int pageStep(int page, int pageCount, int delta);

// The same step, STOPPING at both ends rather than wrapping.
//
// A wrap is only safe when the user can see where it put them. Every page of a
// folder draws its rows at the same eight screen positions, so a page arrived at
// by accident looks exactly like the page that was wanted, and the next tap
// opens a different game. A cold tester walked forward off the last page and
// launched CHECKERS believing it was TRIVIA -- forward from the last page is the
// one step nobody ever means, so it is the one step that does nothing.
//
// The far page stays one tap away on the page bar, which is the same one tap the
// wrap cost and is why the bar carries marks rather than arrows.
int pageStepClamped(int page, int pageCount, int delta);

// The body rect the list occupies. `hasPages` is what the page bar costs it, and
// it is a separate argument rather than derived because pagingFor has to ask
// this question both ways round to resolve the circularity. Shared with the
// builder so the two cannot disagree.
//
// `hasDeviceName` is a property of the FOLDER and not of the mode, which is
// what keeps the chooser showing the same games on the same page as the list it
// was opened from. Making the band appear because the chooser wanted somewhere
// to put its caption would have reflowed APPS, which has no player bar: ten
// rows browsing and nine choosing, so the row under your finger is a different
// app before you have touched anything. The caption goes in the band the folder
// already has, or it does not go anywhere.
fui::Rect listBand(const fui::DeviceContext& device, bool hasDeviceName, bool hasPages);

void buildMenu(toybox::Screen& screen, const MenuModel& model);

}  // namespace shelfui

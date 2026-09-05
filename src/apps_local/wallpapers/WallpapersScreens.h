#pragma once

// The Wallpapers screens. The chrome (header, page label, hints, empty state)
// is a freestanding builder in the XkcdScreens mould, so host-tests/ui/ can
// assert it. The grid of wallpaper thumbnails is the app's own surface: it is
// drawn by the Activity, because a thumbnail is a BMP decoded and scaled down
// off the SD card, which a freestanding screen cannot touch. What lives here
// for the grid is only its GEOMETRY -- the cell rectangles -- shared between the
// Activity's drawing and its hit-test so the two cannot disagree (the rule that
// has caught more bugs in this fork than any other).
//
// Portrait, 480x800. Two columns of thumbnails (Mario's ask), the wallpaper's
// own 480x800 aspect preserved in each cell. The wallpaper currently set as the
// sleep screen wears a thick border; a tap on any cell makes that one the sleep
// screen.

#include "../ui/ToyboxScreen.h"

namespace wallpapersui {

namespace fui = freeink::ui;

// The grid's geometry for the current variant, derived from the panel rather
// than guessed, so the drawing, the captions and the hit-test all read one set
// of rectangles.
struct GridGeom {
  int cols = 2;     // two columns, always
  int rows = 2;     // rows per page
  int perPage = 4;  // cols * rows
  int16_t cellW = 0;
  int16_t cellH = 0;       // the thumbnail area only
  int16_t markerRoom = 0;  // clearance under the thumbnail for the selection marker
  int16_t captionH = 0;    // the name, below the marker's clearance
  int16_t originX = 0;     // top-left of slot 0
  int16_t originY = 0;
  int16_t gapX = 0;
  int16_t gapY = 0;
  int16_t pageDotsY = 0;  // where the page-dot strip sits (when more than one page)
};

GridGeom gridGeom(const fui::DeviceContext& device);

// slot is 0..perPage-1, row-major. thumbRect is the image; cellRect includes
// the caption row; captionRect is empty in variants without captions.
fui::Rect thumbRect(const GridGeom& g, int slot);
fui::Rect cellRect(const GridGeom& g, int slot);
fui::Rect captionRect(const GridGeom& g, int slot);

// Which slot a tap at (x, y) lands on, or -1. Reads the same rectangles the
// Activity draws into.
int cellAt(const GridGeom& g, int x, int y);

// The selection marker: four corner brackets drawn in the cell's PADDING, so
// they touch neither the artwork nor the caption. It lives here rather than in
// the Activity because "does the mark collide with the label" is a question
// about rectangles, and a question about rectangles must be answerable without
// a panel. host-tests/wallcaption walks every built-in name through it.
struct MarkerRects {
  static constexpr int kCount = 8;  // four corners, two arms each
  fui::Rect r[kCount];
};
MarkerRects markerRects(const fui::Rect& thumb);

// The band the brackets occupy BELOW the artwork. The caption's line box must
// start after this, and the gap is what makes the assertion in wallcaption a
// property of the layout rather than of any one string.
int markerBottomExtent(const fui::Rect& thumb);

// What a tap on one of the built screens means. The grid's cells are hit-tested
// against geometry instead (see cellAt), so these are only the chrome controls.
enum : fui::ActionId {
  ActionGetSet = 1,   // fetch the built-in wallpapers over WiFi
  ActionAddOwn = 2,   // the "make your own in a browser" card
  ActionRetry = 3,    // try the fetch again after a failure
  ActionDismiss = 4,  // acknowledge a notice and go back to whatever is on the card
};

// The BEFORE state: the built-in set is not on the card. This screen has to sell
// the set and offer exactly one action, because an empty grid with a lone "+"
// reads as a crash -- the most repeated user-visible failure in this fork, found
// by cold testers twice (a-silent-screen-reads-as-a-crash).
struct OfferModel {
  int count = 0;       // how many wallpapers are on offer
  uint64_t bytes = 0;  // what the download weighs, derived not typed
  const char* warning = nullptr;
  // Some of the built-ins are already here (a resumed or partial fetch), so the
  // offer says "the rest" rather than claiming the whole set is missing.
  int alreadyHave = 0;
};
void buildOffer(toybox::Screen& screen, const OfferModel& model);

// The DOWNLOADING state. Painted from inside the blocking fetch through
// requestUpdateAndWait(), so it must be cheap and must never depend on state the
// download has not settled yet.
struct FetchingModel {
  int done = 0;
  int total = 0;
  bool cancelling = false;
  // Fetching, unpacking and preparing thumbnails are three real phases over the
  // same set. Each used to drive one bar from 0 to 100, so on hardware it
  // filled, reset and filled again -- which reads as the download restarting.
  // One bar now spans all of them and the caption names the running phase: a
  // progress bar may never go backwards without saying why.
  int phase = 0;       // 0 fetch, 1 unpack, 2 thumbnails
  int phaseCount = 3;  // how many such sweeps make one whole bar
};
void buildFetching(toybox::Screen& screen, const FetchingModel& model);

// Where the bar sits across BOTH phases, as at/units. Freestanding because
// "the bar never goes backwards" is a property of the arithmetic, and it must
// be assertable without a panel -- the defect it guards was only ever visible
// on hardware. host-tests/wallcaption walks the whole fetch and asserts it.
struct BarSpan {
  int at = 0;
  int units = 1;
};
BarSpan fetchBarSpan(const FetchingModel& model);

// The FAILED state, and every other "something happened, here is what" screen.
// Always carries an action: a screen that reports a failure and gives you
// nothing to press is a dead end, and Get Books shipped exactly that once.
struct NoticeModel {
  const char* headline = "";
  const char* body = "";
  const char* actionLabel = nullptr;
  fui::ActionId action = 0;
};
void buildNotice(toybox::Screen& screen, const NoticeModel& model);

// The chrome above and around the grid. rightLabel carries the count or the
// page ("PAGE 2 / 3"); when nothing is set yet the hint says so, because a grid
// with no border and no words is indistinguishable from one whose selection
// simply is not drawing (a-silent-screen-reads-as-a-crash).
struct GridChromeModel {
  const char* title = "WALLPAPERS";
  const char* rightLabel = nullptr;
  const char* warning = nullptr;  // free-space advisory, null when there is room
  bool hasActive = false;         // false -> draw the "tap one to set it" hint
};

void buildGridChrome(toybox::Screen& screen, const GridChromeModel& model);

// The empty state: no wallpapers on the card at all. Names the gap and how to
// fill it, so a fresh device does not look broken.
struct EmptyModel {
  const char* title = "WALLPAPERS";
  const char* warning = nullptr;
};

void buildEmpty(toybox::Screen& screen, const EmptyModel& model);

// The help card reached from the "+ Add a wallpaper" tile: how to get your own
// wallpapers onto the card. A screen, so it is testable.
void buildHelp(toybox::Screen& screen);

}  // namespace wallpapersui

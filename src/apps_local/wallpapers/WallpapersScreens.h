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

// How many pages the grid spans. Freestanding because the drawing, the
// hit-test and the page count are three readers of one number, and the one that
// disagreed made the last wallpaper of a page-boundary library unreachable:
// pageCount() counted ONE chrome tile while the other two counted
// specialTiles(), which is two while the built-in set is incomplete.
int pageCountFor(int specialTiles, int libraryCount, int perPage);

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
  ActionChoose = 5,   // the header chip: enter or leave choose-a-set mode
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

// What a tap on the grid MEANS, for the surface gate that refuses taps on a
// frame the user has not seen yet.
//
// Deliberately NOT the selection. The gate exists so a tap cannot act on a
// surface whose pixels have moved under the finger, and moving the brackets
// moves nothing: cell N is wallpaper N whether or not it is the chosen one. The
// things that DO remap a cell are here -- the page, the view, how many
// wallpapers there are, and how many chrome tiles sit in front of them.
//
// Including the selection made every tap deaf for the length of one refresh
// after every tap, which is what "touches get lost" was.
// `choosing` IS in here, unlike the selection, and the difference is the point:
// the selection does not change what a cell does, and choose-a-set mode does --
// a tile tap pins one wallpaper outside it and toggles membership inside it. A
// tap that left the finger against the previous frame must not act on the new
// meaning (same-pixel-different-action).
uint32_t gridMeaning(int page, int view, int libraryCount, int specialTiles, bool choosing);

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
  // The sleep-screen line (#354). Carries every fact that applies at once: what
  // the last selection changed behind the user's back, AND any standing caveat
  // about the wallpaper not reaching the glass. Built by
  // wallpapers::stripLineAfterSelection / reachHint, which are what guarantee
  // one cannot hide the other. Wins the strip -- see buildGridChrome.
  const char* note = nullptr;
  // Choose-a-set mode. Changes the title and the chip's label, and nothing
  // else: the grid below is the same grid, because the wallpapers are what the
  // user is choosing between in both modes. The chip is the ONLY way in and the
  // only way out besides Back, and it is a tap rather than a hold -- a hold on
  // this panel arrives as a tap often enough that a feature behind one
  // intermittently does not exist (MappedInputManager, InputManager::wasTouchTap).
  bool choosing = false;
};

// What the header chip says. One place, because the width the title is fitted
// to comes out of this string's measured width, and a second copy is a second
// thing to edit alone (the same rule as HackerNews' save chip).
const char* chooseChipLabel(bool choosing);

// The lowest-priority line on the strip: how you get to a set at all.
//
// It names the chip's own word, so the two cannot drift -- host-tests/wallpapers
// asserts the sentence CONTAINS chooseChipLabel(false), which is the one case
// where matching the description is the point rather than the bug: rename the
// chip and this must be renamed with it (derived-facts-written-as-literals).
//
// It sits BELOW the free-space advisory deliberately. It is chrome, not news,
// and a permanent hint that outranked a filling card would suppress that
// warning forever.
const char* chooseHint();

void buildGridChrome(toybox::Screen& screen, const GridChromeModel& model);

// The hint strip's box: how wide a sentence may be inside a given safe rect,
// and how tall the strip is. Both exposed so host-tests/wallcaption can measure
// the sentences in the face the strip really resolves rather than assume they
// fit. Assuming is how the first version of this shipped four sentences the
// panel cut, plus a rung whose line box was TALLER than the strip.
//
// buildGridChrome pins the style to FONT_SLOT_SMALL, so a sentence too wide has
// nothing left to step down to and is cut with an ellipsis. A cut sentence is
// the defect the strip exists to avoid.
int16_t hintTextWidth(const freeink::ui::Rect& safe);
int16_t hintStripHeight();

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

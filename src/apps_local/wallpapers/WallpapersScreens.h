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
  // The hold sheet, and the confirm behind its DELETE.
  ActionPreview = 5,        // show this wallpaper full screen, as the sleep screen draws it
  ActionDelete = 6,         // ask to delete it -- opens the confirm, deletes nothing
  ActionKeep = 7,           // the confirm's safe half: leave it alone
  ActionConfirmDelete = 8,  // the only destructive control in this app
};

// ---------------------------------------------------------------------------
// THE HOLD SHEET AND ITS CONFIRM, and why their rectangles are public.
//
// This fork has destroyed user data by putting a new meaning under a pixel a
// finger was already travelling towards (same-pixel-different-action). The
// wallpapers grid is the worst possible host for that: a tap on a cell SETS the
// sleep screen with no confirmation, and a hold arrives as a tap unless
// MappedInputManager::tapWasHeldLong() says otherwise.
//
// So the two screens are laid out against ONE set of rectangles, published here
// and asserted in host-tests/wallcaption, rather than each drawing its own:
//
//   * confirmKeepRect() IS sheetDeleteRect(), the same pixels. The button the
//     user just pressed to reach the confirm becomes the SAFE half of it, so a
//     second press of the same spot -- a double tap, an impatient repeat during
//     the 0.3-2s e-ink repaint, a finger that never moved -- cancels. It cannot
//     delete, because there is nothing destructive there to hit.
//   * confirmDeleteRect() overlaps NEITHER of the sheet's controls. Reaching it
//     takes a deliberate move to a place nothing was a moment ago.
//
// What this does NOT buy, said plainly because a reader will otherwise assume
// it: the confirm's DELETE does sit over the grid's cells. It cannot not --
// the cells span y 134..758 of an 800px panel and the only cell-free bands are
// 46px, 24px and 42px tall, none of which holds a 64px finger target. The grid
// is two screens and one 500ms hold away, and the interaction buffer refuses
// every tap routed against a table the panel has not shown
// (RevealedInteractions::route), so no single remembered tap can reach it.
constexpr int16_t kSheetButtonH = 64;
// The name, and the sentence under it. Published for the same reason the button
// rects are: the sentence's four combinations are measured against this box in
// the real face by host-tests/wallcaption, and a box read from screen.body()
// could not be. The prose box is derived from BOTH ends -- under the headline,
// above the first control -- so moving either shrinks it rather than letting a
// sentence run under a button.
fui::Rect sheetHeadRect(const fui::DeviceContext& device);
fui::Rect sheetProseRect(const fui::DeviceContext& device);
fui::Rect confirmProseRect(const fui::DeviceContext& device);
fui::Rect sheetPreviewRect(const fui::DeviceContext& device);
fui::Rect sheetDeleteRect(const fui::DeviceContext& device);
fui::Rect confirmKeepRect(const fui::DeviceContext& device);
fui::Rect confirmDeleteRect(const fui::DeviceContext& device);

// The sheet a hold opens: this one wallpaper, and the two things you can do to
// it that a tap cannot.
struct SheetModel {
  const char* name = "";  // the wallpaper's display name, not its file name
  bool isActive = false;  // it is the one currently on the sleep screen
};
void buildSheet(toybox::Screen& screen, const SheetModel& model);

// The confirm behind DELETE. `consequence` comes from
// wallpapers::deleteConsequence -- built there so all four of its combinations
// are walked by a test rather than assembled per render.
struct ConfirmModel {
  const char* name = "";
  const char* consequence = "";
};
void buildConfirm(toybox::Screen& screen, const ConfirmModel& model);

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
uint32_t gridMeaning(int page, int view, int libraryCount, int specialTiles);

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

// The screen that replaces buildHelp: scan the code, pick a photo on the phone,
// and it is here. The address is drawn as well as encoded, because a QR is
// unreadable to a person and the one failure this screen has -- a phone on a
// different network -- is one the reader has to be able to check by hand.
struct AddModel {
  const char* url = "";          // "http://crossplay.local/w" -- also what the QR encodes
  const char* altUrl = nullptr;  // "http://192.168.1.42/w" -- printed, never encoded
  int added = 0;                 // how many have arrived while this screen has been up
  const char* status = nullptr;  // a line replacing the prose while connecting
};

// Returns the square the Activity must draw the QR into. The screen cannot draw
// it: QrUtils needs the renderer, and this file compiles against the SDK alone.
fui::Rect buildAdd(toybox::Screen& screen, const AddModel& model);

}  // namespace wallpapersui

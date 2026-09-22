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

#include <string>
#include <string_view>

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
  // The hold sheet, and the confirm behind its DELETE.
  ActionPreview = 5,        // show this wallpaper full screen, as the sleep screen draws it
  ActionDelete = 6,         // ask to delete it -- opens the confirm, deletes nothing
  ActionKeep = 7,           // the confirm's safe half: leave it alone
  ActionConfirmDelete = 8,  // the only destructive control in this app
  ActionChoose = 9,         // the header chip: enter or leave choose-a-set mode
  // The Live screen's controls. The first three are not destructive; the last
  // two are the confirm that stands in front of the one that is.
  ActionLiveToggle = 10,  // start or stop showing what the website sends
  ActionLiveCheck = 11,   // ask the website now instead of at the next check
  ActionLiveAdd = 12,     // let somebody else send to this reader
  // A row in the sender list. Carries the sender's INDEX as its action value, so
  // one id covers four rows and the Activity maps the index back to the id the
  // service named. Values are 0..3 and never negative: a negative action value
  // is dead to touch in this fork.
  ActionLiveSender = 13,
  ActionLiveKeep = 14,    // the confirm's safe half: leave that phone alone
  ActionLiveRevoke = 15,  // the confirm's destructive half
  // The "Your phone" destination's second route. Its FIRST route is
  // ActionAddOwn above, reused rather than duplicated: the offer screen's USE
  // MY OWN PHOTO opens the same local upload server, and two ids for one
  // destination is two things to keep in step.
  ActionLiveOpen = 16,
  // Put the code back after a picture has landed. NOT ActionAddOwn: that one
  // means "get me to the upload server", takes the radio and can end on the no
  // WiFi notice, and the server is already running by the time this control
  // exists. One id per destination, and these are two.
  ActionAddAnother = 17,
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

// The sentence buildSheet draws under the name, in both its forms. Published
// rather than typed inline, because host-tests/wallcaption measures it against
// sheetProseRect in the real face -- and a test holding its own COPY of the
// sentence keeps measuring the old one after the source is edited, and stays
// green while the panel cuts it (derived-facts-written-as-literals).
const char* sheetInstruction(bool isActive);

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
  // Live is what the sleep screen shows. NOT folded into hasActive: that flag
  // only buys silence, and silence is what made this wrong -- the "Your phone"
  // tile wore the selection marker while the strip went on saying nothing was
  // set. Live needs a line of its own, so it is a fact of its own.
  bool liveOn = false;
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

// What the header chip shows. One place, because the room the title is fitted
// to comes out of the chip, and a second copy is a second thing to edit alone
// (the same rule as HackerNews' save chip).
//
// Two glyphs, and host-tests/wallcaption asserts they are two: a chip whose
// modes look alike cannot say whether you are in one.
const freeink::Icon& chooseChipIcon(bool choosing);

// The lowest-priority line on the strip: how you get to a set at all.
//
// It describes the chip rather than quoting it, because the chip carries no
// word to quote -- host-tests/wallcaption asserts it does not name one, which
// is how the old sentence would have gone on pointing at a "CHOOSE" that is no
// longer on the screen (derived-facts-written-as-literals).
//
// It sits BELOW the free-space advisory deliberately. It is chrome, not news,
// and a permanent hint that outranked a filling card would suppress that
// warning forever.
const char* chooseHint();

// The caption on the grid's first tile, and the strip's sentence about it.
//
// Published as a pair, and the pair is the point. The sentence names the tile
// the selection marker is sitting on, so the two carry ONE noun: a second copy
// of "Your phone" typed into the sentence would go on saying it after the tile
// stopped (derived-facts-written-as-literals). host-tests/wallcaption asserts
// the sentence still opens with the caption, which is what makes them one.
//
// The caption lives here rather than in the Activity that draws it because a
// test can link this file and cannot link the Activity.
const char* liveTileCaption();
const char* liveStripLine();

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

// Scan the code, pick a photo on the phone,
// and it is here. The address is drawn as well as encoded, because a QR is
// unreadable to a person and the one failure this screen has -- a phone on a
// different network -- is one the reader has to be able to check by hand.
struct AddModel {
  const char* url = "";          // "http://crossplay.local/w" -- also what the QR encodes
  const char* altUrl = nullptr;  // "http://192.168.1.42/w" -- printed, never encoded
  int added = 0;                 // how many have arrived while this screen has been up
  const char* status = nullptr;  // a line replacing the prose while connecting
  // THE PICTURE THAT JUST LANDED, and it is on the sleep screen. Its display
  // name, or nullptr while nothing has arrived on this visit.
  //
  // The route exists because somebody wanted their own photo on the glass, so
  // it ends with their own photo on the glass: the arrival is committed as the
  // chosen wallpaper by the Activity the moment it lands, which -- through the
  // one exclusivity rule in commitSelection -- also takes Live off the sleep
  // screen. Before this, the upload route ended in a library the person then
  // had to go and tap in, and on a reader with Live running it ended with Live
  // still owning the panel.
  const char* arrived = nullptr;
};

// Where the Activity must draw, for one arrangement of the Add screen.
//
// TWO RECTS FROM ONE FUNCTION rather than a rect and a rule the Activity would
// have to repeat: whether this arrangement is showing a code, a picture or both
// is the screen's own business, and a zero width means "not on this screen".
// The Activity draws what it is given and knows nothing about the arrangement,
// which is what stops the two from disagreeing about which square holds what.
struct AddRects {
  fui::Rect qr{};     // the QR square; zero when no code is on this screen
  fui::Rect thumb{};  // the arrival's thumbnail; zero when no picture is on it
};

// The screen cannot draw either one: QrUtils and the BMP decoder both need the
// renderer, and this file compiles against the SDK alone.
AddRects buildAdd(toybox::Screen& screen, const AddModel& model);

// HOW BIG THE ARRIVAL'S PICTURE IS, published because two things need it and
// they must not each pick their own. The screen places a square of this side;
// the Activity DECODES at this side, off the paint and before the rect exists,
// because a thumbnail is a BMP read off the card and a decode inside a render
// is work on the wrong task. A decode at one size dropped into a rect of
// another is either a gap round the picture or a picture through the caption.
int16_t addPictureSide();

// The Add screen's own strings, for the test that walks it. Functions rather
// than extern constants for the reason liveRemoveMark() is one: a
// header-declared object is a different object per translation unit.
const char* addArrivedHeadline();
const char* addAnotherLabel();
const char* addArrivedLine();
const char* addAgainLine();
// The foot's two words. They are not decoration: while the screen is waiting
// for a picture, leaving ABANDONS the wait, and once one has arrived leaving
// is how the route finishes. A screen that kept saying "BACK STOPS" after a
// success would be describing that success as an abort.
const char* addFootWaiting();
const char* addFootArrived();

// ---------------------------------------------------------------------------
// LIVE: where the grid's "Your phone" tile goes.
//
// THE LIVE PAGE'S ADDRESS, AND THE ONLY PLACE IT IS WRITTEN.
//
// The panel PRINTS this in words and the QR ENCODES it with the code on the
// end. Both halves are on screen at once, next to each other, because a QR
// tells a person nothing and a phone that will not scan leaves the typed
// address as the only way in -- so the two must never name different hosts.
// Two copies of a URL is exactly how one of them goes on pointing at last
// month's host with nothing on screen showing it, which is why the Activity
// asks liveLink() rather than assembling a second one beside the constant.
//
// The page is part of the CrossPlay site (site/live/); the service it talks to
// is fridge.ma-r-s.com and the reader never sends anybody there.
constexpr const char* kLiveAddress = "crossplay.ma-r-s.com/live/";
constexpr const char* kLiveCodeParam = "?c=";

// The address with a code on it, ready to encode. Takes the code in either
// spelling -- the raw six digits from the service or the grouped "482 160" the
// panel prints -- because the screen holds only the grouped one and a link
// carrying a space is a link that does not open.
inline std::string liveLink(std::string_view code) {
  std::string out = std::string("https://") + kLiveAddress + kLiveCodeParam;
  for (const char c : code) {
    if (c != ' ') out.push_back(c);
  }
  return out;
}

// A sleep screen fed from a website. Somebody opens the Live page on their
// phone, sends a drawing or a photo, and the reader shows it. The reader PULLS
// on a schedule and is asleep the rest of the time, so there is no connection
// to report and no "now" to show: this screen's whole job is to say when the
// next check is, how often they come, and who may feed it.
//
// ONE OF THOSE THREE IS THE REASON ANYBODY OPENS IT, and the layout says so.
// The next check is the display cut with nothing above it; the cadence is a
// small line under it; the three controls are a mark and one word each on one
// row; the senders are rows that end in an X. The screen it replaces spent
// NEXT CHECK, HOW OFTEN, CHECK NOW, TURN IT OFF, WHO CAN SEND, TAP TO REMOVE
// and ADD SOMEBODY on the same facts, and two of those pairs said the same
// thing twice ("In about 24 hours" over "Every 24 hours") because the next
// check was computed from the interval rather than from what was left of it.
// That arithmetic moved to live::nextCheckPhrase, where a test walks it.
//
// A CENTRED STACK: the code dominant, the prose and the QR beneath it. Three
// arrangements were built and rendered side by side (a stack, a numbered rail,
// and a split with an inverted panel); Mario picked the stack, and the other
// two went with the macro that chose between them in the same commit. The stack
// is the only one whose unpaired half has a single axis: a person holding the
// reader up to a phone camera or reading digits aloud never has to choose where
// to look first.
//
// Both states, one builder, because they are one destination: before a phone is
// paired the screen is a code to type, and afterwards it is what that code
// bought. Building them apart is how the two would come to disagree about what
// Live even is.
struct LiveModel {
  bool configured = false;  // a phone has been paired with this reader
  bool on = false;          // and Live is what the sleep screen shows
  // UNPAIRED. Both are drawn; only the Activity's own link is encoded, the same
  // split buildAdd draws (a QR tells a person nothing, and the address in words
  // is the only thing to fall back on when a phone will not scan).
  const char* code = "";  // six digits, grouped so they can be read aloud
  const char* url = "";   // kLiveAddress -- drawn in words, never encoded here
  // What the device is doing right now. Both halves of this screen have ONE
  // line for it and neither gains a row: unpaired it replaces "Code lasts ten
  // minutes." under the address, paired it replaces the foot's "BACK RETURNS".
  // Both stacks are centred against a measured height, and a line added to
  // either pushes its last element out of the body.
  //
  // nullptr means there is nothing to report and the standing line stands.
  const char* status = nullptr;
  // PAIRED, and this pair is the screen's whole hierarchy: `nextCheck` is the
  // headline at the display cut and `cadence` is the small line under it.
  //
  // Both come from live::nextCheckPhrase and live::scheduleNote, never
  // assembled per render -- a line built inside a paint is a line no test can
  // walk, and this one is the biggest thing on the panel. They are also short
  // BY CONSTRUCTION rather than by luck: "In about 45 minutes" measures 464px
  // at the display cut against a 448px body, so a phrasing an inch longer would
  // not fail, it would silently drop the headline a rung and the screen would
  // lose its hierarchy without ever looking broken.
  const char* nextCheck = "";  // "In 5 hours", "Any moment", "Paused", "Soon"
  // NOT always the cadence, which is why live::scheduleNote takes the whole
  // schedule: it is "Last check failed." in backoff, and "Every 6 hours when
  // on" while the toggle is off. "Paused" over "Every 6 hours" is the screen
  // saying it is not checking and then naming how often it checks.
  const char* cadence = "";  // "Every 6 hours", "Every 6 hours when on", "3 checks failed."
  struct Sender {
    const char* who = nullptr;
    const char* since = nullptr;  // when they were let in: "12 Sep"
  };
  // A fixed array rather than a vector. This model is filled on the loop task
  // and read on the render task with no lock between them, and this app has
  // already been bitten once by a container reallocated under a paint -- see
  // SheetModel's sheetIsActive_ above.
  //
  // FOUR, and the number here is not the rule. THE SERVICE IS THE ONE THAT
  // DECIDES: it refuses a fifth phone before a code is minted and answers its
  // own cap on every /api/senders. This is the size of the array the screen can
  // draw, kept equal to the service's so a full list fits, and
  // WallpapersActivity.cpp static_asserts the two against each other so they
  // cannot drift in silence. A drawing limit is not a limit -- a fifth sender
  // the service allowed would exist, could write to this fridge, and would be
  // invisible on the one screen that can revoke it.
  static constexpr int kMaxSenders = 4;
  Sender senders[kMaxSenders];
  int senderCount = 0;
  // A join code is up: somebody pressed ADD on a reader that is
  // already paired. The screen shows the CODE half while this is true, which is
  // the same screen the first setup code uses -- there is one way to be given a
  // six-digit number here and it looks the same both times.
  //
  // `configured` stays true underneath it, because it still is: the reader is
  // paired, the picture is still on the glass, and Back goes to the list.
  bool joining = false;
};

// Does this model put a six-digit code on the screen?
//
// Published because TWO readers answer it and one of them is not this file: the
// Activity binds the 82px cut into FONT_SLOT_SMALL for the code screen and the
// button cut for the list, and it has to pick the face set BEFORE the screen is
// built. A second reading of "unpaired, or joining" in the Activity is a second
// thing to edit alone, and the version that got edited alone would draw the
// code somebody is reading down a telephone at 20px.
bool liveShowsCode(const LiveModel& model);

// The sender list, when nobody is on it.
//
// A reader with no senders is RECOVERABLE, not broken: the picture already on
// the glass stays there and ADD is in the row above. It has to say so IN WORDS,
// because an empty region where content belongs is this fork's most repeated
// user-visible failure -- twice found by cold testers, both times reported as a
// crash (a-silent-screen-reads-as-a-crash).
//
// It NAMES the control, which it did not until the header said it did. With
// the list unheaded there is nothing else on the screen to say what ADD adds,
// so a reader in this state had a button with no antecedent -- and this file
// claimed otherwise for as long as that was true.
//
// It is the one sentence left on this screen, and it is left because there is
// nothing else in that space to read.
const char* liveNobodySends();

// The sentence that says which of the two six-digit codes is on the screen.
//
// Both halves of this screen show a code under the word LIVE, and only this
// says whether it makes a fridge or adds a phone to the one that exists.
// Published so host-tests/wallcaption lays the real string out in the real box,
// and so a test can assert it is the one drawn while joining -- a screen that
// went on offering "Send a picture from your phone" over a code meant for
// somebody else would be telling the wrong person what the code does.
const char* liveJoinPrompt();

// What says a row is a control, now that no line does.
//
// The list carried "WHO CAN SEND" over it and "TAP TO REMOVE" beside that:
// two headings for rows that are a name and a date. Both are gone and the row
// ends in an X instead, which is what every list in the world puts at the end
// of a row you can take out.
//
// The affordance is a MARK AND NOT A WORD here, unlike the three controls
// above, and the difference is deliberate rather than an exception: a word per
// row is the word four times, and what a person needs before access is
// destroyed is not a label on the row -- it is the confirm behind it, which
// NAMES the person and says what removing them costs. Pressing a row to find
// out is free; pressing REMOVE is not.
//
// Published so host-tests/wallcaption can assert the mark is drawn once per
// row and never on an empty list, which is the assertion that used to be
// "the hint is on the screen".
const freeink::Icon& liveRemoveMark();

// ---------------------------------------------------------------------------
// THE REVOKE CONFIRM, and why its two rectangles are published.
//
// Revoking is destructive, remote, and SILENT to the person it happens to: they
// are in another country and the service tells them nothing. There is no undo
// and no apology to send. So it gets the same treatment the wallpaper delete
// got, with one difference that matters: the control the user just pressed is a
// ROW in a list, and rows move as the list changes.
//
// So the same defence is built the same way, one step stronger:
//
//   * liveKeepRect() IS liveSendersBand() -- the whole strip the four rows
//     share, not one row of it. The confirm is reached by pressing SOME row and
//     the confirm cannot know which finger arrived where, so the safe half
//     covers every row there is. A second press of the spot that opened it (a
//     double tap, an impatient repeat during a 0.3-2s e-ink repaint, a finger
//     that never moved) lands on KEEP whichever row it was. It is a large
//     button on purpose: it is the safe default on the only screen in Live that
//     destroys anything.
//   * liveRevokeRect() lies wholly outside that band. Reaching it takes a
//     deliberate move to a place no row ever is.
//
// liveRevokeRect() used to sit on the pixels ADD SOMEBODY occupied on the list,
// because the band ended 17px above the foot and there was nowhere else a 64px
// finger target fitted. It no longer does: the third control moved up beside
// the other two when the screen lost its headings, so the strip under the band
// carries nothing on the list at all. host-tests/wallcaption asserts that
// -- no interaction on the list screen touches this rect -- rather than taking
// this paragraph's word for it, which is the difference between a defence and
// a comment about one.
//
// All three are measured through the screen rather than taken from the device,
// because the band is where the rows END UP: it comes out of the face's line
// heights, and a band written as a constant would stop describing the list the
// first time a cut changed.
//
// The row height does NOT depend on the list. Rows are one height whether they
// are laid out side by side or stacked, and the band is reserved whether or not
// it is full, so adding or losing a phone never slides a control under a finger
// that was already travelling (same-pixel-different-action). It also has to be
// that way for the confirm to place anything at all: RevokeModel carries a name
// and no list, so a band that varied with the names would be a band the confirm
// cannot compute.
fui::Rect liveSenderRowRect(toybox::Screen& screen, int index);
fui::Rect liveSendersBand(toybox::Screen& screen);
fui::Rect liveKeepRect(toybox::Screen& screen);
fui::Rect liveRevokeRect(toybox::Screen& screen);

// The confirm itself. The name is drawn on its own line, dominant, rather than
// folded into a sentence: a sentence with a name in it has to be composed, and
// a line composed inside a paint is a line no test can walk -- which is exactly
// the line you would want walked before a screen removes somebody's access.
struct RevokeModel {
  const char* who = "";
  const char* since = nullptr;
};
void buildLiveRevoke(toybox::Screen& screen, const RevokeModel& model);

// The word the confirm stacks over the date, and the only place on the device
// that says what a bare "12 Sep" beside a name MEANS.
//
// The list's rows cannot carry it: it would be the same word four times over a
// list whose whole point is that it is short. But a bare date under no heading
// reads as when that phone last SENT, which is a different fact and the one
// somebody would act on -- so the screen that is about to remove a person says
// it, where there is exactly one date and room for a word over it.
//
// Published so host-tests/wallcaption can assert it is drawn beside the date
// rather than a literal here going stale the first time the wording moves.
const char* liveAddedLabel();

// What removing them actually costs, in the screen's own words. Fixed, and it
// names no person: the person is the line above it.
const char* liveRevokeConsequence();

// Every FIXED sentence the Live screen can put in its one status line.
//
// Enumerated rather than written at the call sites, because this screen's
// characteristic defect is a sentence that STOPS: the Toybox cuts above
// toybox_10 carry no U+2026, so an overflowing line ends at a plausible-looking
// place and the screenshot looks fine. Three shipped into this screen's first
// three renders. host-tests/wallcaption lays every one of these out in the box
// and the face that will draw it and fails if any comes back cut -- which it
// can only do if they are reachable from a test, and a literal typed inside the
// Activity is not.
//
// A sentence the SERVICE sends is not in here and cannot be: surfacing a
// server's refusal verbatim is a rule, and the device does not get to invent
// its own wording for a decision somebody else made. Those are fitted at draw
// time like any other unbounded string.
enum class LiveStatus : uint8_t {
  AskingForCode,
  // ADD, before the service has answered. A separate sentence from
  // AskingForCode and not a tidier shared one: the two codes mean opposite
  // things (one makes a fridge, one adds a phone to this one) and the wait is
  // the only moment the screen can say which is coming.
  AskingToShare,
  WaitingForPhone,
  Connected,
  Checking,
  NothingNew,
  NewMessage,
  // A revoke that worked. It gets a line because the list refetch that follows
  // it takes a round trip, and on this panel a control that reports nothing and
  // a touch that was dropped look exactly alike.
  Removed,
  Disconnected,
  // STOP, while the reader gets word to the service. It takes a radio join and
  // a request, which is seconds, and on this panel a control that reports
  // nothing and a touch that was dropped look exactly alike. Its own sentence
  // rather than Checking's, because it is not a check: nothing is being asked
  // for and nothing will arrive.
  TellingLiveOff,
  kCount,
};
const char* liveStatusLine(LiveStatus status);

// Returns the square the Activity must draw the QR into, empty when this state
// has none -- which now includes the state where no code has been minted yet,
// so a reader waiting on the service, or one that cannot reach it, cannot put
// a scannable square on the glass that stands for nothing.
fui::Rect buildLive(toybox::Screen& screen, const LiveModel& model);

// ---------------------------------------------------------------------------
// YOUR PHONE: the grid tile's destination, and the screen that names both of
// the things a phone can do with this reader.
//
// The tile says where you are going; this says what you can do there. It is
// the arrangement docs/apps/fridge.md settled on before the tile was built,
// and the half that never got built: with the tile going straight to Live, the
// local upload server was reachable only from the offer screen, which stops
// appearing the moment a reader has any wallpapers at all.
struct PhoneModel {
  // One short line under LIVE saying what it is doing. Composed by the
  // Activity from live::scheduleNote, or phoneLiveIdle() when there is no
  // pairing yet. A pointer into a member, like every other string on these
  // screens: render() runs on the other FreeRTOS task.
  const char* liveState = "";
};
void buildPhone(toybox::Screen& screen, const PhoneModel& model);

// The screen's own strings, for the test that walks it. Functions rather than
// extern constants for the reason liveRemoveMark() is one: a header-declared
// object is a different object per translation unit.
const char* phoneLiveIdle();
const char* phoneSendLabel();
const char* phoneLiveLabel();

}  // namespace wallpapersui

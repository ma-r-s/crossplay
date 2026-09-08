#pragma once

// The xkcd screens. Freestanding builders in the ChessScreens mould: a model
// in, a drawn frame out, no renderer and no Activity, so host-tests/ui/ can
// assert what they drew and what they made tappable.
//
// ---------------------------------------------------------------------------
// **Portrait, 480x800**, like every other app on the device, and the panel
// never rotates: a comic meant to be read sideways is stored already turned.
//
// The artwork arrives fitted to the panel width -- scaled once, on a host,
// with a real resampling filter, so the device blits 1:1 whatever it is
// handed. That is what leaves the page view with **one axis of motion**: down,
// half a screen at a time, snapped to a gap in the artwork.
//
// The 4% of comics that fit-to-width cannot render legible carry a second
// rendition, entered from the Confirm button. It is the only place a
// horizontal axis exists, and it is always exactly two columns. See
// XkcdCore.h for why none of this is decided by measuring the artwork.
//
// The reader is the one screen that is mostly not a screen: the comic is the
// app's own surface and the Activity blits it, exactly as chess draws its
// board. What lives here is the bar under it. `readerViewport()` is shared
// between the two so the rect that gets drawn and the rect that gets tapped
// are the same rect -- the rule that has caught more bugs in this fork than
// any other.
// ---------------------------------------------------------------------------

#include "../ui/ToyboxScreen.h"

namespace xkcdui {

namespace fui = freeink::ui;

// Chess uses 1-4, the link layer owns the 200s, Hacker News the 300s.
enum : fui::ActionId {
  ActionOpenLatest = 400,
  ActionBrowse = 401,
  ActionGoToNumber = 402,
  ActionRandom = 403,
  ActionUpdate = 404,
  ActionOpenComic = 405,
  ActionPageOlder = 406,
  ActionPageNewer = 407,
  // The reader. Up and down are the two halves of the comic itself rather than
  // buttons, so they are registered over the artwork; ALT is a real control in
  // the bar.
  ActionPanUp = 408,
  ActionPanDown = 409,
  ActionShowAlt = 410,
  ActionDismiss = 411,
  ActionPrevComic = 412,
  ActionNextComic = 413,
  // The number pad. One action carrying the digit as its value, plus the two
  // edits, so a keypad is fourteen rects and not fourteen actions.
  ActionDigit = 414,
  ActionBackspace = 415,
  ActionGo = 416,
  // The closer view. Reached from the Confirm button rather than a tap
  // target; see buildReaderBar.
  ActionToggleOverview = 417,

  // First run: fetch the pre-built pack from the CrossPlay release.
  ActionDownloadPack = 418,
};

// --- The front door ------------------------------------------------------

struct MenuModel {
  // The newest comic on the card, which is what the headline offers.
  uint16_t latestNum = 0;
  const char* latestTitle = "";
  int comicCount = 0;
  int readCount = 0;
  // -1 when we have not asked the internet yet, which is the normal state.
  // A number is only ever shown after an update, because a count that claims
  // to be live while being a week stale is worse than no count.
  int waiting = -1;
  bool hasArchive = true;
};

void buildMenu(toybox::Screen& screen, const MenuModel& model);

// The band the mosaic is drawn into, shared with the Activity the way
// Connections shares its grid band. See docs/design-language.md on ornament:
// it has to be made of the app's own material and carry the app's own data.
// Here it is one small rectangle per comic **at that comic's own aspect
// ratio**, filled if you have read it. The material is the thing this app is
// actually about -- the wild variation in shape that makes xkcd hard to put on
// a screen at all -- and the data is your own reading.
fui::Rect menuMosaicBand(const fui::DeviceContext& device);

// --- Browsing ------------------------------------------------------------

struct ListModel {
  const char* title = "XKCD";
  const fui::ListItem* items = nullptr;
  int count = 0;
  int selected = 0;
  // "1200-1209 of 3281", or the search that produced this page.
  const char* rightLabel = nullptr;
  bool canPageOlder = false;
  bool canPageNewer = false;
};

void buildList(toybox::Screen& screen, const ListModel& model);

// The band the rows are laid into, shared with the Activity so its paging
// arithmetic and the drawn rows come from one function rather than two that
// are only ever tested together.
fui::Rect listBand(const fui::DeviceContext& device);

// --- The reader ----------------------------------------------------------

struct ReaderModel {
  uint16_t num = 0;
  const char* title = "";

  // The map, in image pixels: the whole artwork, and the part of it you are
  // looking at. Taken straight from xkcd::place rather than recomputed, so the
  // rectangle drawn in the bar and the pixels actually on screen cannot
  // disagree.
  //
  // This replaced a 0..1000 progress rail. A rail answers "how far through",
  // which is the wrong question for a comic that is wider than the screen as
  // well as taller; the map answers "where am I and how much is there", and it
  // costs the same ink.
  int imageW = 0;
  int imageH = 0;
  int viewX = 0;
  int viewY = 0;
  int viewW = 0;
  int viewH = 0;

  bool hasOverview = false;   // there is a closer view to switch to
  bool showingWhole = false;  // and we are in it
  bool hasAlt = false;
};

// The bar under the comic. The comic itself is not drawn here.
void buildReaderBar(toybox::Screen& screen, const ReaderModel& model);

// The map's rect, so the Activity can put it in the same place the builder
// does rather than guessing at it.
fui::Rect readerMapRect(const fui::DeviceContext& device);

// The rect the comic occupies, and the two halves of it that pan. Shared with
// the Activity: it blits into `readerViewport` and registers the halves, so a
// change to the bar height moves the artwork and the tap targets together.
fui::Rect readerViewport(const fui::DeviceContext& device);
fui::Rect readerPanUpHalf(const fui::DeviceContext& device);
fui::Rect readerPanDownHalf(const fui::DeviceContext& device);

// --- The alt text --------------------------------------------------------

struct AltModel {
  uint16_t num = 0;
  const char* title = "";
  const char* alt = "";
};

// xkcd's alt text is the second half of most jokes and is hidden behind a
// hover on the website, which a touch panel has no equivalent for. It gets a
// screen of its own rather than a tooltip, because there is nowhere to hover
// and because on a short wide comic there is no room to put it under the art
// without the layout jumping between comics.
void buildAlt(toybox::Screen& screen, const AltModel& model);

// --- Notices -------------------------------------------------------------

// --- Going to a number ---------------------------------------------------

struct NumberModel {
  // What has been typed so far, as digits. Empty is a legal state and the
  // screen says so rather than showing a bare zero.
  const char* typed = "";
  uint16_t firstNum = 0;
  uint16_t maxNum = 0;
  // False when the typed number is not a comic on this card, which dims GO
  // rather than letting a tap fail silently.
  bool valid = false;
};

// A number pad, not a text search. Looking a comic up by its number is what
// people actually do with xkcd -- the numbers are how they are referred to --
// and it needs ten keys rather than a keyboard and a substring index.
void buildNumber(toybox::Screen& screen, const NumberModel& model);

// --- Notices -------------------------------------------------------------

struct NoticeModel {
  const char* title = "XKCD";
  const char* headline = "";
  const char* detail = "";
  // Left null when there is nothing to do but leave.
  const char* actionLabel = nullptr;
  fui::ActionId action = fui::NO_ACTION;
};

void buildNotice(toybox::Screen& screen, const NoticeModel& model);

}  // namespace xkcdui

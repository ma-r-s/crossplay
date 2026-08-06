#pragma once

// The xkcd screens. Freestanding builders in the ChessScreens mould: a model
// in, a drawn frame out, no renderer and no Activity, so host-tests/ui/ can
// assert what they drew and what they made tappable.
//
// ---------------------------------------------------------------------------
// Everything here is **landscape, 800x480**. XkcdCore.h has the measurements,
// but the short version is that the widest comic ever published is 780px, so
// landscape is the one orientation where nothing is ever scaled down. On a
// 1-bit panel, scaling hand lettering is where legibility dies.
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
  ActionSearch = 402,
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

// The header band this app uses. Slimmer than the portrait screens' 76,
// because in landscape that would cost a sixth of the height a comic needs.
// Named here rather than assumed twice: the builder draws the rule under it
// and the Activity overrides the theme with it, and when those two disagreed
// the rule floated twenty pixels below the band.
inline constexpr int16_t kHeaderBand = 56;

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
  // 0..1000, from xkcd::scrollPermille. Drawn as a rail rather than as
  // "2 of 3": a step count would have to walk the whole comic through the snap
  // rule to be honest, and any cheaper formula would be a second
  // implementation of the step that disagrees the first time a gap is found.
  int permille = 1000;
  bool pans = false;
  bool hasAlt = false;
  bool canPrev = false;
  bool canNext = false;
};

// The bar under the comic. The comic itself is not drawn here.
void buildReaderBar(toybox::Screen& screen, const ReaderModel& model);

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

#pragma once

// The RSS screens. Freestanding builders in the ChessScreens mould: a model in,
// a drawn frame out, no renderer and no Activity, so host-tests/ui/ can assert
// what they drew and what they made tappable.
//
// ---------------------------------------------------------------------------
// Four screens of its own, and two it borrows.
//
//   posts   every post, newest first, or one feed's -- Hacker News's front page
//           with the site's name where the comment count was, and a dot on
//           every post not yet opened
//   feeds   the sites, each with how many of its posts are unread
//   saved   the posts kept on the card (the posts screen, built from the shelf)
//   phone   the QR code a phone scans to add and remove feeds
//
// A post is read in Hacker News's reader (hnui::buildReader, with no second
// view), and every wait and every refusal is Hacker News's notice
// (hnui::buildNotice) under this app's own band. One reader in the fork for
// somebody else's prose, and one way of saying "not here".
//
// The three lists share a strip of segments at the foot -- ALL, FEEDS, SAVED
// -- filled where you are, outlined where you can go, the same pair Hacker News
// draws for its two.
// ---------------------------------------------------------------------------

#include <cstdint>

#include "../ui/ToyboxScreen.h"

namespace rssui {

namespace fui = freeink::ui;

// Hacker News owns the 300s up to 312 (its reader and notice, which this app
// draws through); these sit above them.
enum : fui::ActionId {
  ActionOpenPost = 340,  // value: the row's index in the list being shown
  ActionShowAll = 341,
  ActionShowFeeds = 342,
  ActionShowSaved = 343,
  ActionOpenFeed = 344,  // value: the feed's index
  ActionAddFeeds = 345,  // the phone screen
  ActionPhoneDone = 346,
  ActionStopRefresh = 347,
};

enum class Tab : uint8_t { All, Feeds, Saved };

// --- The lists -----------------------------------------------------------

struct ListModel {
  // The band: the app's name, a feed's name when narrowed to one, FEEDS, SAVED.
  const char* title = "RSS";
  Tab tab = Tab::All;
  // A posts list narrowed to one feed. FEEDS stays filled -- you are among the
  // feeds -- and is the one way back to the list of them, so it stays live.
  bool narrowed = false;
  // A window of rows: items[0] is row `windowFirst` of `count`. The activity
  // builds only the rows a page shows, because a list of thirty feeds is six
  // hundred posts and fitting six hundred headlines to fill one page is waste.
  const fui::ListItem* items = nullptr;
  int count = 0;
  int windowFirst = 0;
  int windowCount = 0;
  int topIndex = 0;
  // The row last opened, drawn inverted the way Hacker News marks it, or -1.
  int selected = -1;
  // Drawn instead of rows when there are none. The control is drawn only when
  // both its label and its action are set.
  const char* emptyHeadline = nullptr;
  const char* emptyMessage = nullptr;
  const char* emptyActionLabel = nullptr;
  fui::ActionId emptyAction = fui::NO_ACTION;
};

// The posts of every feed, of one feed, or of the saved shelf. Rows open with
// ActionOpenPost and their absolute index.
void buildPosts(toybox::Screen& screen, const ListModel& model);

// The feeds, with a way to the phone above the segments. Rows open with
// ActionOpenFeed and their index.
void buildFeeds(toybox::Screen& screen, const ListModel& model);

// The bands the two lists draw rows into. Exported so the activity pages by
// the rows that really fit rather than by a second guess at them.
fui::Rect postsBand(const fui::DeviceContext& device);
fui::Rect feedsBand(const fui::DeviceContext& device);

// A post row: two lines of headline, the same height Hacker News gives a story.
int16_t postRowHeight(const fui::DrawTarget& target, const fui::ThemeTokens& tokens);
// A feed row: one line of name.
int16_t feedRowHeight(const fui::DrawTarget& target, const fui::ThemeTokens& tokens);

// The width a post's headline is drawn into, after the unread mark and the
// site's name, so the activity can fit it to the space the component gives it.
int16_t postTitleWidth(const fui::DrawTarget& target, const fui::DeviceContext& device,
                       const fui::ThemeTokens& tokens);
// And the room the site's name gets, at the right of the row.
int16_t postSiteWidth();
int16_t feedTitleWidth(const fui::DrawTarget& target, const fui::DeviceContext& device,
                       const fui::ThemeTokens& tokens);
// The style a post's site and a feed's count are drawn in.
fui::TextStyle footnoteStyle(const fui::ThemeTokens& tokens);

// The mark in front of a post not yet opened, and the blank one in front of a
// post that has been: the same size, so every headline starts at one x.
fui::BitmapRef unreadMark();
fui::BitmapRef readMark();

// What an empty list says, and whether it offers a way to fill itself.
struct Empty {
  const char* headline = nullptr;
  const char* message = nullptr;
  const char* actionLabel = nullptr;
  fui::ActionId action = fui::NO_ACTION;
};
// `haveFeeds`: any feed at all is subscribed. `oneFeed`: the list is narrowed
// to a single feed.
Empty emptyFor(Tab tab, bool haveFeeds, bool oneFeed);

// --- The phone -----------------------------------------------------------

struct PhoneModel {
  // What the QR carries: the device's own address, generated from the live IP
  // at the moment of drawing.
  const char* url = "";
  // What a person reads and can type: the mDNS name when the responder
  // started, the dotted address when it did not.
  const char* readable = "";
  // One line saying what the phone has done, or what the reader is doing for
  // it: "3 FEEDS", "ADDING ...", "ADDED ...", "NO FEED THERE".
  const char* status = "";
};

// Returns the square the caller draws the code into: QrUtils needs a renderer,
// which this layer does not have.
fui::Rect buildPhone(toybox::Screen& screen, const PhoneModel& model);

}  // namespace rssui

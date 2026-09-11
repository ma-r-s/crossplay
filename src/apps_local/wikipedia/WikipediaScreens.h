#pragma once

// WIKIPEDIA on screen. Freestanding builders over plain models, host-testable
// through host-tests/ui. The article page itself is not drawn here: the book
// engine renders it into the body rect buildArticleChrome() returns.
//
// Three screens carry the app: SEARCH (the home: a field, the matches under
// it, the keyboard the activity draws below), the ARTICLE chrome (title band
// with CONTENTS, a footer with the page and section), and INSTALL (the address,
// a square left for the QR code, and the cable's status). CONTENTS is an
// overlay list; NOTICE is one sentence and a way out.

#include "../ui/ToyboxScreen.h"

namespace wikiui {

namespace fui = freeink::ui;

enum : fui::ActionId {
  ActionResult = 1,    // a match under the field; VALUE is the row
  ActionRecent = 2,    // a recent article; VALUE is the row
  ActionContinue = 3,  // the article you were in
  ActionRandom = 4,
  ActionContents = 5,  // the header's CONTENTS button
  ActionHeading = 6,   // a row of the contents list; VALUE is the heading index
  ActionClose = 7,     // close the contents list
  ActionKey = 8,       // a keyboard key; VALUE is the key value
  ActionInstall = 9,   // the search screen's "get Wikipedia" row
  ActionRetry = 10,
  ActionBack = 11,
  ActionClear = 12,     // clear the query
  ActionField = 13,     // the field itself: raises the keyboard where it is hidden
  ActionMore = 14,      // the contents list's next window
  ActionPrevious = 15,  // the article band's way back: the previous article, or the search
};

constexpr int kMaxResults = 8;
constexpr int kMaxRecent = 10;
// The contents list draws as many rows as fit; this is the step the activity
// pages by before a first render has told it how many that was.
constexpr int kContentsRows = 12;

struct Row {
  const char* title = "";
  bool redirect = false;
};

struct SearchModel {
  const char* query = "";
  Row results[kMaxResults];
  int resultCount = 0;
  bool noMatch = false;      // a non-empty query with nothing under it
  bool moreResults = false;  // more matches than the panel's worth: keep typing
  const char* continueTitle = nullptr;
  int continuePage = 0;  // 1-based page CONTINUE lands on; 0 when unknown
  Row recent[kMaxRecent];
  int recentCount = 0;
  const char* footer = "";  // "7,238,251 articles, May 2026"
  // A pack that is only partly on the card: "3 of 46 parts on the card", with
  // the row that goes back to the install screen. Null when complete.
  const char* partsLine = nullptr;
  // Height the activity reserves at the bottom for the keyboard; 0 when it is
  // not showing.
  int16_t keyboardHeight = 0;
};

void buildSearch(toybox::Screen& screen, const SearchModel& model);

struct ArticleChromeModel {
  const char* title = "";
  bool contents = true;
  bool back = true;  // the band's leading chevron
};

// Draws the band; returns the rect the page is rendered into, above the footer.
fui::Rect buildArticleChrome(toybox::Screen& screen, const ArticleChromeModel& model);

struct ArticleFooterModel {
  const char* left = "";   // "page 12" while building, "12 of 87" once complete
  const char* right = "";  // the section the page is in
  int page = 0;            // 1-based, for a progress rule
  int total = 0;           // 0 until the layout is complete
};

// Drawn after the layout has settled which page is shown, under the page rect.
void buildArticleFooter(toybox::Screen& screen, const ArticleFooterModel& model);

struct ContentsModel {
  const char* title = "";
  const char* const* headings = nullptr;
  const int* pages = nullptr;  // 0-based page of each row, -1 while the layout has not reached it
  int count = 0;
  int current = -1;  // the heading the page is in, marked
  int first = 0;     // window start
};

// Rows are as tall as their heading needs, so a window holds however many fit;
// returns that count, which is what the next window starts after.
int buildContents(toybox::Screen& screen, const ContentsModel& model);

struct InstallModel {
  enum class Stage : uint8_t { Waiting, Connected, Failed };
  Stage stage = Stage::Waiting;
  const char* url = "";
  // "3 of 46 parts on the card" when a pack is already there; null on first install.
  const char* partsLine = nullptr;
};

// Draws everything but the QR code; returns the square left for it.
fui::Rect buildInstall(toybox::Screen& screen, const InstallModel& model);

struct NoticeModel {
  const char* headline = "";
  const char* body = "";
  const char* actionLabel = nullptr;
  fui::ActionId action = 0;
};

void buildNotice(toybox::Screen& screen, const NoticeModel& model);

}  // namespace wikiui

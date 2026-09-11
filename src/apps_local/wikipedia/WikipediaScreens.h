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
  ActionClear = 12,  // clear the query
};

constexpr int kMaxResults = 8;
constexpr int kMaxRecent = 10;
// Rows of the contents list drawn at once; the model says where the window starts.
constexpr int kContentsRows = 12;

struct Row {
  const char* title = "";
  bool redirect = false;
};

struct SearchModel {
  const char* query = "";
  Row results[kMaxResults];
  int resultCount = 0;
  bool noMatch = false;  // a non-empty query with nothing under it
  const char* continueTitle = nullptr;
  Row recent[kMaxRecent];
  int recentCount = 0;
  const char* footer = "";  // "7,238,251 articles, May 2026"
  // A pack that is only partly on the card: "3 of 46 parts on the card", with
  // the row that goes back to the install screen. Null when complete.
  const char* partsLine = nullptr;
  // Height the activity needs at the bottom for the keyboard.
  int16_t keyboardHeight = 0;
};

void buildSearch(toybox::Screen& screen, const SearchModel& model);

struct ArticleChromeModel {
  const char* title = "";
  const char* footerLeft = "";   // "12" while building, "12 of 87" once complete
  const char* footerRight = "";  // the section the page is in
  bool contents = true;
};

// Draws the band and the footer; returns the rect the page is rendered into.
fui::Rect buildArticleChrome(toybox::Screen& screen, const ArticleChromeModel& model);

struct ContentsModel {
  const char* title = "";
  const char* const* headings = nullptr;
  int count = 0;
  int current = -1;  // the heading the page is in, marked
  int first = 0;     // window start
};

void buildContents(toybox::Screen& screen, const ContentsModel& model);

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

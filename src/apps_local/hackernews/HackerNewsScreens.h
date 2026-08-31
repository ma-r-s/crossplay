#pragma once

// The Hacker News screens. Freestanding builders in the ChessScreens mould: a
// model in, a drawn frame out, no renderer and no Activity, so host-tests/ui/
// can assert what they drew and what they made tappable.
//
// ---------------------------------------------------------------------------
// Three screens, and why the reader is one of them rather than two.
//
// An article and a comment thread are the same object here: a long piece of
// text, paged. They differ in where the words came from, which is the
// Activity's problem, and in nothing the screen can see. Giving them one
// builder means the page indicator, the footer and the paging arithmetic cannot
// drift between them, and a reader who has learned one has learned both.
//
// The mark for "this cannot be read here" lives on the notice screen and
// nowhere else. Putting it on a story row was the first design and it was
// wrong: the list only knows the URL, and the URL is a guess. A story whose
// link *looks* like an article but extracts to nothing would carry no mark in
// the list and then refuse to open, which teaches the reader that the mark
// means nothing. One verdict, shown at the moment it is actually known.
// ---------------------------------------------------------------------------

#include <string>

#include "../ui/ToyboxScreen.h"

namespace hnui {

namespace fui = freeink::ui;

// Chess uses 1-4 and the link layer owns the 200s; these stay in the 300s.
enum : fui::ActionId {
  ActionOpenStory = 300,
  ActionPagePrev = 301,
  ActionPageNext = 302,
  // The reader's middle footer button, which swaps between the article and the
  // comments. One action rather than two, because the model already knows which
  // way it is pointing and two would let the label and the effect disagree.
  ActionSwapView = 303,
  ActionNotice = 304,
  // The list's two segments. Two actions rather than one toggle: a segment
  // names a destination absolutely, so tapping the one you are already in does
  // nothing, and neither can disagree with what is on screen.
  ActionShowFrontPage = 306,
  ActionShowSaved = 307,
  // The reader band's save mark, which toggles. Two actions for the same
  // reason: each names an absolute intent, so neither can disagree with what
  // the mark is currently showing.
  ActionSave = 308,
  ActionUnsave = 309,
};

// --- The front page ------------------------------------------------------

struct ListModel {
  const char* title = "HACKER NEWS";
  // Which half of the library is on screen. Same filled-means-here language as
  // the reader's mark, so there is one idea to learn rather than two.
  bool showingSaved = false;
  // Drawn instead of rows when the list is empty. An empty SAVED shelf on a new
  // device is the normal case, and a blank panel reads as a fault.
  const char* emptyHeadline = nullptr;
  const char* emptyMessage = nullptr;
  // Built by the Activity, the way shelfui::MenuModel carries its rows: label
  // is the story, subtitle is "412 points, 88 comments", which is the only
  // metadata worth the ink.
  const fui::ListItem* items = nullptr;
  int count = 0;
  int selected = 0;
  int topIndex = 0;
};

void buildList(toybox::Screen& screen, const ListModel& model);

// The band the list draws into. Shared with the Activity so its scroll maths
// and the drawn rows come from one function rather than two that are only ever
// wrong together.
fui::Rect listBand(const fui::DeviceContext& device);

// How tall a story row is, measured from the fonts that will draw it.
//
// Exported for the same reason as the band, and it is not hypothetical: the
// theme's row height is sized for a one-line row, and the first render of this
// list clipped every "412 points, 88 comments" in half against the row below.
// Deriving it from the real line heights is what stops a font change breaking
// it again, and sharing it with the Activity is what stops the list scrolling
// by a different number of rows than it draws.
int16_t listRowHeight(const fui::DrawTarget& target, const fui::ThemeTokens& tokens);

// The width a story headline is actually drawn into, and the style its count
// footnote uses. Exported so the Activity can fit a title to the same space the
// component will give it rather than to a second guess at that space.
int16_t listTitleWidth(const fui::DrawTarget& target, const fui::DeviceContext& device, const fui::ThemeTokens& tokens);
fui::TextStyle listCountStyle(const fui::ThemeTokens& tokens);

// `text` cut to at most `lines` lines of `width`, breaking only between words
// and ending in an ellipsis when anything was dropped.
//
// This exists because the component clips: a headline too long for its rows is
// simply chopped, so the front page read "Show HN: Simple algorithm and colo"
// and "I am retiring from fulltime writing (". A word broken in half looks like
// a rendering fault, which is the one thing Mario rejected on sight when the
// same question came up for Connections tiles. Shrink to fit, break on a space,
// and say that something was dropped.
std::string fitLines(const fui::DrawTarget& target, const char* text, int16_t width, int lines,
                     const fui::TextStyle& style);

// --- The reader, for both an article and a thread ------------------------

struct ReaderModel {
  const char* title = "";
  // The whole document, NUL-terminated and contiguous, as textArea wants it.
  const char* text = "";
  uint32_t topLine = 0;
  // "3 / 12". Built by the Activity because only it knows the line count.
  const char* pageLabel = "";
  // What the middle footer button offers next. Showing the article means the
  // button says COMMENTS, and the other way round.
  bool showingComments = false;
  // A story with no comments yet, or an unreadable link with no article: the
  // button dims rather than disappearing, so the footer keeps its shape.
  bool swapAvailable = true;
  bool canPagePrev = false;
  bool canPageNext = false;
  // The band's save mark: absent on a thread (there is nothing to save but the
  // article), outlined when it can be saved, filled once it is on the device --
  // and tapping a filled one removes it again.
  bool canSave = false;
  bool saved = false;
};

void buildReader(toybox::Screen& screen, const ReaderModel& model);

// Where the reader's text goes. Exported for the same reason as listBand():
// the Activity pages by counting the lines that fit in this exact rect, and a
// second copy of the geometry is how a page turn starts skipping a line.
fui::Rect readerBody(const fui::DeviceContext& device);

// --- Loading, failure, and "not readable here" ---------------------------

struct NoticeModel {
  const char* headline = "";
  const char* message = "";
  // Drawn above the headline when set. The unreadable mark, and only that.
  const freeink::Icon* mark = nullptr;
  // nullptr draws no button, which is what a loading notice wants: there is
  // nothing to decide yet and Back already works.
  const char* actionLabel = nullptr;
};

void buildNotice(toybox::Screen& screen, const NoticeModel& model);

}  // namespace hnui

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
#include <vector>

#include "../ui/ToyboxScreen.h"

namespace hnui {

namespace fui = freeink::ui;

// Chess uses 1-4 and the link layer owns the 200s; these stay in the 300s.
enum : fui::ActionId {
  ActionOpenStory = 300,
  ActionPagePrev = 301,
  ActionPageNext = 302,
  // The footer's two segments. Two actions rather than one toggle: a segment
  // names a destination absolutely, so tapping the one you are already in is
  // simply nothing, and neither can ever disagree with what is on screen.
  ActionShowArticle = 303,
  ActionShowComments = 305,
  ActionNotice = 304,
  // The list's own two segments, the same shape as the reader's footer.
  ActionShowFrontPage = 306,
  ActionShowSaved = 307,
  // The reader's band mark, which toggles. Two actions rather than one, for the
  // same reason the segments are two: each names an absolute intent, so neither
  // can disagree with what the mark is currently showing.
  ActionSave = 308,
  ActionUnsave = 309,
  ActionLoadFrontPage = 311,
};

// --- The front page ------------------------------------------------------

struct ListModel {
  const char* title = "HACKER NEWS";
  // Which half of the library is on screen. Same filled-means-here language as
  // the reader footer, so there is one idea to learn rather than two.
  bool showingSaved = false;
  // Drawn instead of rows when the list is empty, because an empty SAVED shelf
  // on a new device is the normal case and a blank panel reads as a fault.
  const char* emptyHeadline = nullptr;
  const char* emptyMessage = nullptr;
  // What tapping the empty state does. The headline is the hit target, so the
  // whole body is the control and there is no button to miss.
  fui::ActionId emptyAction = fui::NO_ACTION;
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
//
// Threaded comments are the reason this stopped being a textArea. A reply has
// to look attached to what it answers, and depth expressed as indentation alone
// does not survive wrapping: only a paragraph's first line is indented, so the
// rest runs back to the margin and the structure reads as a typo. Reddit solves
// it with a vertical rule per ancestor, and a rule is something a page of text
// cannot draw for itself. So the reader lays its own lines out and paints them,
// which the design language allows for exactly this: chrome is a component, an
// app's own surface is drawn by hand.
//
// Lines are pre-wrapped by the Activity, which has a draw target and can
// therefore measure. Text and metadata travel as parallel arrays because a
// ListItem-style struct of pointers into a growing vector dangles the moment it
// reallocates -- the same two-pass rule the story rows follow.

struct ReaderLine {
  int16_t depth = 0;      // nesting level; 0 for an article and for a top comment
  bool isAuthor = false;  // the line naming who is speaking
};

// How far one level of nesting moves a comment across, and the rule drawn in
// the space it leaves. Narrow on purpose: five levels of a wide indent would
// leave a 480px panel with nothing to set text in.
constexpr int16_t kThreadIndent = 16;
constexpr int16_t kThreadRule = 2;

struct ReaderModel {
  // The story, in the band. Not "ARTICLE" or "COMMENTS": which of those you are
  // reading is what the footer says, and a title is more use than a category.
  const char* title = "";
  // The cut the title was fitted at, which is not always the biggest one, and
  // how many lines it was given. A smaller cut buys a second line: two lines of
  // the 12px cut fit the 76px band where two of the 16px cut overflowed it.
  fui::FontId titleFont = 0;
  int titleLines = 1;
  const char* const* lineText = nullptr;
  const ReaderLine* lineMeta = nullptr;
  int lineCount = 0;
  uint32_t topLine = 0;
  const char* pageLabel = "";
  bool showingComments = false;
  // Whether each half of the footer can be chosen at all: an unreadable link has
  // no article, a new story has no thread.
  bool articleAvailable = true;
  bool commentsAvailable = true;
  bool canPagePrev = false;
  bool canPageNext = false;
  // The band's save mark: absent on a thread (there is nothing to save but the
  // article), outlined when it can be saved, filled once it is on the device --
  // and tapping a filled one removes it again.
  bool canSave = false;
  bool saved = false;
};

void buildReader(toybox::Screen& screen, const ReaderModel& model);

// Where the reader's text goes, and how many lines of it fit. Exported for the
// same reason as listBand(): the Activity pages by counting the lines that fit
// in this exact rect, and a second copy of the geometry is how a page turn
// starts skipping a line.
fui::Rect readerBody(const fui::DeviceContext& device);
uint16_t readerVisibleLines(const fui::DrawTarget& target, const fui::DeviceContext& device,
                            const fui::ThemeTokens& tokens);

// The width the band gives a story title, once its page label is reserved.
int16_t readerTitleWidth(const fui::DrawTarget& target, const fui::DeviceContext& device,
                         const fui::ThemeTokens& tokens, bool withSaveMark, const char* pageLabel);

// Append `paragraph`, wrapped to the width a comment at `depth` actually gets,
// as one entry per drawn line. Wrapping goes through the SDK's own walker, so a
// line laid out here is a line the renderer will draw identically.
void appendWrapped(const fui::DrawTarget& target, const fui::DeviceContext& device, const fui::ThemeTokens& tokens,
                   const char* paragraph, int depth, bool isAuthor, std::vector<std::string>& text,
                   std::vector<ReaderLine>& meta);

// --- Loading, failure, and "not readable here" ---------------------------

struct NoticeModel {
  // The loudest thing on the screen, in the display cut. On a notice about a
  // story that is the story, because that is the content; what went wrong is
  // the state line under it. Same order as the Connections front door, where
  // the date is the headline and NOT STARTED is the state.
  const char* headline = "";
  // One quiet line under the headline: what this screen is about.
  const char* state = "";
  const char* message = "";
  // Drawn above the headline when set. The unreadable mark, and only that.
  const freeink::Icon* mark = nullptr;
  // nullptr draws no button, which is what a loading notice wants: there is
  // nothing to decide yet and Back already works.
  const char* actionLabel = nullptr;
};

void buildNotice(toybox::Screen& screen, const NoticeModel& model);

}  // namespace hnui

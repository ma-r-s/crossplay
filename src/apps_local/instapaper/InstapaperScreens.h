#pragma once

// The read-later screens. Freestanding builders in the HackerNewsScreens
// mould: a model in, a drawn frame out, no renderer and no Activity, so
// host-tests/ui/ can assert what they drew and what they made tappable.
//
// ---------------------------------------------------------------------------
// Five screens, and two decisions worth stating.
//
// There is no stage ladder. Study's sync draws a four-stage band because a
// first Anki sync moves gigabytes and takes minutes, and a wait that long
// needs to say which part of it you are in. This one moves a few hundred
// kilobytes and takes seconds, so it gets a caption and then a verdict.
// Copying the ladder would have bought a second copy of a component to keep in
// step, in exchange for animating a wait that is not there.
//
// And the reader is a textArea rather than the Hacker News reader's own
// surface. That surface exists for threaded comments -- one vertical rule per
// ancestor beside every line -- and an article has no thread. Where the panel
// is just laying out prose, the SDK's component already does it and does it
// the same way everywhere else in the firmware.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <string>

#include "../ui/ToyboxScreen.h"
#include "../ui/ToyboxWrappedText.h"

namespace instapaperui {

namespace fui = freeink::ui;

// Chess uses 1-4, the link layer owns the 200s, Hacker News the 300s. These
// sit in the 320s, which is inside that block on purpose: the two apps never
// share a screen, and one range per family is easier to keep track of than one
// per app.
enum : fui::ActionId {
  ActionOpenArticle = 320,
  ActionSync = 321,
  ActionPagePrev = 322,
  ActionPageNext = 323,
  ActionArchive = 324,
  ActionNotice = 325,
  ActionPairConfirm = 326,
  ActionUndoArchive = 327,
};

// --- The queue -----------------------------------------------------------

struct QueueModel {
  // Built by the Activity, the way shelfui::MenuModel carries its rows: label
  // is the title, subtitle is "6 min . example.com", value is the reading
  // position when there is one.
  const fui::ListItem* items = nullptr;
  int count = 0;
  int selected = 0;
  int topIndex = 0;
  // Drawn on the band, right-aligned: "SYNCED 14:32" or "NEVER SYNCED". The
  // one fact a reader wants before deciding whether to pull again.
  const char* lastSync = "";
  // An archive is on the card and has not gone up yet, so it can still be
  // taken back. The footer splits to offer it; see buildQueue.
  bool canUndoArchive = false;
};

void buildQueue(toybox::Screen& screen, const QueueModel& model);

// The band the list draws into, and the height of a row, both shared with the
// Activity so its scroll arithmetic and the drawn rows come from one function
// rather than two that are only ever wrong together.
fui::Rect queueBand(const fui::DeviceContext& device);
int16_t queueRowHeight(const fui::DrawTarget& target, const fui::ThemeTokens& tokens);

// The width a title is actually drawn into, so the Activity can fit it to the
// same space the component will give it rather than to a second guess at it.
int16_t queueTitleWidth(const fui::DrawTarget& target, const fui::DeviceContext& device,
                        const fui::ThemeTokens& tokens);

// --- The reader ----------------------------------------------------------

// The reader's body: the words, the cut they are set in, and the wrap that
// counts AND draws them. One object rather than three arguments, because two
// arguments that must agree are two arguments that can disagree -- and a style
// handed to the counting but not to the drawing makes the two fingerprints
// differ, so the article is re-wrapped twice on every paint. That is exactly
// the bug this mechanism exists to remove, restored silently, and no test in
// this fork could catch it: a fake draw target answers every font the same.
struct ReaderBody {
  const char* text = "";
  fui::TextStyle style{};
  toybox::WrappedText* wrap = nullptr;
};

struct ReaderModel {
  const char* title = "";
  uint32_t topLine = 0;
  const char* pageLabel = "";  // "3 / 12", built by the Activity
  bool canPagePrev = false;
  bool canPageNext = false;
};

// The body is passed rather than kept, and by reference, so a caller cannot
// build this screen without one. The alternative was a nullable pointer with a
// fall-back to wrapping the whole article again, which would have brought the
// bug back the first time somebody wrote a new call site.
void buildReader(toybox::Screen& screen, const ReaderModel& model, ReaderBody& body);

// The article's length in lines, wrapped to the width the reader will really
// draw it at. The Activity needs it before it can say which page it is on and
// what reading position to send back to Instapaper. Same object, same rect and
// same style as the drawing, so the two cannot disagree about where a line
// ends -- which is the whole reason readerBody() is exported.
uint32_t readerLineCount(const fui::DrawTarget& target, const fui::DeviceContext& device, ReaderBody& body);

// Where the reader's text goes. Exported for the same reason as queueBand():
// the Activity pages by counting the lines that fit in this exact rect, and a
// second copy of the geometry is how a page turn starts skipping a line.
fui::Rect readerBody(const fui::DeviceContext& device);

// --- Notices: loading, failure, an empty queue, an unreadable article -----

struct NoticeModel {
  const char* headline = "";
  const char* message = "";
  const freeink::Icon* mark = nullptr;
  // nullptr draws no button, which is what a loading notice wants: there is
  // nothing to decide yet and Back already works.
  const char* actionLabel = nullptr;
};

void buildNotice(toybox::Screen& screen, const NoticeModel& model);

// --- Pairing -------------------------------------------------------------

// The QR's rect, so the Activity can draw the code into it with the encoder.
fui::Rect buildPairQr(toybox::Screen& screen, const char* code);

// The confirm screen's two rects: where the account name goes, and the pill
// that accepts. Returned rather than drawn because the name is fitted by the
// Activity and the pill has to be registered as a hit target by whoever knows
// the action.
struct PairConfirmLayout {
  fui::Rect username{};
  fui::Rect pill{};
};
PairConfirmLayout buildPairConfirm(toybox::Screen& screen);

}  // namespace instapaperui

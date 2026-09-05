#pragma once

// The study app's chrome, as free functions over plain models.
//
// Nothing here touches the renderer, storage or the Activity, so
// host-tests/ui can build these against a fake draw target and assert on what
// they drew and what they made tappable. See docs/shelf.md for why that split
// is worth keeping, and ToyboxScreen.h for why everything goes through
// `toybox::Screen` rather than calling FreeInkUI components directly.
//
// The card face is deliberately NOT here: it is the app's own surface, drawn by
// hand into the body rect, in the same sense that a chess board is.

#include "../ui/ToyboxScreen.h"

namespace fui = freeink::ui;

namespace studyui {

// Actions the deck screen can produce.
inline constexpr fui::ActionId ActionStudy = 1;
inline constexpr fui::ActionId ActionForecast = 2;
inline constexpr fui::ActionId ActionSync = 4;
// The deck picker: one action for the rows (value = deck index) and one for
// the confirm door.
inline constexpr fui::ActionId ActionPickDeck = 5;
inline constexpr fui::ActionId ActionPickDone = 6;
// The verdict screen's door: 1 = sync again, 2 = close and go back.
inline constexpr fui::ActionId ActionSyncVerdict = 7;

// How many days either side of today the ornament shows. Two weeks back is far
// enough to see a habit, two weeks forward far enough to see a backlog coming,
// and twenty-seven bars is as many as read as separate at this width.
inline constexpr int kForecastDays = 14;
inline constexpr int kHistoryDays = 14;

struct DeckModel {
  const char* name = "";
  int due = 0;       // cards waiting today
  int fresh = 0;     // new cards available today
  int reviewed = 0;  // answered in this session
  int recalled = 0;  // of those, not Again
  int total = 0;     // cards in the deck
  // Cards falling due on each of the next kForecastDays days, today first.
  // This is the ornament's data: it comes from the same pass over cards.dat
  // that builds the queue, so it costs nothing extra.
  const int* forecast = nullptr;
  // What actually happened, from revlog.dat: index 0 is today, and there are
  // kHistoryDays of it. Drawn beside the forecast as one timeline, because
  // "what I have been doing" and "what is coming" are the same question asked
  // in two directions.
  //
  // Passed as plain numbers rather than as the study::Stats that produced them,
  // so this header stays free of the deck layer. docs/shelf.md is explicit that
  // a screen knows FreeInkUI and Toybox tokens and nothing else -- that is what
  // keeps host-tests/ui able to build it with no src/ on the include path.
  const int* history = nullptr;
  int streak = 0;
  int retention = -1;  // percent, or -1 when there is nothing to average
  int lifetimeReviews = 0;
  bool sessionOver = false;  // nothing left to answer right now
  bool writeFailed = false;
  // No wall clock yet (a flat battery clears the RTC; a sync sets it). Every
  // review answered in this state is stamped near the epoch and would be
  // replayed into the real Anki collection as the card's last review, so the
  // app refuses to log one -- and therefore must refuse to ask for one.
  bool clockUnset = false;
  // Deck folders on the card past the number this reader can open. Their
  // reviews cannot be sent, so silence here reads as "nothing outstanding".
  int decksOverCap = 0;
  // Which of the card's decks this is. With one deck the row is a label; with
  // more it is the switcher, and tapping it cycles. Cycling rather than a list
  // because a card realistically holds two or three decks, and a dedicated
  // screen for choosing among three things is a screen too many.
  int deckIndex = 0;
  int deckCount = 1;
  // The SYNC door's subtitle: LAST SYNC HH:MM, PAIRED, or NOT PAIRED YET.
  char syncSubtitle[40] = "";
  // Paired readers get a fourth door: which Anki decks this reader carries is
  // otherwise answerable only once, during the first sync.
  bool paired = false;
  // Cards waiting in the reader's other decks, so the headline cannot say
  // ALL CLEAR while work sits one tap away.
  int otherWaiting = 0;
};

void buildDeck(toybox::Screen& screen, const DeckModel& model);

// ---- The sync flow surface (docs/apps/study-syncflow-ui.md).
//
// One screen, two faces: the stage ladder while the flow runs, the verdict
// when it ends. The flow fills this model at each transition instead of
// passing two strings; the safety flag is a design input because any
// post-ack failure must LEAD with what is already safe.

enum class SyncStage : uint8_t { Connect = 0, Send, Build, Download };
inline constexpr int kSyncStageCount = 4;

enum class SyncStageState : uint8_t { Pending, Active, Done };

enum class SyncSafety : uint8_t {
  None,                     // busy, or pre-ack terminal where "nothing was sent" is the body
  NothingSent,              // terminal before the ack: the card is exactly as it was
  ReviewsSafe,              // the ack landed: reviews are durable on the bridge
  ReviewsSafePartialDecks,  // safe, and some decks already updated on card
};

enum class SyncVerdictKind : uint8_t { None, Success, Error, Neutral };

struct SyncFlowModel {
  // Busy face (verdict == None).
  SyncStageState stages[kSyncStageCount] = {SyncStageState::Pending, SyncStageState::Pending, SyncStageState::Pending,
                                            SyncStageState::Pending};
  // Wide enough for the widest fact written into it, which is the build
  // clock: "%um%02us" over a uint32 second count. StudyActivity
  // static_asserts its own buffer against this rather than trusting it.
  static constexpr int kFactChars = 24;
  char facts[kSyncStageCount][kFactChars] = {"", "", "", ""};  // per-stage fact, right slot
  char caption[120] = "";                                      // under the active stage
  // Terminal face.
  SyncVerdictKind verdict = SyncVerdictKind::None;
  char title[24] = "";
  char body[192] = "";
  char whatNow[96] = "";
  char factLines[3][40] = {"", "", ""};
  int factCount = 0;
  SyncSafety safety = SyncSafety::None;
  // Whether leaving is safe RIGHT NOW: true from the moment the bridge owns
  // the job, which is before any reviews exist to be safe. A first sync sends
  // nothing, so keying the footer on `safety` hid it from the longest wait
  // any user ever does.
  bool leaveSafe = false;
};

void buildSyncFlow(toybox::Screen& screen, const SyncFlowModel& model);

// ---- Choosing which of the account's decks this reader carries.
//
// A fresh account has chosen nothing, so without this screen a first sync
// delivers an empty manifest and the reader stays empty forever. The bridge
// caps a sync at kMaxChosenDecks decks; the screen enforces the same cap so the
// refusal happens where the user can see it rather than server-side.
inline constexpr int kMaxPickerDecks = 24;

struct DeckPickerModel {
  struct Row {
    const char* name = "";
    int cards = 0;
    bool chosen = false;
  };
  const Row* rows = nullptr;
  int count = 0;
  int topIndex = 0;     // first row drawn; the list pages rather than scrolls
  int visibleRows = 0;  // filled in by the builder, so paging can match
  int chosenCount = 0;
  int maxChosen = 8;   // mirrors StudySync::kMaxChosenDecks
  bool atCap = false;  // drawn as a caption, so the cap explains itself
  // True when the account holds more decks than the list could carry. Drawn,
  // because "my deck is not in this list" and "my deck is on a page that was
  // never sent" look identical from the chair.
  bool withheld = false;
};

void buildDeckPicker(toybox::Screen& screen, DeckPickerModel& model);

// ---- Pairing, in the same chrome. The builders draw the band, the words
// and the confirm pill; they return the rects for the two things only the
// activity can draw -- the QR bitmap (QrUtils wants the renderer) and the
// username (NotoSerif via UITheme: the toybox cuts are ASCII-subset, and a
// CJK account name in them would blind the anti-hijack gate).

// Draws everything but the QR itself; returns the box the QR goes in.
fui::Rect buildPairQr(toybox::Screen& screen, const char* code);

struct PairConfirmLayout {
  fui::Rect username;  // the activity draws the account name here, in serif
  fui::Rect pill;      // the tap gate; the activity keeps it for hit-testing
};
PairConfirmLayout buildPairConfirm(toybox::Screen& screen);

}  // namespace studyui

#pragma once

// The flashcard app. Reads a deck converted from Mario's Anki collection off
// the SD card, schedules with FSRS-5 plus Anki's learning steps, and draws a
// card face that follows his own Anki template -- including randomising the
// hanzi face per card, which is the point of the whole thing.
//
// The split docs/shelf.md asks for:
//
//   StudyFsrs / StudyScheduler / StudyDeck   freestanding, host-tested
//   StudyActivity                            this file: storage, fonts, input
//
// The card face is hand-drawn into the body rect rather than built from
// components, because it is the app's own surface in the sense docs/shelf.md
// means: a headword at 100px over a rule over an example sentence is not a
// list, and expressing it as one would fight the layout the whole way.

#include <HalStorage.h>

#include <memory>
#include <vector>

#include "../../activities/Activity.h"
#include "StudyDeck.h"
#include "StudyFonts.h"
#include "StudyFsrs.h"
#include "StudyImages.h"
#include "StudyScheduler.h"
#include "StudyScreens.h"
#include "StudyStats.h"
#include "StudySync.h"

struct Rect;

class StudyActivity final : public Activity {
 public:
  StudyActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Study", renderer, mappedInput), fsrs_(nullptr), scheduler_(fsrs_, study::Steps::defaults()) {}
  ~StudyActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // A day's session is tens of cards. Holding all 5001 indices to show forty
  // of them would be 20KB for nothing, and the scan is cheap to repeat.
  static constexpr int kMaxQueue = 256;
  // Cards inside a learning step come back within the session. This is how
  // many can be in flight at once -- past that, the next one waits its turn in
  // the main queue rather than being dropped.
  static constexpr int kMaxLearning = 32;

  // Deck is the front door and also the end state: one screen that reflects
  // where the session is, rather than a separate 'finished' page that says
  // the same things with none of the same context.
  // The three Sync* views are the bridge flow: a message screen that carries
  // whatever sentence the flow is on, the pairing QR, and the on-device
  // "Paired to <account> -- confirm?" gate (which closes both directions of
  // the pairing race; see docs/apps/study-sync-bridge-plan.md).
  enum class View : uint8_t { Deck, Card, Image, NoDeck, SyncFlow, PairQr, PairConfirm, DeckPicker };
  enum class Face : uint8_t { Question, Answer };

  bool openDeck();
  void buildQueue();
  bool loadCurrent();
  void grade(study::Rating rating);
  // Save the graded card and append to the review log. Returns false if either
  // write failed, which is surfaced rather than swallowed: a review the user
  // gave that did not reach the card is worse than an error.
  // `revlogOffset` comes back as the byte position of the record appended, so
  // undo can find it again. `written` says whether it means anything: offset 0
  // is where the very first review of a new log goes.
  bool persist(int index, const study::CardState& card, study::Rating rating, const study::Outcome& outcome,
               uint32_t& revlogOffset, bool& written);
  // Pick the next card: a learning card whose minute has come, else the queue,
  // else the learning card that is closest to due.
  bool findDeckDirs();
  void beginDeckSession();
  bool openDeckAt(int index);
  void closeDeck();
  void switchDeck();
  bool takeNext();
  // Take back the last answer. One level only: "I meant Good, not Again" is the
  // case that matters, and a deeper stack would need the queue's whole history
  // to unwind rather than one card's.
  void undo();
  void flushWrites();
  bool canUndo() const { return undo_.valid; }
  int nowMinute() const;

  void refreshStats();
  void buildDeckModel(studyui::DeckModel& out) const;
  void routeAction(const fui::ActionEvent& event);

  bool fitsAsDrawn(int fontId, const char* text, int maxWidth) const;
  void drawCard(const Rect& body);
  void drawImage(const Rect& body);
  // The whole header band is the affordance when a card has a photograph.
  bool cardHasImage() const { return image_.valid(); }
  void drawFooter(const Rect& footer);
  int drawWrapped(int fontId, int y, int maxWidth, const char* text, bool measureOnly = false) const;
  // The same wrap, with one run of codepoints underlined. Anki marks a cloze
  // answer in colour and bolds the target word in an example sentence; this
  // panel has neither colour nor a bold CJK face, so the mark is a rule under
  // the glyphs. `spanLength` of 0 draws exactly what drawWrapped would.
  int drawWrappedUnderlined(int fontId, int y, int maxWidth, const char* text, int spanStart, int spanLength) const;
  // The one body behind both. Kept private and named for what it does rather
  // than folded into drawWrapped with two defaulted arguments: every call
  // site says whether it is marking something, and "0, 0, false" at the end
  // of a draw call says nothing to anybody.
  int drawWrappedMarked(int fontId, int y, int maxWidth, const char* text, int spanStart, int spanLength,
                        bool measureOnly) const;
  // The cloze face, drawn instead of the vocabulary one when the note carries
  // a cloze question. Split out because the two share only the body rect: a
  // cloze card has no headword, no reading and no example sentence, and
  // threading four more conditionals through drawCard hid both.
  void drawClozeCard(const Rect& body);

  HalFile deckFile_;
  HalFile cardFile_;
  HalFile metaFile_;
  HalFile revlogFile_;
  HalFile revlogReadFile_;
  std::unique_ptr<study::ByteSource> deckSource_;
  std::unique_ptr<study::WritableByteSource> cardSource_;
  std::unique_ptr<study::ByteSource> metaSource_;

  study::StudyDeck deck_;
  study::StudyFonts fonts_;
  study::Fsrs fsrs_;
  study::Scheduler scheduler_;
  study::Stats stats_;
  std::unique_ptr<study::ByteSource> revlogSource_;
  study::StudyImages images_;
  std::unique_ptr<study::ByteSource> imageSource_;
  HalFile imageFile_;
  study::ImageRef image_;
  study::Note note_;
  study::CardState card_;
  study::Outcome preview_[4];

  int queue_[kMaxQueue] = {};
  int queueCount_ = 0;
  int queuePos_ = 0;

  // Cards mid-step, with the minute each becomes due again.
  struct Pending {
    int index;
    int dueDay;
    int dueMinute;
  };
  Pending learning_[kMaxLearning] = {};
  int learningCount_ = 0;

  // Everything the last answer changed, so it can be put back exactly. The
  // revlog record is voided in place rather than removed: the file is
  // append-only by design and shrinking it would mean reaching past HalFile.
  // Where takeNext() got the card it is showing. Undo has to put that card
  // back before it can show the previous one again, and the two sources are put
  // back differently: the main queue by rewinding a cursor, the step list by
  // pushing an entry on again.
  enum class Took : uint8_t { Nothing, Queue, Learning };
  Took took_ = Took::Nothing;

  struct Undo {
    bool valid = false;
    int index = -1;
    study::CardState before;
    // Where that review's record starts. Offset 0 is a real position -- it is
    // the first review of a brand new log -- so validity needs its own flag
    // rather than a sentinel value.
    uint32_t revlogOffset = 0;
    bool revlogWritten = false;
    Took took = Took::Nothing;     // where the card now on screen came from
    bool enteredLearning = false;  // grading put it into the step list
    int reviewed = 0;
    int again = 0;
  };
  Undo undo_;

  // Undo takes the leftmost quarter of the footer: the same width as one rating
  // cell on the answer side, so the two faces divide the same bar the same way.
  static constexpr int kUndoSlots = 4;

  // Every deck on the card, discovered at onEnter. Sixteen, not eight: nothing
  // removes a deck folder, so a card accumulates every deck ever chosen, and
  // eight slots were reachable by changing your mind once about five decks.
  // Past the cap a deck is invisible AND unreachable to the sync, so its
  // reviews are never sent. 16 x 48 bytes is 768.
  static constexpr int kMaxDecks = 16;
  char deckNames_[kMaxDecks][48] = {};
  int deckCount_ = 0;
  // Deck folders on the card beyond the kMaxDecks this reader can hold. They
  // are invisible everywhere else, including to the sync, so the number has to
  // reach the screen.
  int decksOverCap_ = 0;
  int deckIndex_ = 0;
  // The open deck's directory, /study/<name>; empty until a deck is open.
  char deckDir_[64] = "";  // "/study/" + a 40-char slug, with room to spare

  int currentIndex_ = -1;
  int today_ = 0;
  int startMinute_ = 0;
  int forecast_[studyui::kForecastDays] = {};
  int dueTotal_ = 0;
  int newTotal_ = 0;
  int reviewedThisSession_ = 0;
  int againThisSession_ = 0;
  bool writeFailed_ = false;

  toybox::Interactions interactions_;
  bool interactionsReady_ = false;

  View view_ = View::NoDeck;
  Face face_ = Face::Question;
  bool fontsReady_ = false;
  uint32_t shuffle_ = 0;

  // ---- The sync flow. Blocking network runs on the loop task (the KOSync
  // pattern); the render task paints through requestUpdateAndWait, and the
  // poll loops pump input themselves -- the sanctioned exception, nothing
  // else pumps while they block.
  void beginSync();
#if !defined(FREEINK_NET_WOLFSSL)
  // Sim-only render harness: STUDY_SYNCFLOW_PREVIEW drives the sync views
  // with canned data so sim-shot can photograph them without a bridge.
  void applySyncFlowPreview(const char* state);
  studyui::SyncFlowModel previewFlow_;
  bool previewFlowSet_ = false;
#endif
  void onSyncWifi(bool connected);
  void runSyncFlow();
  bool runPairing();
  // Blocking, like the rest of the flow: paints the picker and pumps input
  // until the user confirms or leaves. False means the sync should stop.
  bool runDeckPicker();
  std::vector<study::StudySync::DeckChoice> deckChoices_;
  studyui::DeckPickerModel picker_;
  std::vector<studyui::DeckPickerModel::Row> pickerRows_;
  bool buildPayloads(std::vector<study::DeckPayload>& out);
  bool applyManifests(const std::vector<study::DeckManifest>& manifests, std::string& message, int& decksUpdated);
  // Paint the flow model now (requestUpdateAndWait); flowStage() marks every
  // stage before `stage` done, `stage` active, and swaps the caption.
  void showFlow();
  void flowStage(studyui::SyncStage stage, const char* caption);
  // describeQueue makes the footer say what is waiting to study. It cannot be
  // passed in as text: the flow closed the deck for heap, so the counts are
  // zero until endSyncSession reopens it, and an argument is evaluated before
  // the call. Composed inside, after the reopen, it tells the truth.
  void endSyncSession(studyui::SyncVerdictKind kind, studyui::SyncSafety safety, const char* title, const char* body,
                      const char* whatNow = nullptr, bool describeQueue = false);
  void syncTimeIfNeeded();
  void drainInput();
  // The radio is up and a stranger is mid-pairing: sleeping here would read
  // as a crash. The result screen (syncBusy_ false) may sleep normally.
  bool preventAutoSleep() override {
    return syncBusy_ || view_ == View::PairQr || view_ == View::PairConfirm || view_ == View::DeckPicker;
  }

  study::StudySync sync_;
  study::BridgeState bridge_;
  bool syncQueued_ = false;
  bool syncBusy_ = false;
  bool wifiActivated_ = false;
  studyui::SyncFlowModel flow_;
  bool secondPass_ = false;
  // A runtime request for the deck picker; never persisted, so cancelling it
  // cannot leave the reader stuck answering the question on every sync.
  bool pickerRequested_ = false;
  // Cards waiting in the card's OTHER decks, refreshed with the queue rather
  // than per render: "ALL CLEAR" on the open deck was read as "nothing to
  // study today" while another deck held work.
  int countWaitingIn(const char* dirName) const;
  int otherWaiting_ = 0;
  // What the pairing screens draw; only meaningful in the PairQr/PairConfirm
  // views. The confirm tap target is registered by render() here so the poll
  // loop and the drawing agree on one rectangle.
  // The empty-card screen's sync door, registered by render() so the tap
  // test and the pixels cannot drift apart.
  int16_t noDeckSyncX_ = 0;
  int16_t noDeckSyncY_ = 0;
  int16_t noDeckSyncW_ = 0;
  int16_t noDeckSyncH_ = 0;
  // The second door on an empty card, drawn only once decks have been chosen.
  // Zero height means it is not on screen and cannot be tapped.
  int16_t noDeckPickY_ = 0;
  int16_t noDeckPickH_ = 0;
  std::string pairCode_;
  std::string pairUsername_;
  // The confirm tap target (Rect is only forward-declared here).
  int16_t confirmX_ = 0, confirmY_ = 0, confirmW_ = 0, confirmH_ = 0;
};

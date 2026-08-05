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

#include "../../activities/Activity.h"
#include "StudyDeck.h"
#include "StudyFonts.h"
#include "StudyFsrs.h"
#include "StudyScheduler.h"
#include "StudyScreens.h"

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
  enum class View : uint8_t { Deck, Card, NoDeck };
  enum class Face : uint8_t { Question, Answer };

  bool openDeck();
  void buildQueue();
  bool loadCurrent();
  void grade(study::Rating rating);
  // Save the graded card and append to the review log. Returns false if either
  // write failed, which is surfaced rather than swallowed: a review the user
  // gave that did not reach the card is worse than an error.
  bool persist(int index, const study::CardState& card, study::Rating rating, const study::Outcome& outcome);
  // Pick the next card: a learning card whose minute has come, else the queue,
  // else the learning card that is closest to due.
  bool takeNext();
  int nowMinute() const;

  void buildDeckModel(studyui::DeckModel& out) const;
  void routeAction(const fui::ActionEvent& event);

  void drawCard(const Rect& body);
  void drawFooter(const Rect& footer);
  int drawWrapped(int fontId, int y, int maxWidth, const char* text, bool measureOnly = false) const;

  HalFile deckFile_;
  HalFile cardFile_;
  HalFile metaFile_;
  HalFile revlogFile_;
  std::unique_ptr<study::ByteSource> deckSource_;
  std::unique_ptr<study::WritableByteSource> cardSource_;
  std::unique_ptr<study::ByteSource> metaSource_;

  study::StudyDeck deck_;
  study::StudyFonts fonts_;
  study::Fsrs fsrs_;
  study::Scheduler scheduler_;
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
};

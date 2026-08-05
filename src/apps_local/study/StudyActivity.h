#pragma once

// The flashcard app. Reads a deck converted from Mario's Anki collection off
// the SD card, schedules with FSRS-5, and draws a card face that follows his
// own Anki template -- including randomising the hanzi face per card, which is
// the point of the whole thing.
//
// The three-way split docs/shelf.md asks for:
//
//   StudyFsrs / StudyDeck   freestanding, host-tested
//   StudyScreens            FreeInkUI and Toybox tokens only
//   StudyActivity           this file: storage, fonts, input, the shelf
//
// The card face itself is hand-drawn into the body rect rather than built from
// components, because it is the app's own surface in the sense docs/shelf.md
// means: a headword at 100px over a rule over an example sentence is not a
// list, and expressing it as one would fight the layout the whole way.

#include <HalStorage.h>

#include <memory>

#include "../../activities/Activity.h"
#include "StudyDeck.h"
#include "StudyFonts.h"
#include "StudyFsrs.h"

struct Rect;

class StudyActivity final : public Activity {
 public:
  StudyActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Study", renderer, mappedInput), fsrs_(nullptr) {}
  ~StudyActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // The queue is bounded rather than holding every due card. A day's session
  // is tens of cards; holding 5001 indices to show forty of them would be
  // 20KB for nothing, and the scan is cheap enough to repeat tomorrow.
  static constexpr int kMaxQueue = 256;

  enum class Face : uint8_t { Question, Answer };

  bool openDeck();
  // One pass over cards.dat, in chunks, collecting what is due today and then
  // topping up with new cards. Chunked because 5001 separate 32-byte reads is
  // 5001 seeks, and the file is only 156KB.
  void buildQueue();
  bool loadCurrent();
  void advance();
  void grade(study::Rating rating);

  void drawCard(const Rect& body);
  void drawRatingBar(int y, int height);
  // Centres text horizontally in `width`, measuring with the font it will draw
  // with. Returns the baseline-independent top y the caller should advance past.
  int drawCentered(int fontId, int y, int width, const char* text) const;

  HalFile deckFile_;
  HalFile cardFile_;
  HalFile metaFile_;
  std::unique_ptr<study::ByteSource> deckSource_;
  std::unique_ptr<study::ByteSource> cardSource_;
  std::unique_ptr<study::ByteSource> metaSource_;

  study::StudyDeck deck_;
  study::StudyFonts fonts_;
  study::Fsrs fsrs_;
  study::Note note_;
  study::CardState card_;

  int queue_[kMaxQueue] = {};
  int queueCount_ = 0;
  int queuePos_ = 0;
  int today_ = 0;
  int dueCount_ = 0;
  int newCount_ = 0;

  Face face_ = Face::Question;
  bool ready_ = false;
  bool fontsReady_ = false;
  int intervals_[4] = {};
  // Advanced per card so the face changes even when the same card comes back.
  uint32_t shuffle_ = 0;
};

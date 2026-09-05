#pragma once

// TRIVIA: a pack of questions on the card, two ways to play them.
//
// No count here on purpose. The shipped pack is 50,000 clues levelled by
// Jeopardy's dollar value; assemble_pack.py builds one holding whatever a
// rating run has reached, so the figure changes on every build and the front
// door reads it off the card. See docs/apps/trivia.md.
//
// The activity is the thin layer: it owns the pack files, the round and the
// input. The format and the round logic are in TriviaCore.h (freestanding) and
// the drawing is in TriviaScreens.cpp (freestanding). See docs/apps/trivia.md.

#include <memory>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "TriviaCore.h"
#include "TriviaScreens.h"

class TriviaActivity final : public Activity {
 public:
  TriviaActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Trivia", renderer, mappedInput) {}
  ~TriviaActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // Defined in TriviaCore.h, where backFrom() can be tested on the host; the
  // alias keeps every View::Menu call site here unchanged.
  using View = trivia::View;
  // Which mode the HIDDEN notice came from. deal() decides whether it needs a
  // multiple-choice question by reading view_, so the notice has to put the
  // mode back before dealing or Quizmaster would start demanding distractors.
  View flagReturn_ = View::Menu;

  bool openPack();
  void onWifiChosen(bool connected);
  void runPackDownload();
  void showNotice(const char* headline, const char* body, const char* actionLabel = nullptr,
                  freeink::ui::ActionId action = 0);
  bool ensureState(uint32_t count);
  void go(View next);
  void deal();
  void routeAction(int action, int value);

  View view_ = View::Menu;
  int selected_ = -1;
  int difficulty_ = 0;  // 0 = any

  trivia::Pack pack_;
  trivia::PackState state_;
  trivia::Question question_;
  trivia::Choices choices_;
  trivia::Rng rng_{1};
  trivia::Chooser chooser_;
  trivia::Score score_;

  uint32_t current_ = 0;
  bool haveQuestion_ = false;
  bool revealed_ = false;
  int chosen_ = -1;

  char noticeHead_[24] = {};
  char noticeBody_[160] = {};
  const char* noticeAction_ = nullptr;
  freeink::ui::ActionId noticeActionId_ = 0;
  bool downloadCancel_ = false;
  bool downloadQueued_ = false;

  bool flashOnNextPaint_ = false;
  bool interactionsReady_ = false;
  toybox::Interactions interactions_;
};

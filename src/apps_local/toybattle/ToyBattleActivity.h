#pragma once

// Toy Battle on the device. The thin layer: renderer, input, shelf.
//
// Deliberately small for now. It exists so the board can be photographed at
// native size and a layout chosen; the link path, the how-to deck and saved
// games come after that decision, not before it.

#include <memory>

#include "../../MappedInputManager.h"
#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "ToyBattleBrain.h"
#include "ToyBattleCore.h"
#include "ToyBattleFlow.h"

class ToyBattleActivity final : public Activity {
 public:
  ToyBattleActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ToyBattle", renderer, mappedInput) {}
  ~ToyBattleActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void beginGame();
  void takeOpponentTurn();
  void goTo(toybattle::Screen next);
  const char* promptText() const;

  // The line under the title is the app's one channel for saying anything, so
  // it carries two things: the question being asked, and -- until the next
  // action -- what just happened. `notice` is that second one, and it beats the
  // question while it stands.
  void say(const char* message);

  toybattle::Screen screen = toybattle::Screen::Menu;
  toybattle::Game game{};
  toybattle::Draft draft{};
  const char* notice = nullptr;
  int menuSelected = -1;
  uint8_t seat = 0;

  toybox::Interactions interactions;
  bool interactionsReady = false;
};

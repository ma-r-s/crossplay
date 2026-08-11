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
#include "ToyBattleMenus.h"

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
  tbui::MenuModel menuModel() const;
  void openMenu();
  void cycleSetupRow(tbui::SetupRow row);
  void refreshSaveLine();
  void takeOpponentTurn();
  void goTo(toybattle::Screen next);
  const char* promptText() const;

  // The line under the title is the app's one channel for saying anything, so
  // it carries two things: the question being asked, and -- until the next
  // action -- what just happened. `notice` is that second one, and it beats the
  // question while it stands.
  void say(const char* message);

  toybattle::Screen screen = toybattle::Screen::Menu;
  // What was chosen before the game started. Held here rather than read off the
  // Game, because two of the three are decided before a Game exists and the
  // difficulty is not part of a position at all.
  toybattle::Options options{};
  toybattle::Game game{};
  toybattle::Draft draft{};
  const char* notice = nullptr;
  int menuSelected = 0;
  int setupSelected = 0;
  int howToPage = 0;
  int mapTop = 0;
  uint8_t seat = 0;

  // What the front door offers to go back to. `preview` is the board the menu
  // draws: the saved position if there is one, an empty board otherwise, so the
  // ornament is always the map you are about to play.
  bool hasSave = false;
  char saveDetail[48] = {};
  toybattle::Game preview{};
  int played = 0;
  int won = 0;

  // Which of the three treatments to draw. A constant rather than a setting:
  // two of the three get deleted once one is chosen, and a build flag is enough
  // to photograph them side by side in the meantime.
  static constexpr tbui::Look kLook = tbui::Look::FrontDoor;

  toybox::Interactions interactions;
  bool interactionsReady = false;
};

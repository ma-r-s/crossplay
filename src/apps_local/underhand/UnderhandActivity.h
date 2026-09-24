#pragma once

// Underhand on the device: the renderer, the card, the save file and the
// shelf. The rules are UnderhandEngine; the words are UnderhandView; the
// pictures are UnderhandScreens. See docs/apps/underhand.md.
//
// Where Back goes, by screen:
//   the card                -> the menu (the run is kept)
//   the paying panel        -> the card
//   how to play             -> wherever it was opened from
//   the menu, asking to give up -> the menu
//   the menu                -> the shelf
//   the end of a run        -> the menu

#include <atomic>
#include <memory>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "UnderhandSave.h"
#include "UnderhandScreens.h"

class UnderhandActivity final : public Activity {
 public:
  UnderhandActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Underhand", renderer, mappedInput) {}
  ~UnderhandActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class View : uint8_t { Menu, Play, End, Help, Broken };

  bool route(int action, int value);
  bool back();
  void newRun();
  void take(int option);
  void pay(int option, const underhand::Counts& offer);
  void afterChoice();
  void load();
  void save();
  void audit();
  // The card model, built afresh in place for each render: with every way to
  // pay listed it is a few KB, too much for the render task's stack.
  underhandui::CardModel& freshCard();

  // About 40KB: every card, its options and its text. Made once in onEnter.
  std::unique_ptr<underhand::Cards> cards;
  std::unique_ptr<underhandui::CardModel> cardModel;
  underhand::Save state;
  underhand::Rng rng;
  underhand::Game ended;  // the run the end screen is about
  View view = View::Menu;
  int payingFor = -1;          // the option whose paying panel is open
  underhand::Counts picked{};  // what the player has put toward it
  View helpFrom = View::Menu;  // where how to play goes back to
  int helpPage = 0;
  static constexpr int kFastRefreshes = 12;
  int fastRefreshes = 0;
  bool confirmGiveUp = false;  // the menu is asking before the run is thrown away
  bool saveSetAside = false;   // the save could not be read and was renamed .bad
  bool flashNext = false;
  bool auditPending = false;
  std::atomic<bool> ready{false};  // onEnter has finished; render draws nothing before

  toybox::Interactions interactions;
  bool interactionsReady = false;
};

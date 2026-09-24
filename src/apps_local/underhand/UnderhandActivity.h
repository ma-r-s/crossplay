#pragma once

// Underhand on the device: the renderer, the card, the save file and the
// shelf. The rules are UnderhandEngine; the words are UnderhandView; the
// pictures are UnderhandScreens. See docs/apps/underhand.md.
//
// Where Back goes, by screen:
//   the card                -> the menu (the run is kept)
//   the list of ways to pay -> the card
//   how to play             -> wherever it was opened from
//   the menu, asking to give up -> the menu
//   the menu                -> the shelf
//   the end of a run        -> the menu

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

  void route(int action, int value);
  void back();
  void newRun();
  void pay(int option, int way);
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
  bool showOutcome = false;    // the options give their place to what just happened
  int waysFor = -1;            // the option whose ways to pay are listed
  int waysPage = 0;            // which page of them
  View helpFrom = View::Menu;  // where how to play goes back to
  bool confirmGiveUp = false;  // the menu is asking before the run is thrown away
  bool flashNext = false;
  bool auditPending = false;

  toybox::Interactions interactions;
  bool interactionsReady = false;
};

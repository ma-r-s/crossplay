#pragma once

// Yahtzee on the device. The thin layer: renderer, input, shelf, link.
//
// Derives from LinkActivity, so the two-device path is the solo path with a
// different opponent. The game never sees a radio, an address or a packet.

#include <memory>

#include "../link/LinkActivity.h"
#include "../ui/ToyboxScreen.h"
#include "YahtzeeCore.h"
#include "YahtzeeFlow.h"

class YahtzeeActivity final : public linkplay::LinkActivity {
 public:
  YahtzeeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : linkplay::LinkActivity("Yahtzee", renderer, mappedInput) {}
  ~YahtzeeActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 protected:
  linkplay::PlayBase& linkState() override { return play; }
  const linkplay::PlayBase& linkState() const override { return play; }
  const char* linkGameTitle() const override { return "YAHTZEE"; }
  const char* linkHeadline() const override;
  void onMatchStart(bool goesFirst) override;
  bool takeOpponentState() override;
  void onRematch() override;
  void onLinkEnded() override;
  bool matchGameOver() const override { return yahtzee::over(game); }
  // Counted here and nowhere else in a match. It used to be counted in an
  // else-if arm of gameLoop() that multiplayer returns before reaching, so no
  // link game was ever recorded and no final board was ever shown. See
  // link/LinkEndgame.h.
  void onMatchEnded() override;
  void gameLoop() override;
  void gameRender() override;

 private:
  void beginSoloGame();
  void takeOpponentTurn();
  void goTo(yahtzee::Screen next);
  // The running record, for the menu's ornament. Written when a game ends,
  // read once on entry -- knucklebones' pattern.
  void recordResult();
  void loadHistory();

  yahtzee::Screen screen = yahtzee::Screen::Menu;
  yahtzee::Game game{};
  int howToPage = 0;
  int menuSelected = -1;

  // What the menu draws instead of being three rows and white space. Kept here
  // rather than in the core: the rules do not have a history, a device does.
  int played = 0;
  int won = 0;
  int best = 0;
  int yahtzees = 0;
  // The face of the most recent five-of-a-kind this device scored with, so the
  // ornament can draw the hand rather than claim it. 0 until the first one.
  int yahtzeeFace = 0;
  bool resultRecorded = false;
  // Which card this device owns, 0 or 1. Zero in a solo game; the coin toss
  // decides it in a match, and nothing else in the app has to learn seats exist.
  uint8_t seat = 0;

  linkplay::Play<yahtzee::Game> play;

  toybox::Interactions interactions;
  bool interactionsReady = false;
};

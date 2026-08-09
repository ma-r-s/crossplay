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
  void gameLoop() override;
  void gameRender() override;

 private:
  void beginSoloGame();
  void takeOpponentTurn();
  void goTo(yahtzee::Screen next);

  yahtzee::Screen screen = yahtzee::Screen::Menu;
  yahtzee::Game game{};
  int howToPage = 0;
  int menuSelected = -1;
  // Which card this device owns, 0 or 1. Zero in a solo game; the coin toss
  // decides it in a match, and nothing else in the app has to learn seats exist.
  uint8_t seat = 0;

  linkplay::Play<yahtzee::Game> play;

  toybox::Interactions interactions;
  bool interactionsReady = false;
};

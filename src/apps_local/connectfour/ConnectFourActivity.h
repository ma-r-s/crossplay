#pragma once

// Connect Four on the device. The thin layer: renderer, input, shelf, link.
//
// Derives from LinkActivity, so the two-device path is the solo path with a
// different opponent. The game never sees a radio, an address or a packet.

#include <memory>

#include "../link/LinkActivity.h"
#include "../ui/ToyboxScreen.h"
#include "ConnectFourCore.h"
#include "ConnectFourFlow.h"

class ConnectFourActivity final : public linkplay::LinkActivity {
 public:
  ConnectFourActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : linkplay::LinkActivity("ConnectFour", renderer, mappedInput) {}
  ~ConnectFourActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 protected:
  linkplay::PlayBase& linkState() override { return play; }
  const linkplay::PlayBase& linkState() const override { return play; }
  const char* linkGameTitle() const override { return "CONNECT FOUR"; }
  const char* linkHeadline() const override;
  void onMatchStart(bool goesFirst) override;
  bool takeOpponentState() override;
  void onRematch() override;
  void onLinkEnded() override;
  bool matchGameOver() const override { return connectfour::over(game); }
  void gameLoop() override;
  void gameRender() override;

 private:
  void beginSoloGame();
  void takeOpponentTurn();
  void goTo(connectfour::Screen next);

  connectfour::Screen screen = connectfour::Screen::Menu;
  connectfour::Game game{};
  int howToPage = 0;
  int menuSelected = -1;
  // Which colour this device plays. Light in a solo game; the coin toss decides
  // it in a match, and nothing else in the app has to learn seats exist.
  uint8_t seat = connectfour::kLight;

  linkplay::Play<connectfour::Game> play;

  toybox::Interactions interactions;
  bool interactionsReady = false;
};

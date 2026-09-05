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
  void goTo(connectfour::Screen next);
  // The last finished game and the running tally, for the menu's ornament.
  // Written when a game ends, read once on entry -- knucklebones' pattern.
  void recordResult();
  void loadHistory();

  connectfour::Screen screen = connectfour::Screen::Menu;
  connectfour::Game game{};
  int howToPage = 0;
  int menuSelected = -1;

  // What the menu draws instead of being three rows and white space. Kept here
  // rather than in the core: the rules do not have a history, a device does.
  bool hasHistory = false;
  // 0 won, 1 lost, 2 draw, from this device's seat.
  int lastOutcome = 2;
  // Column-major, row 0 at the bottom, always from this device's perspective:
  // kLight is always the seat this device played.
  uint8_t lastCells[connectfour::kCells] = {};
  // Flat indices (column * kRows + row) of the four that ended it, -1s on a
  // draw.
  int lastLine[connectfour::kLine] = {-1, -1, -1, -1};
  int wins = 0;
  int losses = 0;
  int draws = 0;
  bool resultRecorded = false;
  // Which colour this device plays. Light in a solo game; the coin toss decides
  // it in a match, and nothing else in the app has to learn seats exist.
  uint8_t seat = connectfour::kLight;

  linkplay::Play<connectfour::Game> play;

  toybox::Interactions interactions;
  bool interactionsReady = false;
};

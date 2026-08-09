#pragma once

// Checkers on the device. The thin layer: renderer, input, shelf, link.
//
// Derives from LinkActivity, so the two-device path is the solo path with a
// different opponent. The game never sees a radio, an address or a packet.

#include <memory>

#include "../link/LinkActivity.h"
#include "../ui/ToyboxScreen.h"
#include "CheckersCore.h"
#include "CheckersFlow.h"

class CheckersActivity final : public linkplay::LinkActivity {
 public:
  CheckersActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : linkplay::LinkActivity("Checkers", renderer, mappedInput) {}
  ~CheckersActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 protected:
  linkplay::PlayBase& linkState() override { return play; }
  const linkplay::PlayBase& linkState() const override { return play; }
  const char* linkGameTitle() const override { return "CHECKERS"; }
  const char* linkHeadline() const override;
  void onMatchStart(bool goesFirst) override;
  bool takeOpponentState() override;
  void onRematch() override;
  void onLinkEnded() override;
  bool matchGameOver() const override { return checkers::over(game); }
  void gameLoop() override;
  void gameRender() override;

 private:
  void beginSoloGame();
  void takeOpponentTurn();
  void goTo(checkers::Screen next);
  void clearPick();

  checkers::Screen screen = checkers::Screen::Menu;
  checkers::Game game{};
  int howToPage = 0;
  int menuSelected = -1;
  // Which seat this device plays. Light in a solo game; the coin toss decides
  // it in a match, and nothing else in the app has to learn seats exist.
  uint8_t seat = checkers::kLight;

  // The square in hand and where it may land. Owned here because it is the one
  // piece of state between two taps, but computed by the flow from the rules'
  // own move list rather than worked out again.
  int picked = checkers::kNothingPicked;
  uint8_t destinations[checkers::kMaxMoves] = {};
  uint64_t takenMasks[checkers::kMaxMoves] = {};
  int destinationCount = 0;

  linkplay::Play<checkers::Game> play;

  toybox::Interactions interactions;
  bool interactionsReady = false;
};

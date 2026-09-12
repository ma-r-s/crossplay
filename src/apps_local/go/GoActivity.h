#pragma once

// Go on the device. The thin layer: renderer, input, shelf, link, storage.
//
// Derives from LinkActivity, so the nearby game is the solo game with a
// different source of the opponent's move. Nothing in here sees a radio, an
// address or a packet.

#include <memory>

#include "../link/LinkActivity.h"
#include "../ui/ToyboxScreen.h"
#include "GoCore.h"
#include "GoFlow.h"

class GoActivity final : public linkplay::LinkActivity {
 public:
  GoActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : linkplay::LinkActivity("Go", renderer, mappedInput) {}
  ~GoActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;

 protected:
  linkplay::PlayBase& linkState() override { return play; }
  const linkplay::PlayBase& linkState() const override { return play; }
  const char* linkGameTitle() const override { return "GO"; }
  const char* linkHeadline() const override;
  void onMatchStart(bool goesFirst) override;
  bool takeOpponentState() override;
  void onRematch() override;
  void onLinkEnded() override;
  bool matchGameOver() const override { return game.stage == static_cast<uint8_t>(go::Stage::Over); }
  void onMatchEnded() override;
  void gameLoop() override;
  void gameRender() override;

 private:
  void beginSoloGame();
  void takeComputerTurn();
  void goTo(go::Screen next);
  void clearAim();
  void handlePointActivated(int point);
  void toggleDeadAt(int point);
  void enterCounting();
  // Recomputes owner/blackHalves/whiteHalves from the position as it stands.
  // Every door into the counting screen has to pass through this: a resumed
  // game reached it with the three of them still zero, so the board showed no
  // territory at all and the header read B+0.0.
  void refreshCount();
  void finishCounting();
  void recordResult();
  void loadSave();
  void writeSave();
  // Whose move it is, asked of the link when there is one and of the rules when
  // there is not. Never mirrored into a member: taking delivery and sending both
  // move the turn immediately, so a copy taken at the top of a pass is stale by
  // the bottom of it.
  bool myMove() const;
  bool computerToMove() const;

  uint32_t surfaceMeaning() const override;

  go::Screen screen = go::Screen::Menu;
  go::Game game{};
  int howToPage = 0;
  int menuSelected = -1;

  go::Opponent opponent = go::Opponent::Computer;
  go::Level level = go::Level::Medium;
  uint8_t playAs = go::kBlack;

  // Which colour this device plays. Equal to playAs in a solo game against the
  // computer, decided by the coin toss in a match, and meaningless when two
  // people share the device -- which is why `sharedDevice()` exists rather than
  // this being overloaded to mean three things.
  uint8_t seat = go::kBlack;

  // The point a finger has chosen but not committed. The one piece of state
  // between two taps, and the reason a misplaced stone is recoverable.
  int aimed = go::kNothingAimed;
  go::Caution caution = go::Caution::None;

  // The computer's move is started one loop pass AFTER the repaint that shows
  // the human's, so the panel says THINKING before the search begins rather
  // than after it. Chess learned this the expensive way: requestUpdate() only
  // notifies the render task, so deferring by "one pass" without a flag runs
  // the search while the repaint is still in flight.
  bool thinking = false;

  // Whose area each point is, recomputed only when the count changes rather
  // than every paint: the render task must not do a flood fill of the board on
  // every frame.
  uint8_t owner[go::kPoints] = {};
  int blackHalves = 0;
  int whiteHalves = 0;
  bool youAccepted = false;
  bool theyAccepted = false;

  uint32_t seed = 0x9E3779B9u;

  bool hasHistory = false;
  uint8_t lastPoints[go::kPoints] = {};
  bool lastWon = false;
  int lastMarginHalves = 0;
  int wins = 0;
  int losses = 0;
  bool resultRecorded = false;
  // A game that is part-played and can be resumed from the front door.
  bool inProgress = false;

  linkplay::Play<go::Game> play;

  toybox::Interactions interactions;
  bool interactionsReady = false;
};

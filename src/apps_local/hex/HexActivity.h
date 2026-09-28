#pragma once

// Hex on the device. The thin layer: renderer, input, shelf, link, storage.
//
// Derives from LinkActivity, so the nearby game is the solo game with a
// different source of the opponent's move. Nothing in here sees a radio, an
// address or a packet.

#include <memory>

#include "../link/LinkActivity.h"
#include "../ui/ToyboxScreen.h"
#include "HexBrain.h"
#include "HexCore.h"
#include "HexFlow.h"

class HexActivity final : public linkplay::LinkActivity {
 public:
  HexActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : linkplay::LinkActivity("Hex", renderer, mappedInput) {}
  ~HexActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;

  // A search is not idleness. LinkActivity already holds the device awake for
  // the length of a match; a solo game is not a match and `wantsAwake()` is
  // false throughout it.
  //
  // What `thinking` covers is narrower than it looks, and the honest version is
  // worth writing down: the search itself runs with the loop task blocked on
  // the search task's notification, so nothing polls this while it runs. The
  // flag is true across the repaint that announces THINKING, which is the pass
  // before the search starts and the pass the sleep timer can still see -- so
  // this stops the device sleeping INTO a search, not during one. A four-second
  // think is short enough that the timer cannot expire inside it anyway.
  bool preventAutoSleep() override { return thinking || linkplay::LinkActivity::preventAutoSleep(); }

 protected:
  linkplay::PlayBase& linkState() override { return play; }
  const linkplay::PlayBase& linkState() const override { return play; }
  const char* linkGameTitle() const override { return "HEX"; }
  const char* linkHeadline() const override;
  void onMatchStart(bool goesFirst) override;
  bool takeOpponentState() override;
  void onRematch() override;
  void onLinkEnded() override;
  bool matchGameOver() const override { return hex::over(game); }
  void onMatchEnded() override;
  void gameLoop() override;
  void gameRender() override;

 private:
  void beginSoloGame();
  void takeComputerTurn();
  // The search, run where it has room: on a task of this app's own, so a long
  // think cannot starve the core the system watchdog looks at.
  int chooseComputerMove(const hex::Game& snapshot);
#if defined(ARDUINO_ARCH_ESP32)
  void searchLoop();
  static void searchTrampoline(void* self);
#endif
  void goTo(hex::Screen next);
  void handleCellActivated(int cell);
  // The move has been made and it won. One funnel, because a game can end on
  // this device's stone, on the opponent's, or on a state adopted off the wire,
  // and the chain the result screen draws has to be computed exactly once
  // whichever door it came through.
  void noteFinished();
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

  hex::Screen screen = hex::Screen::Menu;
  hex::Game game{};
  int menuSelected = -1;
  int settingsSelected = -1;

  hex::Opponent opponent = hex::Opponent::Computer;
  hex::Level level = hex::Level::Normal;
  uint8_t playAs = hex::kBlack;

  // Which colour this device plays. Equal to playAs in a solo game against the
  // computer, decided by the coin toss in a match, and meaningless when two
  // people share the device -- which is why the screens take `sharedDevice`
  // rather than this being overloaded to mean three things.
  uint8_t seat = hex::kBlack;

  // The computer's move is started one loop pass AFTER the repaint that shows
  // the human's, so the panel says THINKING before the search begins rather
  // than after it. Chess learned this the expensive way: requestUpdate() only
  // notifies the render task, so deferring by "one pass" without a flag runs
  // the search while the repaint is still in flight.
  bool thinking = false;

  // The winning connection, computed once when the game ends.
  uint8_t chain[hex::kMaskBytes] = {};

  uint32_t seed = 0x9E3779B9u;

  // The opponent's name, shortened to its first word, held because the seat
  // card draws it every frame. A device name is three words and up to twenty
  // characters, and the card gives it 124 pixels: whole, it is elided, and what
  // an elision drops is the part the reader needed.
  char theirName[24] = {};

  bool hasHistory = false;
  uint8_t lastCells[hex::kCellBytes] = {};
  bool lastWon = false;
  int wins = 0;
  int losses = 0;
  bool resultRecorded = false;
  // A game that is part-played and can be resumed from the front door.
  bool inProgress = false;

  // Forty-nine kilobytes, held only while the app is open. On the heap rather
  // than in .bss because a shelf of twenty-two games each holding its search
  // out of the static pool is a firmware that does not boot.
  std::unique_ptr<hexbrain::Pool> pool;

#if defined(ARDUINO_ARCH_ESP32)
  // Created in onEnter and ended in onExit, so its stack is only spent while
  // this app is open. Null when the task could not be started, which falls back
  // to searching on the loop task.
  void* searchTask = nullptr;
  void* searchWaiter = nullptr;
  hex::Game searchBoard{};
  hex::Level searchLevel = hex::Level::Normal;
  int searchResult = hex::kNoCell;
  volatile bool searchEnding = false;
#endif

  linkplay::Play<hex::Game> play;

  toybox::Interactions interactions;
  bool interactionsReady = false;
};

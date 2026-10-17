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
  // Throws the game in progress away, from the trash button on the RESUME row.
  void discardGame();
  void takeComputerTurn();
  // The search, run where it has room. See the long note in GoActivity.cpp:
  // michi's deepest path does not fit the loop task's stack, so the move is
  // computed on a task of this app's own and the loop waits for it.
  int chooseComputerMove(const go::Game& snapshot);
#if defined(ARDUINO_ARCH_ESP32)
  void searchLoop();
  static void searchTrampoline(void* self);
#endif
  void goTo(go::Screen next);
  void clearAim();
  void handlePointActivated(int point);
  void toggleDeadAt(int point);
  void enterCounting();
  // The machine declined the count, so the board settles it.
  void resumeFromDisagreement();
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
  int menuSelected = -1;
  int settingsSelected = -1;

  go::Opponent opponent = go::Opponent::Computer;
  go::Level level = go::Level::Medium;
  uint8_t playAs = go::kBlack;
  // Stones the player is spotted, 0 or 2..kMaxHandicap. Its OWN setting, not a
  // property of the level: a level that silently spotted stones made "easy"
  // mean two different things at once.
  int handicap = 0;
  // The board the NEXT new game is played on. Not `game.size`, which is the
  // board the game in progress is already on: changing the setting under a
  // live game would have to reinterpret its stones.
  int boardSize = go::kSmallSize;

  // Which colour this device plays. Equal to playAs in a solo game against the
  // computer, decided by the coin toss in a match, and meaningless when two
  // people share the device -- which is why `sharedDevice()` exists rather than
  // this being overloaded to mean three things.
  uint8_t seat = go::kBlack;

  // The point a finger has chosen but not committed. The one piece of state
  // between two taps, and the reason a misplaced stone is recoverable.
  int aimed = go::kNothingAimed;
  go::Caution caution = go::Caution::None;

  // The machine refused the count and play resumed. Shown on the board until
  // the next stone goes down, because a game that reappears with no explanation
  // reads as a bug.
  bool disagreed = false;
  // You passed and the machine answered with a stone. Tracked because the board
  // cannot tell: `game.passes` is back to zero by the time it is drawn, so a
  // pass that was answered and a pass that was never made look identical.
  bool passWasPlayedThrough = false;

  // The computer's move is started one loop pass AFTER the repaint that shows
  // the human's, so the panel says THINKING before the search begins rather
  // than after it. Chess learned this the expensive way: requestUpdate() only
  // notifies the render task, so deferring by "one pass" without a flag runs
  // the search while the repaint is still in flight.
  bool thinking = false;

  // Whose area each point is, recomputed only when the count changes rather
  // than every paint: the render task must not do a flood fill of the board on
  // every frame.
  uint8_t owner[go::kMaxPoints] = {};
  int blackHalves = 0;
  int whiteHalves = 0;

  uint32_t seed = 0x9E3779B9u;

  // The opponent's name, shortened to its first word, held because the seat
  // band draws it every frame. A device name is three words and up to twenty
  // characters, and the band gives it 168 pixels: whole, it is elided, and what
  // an elision drops is the part the reader needed. Every other link game in
  // this fork does the same.
  char theirName[24] = {};

  bool hasHistory = false;
  uint8_t lastPoints[go::kMaxPoints] = {};
  uint8_t lastSize = go::kSmallSize;
  bool lastWon = false;
  int lastMarginHalves = 0;
  int wins = 0;
  int losses = 0;
  bool resultRecorded = false;
  // A game that is part-played and can be resumed from the front door.
  bool inProgress = false;

#if defined(ARDUINO_ARCH_ESP32)
  // Created in onEnter and ended in onExit, so the 28KB it costs is only spent
  // while this app is open. Null when the task could not be started, which
  // falls back to searching on the loop task.
  void* searchTask = nullptr;
  // Whether the search task's stack came from PSRAM (internal RAM had no
  // contiguous 32KB). Decides how onExit deletes it.
  bool searchStackExternal = false;
  void* searchWaiter = nullptr;
  go::Game searchBoard{};
  go::Level searchLevel = go::Level::Medium;
  int searchResult = go::kPass;
  volatile bool searchEnding = false;
#endif

  linkplay::Play<go::Game> play;

  toybox::Interactions interactions;
  bool interactionsReady = false;
};

#pragma once

// Sudoku on the device. The thin layer: renderer, storage, input, shelf.
//
// A plain Activity rather than a LinkActivity. Sudoku is a solitaire, and the
// only shape a second device could take here is a race, which is continuously
// simultaneous where the link layer is strictly turn-based. Battleship's
// two-move trick works because placing a fleet ends; racing does not. So the
// cycle's multiplayer question has a "no" answer and this is what "no" looks
// like.

#include <memory>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "SudokuFlow.h"
#include "SudokuGame.h"
#include "SudokuScreens.h"

class SudokuActivity final : public Activity {
 public:
  SudokuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Sudoku", renderer, mappedInput) {}
  ~SudokuActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void goTo(sudoku::Screen next);
  void beginGame();
  void takeHint();
  void recordResult();
  void loadState();
  void saveState();
  void tickClock();

  // What a tap on the play surface means. See Activity::surfaceMeaning().
  uint32_t surfaceMeaning() const override;

  sudoku::Screen screen = sudoku::Screen::Menu;
  sudoku::Game game{};
  sudoku::Record record{};
  // 1.1KB of generator scratch, owned here so the rules need no heap and keep
  // nothing in .bss.
  sudoku::Workspace work{};

  // What the next puzzle will be, which is not the same as what the open one
  // is: changing the difficulty must not silently rewrite the grid you are
  // halfway through. Whether the two agree is asked of `game.puzzle.level`
  // through sudoku::canResume(); see SudokuGame.h for why that is not a flag.
  sudoku::Level menuLevel = sudoku::Level::Easy;
  bool hasGame = false;

  // Carving a puzzle takes tens of milliseconds and up to a few hundred at the
  // scarcer levels, so it happens one loop pass AFTER the frame that says so.
  bool generating = false;
  bool generateDeferred = false;
  sudoku::Level generatingLevel = sudoku::Level::Easy;
  uint32_t rng = 0;

  // The finger: which cell it is resting on, since when, and whether the hold
  // has already pencilled. Not a mode; there is no mode.
  int holdCell = sudoku::kNoCell;
  unsigned long holdSinceMs = 0;
  bool holdFired = false;

  unsigned long lastTickMs = 0;
  // What the status capsule says instead of the count. Cleared by the next
  // thing the player does, so it reads as an answer to a question rather than
  // as a state the board is stuck in.
  const char* notice = nullptr;

  int howToPage = 0;
  int menuSelected = -1;
  bool resultRecorded = false;
  bool newBest = false;
  uint32_t previousBestMs = 0;

  toybox::Interactions interactions;
  bool interactionsReady = false;
};

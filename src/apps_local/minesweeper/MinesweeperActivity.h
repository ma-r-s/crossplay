#pragma once

// Minesweeper on the device. The thin layer: renderer, storage, input, shelf.
//
// A plain Activity rather than a LinkActivity: this game is solitaire, and
// wiring it to the link layer to look like its neighbours would be surface
// nobody asked for. The cycle's multiplayer question has a "no" answer and this
// is what "no" looks like.

#include <memory>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "MinesweeperCore.h"
#include "MinesweeperFlow.h"

class MinesweeperActivity final : public Activity {
 public:
  MinesweeperActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Minesweeper", renderer, mappedInput) {}
  ~MinesweeperActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void beginGame();
  void goTo(minesweeper::Screen next);
  void recordResult();
  void loadHistory();

  minesweeper::Screen screen = minesweeper::Screen::Menu;
  minesweeper::Game game{};
  // Dig or flag. A mode, forced by there being one gesture and two verbs; see
  // MinesweeperFlow.h. Reset to Dig on every new board, because that is what
  // the first tap always is.
  minesweeper::Tool tool = minesweeper::Tool::Dig;
  int howToPage = 0;
  int menuSelected = -1;

  bool hasHistory = false;
  minesweeper::Game lastBoard{};
  int wins = 0;
  int losses = 0;
  bool resultRecorded = false;

  toybox::Interactions interactions;
  bool interactionsReady = false;
};

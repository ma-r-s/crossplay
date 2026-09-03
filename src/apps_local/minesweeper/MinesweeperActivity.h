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
  // Which cell a finger is resting on, since when, and whether the hold has
  // already planted its flag. Not a mode: there is no mode. See
  // MinesweeperFlow.h for why the one that existed was built on a false belief.
  int holdColumn = -1;
  int holdRow = -1;
  unsigned long holdSinceMs = 0;
  bool holdFired = false;
  // What a tap on a cell does. Starts on DIG, because the first move of every
  // game is a dig and a player who never finds the control still has a whole
  // game. Survives only the screen, not the app: a fresh board starts digging.
  bool flagMode = false;
  int howToPage = 0;
  int menuSelected = -1;

  bool hasHistory = false;
  minesweeper::Game lastBoard{};
  int wins = 0;
  int losses = 0;
  bool resultRecorded = false;

  toybox::Interactions interactions;
  bool interactionsReady = false;

  // The grid is hit-tested against geometry and never reaches route(), so the
  // table digest cannot see a cell tap. See Activity::surfaceMeaning().
  uint32_t surfaceMeaning() const override;
};

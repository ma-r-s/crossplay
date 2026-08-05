#pragma once

#include <memory>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "DungeonCore.h"
#include "DungeonScreens.h"

// Dungeons & Diagrams: a nonogram whose clues are a dungeon.
//
// Portrait, like everything except Solitaire. The board is a clue lane plus
// eight 52px cells, which is exactly the width a 480px panel has left after its
// margins -- so the layout is decided by the panel rather than chosen.
class DungeonActivity final : public Activity {
 public:
  DungeonActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Dungeon", renderer, mappedInput) {}
  ~DungeonActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class View : uint8_t { Menu, Picker, Board, Won };

  void openPuzzle(int index);
  void settleWin();
  void routeBoardTap(int x, int y);
  void routeButton(int button);

  // The save is the position itself: an index and two bitmasks. There is no
  // move list, because replaying moves to rebuild a board is a second
  // implementation of the state that has to agree with the first one forever.
  void saveState();
  bool loadState();
  // Marks the board dirty. Written on every way out rather than on every tap:
  // a puzzle is hundreds of taps and the card would rather not have hundreds
  // of writes. See kSaveEvery.
  void touchSave();
  void flushSave();

  dungeon::Board board;
  dungeon::Progress progress;
  dungeonui::Layout layout;
  dungeonui::PickerLayout pickerLayout;
  View view = View::Menu;
  // Which tier the picker is showing. Kept across visits so choosing a dungeon,
  // playing it and coming back does not send you to tier 1 again.
  int pickerTier = 0;
  bool interactionsReady = false;
  // Set on the frame a puzzle is finished, so it is recorded exactly once.
  bool recorded = false;
  // The dungeon the win screen is about. Kept because settleWin moves the board
  // on to the next one, and the first version named that next dungeon under the
  // word CLEARED -- a screen congratulating you for a puzzle you have not
  // played.
  int lastCleared = 0;
  // Consumed by the next render(): the full blink instead of the usual fast
  // refresh. Only finishing a dungeon sets it.
  bool flashOnNextPaint = false;
  int unsaved = 0;
  toybox::Interactions interactions;
};

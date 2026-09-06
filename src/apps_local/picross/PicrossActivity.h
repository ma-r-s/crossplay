#pragma once

#include <memory>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "PicrossCore.h"
#include "PicrossScreens.h"

// Picross (nonogram): fill the cells the row and column clues describe.
//
// Portrait, like everything except Solitaire. The board is a clue gutter plus
// an NxN grid whose cell size is chosen to fit the panel, so the layout is
// decided by the panel rather than picked.
class PicrossActivity final : public Activity {
 public:
  PicrossActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Picross", renderer, mappedInput) {}
  ~PicrossActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class View : uint8_t { Menu, Board, Won };

  void openPuzzle(int requested);
  void settleWin();
  void syncPicker();
  void showPage(int page);
  void routeBoardTap(int x, int y);
  void routeButton(int button);
  void setMode(int mode);

  // The save is the position itself: an index, the cell grid, the mistake count
  // and the solved bitmask. No move list -- replaying moves to rebuild a board
  // is a second implementation of the state that must agree with the first.
  void saveState();
  bool loadState();
  void touchSave();
  void flushSave();

  picross::Board board;
  picross::Progress progress;
  picrossui::Layout layout;
  picrossui::PickerLayout pickerLayout;

  uint32_t surfaceMeaning() const override;

  View view = View::Menu;
  // Which puzzle PLAY/RESUME would open: the in-progress one, or the next
  // unsolved. Not necessarily the one loaded in `board` while browsing.
  int selected = 0;
  // Which page of the picker is shown. Written back from the layout after every
  // render, so it is always the page that was actually drawn.
  int menuPage = 0;
  // Consumed by the next picker render: forget `menuPage` and open on the page
  // holding `selected`. Set whenever the picker appears.
  bool menuFollowsSelection = true;
  // The active input mode. FILL commits (a wrong fill locks as a mistake); MARK
  // annotates freely. The two side keys select these directly.
  int mode = picrossui::ModeFill;
  bool interactionsReady = false;
  bool recorded = false;
  // The puzzle the win screen is about and how it went, kept because settleWin
  // moves the board on to the next puzzle.
  int lastCleared = 0;
  int lastMistakes = 0;
  // Consumed by the next render(): the full blink instead of the fast refresh.
  // Set on opening a puzzle, on a mistake (the blink is the buzzer on a
  // soundless device and clears ghosting), on a win, and every so many marks.
  bool flashOnNextPaint = false;
  int paintsSinceFlash = 0;
  int unsaved = 0;
  toybox::Interactions interactions;
};

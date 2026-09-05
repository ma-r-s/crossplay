#include "PicrossActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include "../../activities/ActivityManager.h"
// GUI, for drawButtonHints. Included directly rather than inherited from
// Toybox.h: a header pulled in only for a macro reads as unused to clangd.
#include "../../components/UITheme.h"
#include "../Shelf.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"

namespace {

namespace fui = freeink::ui;
namespace ui = picrossui;

constexpr char kSavePath[] = "/.crosspoint/picross.sav";

// Bumped whenever the layout below changes, per the cache-format rule. An old
// save is then discarded rather than misread. v2 widened `solved` from one word
// to a bitset over the (now much larger) bank and grew the cell grid to kMaxSize.
// v3 widened `index` from uint8_t: a bank of more than 256 puzzles -- which an
// import reaches immediately -- wrapped the index on the way out, so the save
// named a DIFFERENT puzzle than the one being played and restore() then
// discarded every mark for not matching it. Nothing logged; the board simply
// came back empty, and only on the puzzles past the 256th.
constexpr uint8_t kSaveVersion = 3;

// How many MARKS may go unwritten. A committing FILL is flushed immediately --
// it is irreversible and unrepeatable, so losing one to a sleep is the worst
// data loss this game has. Annotations are cheap to redo, so they batch.
constexpr int kSaveEvery = 12;

// After this many board changes with no full refresh, force one to clear the
// ghosting a dense 1-bit grid accumulates over a solve.
constexpr int kFlashEvery = 24;

struct SaveState {
  uint16_t index;
  uint8_t mistakes;
  uint8_t cells[picross::kMaxSize * picross::kMaxSize];
  uint32_t solved[picross::kProgressWords];
} __attribute__((packed));

// The bank has to fit the field that names a puzzle in the save. Derived from
// the field rather than written as a number, so widening one does not leave the
// other behind -- and asserted rather than assumed, because the failure is a
// wrapped index that names a real, wrong puzzle and reads as a lost save.
static_assert(picross::kPuzzleCount <= (1 << (8 * static_cast<int>(sizeof(SaveState::index)))) - 1,
              "the picross bank has outgrown SaveState::index -- widen it and bump kSaveVersion");

}  // namespace

std::unique_ptr<Activity> PicrossActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<PicrossActivity>(renderer, mappedInput);
}

void PicrossActivity::onEnter() {
  toybox::ensureFonts(renderer);
  if (!loadState()) {
    progress = picross::Progress{};
    board.load(0);
  }
  // Open on whatever the save was in the middle of, so the picker points at the
  // resumable puzzle the moment the app appears.
  selected = (board.touched() && !board.solved()) ? board.index() : progress.nextUnsolved();
  view = View::Menu;
  syncPicker();
  recorded = false;
  requestUpdate();
}

void PicrossActivity::onExit() { flushSave(); }

void PicrossActivity::setMode(const int newMode) {
  if (view != View::Board || mode == newMode) return;
  mode = newMode;
  requestUpdate();
}

void PicrossActivity::openPuzzle(const int requested) {
  const int index = picross::isPlayable(requested) ? requested : 0;
  // Resume only when the tapped puzzle is the one already in hand and still
  // unfinished. A different puzzle, or a solved one tapped for a replay, starts
  // clean -- otherwise tapping RESUME would wipe the board it offered to resume,
  // and re-opening a finished picture would show it already done.
  const bool resume = index == board.index() && board.touched() && !board.solved();
  if (!resume) {
    board.load(index);
    unsaved = 1;
    flushSave();
  }
  recorded = false;
  // A fresh, safe resting mode on every open: the first stray tap places a
  // reversible mark, never an irreversible mistake.
  mode = ui::ModeFill;
  view = View::Board;
  // Full refresh on open clears whatever the last screen left behind.
  flashOnNextPaint = true;
  paintsSinceFlash = 0;
  requestUpdate();
}

void PicrossActivity::settleWin() {
  if (!board.solved() || recorded) return;
  recorded = true;
  lastCleared = board.index();
  lastMistakes = board.mistakes();
  progress.markSolved(board.index());
  // The finished board is not worth resuming, so the save carries the next
  // puzzle instead -- and carries the progress, which is what matters.
  board.load(progress.nextUnsolved());
  selected = board.index();
  unsaved = 1;
  flushSave();
  view = View::Won;
  flashOnNextPaint = true;  // the full blink as punctuation for a finished picture
  requestUpdate();
}

// board.index() rather than a layout member: the layout is derived from the
// puzzle and hashing a member would stamp the previous frame's copy
// (surfaceMeaning runs before render). See Activity::surfaceMeaning.
uint32_t PicrossActivity::surfaceMeaning() const {
  const uint32_t withView = paintclock::mixMeaning(paintclock::kMeaningSeed, static_cast<uint32_t>(view));
  return paintclock::mixMeaning(withView, static_cast<uint32_t>(board.index()));
}

void PicrossActivity::routeBoardTap(const int x, const int y) {
  // The board registers as one rect; which cell this pixel is comes from the
  // layout, and route() cannot see that. See Activity::surfaceMeaning().
  if (!surfaceRevealed()) return;
  int row = 0;
  int col = 0;
  if (!layout.cellAt(x, y, row, col)) return;

  bool changed = false;
  bool newMistake = false;
  if (mode == ui::ModeMark) {
    changed = board.mark(row, col);
  } else {
    const int before = board.mistakes();
    changed = board.fill(row, col);
    newMistake = board.mistakes() > before;
  }
  if (!changed) return;

  // A commit is persisted at once; a mark can wait.
  if (mode == ui::ModeMark)
    touchSave();
  else
    flushSave();

  if (board.solved()) {
    settleWin();
    return;
  }
  if (newMistake || ++paintsSinceFlash >= kFlashEvery) {
    flashOnNextPaint = true;
    paintsSinceFlash = 0;
  }
  requestUpdate();
}

// Land the picker on the size group and page that show `selected`, so the
// highlighted tile is on screen the moment the grid appears. The bank is stored
// with each size contiguous, so the group is the run `selected` falls in.
void PicrossActivity::syncPicker() {
  int tab = 0;
  int groupStart = 0;
  for (int i = 1; i <= selected && i < picross::kPuzzleCount; ++i) {
    if (picross::kPuzzles[i].size != picross::kPuzzles[i - 1].size) {
      ++tab;
      groupStart = i;
    }
  }
  menuTab = tab;
  menuPage = (selected - groupStart) / 16;
}

void PicrossActivity::routeButton(const int button) {
  switch (button) {
    case ui::ButtonPlay:
      openPuzzle(selected);
      break;
    case ui::ButtonRestart:
      // A new attempt at the same picture: clears the board and the mistake
      // count. Labelled and placed a row above the mode toggle, away from the
      // thumb, because it discards progress.
      board.load(board.index());
      mode = ui::ModeFill;
      unsaved = 1;
      flushSave();
      flashOnNextPaint = true;
      requestUpdate();
      break;
    case ui::ButtonPuzzles:
      flushSave();
      view = View::Menu;
      syncPicker();
      flashOnNextPaint = true;
      requestUpdate();
      break;
    case ui::ButtonNext:
      openPuzzle(progress.nextUnsolved());
      break;
    default:
      break;
  }
}

void PicrossActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (view == View::Menu) {
      shelf::leave(renderer, mappedInput);  // no app names its own destination
    } else {
      flushSave();
      view = View::Menu;
      syncPicker();
      flashOnNextPaint = true;
      requestUpdate();
    }
    return;
  }

  // The two physical side keys select the input mode directly, so a player who
  // has the device in two hands never has to reach for the capsule. Touch stays
  // complete -- the capsule does the same thing -- so this is an alias, which is
  // the fork's rule for the side keys. Only meaningful on the board.
  if (view == View::Board) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      setMode(ui::ModeFill);
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      setMode(ui::ModeMark);
      return;
    }
  }

  fui::InputSnapshot input;
  int tapX = 0;
  int tapY = 0;
  if (mappedInput.wasScreenTapped(tapX, tapY)) {
    input.touchReleased = true;
    input.touchX = static_cast<int16_t>(tapX);
    input.touchY = static_cast<int16_t>(tapY);
  }
  if (!input.touchReleased || !interactionsReady) return;

  const fui::ActionEvent action = interactions.route(input);
  switch (action.action) {
    case ui::ActionBoard:
      if (view == View::Board) routeBoardTap(tapX, tapY);
      break;
    case ui::ActionButton:
      routeButton(action.value);
      break;
    case ui::ActionMode:
      setMode(action.value);
      break;
    case ui::ActionPick: {
      if (view != View::Menu) break;
      // The grid registers one target and sends -1; the layout that drew the
      // tiles resolves the tap, so the region cannot drift from the pixels.
      // Only the -1 branch is geometry; a real value is already digest-gated.
      if (action.value < 0 && !surfaceRevealed()) break;
      const int picked = action.value >= 0 ? action.value : pickerLayout.indexAt(tapX, tapY);
      if (picross::isPlayable(picked)) openPuzzle(picked);
      break;
    }
    case ui::ActionPage:
      if (view != View::Menu || action.value == menuPage) break;
      menuPage = action.value;
      flashOnNextPaint = true;  // a wholesale page swap earns a clean full refresh
      requestUpdate();
      break;
    case ui::ActionTab:
      if (view != View::Menu || action.value == menuTab) break;
      menuTab = action.value;
      menuPage = 0;  // a different group starts at its first page
      flashOnNextPaint = true;
      requestUpdate();
      break;
    default:
      break;
  }
}

void PicrossActivity::render(RenderLock&&) {
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer, toybox::toyboxFaces());
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions);
  toybox::Screen screen(frame);

  switch (view) {
    case View::Board: {
      ui::BoardModel model;
      model.board = &board;
      model.mode = mode;
      model.solvedCount = progress.solvedCount();
      model.total = picross::kPuzzleCount;
      ui::buildBoard(screen, model, layout);
      break;
    }
    case View::Won: {
      ui::WinModel model;
      model.cleared = &picross::kPuzzles[lastCleared];
      model.mistakes = lastMistakes;
      model.solvedCount = progress.solvedCount();
      model.total = picross::kPuzzleCount;
      model.moreToPlay = progress.solvedCount() < picross::kPuzzleCount;
      ui::buildWin(screen, model);
      break;
    }
    case View::Menu:
    default: {
      ui::MenuModel model;
      model.progress = &progress;
      model.selectedIndex = selected;
      model.inProgressIndex = (board.touched() && !board.solved()) ? board.index() : -1;
      model.solvedCount = progress.solvedCount();
      model.total = picross::kPuzzleCount;
      model.hasProgress = model.inProgressIndex == selected && model.inProgressIndex >= 0;
      model.page = menuPage;
      model.sizeTab = menuTab;
      ui::buildMenu(screen, model, pickerLayout);
      // The picker clamped whatever it was handed; keep our copy in step so the
      // dots and the next tap agree with what was drawn.
      menuPage = pickerLayout.pageOnScreen;
      break;
    }
  }

  interactionsReady = true;
  toybox::reportOverflow(interactions, "Picross");

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(flashOnNextPaint ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
  flashOnNextPaint = false;
}

void PicrossActivity::touchSave() {
  if (++unsaved < kSaveEvery) return;
  flushSave();
}

void PicrossActivity::flushSave() {
  if (unsaved == 0) return;
  unsaved = 0;
  saveState();
}

void PicrossActivity::saveState() {
  SaveState state{};
  state.index = static_cast<uint16_t>(board.index());
  state.mistakes = static_cast<uint8_t>(board.mistakes() > 255 ? 255 : board.mistakes());
  const uint8_t* cells = board.cells();
  for (int i = 0; i < picross::kMaxSize * picross::kMaxSize; ++i) state.cells[i] = cells[i];
  for (int w = 0; w < picross::kProgressWords; ++w) state.solved[w] = progress.solved[w];

  HalFile file;
  if (!Storage.openFileForWrite("PICR", kSavePath, file)) return;
  const uint8_t version = kSaveVersion;
  file.write(&version, 1);
  file.write(reinterpret_cast<const uint8_t*>(&state), sizeof(state));
  file.flush();
}

bool PicrossActivity::loadState() {
  if (!Storage.exists(kSavePath)) return false;
  HalFile file;
  if (!Storage.openFileForRead("PICR", kSavePath, file)) return false;
  uint8_t version = 0;
  if (file.read(&version, 1) != 1 || version != kSaveVersion) return false;
  SaveState state{};
  if (file.read(reinterpret_cast<uint8_t*>(&state), sizeof(state)) != sizeof(state)) return false;
  if (!picross::isPlayable(state.index)) {
    LOG_ERR("PICR", "Save names puzzle %d, which is not playable", state.index);
    return false;
  }
  for (int w = 0; w < picross::kProgressWords; ++w) progress.solved[w] = state.solved[w];
  // restore() drops any byte that cannot be reconciled with the picture, so an
  // edited or corrupted save costs marks rather than an unfinishable board.
  board.restore(state.index, state.cells, state.mistakes);
  return true;
}

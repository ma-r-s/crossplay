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
//
// v4 is the janko import. `solved` is a bitset sized by kProgressWords, which
// is derived from kPuzzleCount, so CHANGING THE BANK CHANGES THIS STRUCT even
// though no field here was touched -- and, worse, the bank is emitted
// size-sorted, so adding 10x10 puzzles renumbers every 15x15 and a bit that
// survived would name a different puzzle. Solved progress from an earlier bank
// therefore cannot be migrated, only discarded, and this bump is what discards
// it by version instead of by a short read that says nothing.
// v5 drops the 15x15 tier. Mario played one on the panel: "it's just not gonna
// work". That is a bank change and therefore a save-format change twice over,
// which is the trap this comment exists for -- NO FIELD BELOW WAS TOUCHED:
//   - `cells` is kMaxSize*kMaxSize and kMaxSize fell from 15 to 10, so the
//     struct is 125 bytes shorter;
//   - `solved` is kProgressWords wide, derived from a bank that fell from 321
//     puzzles to 137, so it is six words shorter;
//   - and the bank is emitted size-sorted, so removing the 15x15s renumbers
//     nothing that survives but leaves every stored index naming a puzzle
//     chosen from a different, larger list.
// A v4 save read as a v5 is therefore garbage that parses. Discarded by
// version, which is the only honest option -- there is nothing to migrate.
//
// v6 replaces the bank wholesale: a different set of 199 pictures across four
// tiers (5, 8, 9 and 10) rather than 137 at one. Again NO FIELD BELOW WAS
// TOUCHED and again the struct changed anyway -- `solved` grew from five words
// to seven with kProgressWords -- and again every stored index names a puzzle
// chosen from an entirely different list, so a v5 `index` of 40 restores one
// player's marks onto a picture they have never seen. The tier count moved from
// one to four as well, which is why menuTab has nothing to migrate from either.
// Discarded by version. There is nothing here that could be carried across.
constexpr uint8_t kSaveVersion = 6;

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

// Land the picker on the page showing `selected`, so the highlighted tile is on
// screen the moment the grid appears.
//
// It sets a flag rather than working the page out, because the page depends on
// how many tiles fit the panel and that is decided in buildMenu. This used to
// divide by a literal 16 -- the number the picker happened to draw when a size-
// tab band sat above the grid -- and the moment that band went the two
// disagreed silently, opening the picker on the wrong page. The picker reports
// what it drew and menuPage is copied back from that.
void PicrossActivity::syncPicker() { menuFollowsSelection = true; }

// Show size group `tab`, from a tab tap. The page resets to the first of that
// group: keeping it would land on page 4 of a tier that has two, and the picker
// would clamp it to the last page rather than the first, which reads as the tab
// remembering somewhere the player has never been.
void PicrossActivity::showTab(const int tab) {
  if (view != View::Menu || tab == menuTab) return;
  menuTab = tab;
  menuPage = 0;
  menuFollowsSelection = false;  // an explicit tab beats "open on the selection"
  flashOnNextPaint = true;       // a wholesale content swap earns a clean full refresh
  requestUpdate();
}

// Show picker page `page`, from a dot tap or a side key. A no-op when it is
// already the page on screen: an e-ink full refresh for nothing is a visible
// blink that says something happened when nothing did.
void PicrossActivity::showPage(const int page) {
  if (view != View::Menu || page == menuPage) return;
  menuPage = page;
  menuFollowsSelection = false;  // an explicit page beats "open on the selection"
  flashOnNextPaint = true;       // a wholesale page swap earns a clean full refresh
  requestUpdate();
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

  // The two physical side keys, which on the X4 Pro are the ONLY buttons there
  // are: back, confirm, left and right are unassigned pins and can never fire
  // (see docs/buttons.md). Physically they are the moulded page-turn keys, and
  // what they do here follows the view:
  //
  //   on the BOARD   select the input mode (Up = FILL, Down = MARK), so a
  //                  player holding the device in two hands never reaches for
  //                  the capsule;
  //   on the PICKER  turn the page, which is what the keys are shaped for and
  //                  what the reader already does with them. 137 puzzles page
  //                  several ways and the page dots were the only way through
  //                  them -- Mario could not change page with the buttons at
  //                  all, which on a page-turn key reads as broken.
  //
  // Both are aliases: touch does everything on both screens (the capsule, the
  // dots), which is the fork's rule for these keys. Neither view can starve the
  // other, because a key is read against the view that is on screen.
  if (view == View::Board) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      setMode(ui::ModeFill);
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      setMode(ui::ModeMark);
      return;
    }
  } else if (view == View::Menu) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      showPage(ui::stepPage(menuPage, pickerLayout.pageCount, -1));
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      showPage(ui::stepPage(menuPage, pickerLayout.pageCount, +1));
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
    case ui::ActionTab:
      showTab(action.value);
      break;
    case ui::ActionPage:
      if (view != View::Menu || action.value == menuPage) break;
      showPage(action.value);
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
      model.followSelection = menuFollowsSelection;
      ui::buildMenu(screen, model, pickerLayout);
      // The picker clamped whatever it was handed, and may have resolved the
      // tab and page from `selected` instead; keep our copies in step so the
      // dots, the tabs, the side keys and the next tap all agree with what was
      // drawn.
      menuPage = pickerLayout.pageOnScreen;
      menuTab = pickerLayout.tabOnScreen;
      menuFollowsSelection = false;
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
  if (file.read(&version, 1) != 1) return false;
  if (version != kSaveVersion) {
    // Says so rather than returning a bare false: a player whose progress
    // vanished after an update is owed a line in the log that explains it, and
    // "the board came back empty" with nothing recorded is exactly the failure
    // v3 was bumped for.
    LOG_ERR("PICR", "Save is v%d, this build reads v%d -- discarding it", version, kSaveVersion);
    return false;
  }
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

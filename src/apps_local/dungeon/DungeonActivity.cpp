#include "DungeonActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include "../../activities/ActivityManager.h"
// GUI, for drawButtonHints. Included directly rather than inherited from
// Toybox.h: a header pulled in only for a macro reads as unused to clangd,
// and it was removed once on exactly that reading.
#include "../../components/UITheme.h"
#include "../Shelf.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"

namespace {

namespace fui = freeink::ui;
namespace ui = dungeonui;

constexpr char kSavePath[] = "/.crosspoint/dungeon.sav";

// Bumped whenever the layout below changes, per the cache-format rule. An old
// save is then discarded rather than misread, which here would mean a board
// with walls belonging to a different dungeon.
constexpr uint8_t kSaveVersion = 1;

// How many taps may go unwritten. A dungeon is a few hundred of them and the
// SD card would rather not have a write per tap; the cost of the floor is
// stated rather than hidden: pull the power mid-puzzle and you lose at most
// the last twelve marks.
constexpr int kSaveEvery = 12;

// Everything the save holds. Trivially copyable and written as one block.
struct SaveState {
  uint8_t index;
  uint64_t walls;
  uint64_t floors;
  uint64_t solvedLow;
  uint64_t solvedHigh;
} __attribute__((packed));

}  // namespace

std::unique_ptr<Activity> DungeonActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<DungeonActivity>(renderer, mappedInput);
}

void DungeonActivity::onEnter() {
  toybox::ensureFonts(renderer);
  if (!loadState()) {
    progress = dungeon::Progress{};
    board.load(0);
  }
  pickerTier = board.puzzle().tier;
  view = View::Menu;
  recorded = false;
  requestUpdate();
}

void DungeonActivity::onExit() { flushSave(); }

void DungeonActivity::openPuzzle(const int index) {
  // Reopening the dungeon already in hand keeps its marks; moving to a
  // different one starts clean. Without this, tapping RESUME would wipe the
  // board it offered to resume.
  if (index != board.index()) {
    board.load(index);
    unsaved = 1;
    flushSave();
  }
  pickerTier = board.puzzle().tier;
  recorded = false;
  view = View::Board;
  requestUpdate();
}

// Everything finishing a dungeon changes happens here, exactly once. Not in
// render(): a builder that mutates state cannot be host-tested, and render()
// runs more than once per tap.
void DungeonActivity::settleWin() {
  if (!board.solved() || recorded) return;
  recorded = true;
  lastCleared = board.index();
  progress.markSolved(board.index());
  // The finished board is not worth resuming, so the save carries the next
  // dungeon instead -- and carries the progress, which is the part that matters.
  board.load(progress.nextUnsolved());
  unsaved = 1;
  flushSave();
  view = View::Won;
  // The refresh flash as punctuation, per docs/design-language.md. Finishing a
  // dungeon is the one moment in this game that has earned the full blink.
  flashOnNextPaint = true;
  // And ask for the paint. Without this the winning tap changed the view and
  // drew nothing: the board sat there looking unfinished until the next tap
  // repainted it, which is indistinguishable from the tap having missed.
  requestUpdate();
}

void DungeonActivity::routeBoardTap(const int x, const int y) {
  int row = 0;
  int col = 0;
  if (!layout.cellAt(x, y, row, col)) return;
  board.tap(row, col);
  touchSave();
  if (board.solved()) {
    settleWin();
  } else {
    requestUpdate();
  }
}

void DungeonActivity::routeButton(const int button) {
  switch (button) {
    case ui::ButtonPlay:
      openPuzzle(board.index());
      break;
    case ui::ButtonChoose:
      view = View::Picker;
      requestUpdate();
      break;
    case ui::ButtonGuide:
      guidePage = 0;
      view = View::Guide;
      requestUpdate();
      break;
    case ui::ButtonGuideBack:
      if (guidePage > 0) {
        --guidePage;
      } else {
        view = View::Menu;
      }
      requestUpdate();
      break;
    case ui::ButtonGuideNext:
      // Past the last page is the tutorial itself. The guide teaches the rules
      // and then hands over the board they apply to, which is the whole reason
      // the tutorial is not a cell on the map.
      if (guidePage + 1 < ui::guidePageCount()) {
        ++guidePage;
        requestUpdate();
      } else {
        openPuzzle(0);
      }
      break;
    case ui::ButtonReset:
      board.reset();
      unsaved = 1;
      flushSave();
      requestUpdate();
      break;
    case ui::ButtonMenu:
      flushSave();
      view = View::Menu;
      requestUpdate();
      break;
    case ui::ButtonNext:
      openPuzzle(progress.nextUnsolved());
      break;
    default:
      break;
  }
}

void DungeonActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (view == View::Menu) {
      // See src/apps_local/Shelf.h: no app names its own destination.
      shelf::leave(renderer, mappedInput);
    } else if (view == View::Board) {
      flushSave();
      view = View::Menu;
      requestUpdate();
    } else if (view == View::Guide && guidePage > 0) {
      --guidePage;
      requestUpdate();
    } else {
      view = View::Menu;
      requestUpdate();
    }
    return;
  }

  fui::InputSnapshot input;
  int tapX = 0;
  int tapY = 0;
  if (mappedInput.wasScreenTapped(tapX, tapY)) {
    input.touchReleased = true;
    input.touchX = static_cast<int16_t>(tapX);
    input.touchY = static_cast<int16_t>(tapY);
  }
  // Touch only, like the rest of this fork's games. See the note in
  // ConnectionsActivity for why there is no cursor.
  if (!input.touchReleased || !interactionsReady) return;

  const fui::ActionEvent action = interactions.route(input);
  switch (action.action) {
    case ui::ActionBoard:
      if (view == View::Board) routeBoardTap(tapX, tapY);
      break;
    case ui::ActionButton:
      routeButton(action.value);
      break;
    case ui::ActionPick: {
      // A campaign grid registers one target for the whole thing and sends -1,
      // because sixty-four hit rects do not fit the interaction buffer. The
      // layout that drew the cells is what resolves the tap, so the region can
      // never drift from the pixels.
      if (action.value >= 0) {
        openPuzzle(action.value);
        break;
      }
      const int slot = pickerLayout.indexAt(tapX, tapY);
      if (slot >= 0) openPuzzle(1 + slot);
      break;
    }
    case ui::ActionPage: {
      // Value 0 means the tap landed on a grid that chooses a tier rather than
      // a dungeon; anything else is a step.
      if (action.value == 0) {
        const int slot = pickerLayout.indexAt(tapX, tapY);
        if (slot >= 0) {
          pickerTier = slot / 8 + 1;
          requestUpdate();
        }
        break;
      }
      const int next = pickerTier + action.value;
      if (next >= 0 && next <= 8) {
        pickerTier = next;
        requestUpdate();
      }
      break;
    }
    default:
      break;
  }
}

void DungeonActivity::render(RenderLock&&) {
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer, toybox::toyboxFaces());
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions);
  toybox::Screen screen(frame, toybox::themeTokens());

  switch (view) {
    case View::Board: {
      ui::BoardModel model;
      model.board = &board;
      model.solvedCount = progress.solvedCount();
      model.total = dungeon::kPuzzleCount;
      model.solved = board.solved();
      ui::buildBoard(screen, model, layout);
      break;
    }
    case View::Guide: {
      ui::GuideModel model;
      model.page = guidePage;
      model.pageCount = ui::guidePageCount();
      ui::buildGuide(screen, model);
      break;
    }
    case View::Picker: {
      ui::PickerModel model;
      model.tier = pickerTier;
      model.firstIndex = dungeon::tierStart(pickerTier);
      model.count = dungeon::tierCount(pickerTier);
      model.current = board.index();
      model.solvedCount = progress.solvedCount();
      model.total = dungeon::kPuzzleCount;
      for (int i = 0; i < model.count; ++i) {
        if (progress.isSolved(model.firstIndex + i)) model.solved |= static_cast<uint16_t>(1u << i);
      }
      model.progress = &progress;
      model.nextIndex = progress.nextUnsolved();
      ui::buildPicker(screen, model, pickerLayout);
      break;
    }
    case View::Won: {
      ui::WinModel model;
      // The dungeon just finished, not the one now loaded: settleWin has
      // already moved the board on to the next one.
      model.dungeonName = dungeon::kPuzzles[lastCleared].name;
      model.cleared = &dungeon::kPuzzles[lastCleared];
      model.solvedCount = progress.solvedCount();
      model.total = dungeon::kPuzzleCount;
      model.moreToPlay = progress.solvedCount() < dungeon::kPuzzleCount;
      ui::buildWin(screen, model);
      break;
    }
    case View::Menu:
    default: {
      ui::MenuModel model;
      model.dungeonName = board.puzzle().name;
      model.tier = board.puzzle().tier;
      model.solvedCount = progress.solvedCount();
      model.total = dungeon::kPuzzleCount;
      model.hasProgress = board.touched();
      model.progress = &progress;
      ui::buildMenu(screen, model);
      break;
    }
  }

  interactionsReady = true;
  toybox::reportOverflow(interactions, "Dungeon");

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(flashOnNextPaint ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
  flashOnNextPaint = false;
}

void DungeonActivity::touchSave() {
  if (++unsaved < kSaveEvery) return;
  flushSave();
}

void DungeonActivity::flushSave() {
  if (unsaved == 0) return;
  unsaved = 0;
  saveState();
}

void DungeonActivity::saveState() {
  SaveState state;
  state.index = static_cast<uint8_t>(board.index());
  state.walls = board.wallMask();
  state.floors = board.floorMask();
  state.solvedLow = progress.low;
  state.solvedHigh = progress.high;

  HalFile file;
  if (!Storage.openFileForWrite("DUNG", kSavePath, file)) return;
  const uint8_t version = kSaveVersion;
  file.write(&version, 1);
  file.write(reinterpret_cast<const uint8_t*>(&state), sizeof(state));
  file.flush();
}

bool DungeonActivity::loadState() {
  if (!Storage.exists(kSavePath)) return false;
  HalFile file;
  if (!Storage.openFileForRead("DUNG", kSavePath, file)) return false;
  uint8_t version = 0;
  if (file.read(&version, 1) != 1 || version != kSaveVersion) return false;
  SaveState state;
  if (file.read(reinterpret_cast<uint8_t*>(&state), sizeof(state)) != sizeof(state)) return false;
  if (state.index >= dungeon::kPuzzleCount) {
    LOG_ERR("DUNG", "Save names dungeon %d of %d", state.index, dungeon::kPuzzleCount);
    return false;
  }
  progress.low = state.solvedLow;
  progress.high = state.solvedHigh;
  // restore() strips anything standing on a monster or a chest, so an edited
  // or corrupted save costs the position rather than producing a board that
  // cannot be finished. See DungeonCore::restore.
  board.restore(state.index, state.walls, state.floors);
  return true;
}

#include "MinesweeperActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstdlib>

#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "MinesweeperScreens.h"

namespace ms = minesweeper;

namespace {
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
constexpr char kHistoryPath[] = "/.crosspoint/minesweeper.sav";
#endif
}  // namespace

std::unique_ptr<Activity> MinesweeperActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<MinesweeperActivity>(renderer, mappedInput);
}

void MinesweeperActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  screen = ms::Screen::Menu;
  menuSelected = -1;
  loadHistory();
  requestUpdate();
}

void MinesweeperActivity::loadHistory() {
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
  if (!Storage.exists(kHistoryPath)) return;
  char buffer[512] = {};
  if (Storage.readFileToBuffer(kHistoryPath, buffer, sizeof(buffer)) == 0) return;

  // Parsed into locals and committed only if the whole line is good: a
  // truncated file leaves an empty menu rather than half a minefield. Losing
  // this costs an ornament, so it fails quietly.
  int values[2 + ms::kCells] = {};
  char* cursor = buffer;
  for (int i = 0; i < static_cast<int>(sizeof(values) / sizeof(values[0])); ++i) {
    char* next = nullptr;
    const long value = strtol(cursor, &next, 10);
    if (next == cursor) return;
    values[i] = static_cast<int>(value);
    cursor = next;
  }

  int at = 0;
  wins = values[at++];
  losses = values[at++];
  ms::Game board{};
  for (int column = 0; column < ms::kColumns; ++column) {
    for (int row = 0; row < ms::kRows; ++row) board.cell[column][row] = static_cast<uint8_t>(values[at++]);
  }
  lastBoard = board;
  hasHistory = true;
#endif
}

void MinesweeperActivity::recordResult() {
  if (resultRecorded) return;
  resultRecorded = true;

  lastBoard = game;
  hasHistory = true;
  if (game.status == ms::Status::Won) ++wins;
  if (game.status == ms::Status::Lost) ++losses;

#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
  char line[512];
  int used = snprintf(line, sizeof(line), "%d %d", wins, losses);
  for (int column = 0; column < ms::kColumns && used > 0 && used < static_cast<int>(sizeof(line)); ++column) {
    for (int row = 0; row < ms::kRows; ++row) {
      used += snprintf(line + used, sizeof(line) - used, " %d", lastBoard.cell[column][row]);
    }
  }
  if (used <= 0 || used >= static_cast<int>(sizeof(line))) {
    LOG_ERR("MINE", "History line did not fit %d bytes", static_cast<int>(sizeof(line)));
    return;
  }
  snprintf(line + used, sizeof(line) - used, "\n");
  Storage.writeFile(kHistoryPath, String(line));
#endif
}

void MinesweeperActivity::goTo(const ms::Screen next) {
  screen = next;
  requestUpdate();
}

void MinesweeperActivity::beginGame() {
  // millis() is the only entropy that differs between two boots, and it enters
  // here and nowhere deeper: the core takes a seed and never reaches for a
  // clock, which is what keeps a board replayable from its seed.
  ms::start(game, static_cast<uint32_t>(millis()) * 2654435761u + 1u);
  // Always Dig on a fresh board. The first tap is always a dig, so starting in
  // Flag would be a mode nobody chose.
  tool = ms::Tool::Dig;
  resultRecorded = false;
  goTo(ms::Screen::Board);
}

void MinesweeperActivity::loop() {
  namespace fui = freeink::ui;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (ms::leavesApp(screen)) {
      shelf::leave(renderer, mappedInput);
      return;
    }
    goTo(ms::back(screen));
    return;
  }

  // The board settles itself: the rules decide when it is over, and the flow
  // says which screen that implies. Neither is decided twice.
  if (screen == ms::Screen::Board && ms::over(game)) {
    recordResult();
    goTo(ms::Screen::Result);
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
  if (!input.touchReleased || !interactionsReady) return;

  // The grid is not in the interaction buffer -- eighty cells against a
  // twenty-four slot buffer -- so it is hit-tested from the same geometry that
  // drew it. Tried first, because it covers most of the screen; anything
  // outside it falls through to the registered controls.
  if (screen == ms::Screen::Board) {
    const fui::DeviceContext device = toybox::makeTarget(renderer).deviceContext();
    int column = 0;
    int row = 0;
    if (mineui::cellAt(device, tapX, tapY, column, row)) {
      // The rules decide legality, not the screen and not here.
      const bool changed = tool == ms::Tool::Dig ? ms::reveal(game, column, row) : ms::toggleFlag(game, column, row);
      if (changed) requestUpdate();
      return;
    }
  }

  const fui::ActionEvent event = interactions.route(input);
  switch (event.action) {
    case mineui::ActionMenuRow:
      switch (static_cast<mineui::MenuRow>(event.value)) {
        case mineui::MenuRow::Play:
          beginGame();
          return;
        case mineui::MenuRow::HowTo:
          howToPage = 0;
          goTo(ms::Screen::HowTo);
          return;
        case mineui::MenuRow::Count:
          return;
      }
      return;

    case mineui::ActionPickDig:
      // Separate actions rather than a toggle, so tapping the tool you already
      // hold does nothing instead of silently switching you to the other.
      if (tool != ms::Tool::Dig) {
        tool = ms::Tool::Dig;
        requestUpdate();
      }
      return;

    case mineui::ActionPickFlag:
      if (tool != ms::Tool::Flag) {
        tool = ms::Tool::Flag;
        requestUpdate();
      }
      return;

    case mineui::ActionHowToNext:
      if (howToPage + 1 < mineui::howToPages()) {
        ++howToPage;
        requestUpdate();
        return;
      }
      goTo(ms::Screen::Menu);
      return;

    case mineui::ActionAgain:
      beginGame();
      return;

    case mineui::ActionDone:
      goTo(ms::Screen::Menu);
      return;

    default:
      return;
  }
}

void MinesweeperActivity::render(RenderLock&&) {
  namespace fui = freeink::ui;

  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, device, noInput, interactions);
  toybox::Screen surface(frame);

  switch (screen) {
    case ms::Screen::Menu: {
      mineui::MenuModel model;
      model.selected = menuSelected;
      model.hasHistory = hasHistory;
      model.lastBoard = lastBoard;
      model.wins = wins;
      model.losses = losses;
      mineui::buildMenu(surface, model);
      break;
    }
    case ms::Screen::HowTo: {
      mineui::HowToModel model;
      model.page = howToPage;
      mineui::buildHowTo(surface, model);
      break;
    }
    case ms::Screen::Board: {
      mineui::BoardModel model;
      model.game = game;
      model.tool = tool;
      model.showMines = ms::over(game);
      mineui::buildBoard(surface, model);
      break;
    }
    case ms::Screen::Result: {
      mineui::ResultModel model;
      model.won = game.status == ms::Status::Won;
      int revealed = 0;
      int flagsRight = 0;
      for (int column = 0; column < ms::kColumns; ++column) {
        for (int row = 0; row < ms::kRows; ++row) {
          const uint8_t cell = game.cell[column][row];
          if ((cell & ms::kRevealed) && !(cell & ms::kMine)) ++revealed;
          if ((cell & ms::kFlagged) && (cell & ms::kMine)) ++flagsRight;
        }
      }
      model.revealed = revealed;
      model.flagsRight = flagsRight;
      mineui::buildResult(surface, model);
      break;
    }
  }

  interactionsReady = true;
  toybox::reportOverflow(interactions, "Minesweeper");

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

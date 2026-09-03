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

// How long a finger must rest on a cell to plant a flag. Long enough that a
// deliberate dig never trips it, short enough that it does not feel broken on a
// panel whose own refresh is half a second.
constexpr unsigned long kFlagHoldMs = 400;
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
  holdColumn = -1;
  holdRow = -1;
  holdFired = false;
  flagMode = false;
  resultRecorded = false;
  goTo(ms::Screen::Board);
}

// What a tap on the grid means. Two things decide it and neither is in the
// interaction table.
//
// `screen`, because the grid geometry is only live on the board while the
// panel may still be showing the menu -- whose PLAY row sits exactly where a
// cell now is, so the tap that started the game would also dig.
//
// `flagMode`, because the FLAG capsule is registered with an identical rect,
// action, value and inputMask and flips only StateSelected, which the digest
// ignores as paint. The same cell digs or flags with nothing in the table to
// say which, and that is the whole worked example.
//
// Deliberately NOT here: the board itself. Revealing a cell repaints, and
// folding the position in would gate the next dig for a full refresh, which is
// the frozen-device failure. A change to the board is also always the player's
// own last tap -- there is no opponent here to move it underneath them.
uint32_t MinesweeperActivity::surfaceMeaning() const {
  const uint32_t withScreen = paintclock::mixMeaning(paintclock::kMeaningSeed, static_cast<uint32_t>(screen));
  const uint32_t withMode = paintclock::mixMeaning(withScreen, flagMode ? 1u : 0u);
  // The live/dead bit: a settled board keeps its pixels and stops accepting
  // moves, so the surface changes meaning without moving.
  return paintclock::mixMeaning(withMode, ms::over(game) ? 1u : 0u);
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

  // The rules decide when it is over; the record is written the moment they
  // do (latched by resultRecorded). The screen does NOT change: the settled
  // board stays, mines bared, wearing the verdict as its capsule. Navigating
  // away here was what made every ending anticlimactic -- the finished
  // minefield flashed for under a repaint and was gone.
  if (screen == ms::Screen::Board && ms::over(game)) recordResult();

  // Hold to flag, tap to dig.
  //
  // The grid is not in the interaction buffer -- eighty cells against a
  // twenty-four slot buffer -- so neither the tap nor the hold can go through
  // FreeInkUI's TouchHoldRouter, which routes against that buffer. Both are
  // done here against the same geometry that drew the pixels.
  //
  // swallowCurrentTouch() is the piece that makes a hold usable: it exists
  // precisely so a long press can fire while the finger is still down without
  // the ensuing lift also arriving as a tap. Without it, planting a flag would
  // immediately dig the cell it was planted on.
  if (screen == ms::Screen::Board) {
    const fui::DeviceContext device = toybox::makeTarget(renderer).deviceContext();
    int hx = 0;
    int hy = 0;
    int column = 0;
    int row = 0;
    if (mappedInput.isScreenTouchHeld(hx, hy) && mineui::cellAt(device, hx, hy, column, row)) {
      if (!surfaceRevealed()) {
        // Void the contact instead of just ignoring it. Leaving the latch
        // running would let the hold fire the instant the gate opens, planting
        // a flag against a board the player had not seen yet -- the timer
        // would already have expired against the screen underneath.
        holdColumn = -1;
        holdRow = -1;
        holdFired = false;
        return;
      }
      if (column != holdColumn || row != holdRow) {
        // Moved to another cell: the hold starts again there, so sliding a
        // finger across the board never plants a flag you did not mean.
        holdColumn = column;
        holdRow = row;
        holdSinceMs = millis();
        holdFired = false;
        requestUpdate();
      } else if (!holdFired && millis() - holdSinceMs >= kFlagHoldMs) {
        // Still a flag, in both modes, rather than "whatever the mode is not".
        // The symmetry was tempting and is a trap: in FLAG mode it would make
        // a held finger dig, and digging is the move that can end the game.
        // A hold you did not mean should never cost more than an extra tap to
        // undo. In FLAG mode it simply agrees with the tap.
        holdFired = true;
        if (ms::toggleFlag(game, column, row)) requestUpdate();
        mappedInput.swallowCurrentTouch();
      }
      return;
    }
    if (holdColumn >= 0) {
      holdColumn = -1;
      holdRow = -1;
      requestUpdate();
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

  if (screen == ms::Screen::Board) {
    const fui::DeviceContext device = toybox::makeTarget(renderer).deviceContext();
    int column = 0;
    int row = 0;
    if (mineui::cellAt(device, tapX, tapY, column, row)) {
      // Guarded here rather than around the whole branch so a tap that misses
      // the grid still reaches route(), which has its own digest gate for the
      // chrome. See Activity::surfaceMeaning().
      if (!surfaceRevealed()) return;
      // The rules decide legality, not the screen and not here.
      const bool changed = flagMode ? ms::toggleFlag(game, column, row) : ms::reveal(game, column, row);
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

    case mineui::ActionToggleMode:
      flagMode = !flagMode;
      requestUpdate();
      return;

    case mineui::ActionSeeResult:
      goTo(ms::Screen::Result);
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
      model.holdColumn = holdColumn;
      model.holdRow = holdRow;
      model.showMines = ms::over(game);
      model.flagMode = flagMode;
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

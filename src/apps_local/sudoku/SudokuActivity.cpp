#include "SudokuActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"

namespace sk = sudoku;

namespace {
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
constexpr char kStatePath[] = "/.crosspoint/sudoku.sav";
// Bumped when the layout of the line below changes. An older file is discarded
// rather than misread.
constexpr int kStateVersion = 1;
// Eighteen small integers, then 81 + 81 digits and 81 three-digit hex masks.
constexpr int kStateBytes = 768;
#endif

// How long a finger rests on a cell before it pencils. The same 400ms
// Minesweeper uses to plant a flag, deliberately: this is the device's one
// two-gesture idiom and two apps disagreeing about its timing would make it
// feel broken in whichever one the player met second.
constexpr unsigned long kPencilHoldMs = 400;

sk::Level nextLevel(const sk::Level level) {
  return static_cast<sk::Level>((static_cast<int>(level) + 1) % sk::kLevelCount);
}
}  // namespace

std::unique_ptr<Activity> SudokuActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<SudokuActivity>(renderer, mappedInput);
}

void SudokuActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  screen = sk::Screen::Menu;
  menuSelected = -1;
  notice = nullptr;
  lastTickMs = millis();
  // The only entropy that differs between two boots, and it enters here and
  // nowhere deeper: the rules take a seed and never reach for a clock, which is
  // what keeps a puzzle reproducible from one.
  rng = static_cast<uint32_t>(millis()) * 2654435761u + 1u;
  loadState();
  requestUpdate();
}

void SudokuActivity::onExit() {
  // Runs on sleep as well as on leaving, which is the case that matters: the
  // player puts the device down by doing nothing at all.
  saveState();
  Activity::onExit();
}

void SudokuActivity::goTo(const sk::Screen next) {
  screen = next;
  notice = nullptr;
  requestUpdate();
}

void SudokuActivity::beginGame() {
  generating = true;
  generateDeferred = true;
  generatingLevel = menuLevel;
  resultRecorded = false;
  newBest = false;
  holdCell = sk::kNoCell;
  holdFired = false;
  goTo(sk::Screen::Board);
}

void SudokuActivity::tickClock() {
  const unsigned long now = millis();
  const unsigned long since = now - lastTickMs;
  lastTickMs = now;
  if (screen != sk::Screen::Board || generating || game.solvedFlag != 0 || !hasGame) return;
  // No clock is drawn while solving: a running timer on a panel that takes
  // 300ms to repaint would spend the whole battery telling you what time it is.
  // It is recorded and shown once, at the end.
  game.elapsedMs += static_cast<uint32_t>(since);
}

void SudokuActivity::takeHint() {
  // A digit that disagrees with the answer but clashes with nothing yet is the
  // one thing the board deliberately will not tell you, so it is the first
  // thing the hint checks: any deduction made past a wrong digit is a lie.
  const int wrong = sk::firstWrong(game);
  if (wrong != sk::kNoCell) {
    game.hintCell = static_cast<uint8_t>(wrong);
    game.hintDigit = 0;
    notice = "WRONG DIGIT";
    ++game.hintsUsed;
    requestUpdate();
    return;
  }

  uint8_t board[sk::kCells];
  for (int cell = 0; cell < sk::kCells; ++cell) board[cell] = sk::valueAt(game, cell);
  const sk::Hint hint = sk::nextHint(board, sk::ceilingFor(sk::Level::Expert));
  if (!hint.found) {
    notice = "NOTHING YET";
    requestUpdate();
    return;
  }
  // The cell and the rule, never the digit. Mario picked hints that name what
  // you can solve and what proves it; one that filled the cell in would be a
  // solve button wearing a different label.
  game.hintCell = static_cast<uint8_t>(hint.cell);
  game.hintDigit = static_cast<uint8_t>(hint.digit);
  game.hintTechnique = static_cast<uint8_t>(hint.technique);
  ++game.hintsUsed;
  notice = sk::techniqueName(hint.technique);
  requestUpdate();
}

void SudokuActivity::recordResult() {
  if (resultRecorded) return;
  resultRecorded = true;
  const int index = static_cast<int>(game.puzzle.level);
  previousBestMs = record.bestMs[index];
  sk::recordSolve(record, game.puzzle.level, game.elapsedMs, game.hintsUsed);
  newBest = game.hintsUsed == 0 && record.bestMs[index] == game.elapsedMs && previousBestMs != game.elapsedMs;
  saveState();
}

void SudokuActivity::loadState() {
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
  if (!Storage.exists(kStatePath)) return;
  auto buffer = makeUniqueNoThrow<char[]>(kStateBytes);
  if (!buffer) {
    LOG_ERR("SUDOKU", "OOM: %d bytes for the save", kStateBytes);
    return;
  }
  std::memset(buffer.get(), 0, kStateBytes);
  if (Storage.readFileToBuffer(kStatePath, buffer.get(), kStateBytes) == 0) return;

  // Parsed into locals and committed only at the end: a truncated file leaves a
  // fresh app rather than half a puzzle.
  constexpr int kHeaderCount = 18;
  long header[kHeaderCount] = {};
  char* cursor = buffer.get();
  for (int i = 0; i < kHeaderCount; ++i) {
    char* next = nullptr;
    const long value = strtol(cursor, &next, 10);
    if (next == cursor) return;
    header[i] = value;
    cursor = next;
  }
  if (header[0] != kStateVersion) return;

  // Three fixed-width runs: the clues, the player's digits, their marks.
  auto takeRun = [&cursor](char* out, const int length) {
    while (*cursor == ' ' || *cursor == '\n' || *cursor == '\r') ++cursor;
    for (int i = 0; i < length; ++i) {
      if (cursor[i] == '\0') return false;
      out[i] = cursor[i];
    }
    cursor += length;
    return true;
  };
  char clues[sk::kCells + 1] = {};
  char entries[sk::kCells + 1] = {};
  char marks[sk::kCells * 3 + 1] = {};
  if (!takeRun(clues, sk::kCells) || !takeRun(entries, sk::kCells) || !takeRun(marks, sk::kCells * 3)) return;

  sk::Puzzle puzzle;
  for (int cell = 0; cell < sk::kCells; ++cell) {
    const char c = clues[cell];
    if (c < '0' || c > '9') return;
    puzzle.given[cell] = static_cast<uint8_t>(c - '0');
  }

  // The answer is not saved, it is re-derived. The puzzle has exactly one
  // solution, so the clues determine it, and a save that cannot be solved is a
  // corrupt save rather than a puzzle with a surprising answer. This is also
  // what makes the file independent of the generator: a later generator changes
  // nothing about how an old grid reads.
  sk::Grid grid;
  if (!sk::load(grid, puzzle.given)) return;
  const sk::SolveReport report = sk::solve(grid, sk::ceilingFor(sk::Level::Expert));
  if (!report.solved || report.broken) return;
  for (int cell = 0; cell < sk::kCells; ++cell) puzzle.solution[cell] = grid.value[cell];
  puzzle.hardest = report.hardest;
  puzzle.level = sk::levelOf(report.hardest);
  puzzle.clues = 0;
  for (int cell = 0; cell < sk::kCells; ++cell) {
    if (puzzle.given[cell] != 0) ++puzzle.clues;
  }

  sk::Game restored{};
  restored.puzzle = puzzle;
  for (int cell = 0; cell < sk::kCells; ++cell) {
    const char c = entries[cell];
    if (c < '0' || c > '9') return;
    restored.entry[cell] = puzzle.given[cell] != 0 ? 0 : static_cast<uint8_t>(c - '0');
    char hex[4] = {marks[cell * 3], marks[cell * 3 + 1], marks[cell * 3 + 2], '\0'};
    restored.note[cell] = static_cast<sk::Mask>(strtol(hex, nullptr, 16) & sk::kAllDigits);
  }

  int at = 1;
  // Range-checked rather than reduced: header[] is a long, and a negative one
  // survives `% kLevelCount` as a negative, which becomes Level(255) and indexes
  // record.bestMs[255] off the end of a four-element array when the menu draws.
  const long savedLevel = header[at++];
  menuLevel = static_cast<sk::Level>(savedLevel >= 0 && savedLevel < sk::kLevelCount ? savedLevel : 0);
  restored.elapsedMs = static_cast<uint32_t>(header[at++]);
  restored.hintsUsed = static_cast<uint8_t>(header[at++]);
  const long armed = header[at++];
  restored.armed = static_cast<uint8_t>(armed >= 1 && armed <= sk::kSize ? armed : 1);
  const long hintCell = header[at++];
  restored.hintCell = static_cast<uint8_t>(hintCell >= 0 && hintCell < sk::kCells ? hintCell : sk::kNoCell);
  restored.hintDigit = static_cast<uint8_t>(header[at++]);
  restored.solvedFlag = header[at++] != 0 ? 1 : 0;
  const bool savedHasGame = header[at++] != 0;
  for (int i = 0; i < sk::kLevelCount; ++i) record.solved[i] = static_cast<uint16_t>(header[at++]);
  for (int i = 0; i < sk::kLevelCount; ++i) record.bestMs[i] = static_cast<uint32_t>(header[at++]);
  record.hintsTaken = static_cast<uint16_t>(header[at++]);

  game = restored;
  hasGame = savedHasGame;
  resultRecorded = game.solvedFlag != 0;

  // The menu opens on the level of the puzzle it is offering to resume, which
  // is a repair as much as a preference. `menuLevel` is a stored field and
  // `puzzle.level` is re-derived from the clues above, so a card written while
  // the player was merely BROWSING the difficulty row holds the two disagreeing
  // -- and the door reads what they say, so it would offer NEW PUZZLE over a
  // good grid. A level picked and never played is not worth carrying across a
  // restart; a half-solved puzzle is.
  if (hasGame && game.solvedFlag == 0) menuLevel = game.puzzle.level;
#endif
}

void SudokuActivity::saveState() {
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
  auto buffer = makeUniqueNoThrow<char[]>(kStateBytes);
  if (!buffer) {
    LOG_ERR("SUDOKU", "OOM: %d bytes for the save", kStateBytes);
    return;
  }
  int used =
      std::snprintf(buffer.get(), kStateBytes, "%d %d %u %d %d %d %d %d %d", kStateVersion, static_cast<int>(menuLevel),
                    game.elapsedMs, game.hintsUsed, game.armed, game.hintCell >= sk::kCells ? -1 : game.hintCell,
                    game.hintDigit, game.solvedFlag, hasGame ? 1 : 0);
  for (int i = 0; i < sk::kLevelCount && used > 0 && used < kStateBytes; ++i) {
    used += std::snprintf(buffer.get() + used, static_cast<size_t>(kStateBytes - used), " %d", record.solved[i]);
  }
  for (int i = 0; i < sk::kLevelCount && used > 0 && used < kStateBytes; ++i) {
    used += std::snprintf(buffer.get() + used, static_cast<size_t>(kStateBytes - used), " %u", record.bestMs[i]);
  }
  if (used > 0 && used < kStateBytes) {
    used += std::snprintf(buffer.get() + used, static_cast<size_t>(kStateBytes - used), " %d\n", record.hintsTaken);
  }
  if (used <= 0 || used + 2 * sk::kCells + 3 * sk::kCells + 4 >= kStateBytes) {
    LOG_ERR("SUDOKU", "Save did not fit %d bytes", kStateBytes);
    return;
  }

  for (int cell = 0; cell < sk::kCells; ++cell) {
    buffer[used++] = static_cast<char>('0' + game.puzzle.given[cell]);
  }
  buffer[used++] = '\n';
  for (int cell = 0; cell < sk::kCells; ++cell) {
    buffer[used++] = static_cast<char>('0' + (game.entry[cell] <= 9 ? game.entry[cell] : 0));
  }
  buffer[used++] = '\n';
  for (int cell = 0; cell < sk::kCells; ++cell) {
    static const char kHex[] = "0123456789ABCDEF";
    const sk::Mask mask = game.note[cell];
    buffer[used++] = kHex[(mask >> 8) & 0xF];
    buffer[used++] = kHex[(mask >> 4) & 0xF];
    buffer[used++] = kHex[mask & 0xF];
  }
  buffer[used++] = '\n';
  buffer[used] = '\0';
  Storage.writeFile(kStatePath, String(buffer.get()));
#endif
}

// Only two things move the pixel-to-effect map of the grid and the pad.
//
// `screen`, because the geometry is live only on the board while the panel may
// still be showing the menu underneath it, and `generating`, because a puzzle
// arriving an arbitrary number of passes later (see the generator poll in
// loop()) makes a live grid appear with no tap at all -- the one change here
// the player did not cause.
//
// `game.armed` is deliberately absent, and it is the whole reason this is not
// simply "everything the hit-test reads". Picking a digit off the pad and
// placing it in six cells is the app's core loop; the pad tap changes `armed`,
// so hashing it would drop the very next cell tap every time -- including the
// second half of picking a digit up off a clue. The cell contents are absent
// for the same reason: they change on every entry, and gating consecutive
// entries is the frozen-device failure. Neither moves which cell a pixel is.
uint32_t SudokuActivity::surfaceMeaning() const {
  const uint32_t withScreen = paintclock::mixMeaning(paintclock::kMeaningSeed, static_cast<uint32_t>(screen));
  return paintclock::mixMeaning(withScreen, generating ? 1u : 0u);
}

void SudokuActivity::loop() {
  namespace fui = freeink::ui;

  tickClock();

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (sk::leavesApp(screen)) {
      saveState();
      shelf::leave(renderer, mappedInput);
      return;
    }
    if (screen == sk::Screen::Board) {
      // Back off a board that is still being carved CANCELS the carve, and that
      // is not tidiness. The poll below does not look at `screen`, so an
      // abandoned carve kept running on the menu and landed through
      // startGame() -- replacing the saved game and rewriting the card from a
      // screen the player was only reading, with no tap at all. The window is
      // not a frame either: 24 attempts a pass carries roughly even odds at the
      // scarcer levels, so MAKING ONE can sit there for several passes.
      generating = false;
      generateDeferred = false;
      saveState();
    }
    goTo(sk::back(screen));
    return;
  }

  // Carving runs one pass after the frame that announced it, so MAKING ONE is
  // on the panel before the main loop disappears for a while. The budget is per
  // pass rather than per puzzle: at the scarcer levels this can take a few
  // dozen attempts, and spending them all in one pass is what a freeze is.
  if (generating) {
    if (generateDeferred) {
      generateDeferred = false;
      return;
    }
    sk::Puzzle puzzle;
    if (sk::generate(puzzle, generatingLevel, work, rng, 24)) {
      sk::startGame(game, puzzle);
      hasGame = true;
      generating = false;
      saveState();
      requestUpdate();
    }
    return;
  }

  // The rules decide when it is over and the record is written the moment they
  // do. The SCREEN does not change: the finished grid stays, wearing SOLVED as
  // its capsule, and that capsule is the door to the stats.
  if (screen == sk::Screen::Board && game.solvedFlag != 0) recordResult();

  // Hold to pencil, tap to write.
  //
  // Neither the grid nor the pad is in the interaction buffer -- ninety
  // regions against twenty-four slots -- so both are hit-tested here against
  // the same geometry that drew them. swallowCurrentTouch() is what makes a
  // hold usable: it exists so a long press can fire while the finger is still
  // down without the lift arriving as a tap as well, which would otherwise
  // write a digit into the cell you just pencilled.
  if (screen == sk::Screen::Board && !generating) {
    const fui::DeviceContext device = toybox::makeTarget(renderer).deviceContext();
    int holdX = 0;
    int holdY = 0;
    int cell = 0;
    if (mappedInput.isScreenTouchHeld(holdX, holdY) && sudokuui::cellAt(device, holdX, holdY, cell)) {
      if (!surfaceRevealed()) {
        // Void the contact rather than leave its timer running against a grid
        // the panel has not shown: the hold would otherwise fire the instant
        // the gate opens, pencilling a cell the player never rested on.
        holdCell = sk::kNoCell;
        holdFired = false;
        return;
      }
      if (cell != holdCell) {
        // Moved to another cell, so the hold restarts there: dragging a finger
        // across the grid never pencils a cell you did not mean.
        holdCell = cell;
        holdSinceMs = millis();
        holdFired = false;
        requestUpdate();
      } else if (!holdFired && millis() - holdSinceMs >= kPencilHoldMs) {
        holdFired = true;
        notice = nullptr;
        sk::holdCell(game, cell);
        requestUpdate();
        mappedInput.swallowCurrentTouch();
      }
      return;
    }
    if (holdCell != sk::kNoCell) {
      holdCell = sk::kNoCell;
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

  if (screen == sk::Screen::Board) {
    const fui::DeviceContext device = toybox::makeTarget(renderer).deviceContext();
    int cell = 0;
    int digit = 0;
    if (sudokuui::cellAt(device, tapX, tapY, cell)) {
      // Guarded inside the branch, so a tap that misses grid and pad still
      // reaches route() and its own digest gate for the chrome.
      if (!surfaceRevealed()) return;
      notice = nullptr;
      sk::tapCell(game, cell);
      requestUpdate();
      return;
    }
    if (sudokuui::padKeyAt(device, tapX, tapY, digit)) {
      if (!surfaceRevealed()) return;
      notice = nullptr;
      game.armed = static_cast<uint8_t>(digit);
      requestUpdate();
      return;
    }
  }

  const fui::ActionEvent event = interactions.route(input);
  switch (event.action) {
    case sudokuui::ActionPlay:
      // Resuming and starting are the same door, because the difference is a
      // fact about the save rather than a choice the player should have to make.
      // Which one it is has exactly one definition, shared with the label the
      // player read before tapping: a second copy of it here is how a door does
      // something other than what it says.
      if (sk::canResume(game, hasGame, menuLevel)) {
        goTo(sk::Screen::Board);
        return;
      }
      beginGame();
      return;

    case sudokuui::ActionMenuRow:
      switch (static_cast<sudokuui::MenuRow>(event.value)) {
        case sudokuui::MenuRow::Level:
          // The row changes in place and the headline relabels itself. A
          // setting that threw the grid away and jumped to a new one would
          // apply before the player had seen it change.
          menuLevel = nextLevel(menuLevel);
          menuSelected = static_cast<int>(sudokuui::MenuRow::Level);
          requestUpdate();
          return;
        case sudokuui::MenuRow::HowTo:
          howToPage = 0;
          goTo(sk::Screen::HowTo);
          return;
        case sudokuui::MenuRow::Count:
          return;
      }
      return;

    case sudokuui::ActionHowToNext:
      if (howToPage + 1 < sudokuui::howToPages()) {
        ++howToPage;
        requestUpdate();
        return;
      }
      goTo(sk::Screen::Menu);
      return;

    case sudokuui::ActionUndo:
      if (sk::undoOnce(game)) {
        notice = nullptr;
        requestUpdate();
      }
      return;

    case sudokuui::ActionHint:
      if (game.solvedFlag == 0) takeHint();
      return;

    case sudokuui::ActionSeeResult:
      goTo(sk::Screen::Result);
      return;

    case sudokuui::ActionAgain:
      beginGame();
      return;

    case sudokuui::ActionDone:
      goTo(sk::Screen::Board);
      return;

    default:
      return;
  }
}

void SudokuActivity::render(RenderLock&&) {
  namespace fui = freeink::ui;

  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, device, noInput, interactions);
  toybox::Screen surface(frame);

  switch (screen) {
    case sk::Screen::Menu: {
      sudokuui::MenuModel model;
      model.level = menuLevel;
      model.hasGame = hasGame;
      model.game = game;
      model.record = record;
      model.selected = menuSelected;
      sudokuui::buildMenu(surface, model);
      break;
    }
    case sk::Screen::HowTo: {
      sudokuui::HowToModel model;
      model.page = howToPage;
      sudokuui::buildHowTo(surface, model);
      break;
    }
    case sk::Screen::Board: {
      sudokuui::BoardModel model;
      model.game = game;
      model.holdCell = holdCell;
      model.generating = generating;
      model.notice = notice;
      sudokuui::buildBoard(surface, model);
      break;
    }
    case sk::Screen::Result: {
      sudokuui::ResultModel model;
      model.game = game;
      model.level = game.puzzle.level;
      model.hardest = game.puzzle.hardest;
      model.elapsedMs = game.elapsedMs;
      model.bestMs = record.bestMs[static_cast<int>(game.puzzle.level)];
      model.newBest = newBest;
      model.hintsUsed = game.hintsUsed;
      model.clues = game.puzzle.clues;
      model.solvedAtThisLevel = record.solved[static_cast<int>(game.puzzle.level)];
      sudokuui::buildResult(surface, model);
      break;
    }
  }

  interactionsReady = true;
  toybox::reportOverflow(interactions, "Sudoku");

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

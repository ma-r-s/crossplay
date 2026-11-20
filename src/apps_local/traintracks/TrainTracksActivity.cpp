#include "TrainTracksActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>

#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "TrainTracksFallback.h"
#include "TrainTracksSave.h"
#include "TrainTracksSolver.h"

namespace tt = traintracks;

namespace {
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
constexpr char kStatePath[] = "/.crosspoint/traintracks.sav";
#endif
// Attempts per loop pass. A Hard attempt is tens of milliseconds on the
// ESP32-S3 by estimate, so a few per pass keeps the loop responsive.
constexpr int kAttemptsPerPass = 3;
// Past either limit, a bundled puzzle instead: New Game never fails.
constexpr uint16_t kMaxAttempts = 400;
constexpr unsigned long kBudgetMs = 10000;
}  // namespace

std::unique_ptr<Activity> TrainTracksActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<TrainTracksActivity>(renderer, mappedInput);
}

void TrainTracksActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  work = makeUniqueNoThrow<tt::GenWork>();
  if (!work) {
    LOG_ERR("TRACKS", "OOM: %d bytes for generator scratch", static_cast<int>(sizeof(tt::GenWork)));
    shelf::leave(renderer, mappedInput);
    return;
  }
  screen = tt::Screen::Menu;
  lastTickMs = millis();
  loadState();
  if (screen != tt::Screen::Board) goToFrontDoor();
  flashOnNextPaint = true;
  requestUpdate();
}

void TrainTracksActivity::onExit() {
  // Runs on sleep as well as on leaving: the player puts the device down by
  // doing nothing at all, and wake is a chip reset.
  if (work) {
    cancelGeneration();
    saveState();
  }
  work.reset();
  Activity::onExit();
}

bool TrainTracksActivity::handleHomeGesture() {
  cancelGeneration();
  return false;
}

void TrainTracksActivity::goTo(const tt::Screen next) {
  screen = next;
  notice = nullptr;
  requestUpdate();
}

void TrainTracksActivity::goToFrontDoor() {
  if (unfinished()) {
    goTo(tt::Screen::Menu);
    return;
  }
  enterNewGame(menuLevel);
}

// The new-puzzle screen, dealing a fresh preview at `level`.
void TrainTracksActivity::enterNewGame(const tt::Level level) {
  goTo(tt::Screen::NewGame);
  startPreview(level);
}

void TrainTracksActivity::startPreview(const tt::Level level) {
  menuLevel = level;
  previewReady = false;
  generating = true;
  generateDeferred = true;
  nextAttempt = 0;
  generateStartMs = millis();
  // The only entropy, entering here and nowhere deeper: the rules take a seed
  // and never reach for a clock, which keeps a puzzle reproducible from one.
  seed = static_cast<uint32_t>(millis()) * 2654435761u + 1u;
  requestUpdate();
}

void TrainTracksActivity::cancelGeneration() {
  generating = false;
  generateDeferred = false;
}

void TrainTracksActivity::stepGeneration() {
  for (int k = 0; k < kAttemptsPerPass; ++k) {
    tt::Generated made;
    const bool timedOut = millis() - generateStartMs > kBudgetMs || nextAttempt >= kMaxAttempts;
    bool ok;
    if (timedOut) {
      LOG_ERR("TRACKS", "Generation over budget after %u attempts; using a bundled puzzle", nextAttempt);
      ok = tt::loadFallback(menuLevel, static_cast<int>(seed % tt::kFallbackPerLevel), made, *work);
    } else {
      ok = tt::generateAttempt(menuLevel, seed, nextAttempt++, made, *work);
    }
    if (!ok && !timedOut) continue;
    generating = false;
    if (!ok) {
      LOG_ERR("TRACKS", "Bundled puzzle failed to regenerate");
      requestUpdate();
      return;
    }
    LOG_INF("TRACKS", "Puzzle ready: level %d seed %u attempt %u in %lu ms", static_cast<int>(menuLevel), made.seed,
            made.attempt, millis() - generateStartMs);
    preview = tt::Game{};
    preview.level = menuLevel;
    preview.seed = made.seed;
    preview.attempt = made.attempt;
    preview.puzzle = made.puzzle;
    tt::resetBoard(preview.board, preview.puzzle);
    previewReady = true;
    requestUpdate();
    return;
  }
}

// PLAY PUZZLE: the one place the saved game is replaced.
void TrainTracksActivity::playPreview() {
  if (!previewReady) return;
  game = preview;
  hasGame = true;
  previewReady = false;
  showWrong = false;
  hintCell = -1;
  lastTickMs = millis();
  flashOnNextPaint = true;
  goTo(tt::Screen::Board);
  saveState();
}

void TrainTracksActivity::tickClock() {
  const unsigned long now = millis();
  const unsigned long since = now - lastTickMs;
  lastTickMs = now;
  if (screen != tt::Screen::Board || !hasGame || game.solved) return;
  // Not drawn while solving: a running clock on this panel would spend the
  // battery telling you the time. It is shown once, when the track is done.
  game.elapsedMs += static_cast<uint32_t>(since);
}

// Every change to the board clears what CHECK and HINT left on it, then asks
// whether that change finished the puzzle.
void TrainTracksActivity::afterMove() {
  showWrong = false;
  hintCell = -1;
  notice = nullptr;
  if (tt::isWin(game.puzzle, game.board, work->solver)) {
    game.solved = true;
    flashOnNextPaint = true;
    saveState();
  }
  requestUpdate();
}

void TrainTracksActivity::check() {
  if (game.checks < 0xFFFF) ++game.checks;
  hintCell = -1;
  const int count = tt::checkBoard(game.puzzle, game.board, wrong);
  showWrong = count > 0;
  if (count == 0) {
    std::snprintf(noticeText, sizeof(noticeText), "NOTHING WRONG SO FAR");
  } else {
    std::snprintf(noticeText, sizeof(noticeText), "%d WRONG", count);
  }
  notice = noticeText;
  requestUpdate();
}

void TrainTracksActivity::hint() {
  const tt::Hint found = tt::findHint(game.puzzle, game.board, work->solver);
  if (!tt::applyHint(game, found)) return;
  if (game.hints < 0xFFFF) ++game.hints;
  afterMove();
  if (game.solved) return;
  hintCell = found.cell;
  switch (found.kind) {
    case tt::HintKind::Fixed:
      notice = "HINT: PUT ONE RIGHT";
      break;
    case tt::HintKind::Track:
      notice = "HINT: TRACK HERE";
      break;
    case tt::HintKind::Cross:
      notice = "HINT: NO TRACK HERE";
      break;
    case tt::HintKind::None:
      notice = nullptr;
      break;
  }
}

void TrainTracksActivity::loadState() {
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
  if (!Storage.exists(kStatePath)) return;
  auto buffer = makeUniqueNoThrow<char[]>(tt::kSaveBytes + 1);
  if (!buffer) {
    LOG_ERR("TRACKS", "OOM: %d bytes for the save", tt::kSaveBytes);
    return;
  }
  std::memset(buffer.get(), 0, tt::kSaveBytes + 1);
  if (Storage.readFileToBuffer(kStatePath, buffer.get(), tt::kSaveBytes + 1, tt::kSaveBytes) == 0) return;
  tt::SaveState state;
  state.menuLevel = menuLevel;
  // A save that does not hold together opens the front door without a word:
  // the difficulty is kept if it can be, the game is not.
  if (!tt::readSave(buffer.get(), state, *work)) {
    LOG_INF("TRACKS", "Save discarded");
    menuLevel = state.menuLevel;
    return;
  }
  menuLevel = state.menuLevel;
  hasGame = state.hasGame;
  if (hasGame) game = state.game;
  // Asleep on the board, or gone Home from it: straight back to it.
  if (state.onBoard && hasGame && !game.solved) screen = tt::Screen::Board;
#endif
}

void TrainTracksActivity::saveState() {
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
  auto buffer = makeUniqueNoThrow<char[]>(tt::kSaveBytes);
  if (!buffer) {
    LOG_ERR("TRACKS", "OOM: %d bytes for the save", tt::kSaveBytes);
    return;
  }
  tt::SaveState state;
  state.menuLevel = menuLevel;
  state.hasGame = hasGame;
  state.onBoard = screen == tt::Screen::Board && hasGame && !game.solved;
  state.game = game;
  if (tt::writeSave(state, buffer.get(), tt::kSaveBytes) == 0) {
    LOG_ERR("TRACKS", "Save did not fit %d bytes", tt::kSaveBytes);
    return;
  }
  Storage.writeFile(kStatePath, String(buffer.get()));
#endif
}

// What a grid tap means: which screen is live, and the grid's size, which is
// the whole of the pixel-to-square map. A deal landing on the new-puzzle
// screen changes what a tap on its preview does, so readiness is in it too.
uint32_t TrainTracksActivity::surfaceMeaning() const {
  uint32_t meaning = paintclock::mixMeaning(paintclock::kMeaningSeed, static_cast<uint32_t>(screen));
  meaning = paintclock::mixMeaning(meaning, previewReady ? 1u : 0u);
  return paintclock::mixMeaning(meaning, hasGame ? game.puzzle.size : 0u);
}

void TrainTracksActivity::loop() {
  namespace fui = freeink::ui;

  tickClock();

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (tt::leavesApp(screen, unfinished())) {
      saveState();
      shelf::leave(renderer, mappedInput);
      return;
    }
    const tt::Screen from = screen;
    const tt::Screen to = tt::back(from, unfinished());
    if (to == tt::Screen::NewGame && from != tt::Screen::HowTo) {
      // Off a finished board: the same difficulty again to start with.
      enterNewGame(from == tt::Screen::Board && hasGame ? game.level : menuLevel);
    } else {
      // From the how-to the preview it was opened over is still there.
      goTo(to);
    }
    saveState();
    return;
  }

  // The preview is laid only while its screen is up; input still runs, so a
  // difficulty can be changed or the screen left mid-deal.
  if (generating && screen == tt::Screen::NewGame) {
    if (generateDeferred) {
      generateDeferred = false;
    } else {
      stepGeneration();
    }
  }

  // The side keys step: through history on the board, through pages in the
  // how-to. They have no "where", so stepping is all a key does here
  // (docs/buttons.md).
  const bool up = mappedInput.wasReleased(MappedInputManager::Button::Up);
  const bool down = mappedInput.wasReleased(MappedInputManager::Button::Down);
  if (up || down) {
    if (screen == tt::Screen::Board && hasGame && !game.solved) {
      if ((up ? tt::undo(game.board) : tt::redo(game.board)) >= 0) afterMove();
    } else if (screen == tt::Screen::HowTo) {
      const int next = howToPage + (down ? 1 : -1);
      if (next >= 0 && next < traintracksui::howToPages()) {
        howToPage = next;
        requestUpdate();
      }
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
  if (!input.touchReleased || !interactionsReady) return;

  if (screen == tt::Screen::Board && hasGame) {
    const fui::DeviceContext device = toybox::makeTarget(renderer).deviceContext();
    const traintracksui::Geometry g = traintracksui::boardGeometry(device, game.puzzle.size);
    int row = 0;
    int col = 0;
    if (traintracksui::cellAt(g, tapX, tapY, row, col)) {
      // Guarded inside the branch, so a tap that misses the grid still reaches
      // route() and its own gate for the controls.
      if (!surfaceRevealed() || game.solved) return;
      if (tt::tapCell(game.board, game.puzzle, row, col)) afterMove();
      return;
    }
  }

  const fui::ActionEvent event = interactions.route(input);
  switch (event.action) {
    case traintracksui::ActionPlay:
      if (unfinished()) goTo(tt::Screen::Board);
      return;
    case traintracksui::ActionNewGame: {
      // Off a finished board, the same difficulty again to start with.
      enterNewGame(screen == tt::Screen::Board && hasGame ? game.level : menuLevel);
      return;
    }
    case traintracksui::ActionLevel:
      // A different difficulty deals again; the one already shown keeps its deal.
      if (event.value >= 0 && event.value < tt::kLevelCount && static_cast<tt::Level>(event.value) != menuLevel) {
        startPreview(static_cast<tt::Level>(event.value));
      }
      return;
    case traintracksui::ActionPlayPreview:
      playPreview();
      return;
    case traintracksui::ActionHowTo:
      howToPage = 0;
      goTo(tt::Screen::HowTo);
      return;
    case traintracksui::ActionHowToNext:
      if (howToPage + 1 < traintracksui::howToPages()) {
        ++howToPage;
        requestUpdate();
        return;
      }
      goTo(tt::Screen::NewGame);
      return;
    case traintracksui::ActionUndo:
      if (!game.solved && tt::undo(game.board) >= 0) afterMove();
      return;
    case traintracksui::ActionRedo:
      if (!game.solved && tt::redo(game.board) >= 0) afterMove();
      return;
    case traintracksui::ActionCheck:
      if (hasGame && !game.solved) check();
      return;
    case traintracksui::ActionHint:
      if (hasGame && !game.solved) hint();
      return;
    case traintracksui::ActionMenu:
      goTo(tt::Screen::Menu);
      saveState();
      return;
    default:
      return;
  }
}

void TrainTracksActivity::render(RenderLock&&) {
  namespace fui = freeink::ui;

  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, device, noInput, interactions);
  toybox::Screen surface(frame);

  switch (screen) {
    case tt::Screen::Menu: {
      traintracksui::MenuModel model;
      model.game = unfinished() ? &game : nullptr;
      traintracksui::buildMenu(surface, model);
      break;
    }
    case tt::Screen::NewGame: {
      traintracksui::NewGameModel model;
      model.selected = menuLevel;
      model.preview = previewReady ? &preview : nullptr;
      traintracksui::buildNewGame(surface, model);
      break;
    }
    case tt::Screen::HowTo: {
      traintracksui::HowToModel model;
      model.page = howToPage;
      traintracksui::buildHowTo(surface, model);
      break;
    }
    case tt::Screen::Board: {
      traintracksui::BoardModel model;
      model.puzzle = &game.puzzle;
      model.board = &game.board;
      model.level = game.level;
      model.solved = game.solved;
      model.elapsedMs = game.elapsedMs;
      model.hints = game.hints;
      model.checks = game.checks;
      model.notice = notice;
      model.wrong = showWrong ? wrong : nullptr;
      model.hintCell = hintCell;
      traintracksui::buildBoard(surface, model);
      break;
    }
  }

  interactionsReady = true;
  toybox::reportOverflow(interactions, "Train Tracks");
  renderer.displayBuffer(flashOnNextPaint ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
  flashOnNextPaint = false;
}

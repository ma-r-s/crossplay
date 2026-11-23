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
// Attempts per loop pass. One: a Hard attempt measured 0.4 to 1.5s on the
// Picco, and three in a pass held the loop for 4.1s, close to the watchdog and
// deaf to taps throughout.
constexpr int kAttemptsPerPass = 1;
// Past either limit, a bundled puzzle instead: New Game never fails.
constexpr uint16_t kMaxAttempts = 400;
constexpr unsigned long kBudgetMs = 10000;
// A hold that crosses a square. 400ms, as Sudoku's pencil and Minesweeper's
// flag: the device's one two-gesture idiom must time the same everywhere.
constexpr unsigned long kHoldMs = 400;
// When a resting finger is first drawn as a hold. Taps on the Picco took 140
// to 250ms from touch to lift, so a frame drawn sooner would repaint on most
// taps for nothing and delay the tap's own frame behind it.
constexpr unsigned long kHoldShowMs = 250;
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

// A different screen replaces everything on the panel, and a fast refresh over
// that much change leaves the last screen's lines as ghosts, which on a puzzle
// read as track that is not there. So every screen change, every how-to page
// and every new preview earns a full refresh; a move on the board, which
// changes a square or two, stays fast.
void TrainTracksActivity::goTo(const tt::Screen next) {
  // A deal belongs to the new-puzzle screen. The how-to is opened over it and
  // returns to it, so a deal pauses there, its time budget restarting on the
  // way back; any other screen drops it.
  if (next == tt::Screen::NewGame && screen == tt::Screen::HowTo && generating) {
    generateStartMs = millis();
  } else if (next != tt::Screen::NewGame && next != tt::Screen::HowTo) {
    cancelGeneration();
    dealLanded = false;
  }
  // A hold belongs to the board it began on.
  heldCell = -1;
  holdFired = false;
  holdShown = false;
  screen = next;
  notice = nullptr;
  flashOnNextPaint = true;
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
  LOG_DBG("TRACKS", "Dealing level %d", static_cast<int>(level));
  menuLevel = level;
  previewReady = false;
  dealLanded = false;
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
    landed = made;
    dealLanded = true;
    publishDeal();
    return;
  }
}

// Publishes a finished deal under the render lock, so a frame already being
// drawn shows the old preview whole and the next one the new preview whole,
// and the full refresh it asks for cannot be spent on the frame before it.
//
// Only TRIES the lock. The render task holds it through the panel refresh,
// 730ms for a full one on the Picco, and a loop blocked that long reads no
// taps: blocking here is what made HOW TO PLAY seem dead after a deal. A deal
// that cannot be published yet waits in `landed` for a later pass.
bool TrainTracksActivity::publishDeal() {
  if (!dealLanded) return true;
  RenderLock lock(RenderLock::Mode::Try);
  if (!lock.ownsLock()) return false;
  preview = tt::Game{};
  preview.level = menuLevel;
  preview.seed = landed.seed;
  preview.attempt = landed.attempt;
  preview.puzzle = landed.puzzle;
  tt::resetBoard(preview.board, preview.puzzle);
  previewReady = true;
  dealLanded = false;
  flashOnNextPaint = true;  // the last preview, maybe another size, must not ghost
  lock.unlock();
  requestUpdate();
  return true;
}

// PLAY PUZZLE: the one place the saved game is replaced.
void TrainTracksActivity::playPreview() {
  if (!previewReady) return;
  game = preview;
  hasGame = true;
  previewReady = false;
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

// Every change to the board: the hint's mark and the status line answer only
// the move they were about, so they go; CHECK's flags stay until the next CHECK
// or the solve. Then, did that change finish the puzzle?
void TrainTracksActivity::afterMove() {
  hintCell = -1;
  notice = nullptr;
  touchSave();
  tt::settle(game.board, game.puzzle);
  if (tt::isSolved(game.puzzle, game.board, work->solver)) {
    tt::tidySolved(game.board, game.puzzle);
    game.solved = true;
    for (uint8_t& flag : game.marked) flag = 0;
    heldCell = -1;
    holdShown = false;
    flashOnNextPaint = true;
    saveState();
  }
  requestUpdate();
}

void TrainTracksActivity::check() {
  if (game.checks < 0xFFFF) ++game.checks;
  touchSave();
  hintCell = -1;
  const int count = tt::checkBoard(game.puzzle, game.board, game.marked);
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
  if (tt::hintSolves(game, found, hintProbe, work->solver)) {
    // The last move is the player's. Not counted: no help was given, and
    // nothing on the board changed, so CHECK's marks stay where they are.
    hintCell = -1;
    notice = "LAST MOVE IS YOURS";
    requestUpdate();
    return;
  }
  if (!tt::applyHint(game, found)) return;
  if (game.hints < 0xFFFF) ++game.hints;
  // The hinted square is right now, so its CHECK flag has nothing left to say.
  game.marked[found.cell] = 0;
  afterMove();
  if (game.solved) return;
  hintCell = found.cell;
  // The square is marked on the board; the line says how much help so far.
  std::snprintf(noticeText, sizeof(noticeText), "%u%s HINT", static_cast<unsigned>(game.hints),
                tt::ordinalSuffix(game.hints));
  notice = noticeText;
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
  unsaved = false;
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
  if (!Storage.writeFile(kStatePath, String(buffer.get()))) LOG_ERR("TRACKS", "Save failed: %s", kStatePath);
#endif
}

void TrainTracksActivity::touchSave() {
  const unsigned long now = millis();
  if (!unsaved) unsavedSinceMs = now;
  unsaved = true;
  lastChangeMs = now;
}

void TrainTracksActivity::maybeSave() {
  if (!unsaved || heldCell >= 0) return;
  const unsigned long now = millis();
  if (now - lastChangeMs < kSaveQuietMs && now - unsavedSinceMs < kSaveMaxMs) return;
  saveState();
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
  maybeSave();

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (tt::leavesApp(screen, unfinished())) {
      // onExit saves.
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
  if (dealLanded && screen == tt::Screen::NewGame) publishDeal();
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
        flashOnNextPaint = true;
        requestUpdate();
      }
    }
    return;
  }

  // Hold to cross, tap to lay track. The board is not in the interaction
  // table, so a hold is read here against the geometry that drew it.
  // swallowCurrentTouch() is what makes it usable: the hold fires while the
  // finger is still down, and the lift must not then arrive as a tap too.
  if (screen == tt::Screen::Board && hasGame && !game.solved) {
    const fui::DeviceContext device = toybox::makeTarget(renderer).deviceContext();
    const traintracksui::Geometry g = traintracksui::boardGeometry(device, game.puzzle.size);
    int holdX = 0;
    int holdY = 0;
    int row = 0;
    int col = 0;
    if (mappedInput.isScreenTouchHeld(holdX, holdY) && traintracksui::cellAt(g, holdX, holdY, row, col)) {
      if (!surfaceRevealed()) {
        heldCell = -1;
        holdFired = false;
        return;
      }
      const int cell = tt::cellIndex(row, col);
      const unsigned long now = millis();
      if (cell != heldCell) {
        // Moved to another square: the hold restarts there, so dragging across
        // the board never crosses a square the finger did not rest on.
        if (holdShown) requestUpdate();
        heldCell = cell;
        holdSinceMs = now;
        holdFired = false;
        holdShown = false;
      } else if (!holdFired && now - holdSinceMs >= kHoldMs) {
        holdFired = true;
        holdShown = false;
        if (tt::holdCell(game.board, game.puzzle, row, col)) {
          afterMove();
        } else {
          requestUpdate();
        }
        mappedInput.swallowCurrentTouch();
      } else if (!holdFired && !holdShown && now - holdSinceMs >= kHoldShowMs) {
        holdShown = true;
        requestUpdate();
      }
      return;
    }
    if (heldCell >= 0) {
      if (holdShown) requestUpdate();
      heldCell = -1;
      holdShown = false;
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
  if (!input.touchReleased) return;
  if (!interactionsReady) {
    LOG_DBG("TRACKS", "Tap at %d,%d dropped: a frame was being built", tapX, tapY);
    return;
  }

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

  if (!interactions.routable()) {
    LOG_DBG("TRACKS", "Tap at %d,%d held: the panel has not shown this screen yet", tapX, tapY);
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
      // Any difficulty deals again, the one already chosen included: tapping it
      // is how to ask for a different puzzle at the same level.
      if (event.value >= 0 && event.value < tt::kLevelCount) startPreview(static_cast<tt::Level>(event.value));
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
        flashOnNextPaint = true;
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

  // Read before drawing, so a full refresh asked for while this frame is being
  // built belongs to the next frame, the one that shows what asked for it.
  const bool flash = flashOnNextPaint;
  flashOnNextPaint = false;

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
      model.wrong = game.marked;
      model.hintCell = hintCell;
      model.holdCell = holdShown ? heldCell : -1;
      traintracksui::buildBoard(surface, model);
      break;
    }
  }

  interactionsReady = true;
  toybox::reportOverflow(interactions, "Train Tracks");
  renderer.displayBuffer(flash ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
}

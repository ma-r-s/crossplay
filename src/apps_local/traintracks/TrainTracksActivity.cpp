#include "TrainTracksActivity.h"

#include <Logging.h>
#include <Memory.h>

#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "TrainTracksFallback.h"
#include "TrainTracksSolver.h"

namespace tt = traintracks;

namespace {
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
  beginGame(level);
}

void TrainTracksActivity::onExit() {
  work.reset();
  Activity::onExit();
}

void TrainTracksActivity::beginGame(const tt::Level next) {
  level = next;
  solved = false;
  generating = true;
  generateDeferred = true;
  nextAttempt = 0;
  generateStartMs = millis();
  // The only entropy, entering here and nowhere deeper: the rules take a seed
  // and never reach for a clock, which keeps a puzzle reproducible from one.
  seed = static_cast<uint32_t>(millis()) * 2654435761u + 1u;
  requestUpdate();
}

void TrainTracksActivity::stepGeneration() {
  for (int k = 0; k < kAttemptsPerPass; ++k) {
    tt::Generated made;
    const bool timedOut = millis() - generateStartMs > kBudgetMs || nextAttempt >= kMaxAttempts;
    bool ok;
    if (timedOut) {
      LOG_ERR("TRACKS", "Generation over budget after %u attempts; using a bundled puzzle", nextAttempt);
      ok = tt::loadFallback(level, static_cast<int>(seed % tt::kFallbackPerLevel), made, *work);
    } else {
      ok = tt::generateAttempt(level, seed, nextAttempt++, made, *work);
    }
    if (!ok && !timedOut) continue;
    if (!ok) {
      LOG_ERR("TRACKS", "Bundled puzzle failed to regenerate");
      return;
    }
    LOG_INF("TRACKS", "Puzzle ready: level %d seed %u attempt %u in %lu ms", static_cast<int>(level), made.seed,
            made.attempt, millis() - generateStartMs);
    puzzle = made.puzzle;
    tt::resetBoard(board, puzzle);
    generating = false;
    requestUpdate();
    return;
  }
}

// What a grid tap means: which screen is live, and the grid's size, which is
// the whole of the pixel-to-square map.
uint32_t TrainTracksActivity::surfaceMeaning() const {
  const uint32_t live = paintclock::mixMeaning(paintclock::kMeaningSeed, generating ? 1u : 0u);
  return paintclock::mixMeaning(live, generating ? 0u : puzzle.size);
}

void TrainTracksActivity::loop() {
  namespace fui = freeink::ui;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    shelf::leave(renderer, mappedInput);
    return;
  }

  if (generating) {
    if (generateDeferred) {
      generateDeferred = false;
      return;
    }
    stepGeneration();
    return;
  }

  // The side keys step through history; they have no "where", so they are the
  // one thing here a key may do (docs/buttons.md). Portrait, so Up is Up.
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    if (tt::undo(board) >= 0) requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (tt::redo(board) >= 0) requestUpdate();
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

  const fui::DeviceContext device = toybox::makeTarget(renderer).deviceContext();
  const traintracksui::Geometry g = traintracksui::boardGeometry(device, puzzle.size);
  int row = 0;
  int col = 0;
  if (traintracksui::cellAt(g, tapX, tapY, row, col)) {
    if (!surfaceRevealed() || solved) return;
    if (tt::tapCell(board, puzzle, row, col)) {
      solved = tt::isWin(puzzle, board, work->solver);
      requestUpdate();
    }
    return;
  }

  const fui::ActionEvent event = interactions.route(input);
  switch (event.action) {
    case traintracksui::ActionUndo:
      if (tt::undo(board) >= 0) requestUpdate();
      return;
    case traintracksui::ActionRedo:
      if (tt::redo(board) >= 0) requestUpdate();
      return;
    case traintracksui::ActionMenu:
      // No front door until Milestone 4: MENU deals a new puzzle for now.
      beginGame(level);
      return;
    case traintracksui::ActionCheck:
    case traintracksui::ActionHint:
      // Milestone 4.
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

  traintracksui::BoardModel model;
  model.puzzle = &puzzle;
  model.board = &board;
  model.level = level;
  model.generating = generating;
  model.solved = solved;
  traintracksui::buildBoard(surface, model);

  interactionsReady = true;
  toybox::reportOverflow(interactions, "Train Tracks");
  renderer.displayBuffer();
}

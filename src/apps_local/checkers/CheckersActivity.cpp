#include "CheckersActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdlib>

#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "CheckersBrain.h"
#include "CheckersScreens.h"

namespace ck = checkers;

std::unique_ptr<Activity> CheckersActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<CheckersActivity>(renderer, mappedInput);
}

void CheckersActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  screen = ck::Screen::Menu;
  menuSelected = -1;
  loadHistory();
  requestUpdate();
}

// Beside the reader's own state and the player's name. A fork-local fact in a
// fork-local file, the pattern knucklebones.sav set.
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
constexpr char kHistoryPath[] = "/.crosspoint/checkers.sav";
#endif

// wins losses draws outcome yourPieces theirPieces, then the 64 cells of the
// final position, always from the light perspective so the ornament draws your
// men nearest you whatever seat the game was played from.
constexpr int kHistoryValues = 6 + ck::kCells;

void CheckersActivity::loadHistory() {
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
  if (!Storage.exists(kHistoryPath)) return;
  char buffer[256] = {};
  if (Storage.readFileToBuffer(kHistoryPath, buffer, sizeof(buffer)) == 0) return;

  // Parsed into locals and committed only if the whole line is good, so a
  // truncated file leaves an empty menu rather than half a board. Losing this
  // costs an ornament, so it fails quietly.
  int values[kHistoryValues] = {};
  char* cursor = buffer;
  for (int i = 0; i < kHistoryValues; ++i) {
    char* next = nullptr;
    const long value = strtol(cursor, &next, 10);
    if (next == cursor) return;
    values[i] = static_cast<int>(value);
    cursor = next;
  }

  int at = 0;
  wins = values[at++];
  losses = values[at++];
  draws = values[at++];
  lastOutcome = values[at++];
  lastYourPieces = values[at++];
  lastTheirPieces = values[at++];
  for (int i = 0; i < ck::kCells; ++i) lastCells[i] = static_cast<uint8_t>(values[at++]);
  hasHistory = true;
#endif
}

void CheckersActivity::recordResult() {
  if (resultRecorded) return;
  resultRecorded = true;

  const ck::Outcome result = ck::outcome(game);
  const bool won = (seat == ck::kLight && result == ck::Outcome::LightWins) ||
                   (seat == ck::kDarkSeat && result == ck::Outcome::DarkWins);
  lastOutcome = 2;
  if (result != ck::Outcome::Draw) lastOutcome = won ? 0 : 1;
  if (result == ck::Outcome::Draw)
    ++draws;
  else if (won)
    ++wins;
  else
    ++losses;

  // The final position, rotated to the light perspective when this device
  // played dark, so the saved board always reads with your men nearest you.
  for (int i = 0; i < ck::kCells; ++i) {
    lastCells[i] = game.cell[seat == ck::kLight ? i : ck::kCells - 1 - i];
  }
  lastYourPieces = ck::pieceCount(game, seat);
  lastTheirPieces = ck::pieceCount(game, seat == ck::kLight ? ck::kDarkSeat : ck::kLight);
  hasHistory = true;

#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
  char line[256];
  int used = snprintf(line, sizeof(line), "%d %d %d %d %d %d", wins, losses, draws, lastOutcome, lastYourPieces,
                      lastTheirPieces);
  for (int i = 0; i < ck::kCells && used > 0 && used < static_cast<int>(sizeof(line)); ++i) {
    used += snprintf(line + used, sizeof(line) - used, " %d", lastCells[i]);
  }
  if (used <= 0 || used >= static_cast<int>(sizeof(line))) {
    LOG_ERR("CHECK", "History line did not fit %d bytes", static_cast<int>(sizeof(line)));
    return;
  }
  snprintf(line + used, sizeof(line) - used, "\n");
  Storage.writeFile(kHistoryPath, String(line));
#endif
}

void CheckersActivity::goTo(const ck::Screen next) {
  screen = next;
  requestUpdate();
}

void CheckersActivity::clearPick() {
  picked = ck::kNothingPicked;
  destinationCount = 0;
}

void CheckersActivity::beginSoloGame() {
  ck::start(game);
  seat = ck::kLight;
  resultRecorded = false;
  clearPick();
  goTo(ck::Screen::Board);
}

void CheckersActivity::takeOpponentTurn() {
  // One move per pass, not a loop to the end. The panel takes half a second to
  // repaint, so playing several of their moves before showing anything would
  // look like a freeze followed by a skip.
  if (ck::over(game) || game.turn == seat) return;
  ck::Move move{};
  if (!ck::chooseMove(game, move)) return;
  ck::play(game, move);
  requestUpdate();
}

const char* CheckersActivity::linkHeadline() const {
  if (linkPhase() == linkplay::PlayBase::Phase::Searching) return "LOOKING FOR A PLAYER";
  const ck::Outcome result = ck::outcome(game);
  if (result == ck::Outcome::Running) return "CHECKERS";
  if (result == ck::Outcome::Draw) return "A DRAW";
  const bool won = (seat == ck::kLight && result == ck::Outcome::LightWins) ||
                   (seat == ck::kDarkSeat && result == ck::Outcome::DarkWins);
  return won ? "YOU WIN" : "THEY WIN";
}

void CheckersActivity::onMatchStart(const bool goesFirst) {
  // start() puts light to move, so whoever goes first IS light. The follower
  // does not deal: the whole board travels on every move, so its first delivery
  // is the real one.
  seat = goesFirst ? ck::kLight : ck::kDarkSeat;
  resultRecorded = false;
  clearPick();
  // BOTH sides deal, unlike Knucklebones where only the first mover does.
  // Checkers has no randomness: the opening position is fixed, so start() is
  // identical on both devices and there is nothing to wait for.
  //
  // The follower used to start from a zeroed Game, and that was a serious bug
  // rather than a cosmetic one -- an empty board has no legal move, so over()
  // is true and outcome() reads DarkWins. The follower announced "YOU WIN"
  // before the leader had moved, and LinkActivity latched a rematch it never
  // cleared, so it never reached the board again for the rest of the match.
  ck::start(game);
  goTo(ck::Screen::Board);
}

bool CheckersActivity::takeOpponentState() {
  const bool took = play.takeOpponent(game);
  // Their move landing invalidates anything in hand: the piece you had picked
  // up may have just been captured.
  if (took) clearPick();
  return took;
}

void CheckersActivity::onRematch() { onMatchStart(play.goesFirst()); }

void CheckersActivity::onLinkEnded() {
  seat = ck::kLight;
  clearPick();
  goTo(ck::Screen::Menu);
}

// `seat` is in here because it is not a mode bit, it is the pixel-to-square
// map itself: checkui::squareAt takes it and flips the board, so the same
// pixel is a different square when the coin toss lands the other way. A
// rematch changes it (onMatchStart), and nothing about that is caused by the
// finger already on the glass.
//
// The live/dead bit is the one this app most needs. driveLink() runs ahead of
// gameLoop() on every pass and can hand the turn over, land an opponent's
// move, or end the match, all with no tap from this player -- and a board that
// was inert one pass ago starts accepting moves while the panel still shows
// the position before the opponent's move.
//
// `picked` is deliberately absent. Lifting a piece and tapping a destination
// is every move in the game, the lift repaints, and hashing the selection
// would eat the second tap of every one of them. It also does not move which
// square a pixel is, which is the test.
uint32_t CheckersActivity::surfaceMeaning() const {
  const uint32_t withScreen = paintclock::mixMeaning(paintclock::kMeaningSeed, static_cast<uint32_t>(screen));
  const uint32_t withSeat = paintclock::mixMeaning(withScreen, seat);
  const bool live = (inMatch() ? linkYourTurn() : game.turn == seat) && !ck::over(game);
  return paintclock::mixMeaning(withSeat, live ? 1u : 0u);
}

void CheckersActivity::gameLoop() {
  namespace fui = freeink::ui;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (inMatch()) {
      leaveLink();
      return;
    }
    if (ck::leavesApp(screen)) {
      shelf::leave(renderer, mappedInput);
      return;
    }
    goTo(ck::back(screen));
    return;
  }

  // HOW TO PLAY pages on the two side keys. They are the device's only physical
  // buttons and the case labels them previous and next page; a page of a
  // how-to is a page. NEXT stays tappable -- a button is never the only route.
  if (screen == ck::Screen::HowTo) {
    const bool forward = mappedInput.wasReleased(MappedInputManager::Button::Down);
    const bool backward = mappedInput.wasReleased(MappedInputManager::Button::Up);
    if (forward || backward) {
      const int pages = checkui::howToPages();
      howToPage = (howToPage + (forward ? 1 : pages - 1)) % pages;
      requestUpdate();
      return;
    }
  }

  if (screen == ck::Screen::Board) {
    if (inMatch()) {
      if (!linkYourTurn()) {
        if (takeOpponentState()) requestUpdate();
        return;
      }
    } else if (!ck::over(game) && game.turn != seat) {
      takeOpponentTurn();
      return;
    } else if (ck::over(game)) {
      recordResult();
      goTo(ck::Screen::Result);
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

  // Sixty-four squares against a twenty-four slot interaction buffer, so the
  // board is hit-tested from the geometry that drew it. Tried before the
  // registered controls, because it covers most of the screen.
  if (screen == ck::Screen::Board) {
    const fui::DeviceContext device = toybox::makeTarget(renderer).deviceContext();
    int file = 0;
    int rank = 0;
    if (checkui::squareAt(device, tapX, tapY, seat, file, rank)) {
      // Sixty-four squares do not fit the interaction table, so this tap never
      // reaches route(). See Activity::surfaceMeaning().
      if (!surfaceRevealed()) return;
      const int square = ck::indexOf(file, rank);
      const bool mine = inMatch() ? linkYourTurn() : game.turn == seat;
      if (!mine || ck::over(game)) return;

      if (picked != ck::kNothingPicked) {
        ck::Move move{};
        if (ck::moveBetween(game, picked, square, move)) {
          // The rules built this move; nothing here constructs one, so a
          // destination they never offered cannot be played.
          if (ck::play(game, move)) {
            clearPick();
            if (inMatch()) play.play(game);
            requestUpdate();
          }
          return;
        }
      }
      // Picking up, or picking a different piece. canPick asks the move list
      // rather than ownership, so a piece with no legal move -- common under
      // mandatory capture -- cannot be lifted at all.
      if (ck::canPick(game, square)) {
        picked = square;
        destinationCount = ck::destinations(game, square, destinations, takenMasks, ck::kMaxMoves);
        requestUpdate();
      } else if (picked != ck::kNothingPicked) {
        clearPick();
        requestUpdate();
      }
      // Tapping a piece that cannot move with nothing in hand changes nothing,
      // so it must not repaint. A refresh that leaves the screen identical is
      // worse than doing nothing: on e-ink the panel visibly blinks and comes
      // back the same, which is exactly what a bug looks like.
      return;
    }
  }

  const fui::ActionEvent event = interactions.route(input);
  switch (event.action) {
    case checkui::ActionMenuRow:
      switch (static_cast<checkui::MenuRow>(event.value)) {
        case checkui::MenuRow::Play:
          beginSoloGame();
          return;
        case checkui::MenuRow::PlayNearby:
          enterLink(linkplay::GameId::Checkers);
          return;
        case checkui::MenuRow::HowTo:
          howToPage = 0;
          goTo(ck::Screen::HowTo);
          return;
        case checkui::MenuRow::Count:
          return;
      }
      return;

    case checkui::ActionHowToNext:
      if (howToPage + 1 < checkui::howToPages()) {
        ++howToPage;
        requestUpdate();
        return;
      }
      goTo(ck::Screen::Menu);
      return;

    case checkui::ActionAgain:
      if (inMatch()) {
        proposeRematch();
        return;
      }
      beginSoloGame();
      return;

    case checkui::ActionDone:
      goTo(ck::Screen::Menu);
      return;

    default:
      return;
  }
}

void CheckersActivity::gameRender() {
  namespace fui = freeink::ui;

  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, device, noInput, interactions);
  toybox::Screen surface(frame);

  switch (screen) {
    case ck::Screen::Menu: {
      checkui::MenuModel model;
      model.selected = menuSelected;
      model.hasHistory = hasHistory;
      model.wins = wins;
      model.losses = losses;
      model.draws = draws;
      model.lastOutcome = lastOutcome;
      model.lastCells = lastCells;
      model.lastYourPieces = lastYourPieces;
      model.lastTheirPieces = lastTheirPieces;
      checkui::buildMenu(surface, model);
      break;
    }
    case ck::Screen::HowTo: {
      checkui::HowToModel model;
      model.page = howToPage;
      checkui::buildHowTo(surface, model);
      break;
    }
    case ck::Screen::Board: {
      checkui::BoardModel model;
      model.game = game;
      model.picked = picked;
      model.destinationCount = destinationCount;
      for (int i = 0; i < destinationCount; ++i) {
        model.destinations[i] = destinations[i];
        model.takenMasks[i] = takenMasks[i];
      }
      model.movable = ck::movableSquares(game);
      model.mustTake = ck::captureAvailable(game);
      model.yourPieces = ck::pieceCount(game, seat);
      model.theirPieces = ck::pieceCount(game, seat == ck::kLight ? ck::kDarkSeat : ck::kLight);
      model.seat = seat;
      model.yourTurn = inMatch() ? linkYourTurn() : game.turn == seat;
      model.opponentName = inMatch() ? opponentName() : nullptr;
      checkui::buildBoard(surface, model);
      break;
    }
    case ck::Screen::Result: {
      checkui::ResultModel model;
      model.outcome = ck::outcome(game);
      model.seat = seat;
      model.yourPieces = ck::pieceCount(game, seat);
      model.theirPieces = ck::pieceCount(game, seat == ck::kLight ? ck::kDarkSeat : ck::kLight);
      model.opponentName = inMatch() ? opponentName() : nullptr;
      checkui::buildResult(surface, model);
      break;
    }
  }

  interactionsReady = true;
  toybox::reportOverflow(interactions, "Checkers");

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

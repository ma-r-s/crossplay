#include "CheckersActivity.h"

#include <Memory.h>

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
  requestUpdate();
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
        destinationCount = ck::destinations(game, square, destinations, ck::kMaxMoves);
      } else {
        clearPick();
      }
      requestUpdate();
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
      for (int i = 0; i < destinationCount; ++i) model.destinations[i] = destinations[i];
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

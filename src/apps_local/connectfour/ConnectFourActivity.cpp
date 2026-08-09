#include "ConnectFourActivity.h"

#include <Memory.h>

#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "ConnectFourBrain.h"
#include "ConnectFourScreens.h"

namespace c4 = connectfour;

std::unique_ptr<Activity> ConnectFourActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<ConnectFourActivity>(renderer, mappedInput);
}

void ConnectFourActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  screen = c4::Screen::Menu;
  menuSelected = -1;
  requestUpdate();
}

void ConnectFourActivity::goTo(const c4::Screen next) {
  screen = next;
  requestUpdate();
}

void ConnectFourActivity::beginSoloGame() {
  c4::start(game);
  seat = c4::kLight;
  goTo(c4::Screen::Board);
}

void ConnectFourActivity::takeOpponentTurn() {
  // One drop per pass, not a loop to the end. The panel takes half a second to
  // repaint, so playing several of their moves before showing anything would
  // look like a freeze followed by a skip.
  if (c4::over(game) || game.turn == seat) return;
  const int column = c4::chooseColumn(game);
  if (column == c4::kNoColumn) return;
  c4::drop(game, column);
  requestUpdate();
}

const char* ConnectFourActivity::linkHeadline() const {
  if (linkPhase() == linkplay::PlayBase::Phase::Searching) return "LOOKING FOR A PLAYER";
  if (game.outcome == c4::Outcome::Running) return "CONNECT FOUR";
  if (game.outcome == c4::Outcome::Draw) return "A DRAW";
  const bool won = (seat == c4::kLight && game.outcome == c4::Outcome::LightWins) ||
                   (seat == c4::kDark && game.outcome == c4::Outcome::DarkWins);
  return won ? "YOU WIN" : "THEY WIN";
}

void ConnectFourActivity::onMatchStart(const bool goesFirst) {
  // start() puts light to move, so whoever goes first IS light.
  seat = goesFirst ? c4::kLight : c4::kDark;
  // BOTH sides deal. Connect Four has no randomness: the opening board is
  // empty on any device, so start() is identical on both and there is nothing
  // to wait for.
  //
  // This is not a free choice. Checkers shipped with only the leader starting,
  // and the follower's zeroed board read as a finished game -- it announced a
  // winner before the first move and latched a rematch it never cleared. An
  // empty Connect Four board is a legal position, so the failure would be
  // quieter here and no less wrong: the two devices would disagree about
  // lastColumn until the first packet arrived.
  c4::start(game);
  goTo(c4::Screen::Board);
}

bool ConnectFourActivity::takeOpponentState() { return play.takeOpponent(game); }

void ConnectFourActivity::onRematch() { onMatchStart(play.goesFirst()); }

void ConnectFourActivity::onLinkEnded() {
  seat = c4::kLight;
  goTo(c4::Screen::Menu);
}

void ConnectFourActivity::gameLoop() {
  namespace fui = freeink::ui;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (inMatch()) {
      leaveLink();
      return;
    }
    if (c4::leavesApp(screen)) {
      shelf::leave(renderer, mappedInput);
      return;
    }
    goTo(c4::back(screen));
    return;
  }

  if (screen == c4::Screen::Board) {
    if (inMatch()) {
      if (!linkYourTurn()) {
        if (takeOpponentState()) requestUpdate();
        return;
      }
    } else if (!c4::over(game) && game.turn != seat) {
      takeOpponentTurn();
      return;
    } else if (c4::over(game)) {
      goTo(c4::Screen::Result);
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

  // Forty-two cells and seven slots against a twenty-four slot interaction
  // buffer, so the board is hit-tested from the geometry that drew it. Tried
  // before the registered controls, because it covers most of the screen.
  if (screen == c4::Screen::Board) {
    const fui::DeviceContext device = toybox::makeTarget(renderer).deviceContext();
    const int column = c4ui::columnAt(device, tapX, tapY);
    if (column != c4::kNoColumn) {
      const bool mine = inMatch() ? linkYourTurn() : game.turn == seat;
      if (!mine || c4::over(game)) return;
      // A full column changes nothing, so it must not repaint. A refresh that
      // leaves the screen identical is worse than doing nothing: the panel
      // visibly blinks and comes back the same, which is what a bug looks like.
      if (!c4::drop(game, column)) return;
      if (inMatch()) play.play(game);
      requestUpdate();
      return;
    }
  }

  const fui::ActionEvent event = interactions.route(input);
  switch (event.action) {
    case c4ui::ActionMenuRow:
      switch (static_cast<c4ui::MenuRow>(event.value)) {
        case c4ui::MenuRow::Play:
          beginSoloGame();
          return;
        case c4ui::MenuRow::PlayNearby:
          enterLink(linkplay::GameId::ConnectFour);
          return;
        case c4ui::MenuRow::HowTo:
          howToPage = 0;
          goTo(c4::Screen::HowTo);
          return;
        case c4ui::MenuRow::Count:
          return;
      }
      return;

    case c4ui::ActionHowToNext:
      if (howToPage + 1 < c4ui::howToPages()) {
        ++howToPage;
        requestUpdate();
        return;
      }
      goTo(c4::Screen::Menu);
      return;

    case c4ui::ActionAgain:
      if (inMatch()) {
        proposeRematch();
        return;
      }
      beginSoloGame();
      return;

    case c4ui::ActionDone:
      goTo(c4::Screen::Menu);
      return;

    default:
      return;
  }
}

void ConnectFourActivity::gameRender() {
  namespace fui = freeink::ui;

  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, device, noInput, interactions);
  toybox::Screen surface(frame);

  switch (screen) {
    case c4::Screen::Menu: {
      c4ui::MenuModel model;
      model.selected = menuSelected;
      c4ui::buildMenu(surface, model);
      break;
    }
    case c4::Screen::HowTo: {
      c4ui::HowToModel model;
      model.page = howToPage;
      c4ui::buildHowTo(surface, model);
      break;
    }
    case c4::Screen::Board: {
      c4ui::BoardModel model;
      model.game = game;
      model.open = c4::openColumns(game);
      model.seat = seat;
      model.yourTurn = inMatch() ? linkYourTurn() : game.turn == seat;
      model.opponentName = inMatch() ? opponentName() : nullptr;
      c4ui::buildBoard(surface, model);
      break;
    }
    case c4::Screen::Result: {
      c4ui::ResultModel model;
      model.game = game;
      model.outcome = game.outcome;
      model.seat = seat;
      // The winning line comes from the rules' own search, so the cells marked
      // ARE the cells that won rather than a second opinion about where they
      // were.
      if (game.outcome == c4::Outcome::LightWins) c4::winningLine(game, c4::kLight, model.line);
      else if (game.outcome == c4::Outcome::DarkWins) c4::winningLine(game, c4::kDark, model.line);
      model.opponentName = inMatch() ? opponentName() : nullptr;
      c4ui::buildResult(surface, model);
      break;
    }
  }

  interactionsReady = true;
  toybox::reportOverflow(interactions, "ConnectFour");

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

#include "YahtzeeActivity.h"

#include <Memory.h>

#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxSeed.h"
#include "../ui/ToyboxTheme.h"
#include "YahtzeeBrain.h"
#include "YahtzeeScreens.h"

namespace yz = yahtzee;

std::unique_ptr<Activity> YahtzeeActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<YahtzeeActivity>(renderer, mappedInput);
}

void YahtzeeActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  screen = yz::Screen::Menu;
  menuSelected = -1;
  requestUpdate();
}

void YahtzeeActivity::goTo(const yz::Screen next) {
  screen = next;
  requestUpdate();
}

void YahtzeeActivity::beginSoloGame() {
  yz::start(game, toybox::seed());
  seat = 0;
  goTo(yz::Screen::Card);
}

void YahtzeeActivity::takeOpponentTurn() {
  // One step per pass, not a whole turn in one go. The panel takes half a
  // second to repaint, so rolling three times and taking a box before showing
  // anything would look like a freeze followed by a jump -- and the dice going
  // past is most of what makes watching an opponent's turn bearable.
  if (yz::over(game) || game.turn == seat) return;
  const yz::Card& card = game.card[game.turn];
  if (yz::canRoll(game)) {
    if (game.rollsUsed > 0) game.held = yz::chooseHold(card, game.die);
    yz::roll(game);
    requestUpdate();
    return;
  }
  const yz::Category box = yz::chooseBox(card, game.die);
  if (box == yz::Category::Count) return;
  yz::take(game, box);
  requestUpdate();
}

const char* YahtzeeActivity::linkHeadline() const {
  if (linkPhase() == linkplay::PlayBase::Phase::Searching) return "LOOKING FOR A PLAYER";
  if (!yz::over(game)) return "YAHTZEE";
  const int mine = yz::total(game.card[seat]);
  const int theirs = yz::total(game.card[1 - seat]);
  if (mine == theirs) return "A TIE";
  return mine > theirs ? "YOU WIN" : "THEY WIN";
}

void YahtzeeActivity::onMatchStart(const bool goesFirst) {
  seat = goesFirst ? 0 : 1;
  // Only the LEADER deals. Unlike Checkers and Connect Four, Yahtzee has
  // randomness -- the dice -- so the two devices cannot both start and agree.
  // The follower waits for the leader's first state to arrive, which it will,
  // because the whole game travels on every move.
  //
  // Starting on a zeroed Game here is safe in a way it was not in Checkers: an
  // empty Yahtzee card is not a finished game, so over() is false and nothing
  // announces a winner before the first roll.
  if (goesFirst) yz::start(game, toybox::seed());
  goTo(yz::Screen::Card);
}

bool YahtzeeActivity::takeOpponentState() { return play.takeOpponent(game); }

void YahtzeeActivity::onRematch() { onMatchStart(play.goesFirst()); }

void YahtzeeActivity::onLinkEnded() {
  seat = 0;
  goTo(yz::Screen::Menu);
}

void YahtzeeActivity::gameLoop() {
  namespace fui = freeink::ui;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (inMatch()) {
      leaveLink();
      return;
    }
    if (yz::leavesApp(screen)) {
      shelf::leave(renderer, mappedInput);
      return;
    }
    goTo(yz::back(screen));
    return;
  }

  if (screen == yz::Screen::Card) {
    if (inMatch()) {
      if (!linkYourTurn()) {
        if (takeOpponentState()) requestUpdate();
        return;
      }
    } else if (!yz::over(game) && game.turn != seat) {
      takeOpponentTurn();
      return;
    } else if (yz::over(game)) {
      goTo(yz::Screen::Result);
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

  // The dice and the table are hit-tested from the geometry that drew them, and
  // tried before the registered controls because between them they cover most
  // of the screen.
  if (screen == yz::Screen::Card) {
    const bool mine = inMatch() ? linkYourTurn() : game.turn == seat;
    const fui::DeviceContext device = toybox::makeTarget(renderer).deviceContext();

    const int die = yzui::dieAt(device, tapX, tapY);
    if (die >= 0) {
      // toggleHold refuses outside the two moments it means anything, and a
      // refused tap must not repaint: a panel that visibly blinks and comes
      // back identical is what a bug looks like.
      if (mine && yz::toggleHold(game, die)) requestUpdate();
      return;
    }

    const int category = yzui::categoryAt(device, tapX, tapY);
    if (category >= 0) {
      if (!mine) return;
      if (!yz::take(game, static_cast<yz::Category>(category))) return;
      if (inMatch()) play.play(game);
      requestUpdate();
      return;
    }
  }

  const fui::ActionEvent event = interactions.route(input);
  switch (event.action) {
    case yzui::ActionMenuRow:
      switch (static_cast<yzui::MenuRow>(event.value)) {
        case yzui::MenuRow::Play:
          beginSoloGame();
          return;
        case yzui::MenuRow::PlayNearby:
          enterLink(linkplay::GameId::Yahtzee);
          return;
        case yzui::MenuRow::HowTo:
          howToPage = 0;
          goTo(yz::Screen::HowTo);
          return;
        case yzui::MenuRow::Count:
          return;
      }
      return;

    case yzui::ActionRoll: {
      const bool mine = inMatch() ? linkYourTurn() : game.turn == seat;
      if (!mine) return;
      if (!yz::roll(game)) return;
      requestUpdate();
      return;
    }

    case yzui::ActionHowToNext:
      if (howToPage + 1 < yzui::howToPages()) {
        ++howToPage;
        requestUpdate();
        return;
      }
      goTo(yz::Screen::Menu);
      return;

    case yzui::ActionAgain:
      if (inMatch()) {
        proposeRematch();
        return;
      }
      beginSoloGame();
      return;

    case yzui::ActionDone:
      goTo(yz::Screen::Menu);
      return;

    default:
      return;
  }
}

void YahtzeeActivity::gameRender() {
  namespace fui = freeink::ui;

  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, device, noInput, interactions);
  toybox::Screen surface(frame);

  switch (screen) {
    case yz::Screen::Menu: {
      yzui::MenuModel model;
      model.selected = menuSelected;
      yzui::buildMenu(surface, model);
      break;
    }
    case yz::Screen::HowTo: {
      yzui::HowToModel model;
      model.page = howToPage;
      yzui::buildHowTo(surface, model);
      break;
    }
    case yz::Screen::Card: {
      yzui::CardModel model;
      model.game = game;
      model.takeable = yz::takeableBoxes(game, seat);
      model.joker = yz::jokerForcing(game, seat);
      model.seat = seat;
      model.yourTurn = inMatch() ? linkYourTurn() : game.turn == seat;
      model.opponentName = inMatch() ? opponentName() : nullptr;
      yzui::buildCard(surface, model);
      break;
    }
    case yz::Screen::Result: {
      yzui::ResultModel model;
      model.yours = game.card[seat];
      model.theirs = game.card[1 - seat];
      model.yourTotal = yz::total(model.yours);
      model.theirTotal = yz::total(model.theirs);
      model.opponentName = inMatch() ? opponentName() : nullptr;
      yzui::buildResult(surface, model);
      break;
    }
  }

  interactionsReady = true;
  toybox::reportOverflow(interactions, "Yahtzee");

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

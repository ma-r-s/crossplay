#include "ToyBattleActivity.h"

#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "ToyBattleScreens.h"

namespace tb = toybattle;

std::unique_ptr<Activity> ToyBattleActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return std::make_unique<ToyBattleActivity>(renderer, mappedInput);
}

void ToyBattleActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  screen = tb::Screen::Menu;
  menuSelected = -1;
  requestUpdate();
}

void ToyBattleActivity::beginGame() {
  // millis() is the only entropy on this device that differs between two boots,
  // and it enters here and nowhere deeper: the core takes a seed and never
  // reaches for a clock, which is what keeps a game replayable from its seed.
  //
  // This used to deal a rigged position -- fixed seed, twelve moves played out,
  // the rack force-fed -- so two layout variants could be photographed from the
  // same board. That was scaffolding for choosing a drawing, and it had no
  // business surviving into a game somebody sits down with.
  game.newGame(static_cast<uint32_t>(millis()) * 2654435761u + 1u, static_cast<int>(tb::TerrainId::CastleField), 0,
               /*withSpecialBases=*/true);
  draft.clear();
  notice = nullptr;
  seat = 0;
  goTo(tb::Screen::Board);
}

void ToyBattleActivity::takeOpponentTurn() {
  const tb::Observation obs = tb::observe(game, game.turn);
  game.apply(tb::chooseMove(obs, tb::Skill::General));
  requestUpdate();
}

void ToyBattleActivity::goTo(const tb::Screen next) {
  screen = next;
  requestUpdate();
}

void ToyBattleActivity::say(const char* message) {
  notice = message;
  requestUpdate();
}

const char* ToyBattleActivity::promptText() const {
  // What just happened outranks what is being asked: a player who tapped
  // something needs to know why it did not work more than they need reminding
  // of the question.
  if (notice != nullptr && *notice != '\0') return notice;
  if (game.currentPhase() != tb::Phase::Playing) return game.winner == seat ? "YOU WIN" : "YOU LOSE";
  if (game.turn != seat) return "THEY ARE THINKING";
  switch (tb::pending(game, draft)) {
    case tb::Ask::Troop:
      // The two things a turn can be, said once, where the question is asked.
      return draft.move.stepCount > draft.step ? "AND ONE MORE" : "TAP A TROOP, OR DRAW TWO";
    case tb::Ask::Slot:
      return "TAP A BASE";
    case tb::Ask::JumboVictim:
      return "REMOVE AN ADJACENT TROOP?";
    case tb::Ask::DrawOffer:
      return "REINFORCEMENTS?";
    case tb::Ask::StealOffer:
      return "SHOOT INTO THEIR RACK?";
    case tb::Ask::ChainOffer:
      return "PLACE A SECOND TROOP?";
    case tb::Ask::RecallFrom:
      return "CALL ONE OF YOURS HOME?";
    case tb::Ask::ShoveFrom:
      return "SHOVE ONE OF THEIRS?";
    case tb::Ask::ShoveTo:
      return "SHOVE IT WHERE?";
    case tb::Ask::ExhumeKind:
      return "RAISE ONE FROM THE DISCARD?";
    case tb::Ask::BaseOffer:
      return "USE THE BASE?";
    case tb::Ask::Ready:
      return "";
  }
  return "";
}

void ToyBattleActivity::loop() {
  namespace fui = freeink::ui;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (tb::leavesApp(screen)) {
      shelf::leave(renderer, mappedInput);
      return;
    }
    goTo(tb::back(screen));
    return;
  }

  if (screen == tb::Screen::Board && game.currentPhase() != tb::Phase::Playing) {
    // A finished game is a different screen. Leaving it on the board with the
    // outcome in the prompt line left nowhere to go but Back.
    goTo(tb::Screen::Result);
    return;
  }

  if (screen == tb::Screen::Board && game.currentPhase() == tb::Phase::Playing && game.turn != seat) {
    notice = nullptr;
    takeOpponentTurn();
    return;
  }

  int tapX = 0, tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY) || !interactionsReady) return;

  // The board and the rack are 25 targets against a 24-slot buffer, so both are
  // hit-tested from the geometry that drew them, before the registered
  // controls get a look.
  if (screen == tb::Screen::Board && game.turn == seat && game.currentPhase() == tb::Phase::Playing) {
    const fui::DeviceContext device = toybox::makeTarget(renderer).deviceContext();
    const tb::Ask ask = tb::pending(game, draft);

    const int kind = tbui::rackAt(device, tapX, tapY);
    if (kind >= 0) {
      const tb::Troop troop = static_cast<tb::Troop>(kind);
      if (tb::answerTroop(game, draft, troop)) {
        // Picking a card says what the card does, which is the moment the
        // player wants to know it.
        say(tbui::troopBlurb(troop));
      } else {
        say(tbui::refusalBlurb(tb::whyNotTroop(game, draft, troop)));
      }
      return;
    }
    const int slot = tbui::slotAt(device, game.board(), tapX, tapY);
    if (slot >= 0) {
      const tb::Refusal why = tb::whyNotSlot(game, draft, slot);
      if (why != tb::Refusal::None) {
        say(tbui::refusalBlurb(why));
        return;
      }
      const bool took = ask == tb::Ask::Slot ? tb::answerSlot(game, draft, slot) : tb::answerTarget(game, draft, slot);
      if (took) {
        notice = nullptr;
        if (tb::pending(game, draft) == tb::Ask::Ready) {
          game.apply(draft.move);
          draft.clear();
        }
        requestUpdate();
      }
      return;
    }
  }

  fui::InputSnapshot input;
  input.touchReleased = true;
  input.touchX = static_cast<int16_t>(tapX);
  input.touchY = static_cast<int16_t>(tapY);
  const fui::ActionEvent event = interactions.route(input);
  switch (event.action) {
    case tbui::ActionMenuRow:
      switch (static_cast<tbui::MenuRow>(event.value)) {
        case tbui::MenuRow::Play:
          beginGame();
          return;
        case tbui::MenuRow::HowTo:
          goTo(tb::Screen::HowTo);
          return;
        case tbui::MenuRow::Count:
          return;
      }
      return;
    case tbui::ActionAgain:
      beginGame();
      return;
    case tbui::ActionDone:
      goTo(tb::Screen::Menu);
      return;
    case tbui::ActionDraw:
      // The other half of a turn, and it had no way in before now.
      if (game.apply(tb::Move::draw())) {
        draft.clear();
        say("YOU DREW TWO");
      }
      return;
    case tbui::ActionBrief:
      goTo(tb::Screen::Brief);
      return;
    case tbui::ActionCancel:
      // Backing out of a half-built move, which the board also had no way to
      // do: a mis-tapped troop used to be a dead end.
      draft.clear();
      notice = nullptr;
      requestUpdate();
      return;
    case tbui::ActionSkip:
    case tbui::ActionTake:
      if (tb::answerOffer(game, draft, event.action == tbui::ActionTake)) {
        notice = nullptr;
        if (tb::pending(game, draft) == tb::Ask::Ready) {
          game.apply(draft.move);
          draft.clear();
        }
        requestUpdate();
      }
      return;
    default:
      return;
  }
}

void ToyBattleActivity::render(RenderLock&&) {
  namespace fui = freeink::ui;

  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, device, noInput, interactions);
  toybox::Screen surface(frame);

  switch (screen) {
    case tb::Screen::Brief: {
      tbui::BriefModel model;
      model.board = &game.board();
      model.specialBases = game.specialBases != 0;
      tbui::buildBrief(surface, model);
      break;
    }
    case tb::Screen::Result: {
      tbui::ResultModel model;
      model.game = game;
      model.seat = seat;
      tbui::buildResult(surface, model);
      break;
    }
    case tb::Screen::HowTo:
      tbui::buildHowTo(surface);
      break;
    case tb::Screen::Menu:
    case tb::Screen::Setup: {
      tbui::MenuModel model;
      model.selected = menuSelected;
      tbui::buildMenu(surface, model);
      break;
    }
    case tb::Screen::Board: {
      tbui::BoardModel model;
      model.game = game;
      model.draft = draft;
      model.seat = seat;
      model.yourTurn = game.turn == seat;
      model.prompt = promptText();
      model.canDraw = game.turn == seat && draft.empty() && game.canDraw(seat);
      tbui::buildBoard(surface, model);
      break;
    }
  }

  interactionsReady = true;
  toybox::reportOverflow(interactions, "ToyBattle");

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

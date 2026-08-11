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
  // A fixed seed while the layout is being chosen, so two variants photograph
  // the same position and the comparison is about the drawing.
  game.newGame(20260810u, static_cast<int>(tb::TerrainId::CastleField), 0, /*withSpecialBases=*/true);
  draft.clear();
  seat = 0;

  // Play a few turns of brain against brain so the shot shows a board with
  // something on it. An empty board tells you nothing about a layout.
  for (int i = 0; i < 12 && game.currentPhase() == tb::Phase::Playing; ++i) {
    const tb::Observation obs = tb::observe(game, game.turn);
    if (!game.apply(tb::chooseMove(obs, tb::Skill::General))) break;
  }
  // And make sure the near seat is actually holding something: a rack of eight
  // empty slots shows nothing about how the rack reads.
  game.turn = seat;
  while (game.rackSize(seat) < 5 && game.canDraw(seat)) {
    game.apply(tb::Move::draw());
    game.turn = seat;
  }
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

const char* ToyBattleActivity::capsuleText() const {
  if (game.currentPhase() != tb::Phase::Playing) return game.winner == seat ? "YOU WIN" : "YOU LOSE";
  if (game.turn != seat) return "THEIR MOVE";
  switch (tb::pending(game, draft)) {
    case tb::Ask::Troop:
      return "PICK A TROOP";
    case tb::Ask::Slot:
      return "PICK A BASE";
    case tb::Ask::JumboVictim:
      return "REMOVE ONE, OR TAP TO SKIP";
    case tb::Ask::DrawOffer:
      return "TAKE THE REINFORCEMENTS?";
    case tb::Ask::StealOffer:
      return "SHOOT INTO THEIR RACK?";
    case tb::Ask::ChainOffer:
      return "PLACE ANOTHER?";
    case tb::Ask::RecallFrom:
      return "CALL ONE HOME, OR SKIP";
    case tb::Ask::ShoveFrom:
      return "SHOVE WHICH ONE?";
    case tb::Ask::ShoveTo:
      return "SHOVE IT WHERE?";
    case tb::Ask::ExhumeKind:
      return "RAISE ONE, OR SKIP";
    case tb::Ask::BaseOffer:
      return "USE THE BASE?";
    case tb::Ask::Ready:
      return "DONE";
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

  if (screen == tb::Screen::Board && game.currentPhase() == tb::Phase::Playing && game.turn != seat) {
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
      if (tb::answerTroop(game, draft, static_cast<tb::Troop>(kind))) requestUpdate();
      return;
    }
    const int slot = tbui::slotAt(device, game.board(), tapX, tapY);
    if (slot >= 0) {
      const bool took = ask == tb::Ask::Slot ? tb::answerSlot(game, draft, slot) : tb::answerTarget(game, draft, slot);
      if (took) {
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
      if (static_cast<tbui::MenuRow>(event.value) == tbui::MenuRow::Play) beginGame();
      return;
    case tbui::ActionCapsule: {
      // The capsule is how an offer is taken or an optional effect declined.
      const tb::Ask ask = tb::pending(game, draft);
      const bool offer = ask == tb::Ask::DrawOffer || ask == tb::Ask::StealOffer || ask == tb::Ask::ChainOffer ||
                         ask == tb::Ask::BaseOffer;
      if (tb::answerOffer(game, draft, offer)) {
        if (tb::pending(game, draft) == tb::Ask::Ready) {
          game.apply(draft.move);
          draft.clear();
        }
        requestUpdate();
      }
      return;
    }
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
    case tb::Screen::Menu:
    case tb::Screen::Setup:
    case tb::Screen::HowTo:
    case tb::Screen::Result: {
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
      model.capsule = capsuleText();
      const tb::Ask ask = tb::pending(game, draft);
      model.capsuleLive = ask != tb::Ask::Troop && ask != tb::Ask::Slot && ask != tb::Ask::Ready;
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

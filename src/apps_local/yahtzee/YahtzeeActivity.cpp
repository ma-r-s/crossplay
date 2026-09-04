#include "YahtzeeActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdlib>

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
  loadHistory();
  requestUpdate();
}

// Beside the reader's own state and the player's name. A fork-local fact in a
// fork-local file, the pattern knucklebones.sav set.
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
constexpr char kHistoryPath[] = "/.crosspoint/yahtzee.sav";
#endif

// played won best yahtzees yahtzeeFace.
constexpr int kHistoryValues = 5;

void YahtzeeActivity::loadHistory() {
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
  if (!Storage.exists(kHistoryPath)) return;
  char buffer[64] = {};
  if (Storage.readFileToBuffer(kHistoryPath, buffer, sizeof(buffer)) == 0) return;

  // Parsed into locals and committed only if the whole line is good. Losing
  // this costs an ornament, so it fails quietly.
  int values[kHistoryValues] = {};
  char* cursor = buffer;
  for (int i = 0; i < kHistoryValues; ++i) {
    char* next = nullptr;
    const long value = strtol(cursor, &next, 10);
    if (next == cursor) return;
    values[i] = static_cast<int>(value);
    cursor = next;
  }

  played = values[0];
  won = values[1];
  best = values[2];
  yahtzees = values[3];
  yahtzeeFace = values[4];
#endif
}

void YahtzeeActivity::recordResult() {
  if (resultRecorded) return;
  resultRecorded = true;

  const yz::Card& yours = game.card[seat];
  const yz::Card& theirs = game.card[1 - seat];
  const int mine = yz::total(yours);
  ++played;
  if (mine > yz::total(theirs)) ++won;
  if (mine > best) best = mine;
  // Every Yahtzee this game: the box itself when it scored, plus each bonus.
  if (yours.box[static_cast<int>(yz::Category::Yahtzee)] == 50) ++yahtzees;
  yahtzees += yours.yahtzeeBonuses;

#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
  char line[64];
  const int used = snprintf(line, sizeof(line), "%d %d %d %d %d\n", played, won, best, yahtzees, yahtzeeFace);
  if (used <= 0 || used >= static_cast<int>(sizeof(line))) {
    LOG_ERR("YAHT", "History line did not fit %d bytes", static_cast<int>(sizeof(line)));
    return;
  }
  Storage.writeFile(kHistoryPath, String(line));
#endif
}

void YahtzeeActivity::goTo(const yz::Screen next) {
  screen = next;
  requestUpdate();
}

void YahtzeeActivity::beginSoloGame() {
  yz::start(game, toybox::seed());
  seat = 0;
  resultRecorded = false;
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
  resultRecorded = false;
  // Only the LEADER deals. Unlike Checkers and Connect Four, Yahtzee has
  // randomness -- the dice -- so the two devices cannot both start and agree.
  // The follower waits for the leader's first state to arrive, which it will,
  // because the whole game travels on every move.
  //
  // BOTH sides start a card; only the leader's dice are the ones that count,
  // and they arrive in its first state. The follower used to keep whatever it
  // was holding, and a kept card is a FULL card -- kUnscored is -1, so a game
  // left over from before is over() and the new match opened on the old score
  // sheet with the old result counted a second time.
  yz::start(game, toybox::seed());
  goTo(yz::Screen::Card);
}

bool YahtzeeActivity::takeOpponentState() { return play.takeOpponent(game); }

void YahtzeeActivity::onRematch() { onMatchStart(play.goesFirst()); }

void YahtzeeActivity::onLinkEnded() {
  seat = 0;
  goTo(yz::Screen::Menu);
}

void YahtzeeActivity::onMatchEnded() {
  recordResult();
  // The same screen the solo game ends on. In a match it used to be
  // unreachable, so the finished board went straight to ANOTHER GAME? and the
  // loser saw nothing at all of the move that beat them.
  goTo(yz::Screen::Result);
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

  // HOW TO PLAY pages on the two side keys. They are the device's only physical
  // buttons and the case labels them previous and next page; a page of a
  // how-to is a page. NEXT stays tappable -- a button is never the only route.
  if (screen == yz::Screen::HowTo) {
    const bool forward = mappedInput.wasReleased(MappedInputManager::Button::Down);
    const bool backward = mappedInput.wasReleased(MappedInputManager::Button::Up);
    if (forward || backward) {
      const int pages = yzui::howToPages();
      howToPage = (howToPage + (forward ? 1 : pages - 1)) % pages;
      requestUpdate();
      return;
    }
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
      recordResult();
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
      // Remember the hand before it is spent: five equal dice scored into any
      // box are a rolled Yahtzee, and the face is what the ornament draws.
      const bool fiveEqual = game.die[0] == game.die[1] && game.die[1] == game.die[2] && game.die[2] == game.die[3] &&
                             game.die[3] == game.die[4];
      if (!yz::take(game, static_cast<yz::Category>(category))) return;
      if (fiveEqual) yahtzeeFace = game.die[0];
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
      // DONE on a finished MATCH means done with the match, not just with the
      // screen: the radio is still up and the link screen would slam over the
      // menu the moment the hold ended. leaveLink() is what puts the app back
      // on its own menu, and it tells the other device on the way out.
      if (inMatch()) {
        leaveLink();
        return;
      }
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
      model.played = played;
      model.won = won;
      model.best = best;
      model.yahtzees = yahtzees;
      model.yahtzeeFace = yahtzeeFace;
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

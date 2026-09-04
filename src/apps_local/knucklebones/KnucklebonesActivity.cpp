#include "KnucklebonesActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstdlib>

#include "../Shelf.h"
#include "../player/PlayerName.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "KnucklebonesBrain.h"
#include "KnucklebonesScreens.h"

namespace kb = knucklebones;

std::unique_ptr<Activity> KnucklebonesActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<KnucklebonesActivity>(renderer, mappedInput);
}

void KnucklebonesActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  screen = kb::Screen::Menu;
  menuSelected = -1;
  loadHistory();
  requestUpdate();
}

// Beside the reader's own state and the player's name. A fork-local fact in a
// fork-local file, the same pattern player.cfg set.
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
constexpr char kHistoryPath[] = "/.crosspoint/knucklebones.sav";
#endif

void KnucklebonesActivity::loadHistory() {
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
  if (!Storage.exists(kHistoryPath)) return;
  char buffer[128] = {};
  if (Storage.readFileToBuffer(kHistoryPath, buffer, sizeof(buffer)) == 0) return;

  // Parsed into locals and committed only if the whole line is good, so a
  // truncated file leaves an empty menu rather than half a board. Losing this
  // costs an ornament, so it fails quietly.
  int values[3 + kb::kColumns * kb::kRows * 2] = {};
  char* cursor = buffer;
  for (int i = 0; i < static_cast<int>(sizeof(values) / sizeof(values[0])); ++i) {
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
  kb::Grid mine{};
  kb::Grid theirs{};
  for (int column = 0; column < kb::kColumns; ++column) {
    for (int row = 0; row < kb::kRows; ++row) mine.cell[column][row] = static_cast<uint8_t>(values[at++]);
  }
  for (int column = 0; column < kb::kColumns; ++column) {
    for (int row = 0; row < kb::kRows; ++row) theirs.cell[column][row] = static_cast<uint8_t>(values[at++]);
  }
  lastYours = mine;
  lastTheirs = theirs;
  hasHistory = true;
#endif
}

void KnucklebonesActivity::recordResult() {
  if (resultRecorded) return;
  resultRecorded = true;

  lastYours = game.grid[seat];
  lastTheirs = game.grid[1 - seat];
  hasHistory = true;
  const int mine = kb::score(lastYours);
  const int theirs = kb::score(lastTheirs);
  if (mine > theirs)
    ++wins;
  else if (theirs > mine)
    ++losses;
  else
    ++draws;

#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
  char line[128];
  int used = snprintf(line, sizeof(line), "%d %d %d", wins, losses, draws);
  const kb::Grid* grids[2] = {&lastYours, &lastTheirs};
  for (int which = 0; which < 2 && used > 0 && used < static_cast<int>(sizeof(line)); ++which) {
    for (int column = 0; column < kb::kColumns; ++column) {
      for (int row = 0; row < kb::kRows; ++row) {
        used += snprintf(line + used, sizeof(line) - used, " %d", grids[which]->cell[column][row]);
      }
    }
  }
  if (used <= 0 || used >= static_cast<int>(sizeof(line))) {
    LOG_ERR("KNUCK", "History line did not fit %d bytes", static_cast<int>(sizeof(line)));
    return;
  }
  snprintf(line + used, sizeof(line) - used, "\n");
  Storage.writeFile(kHistoryPath, String(line));
#endif
}

void KnucklebonesActivity::goTo(const kb::Screen next) {
  screen = next;
  requestUpdate();
}

void KnucklebonesActivity::beginSoloMatch() {
  // millis() is the only entropy on this device that differs between two boots.
  // It enters here and nowhere deeper: the core takes a seed and never reaches
  // for a clock, which is what keeps a match replayable and its screenshot
  // reproducible.
  kb::start(game, static_cast<uint32_t>(millis()) * 2654435761u + 1u);
  seat = 0;
  resultRecorded = false;
  goTo(kb::Screen::Board);
}

void KnucklebonesActivity::takeOpponentTurn() {
  // One placement per pass, not a loop to the end of the game. The panel takes
  // half a second to repaint, so playing several of their moves before showing
  // anything would look like a freeze followed by a skip.
  if (kb::over(game) || game.turn == seat) return;
  const int column = kb::chooseColumn(game);
  if (column < 0) return;
  kb::place(game, column);
  requestUpdate();
}

const char* KnucklebonesActivity::linkHeadline() const {
  if (linkPhase() == linkplay::PlayBase::Phase::Searching) return "LOOKING FOR A PLAYER";
  if (!kb::over(game)) return "KNUCKLEBONES";
  const int mine = kb::score(game.grid[seat]);
  const int theirs = kb::score(game.grid[1 - seat]);
  if (mine == theirs) return "A DRAW";
  return mine > theirs ? "YOU WIN" : "THEY WIN";
}

void KnucklebonesActivity::onMatchStart(const bool goesFirst) {
  // start() sets turn to 0, so whoever goes first IS seat 0. The follower does
  // not deal at all: the whole game travels on every move, so its first
  // delivery is the real one, and dealing locally would only put a different
  // die on screen for the half second before that arrives.
  seat = goesFirst ? 0 : 1;
  resultRecorded = false;
  if (goesFirst) {
    kb::start(game, static_cast<uint32_t>(millis()) * 2654435761u + 1u);
  } else {
    game = kb::Game{};
  }
  goTo(kb::Screen::Board);
}

bool KnucklebonesActivity::takeOpponentState() {
  // A straight copy: the shared state IS the game, so there is no merge to get
  // wrong. Battleship needs a merge because its state holds both fleets; this
  // one holds one board that both devices agree on.
  return play.takeOpponent(game);
}

void KnucklebonesActivity::onRematch() { onMatchStart(play.goesFirst()); }

void KnucklebonesActivity::onLinkEnded() {
  seat = 0;
  goTo(kb::Screen::Menu);
}

void KnucklebonesActivity::onMatchEnded() {
  recordResult();
  // The same screen the solo game ends on. In a match it used to be
  // unreachable, so the finished board went straight to ANOTHER GAME? and the
  // loser saw nothing at all of the move that beat them.
  goTo(kb::Screen::Result);
}

void KnucklebonesActivity::gameLoop() {
  namespace fui = freeink::ui;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // The flow owns this, not the activity. Every screen has a defined Back and
    // exactly one of them leaves the app; asking here rather than deciding here
    // is what keeps that true.
    if (inMatch()) {
      leaveLink();
      return;
    }
    if (kb::leavesApp(screen)) {
      shelf::leave(renderer, mappedInput);
      return;
    }
    goTo(kb::back(screen));
    return;
  }

  if (screen == kb::Screen::Board) {
    if (inMatch()) {
      // Their move arrives here rather than in a tap handler, so taking
      // delivery and watching them play are the same code path.
      if (!linkYourTurn()) {
        if (takeOpponentState()) requestUpdate();
        return;
      }
    } else if (!kb::over(game) && game.turn != seat) {
      takeOpponentTurn();
      return;
    } else if (kb::over(game)) {
      recordResult();
      goTo(kb::Screen::Result);
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

  const fui::ActionEvent event = interactions.route(input);
  switch (event.action) {
    case knuckleui::ActionMenuRow:
      switch (static_cast<knuckleui::MenuRow>(event.value)) {
        case knuckleui::MenuRow::Play:
          beginSoloMatch();
          return;
        case knuckleui::MenuRow::PlayNearby:
          // No lobby, no host, no code. They tap this and the next thing they
          // see is a board.
          enterLink(linkplay::GameId::Knucklebones);
          return;
        case knuckleui::MenuRow::HowTo:
          howToPage = 0;
          goTo(kb::Screen::HowTo);
          return;
        case knuckleui::MenuRow::Count:
          return;
      }
      return;

    case knuckleui::ActionHowToNext:
      if (howToPage + 1 < knuckleui::howToPages()) {
        ++howToPage;
        requestUpdate();
        return;
      }
      goTo(kb::Screen::Menu);
      return;

    case knuckleui::ActionColumn:
      // The rules decide whether this is legal, not the screen and not here.
      // place() refuses and changes nothing if it is not, so a stale frame
      // cannot half-apply a move.
      if (!kb::place(game, event.value)) return;
      // Sending is refused unless the phase is YourTurn, which already rules
      // out a stale board -- so a solo game simply never sends.
      if (inMatch()) play.play(game);
      requestUpdate();
      return;

    case knuckleui::ActionAgain:
      if (inMatch()) {
        proposeRematch();
        return;
      }
      beginSoloMatch();
      return;

    case knuckleui::ActionDone:
      goTo(kb::Screen::Menu);
      return;

    default:
      return;
  }
}

void KnucklebonesActivity::gameRender() {
  namespace fui = freeink::ui;

  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, device, noInput, interactions);
  toybox::Screen surface(frame);

  switch (screen) {
    case kb::Screen::Menu: {
      knuckleui::MenuModel model;
      model.selected = menuSelected;
      model.hasHistory = hasHistory;
      model.lastYours = lastYours;
      model.lastTheirs = lastTheirs;
      model.wins = wins;
      model.losses = losses;
      model.draws = draws;
      knuckleui::buildMenu(surface, model);
      break;
    }
    case kb::Screen::HowTo: {
      knuckleui::HowToModel model;
      model.page = howToPage;
      model.pageCount = knuckleui::howToPages();
      knuckleui::buildHowTo(surface, model);
      break;
    }
    case kb::Screen::Board: {
      knuckleui::BoardModel model;
      model.yours = game.grid[seat];
      model.theirs = game.grid[1 - seat];
      model.die = game.die;
      // In a match the turn comes from the link, not from the board: their move
      // is not yours until you have taken delivery of it, and the phase is what
      // knows that.
      model.yourTurn = inMatch() ? linkYourTurn() : game.turn == seat;
      model.opponentName = inMatch() ? opponentName() : nullptr;
      model.waiting = inMatch() && !linkYourTurn();
      knuckleui::buildBoard(surface, model);
      break;
    }
    case kb::Screen::Result: {
      knuckleui::ResultModel model;
      model.yourScore = kb::score(game.grid[seat]);
      model.theirScore = kb::score(game.grid[1 - seat]);
      model.opponentName = inMatch() ? opponentName() : nullptr;
      knuckleui::buildResult(surface, model);
      break;
    }
  }

  interactionsReady = true;
  toybox::reportOverflow(interactions, "Knucklebones");

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

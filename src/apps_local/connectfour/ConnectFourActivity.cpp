#include "ConnectFourActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdlib>
#include <cstring>

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
  loadHistory();
  requestUpdate();
}

// Beside the reader's own state and the player's name. A fork-local fact in a
// fork-local file, the pattern knucklebones.sav set.
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
constexpr char kHistoryPath[] = "/.crosspoint/connectfour.sav";
#endif

// wins losses draws outcome, the four winning cells as flat indices (-1s on a
// draw), then the 42 cells column-major bottom-up -- colours normalised so
// light is always the seat this device played.
constexpr int kHistoryValues = 4 + c4::kLine + c4::kCells;

void ConnectFourActivity::loadHistory() {
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
  for (int i = 0; i < c4::kLine; ++i) lastLine[i] = values[at++];
  for (int i = 0; i < c4::kCells; ++i) lastCells[i] = static_cast<uint8_t>(values[at++]);
  hasHistory = true;
#endif
}

void ConnectFourActivity::recordResult() {
  if (resultRecorded) return;
  resultRecorded = true;

  const bool won = (seat == c4::kLight && game.outcome == c4::Outcome::LightWins) ||
                   (seat == c4::kDark && game.outcome == c4::Outcome::DarkWins);
  lastOutcome = 2;
  if (game.outcome != c4::Outcome::Draw) lastOutcome = won ? 0 : 1;
  if (game.outcome == c4::Outcome::Draw)
    ++draws;
  else if (won)
    ++wins;
  else
    ++losses;

  // Colours normalised so light is always this device's seat. The board never
  // rotates in Connect Four -- gravity reads the same from both chairs -- so a
  // colour swap is the whole normalisation.
  for (int column = 0; column < c4::kColumns; ++column) {
    for (int row = 0; row < c4::kRows; ++row) {
      uint8_t cell = game.cell[column][row];
      if (seat == c4::kDark && cell != c4::kEmpty) cell = c4::other(cell);
      lastCells[column * c4::kRows + row] = cell;
    }
  }
  for (int i = 0; i < c4::kLine; ++i) lastLine[i] = -1;
  if (game.outcome == c4::Outcome::LightWins)
    c4::winningLine(game, c4::kLight, lastLine);
  else if (game.outcome == c4::Outcome::DarkWins)
    c4::winningLine(game, c4::kDark, lastLine);
  hasHistory = true;

#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
  char line[256];
  int used = snprintf(line, sizeof(line), "%d %d %d %d %d %d %d %d", wins, losses, draws, lastOutcome, lastLine[0],
                      lastLine[1], lastLine[2], lastLine[3]);
  for (int i = 0; i < c4::kCells && used > 0 && used < static_cast<int>(sizeof(line)); ++i) {
    used += snprintf(line + used, sizeof(line) - used, " %d", lastCells[i]);
  }
  if (used <= 0 || used >= static_cast<int>(sizeof(line))) {
    LOG_ERR("C4", "History line did not fit %d bytes", static_cast<int>(sizeof(line)));
    return;
  }
  snprintf(line + used, sizeof(line) - used, "\n");
  Storage.writeFile(kHistoryPath, String(line));
#endif
}

void ConnectFourActivity::goTo(const c4::Screen next) {
  screen = next;
  requestUpdate();
}

void ConnectFourActivity::beginSoloGame() {
  c4::start(game);
  seat = c4::kLight;
  resultRecorded = false;
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
  resultRecorded = false;
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

bool ConnectFourActivity::takeOpponentState() {
  // Validate what arrives. plausible() has claimed since it was written that it
  // is "checked on anything arriving over the wire", and nothing called it:
  // linkplay checks the byte length and copies. A packet landed in the live
  // Game unexamined.
  //
  // Taken into a scratch copy first, so a rejected state leaves the board
  // exactly as it was rather than half-overwritten. A dropped update reads as
  // the opponent still thinking, which is the honest failure and one the
  // player can act on; a corrupt board is neither.
  c4::Game arriving{};
  if (!play.takeOpponent(arriving)) return false;
  if (!c4::plausible(arriving)) {
    LOG_ERR("C4", "rejected an implausible board from the wire");
    return false;
  }
  game = arriving;
  return true;
}

void ConnectFourActivity::onRematch() { onMatchStart(play.goesFirst()); }

void ConnectFourActivity::onLinkEnded() {
  seat = c4::kLight;
  goTo(c4::Screen::Menu);
}

void ConnectFourActivity::onMatchEnded() {
  recordResult();
  // The same screen the solo game ends on. In a match it used to be
  // unreachable, so the finished board went straight to ANOTHER GAME? and the
  // loser saw nothing at all of the move that beat them.
  goTo(c4::Screen::Result);
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

  // HOW TO PLAY pages on the two side keys. They are the device's only physical
  // buttons and the case labels them previous and next page; a page of a
  // how-to is a page. NEXT stays tappable -- a button is never the only route.
  if (screen == c4::Screen::HowTo) {
    const bool forward = mappedInput.wasReleased(MappedInputManager::Button::Down);
    const bool backward = mappedInput.wasReleased(MappedInputManager::Button::Up);
    if (forward || backward) {
      const int pages = c4ui::howToPages();
      howToPage = (howToPage + (forward ? 1 : pages - 1)) % pages;
      requestUpdate();
      return;
    }
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
      recordResult();
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
      model.hasHistory = hasHistory;
      model.wins = wins;
      model.losses = losses;
      model.draws = draws;
      model.lastOutcome = lastOutcome;
      model.lastCells = lastCells;
      model.lastLine = lastLine;
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
      if (game.outcome == c4::Outcome::LightWins)
        c4::winningLine(game, c4::kLight, model.line);
      else if (game.outcome == c4::Outcome::DarkWins)
        c4::winningLine(game, c4::kDark, model.line);
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

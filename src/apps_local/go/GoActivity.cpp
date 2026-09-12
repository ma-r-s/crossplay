#include "GoActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdlib>

#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "GoEngine.h"
#include "GoSave.h"
#include "GoScreens.h"

std::unique_ptr<Activity> GoActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<GoActivity>(renderer, mappedInput);
}

// Beside the reader's own state and the player's name. A fork-local fact in a
// fork-local file, the pattern knucklebones.sav set.
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
constexpr char kSavePath[] = "/.crosspoint/go.sav";
constexpr char kSaveTempPath[] = "/.crosspoint/go.sav.tmp";
#endif

void GoActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  screen = go::Screen::Menu;
  menuSelected = -1;
  clearAim();
  loadSave();
  // The seed has to differ between boots or the computer plays the same game
  // every time. millis() at entry is the only entropy this app can reach, and
  // it is enough: nothing here is a secret.
  seed ^= static_cast<uint32_t>(millis()) * 2654435761u;
  if (seed == 0) seed = 0x9E3779B9u;
  requestUpdate();
}

void GoActivity::onExit() {
  writeSave();
  Activity::onExit();
}

void GoActivity::loadSave() {
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
  if (!Storage.exists(kSavePath)) return;
  char buffer[1400] = {};
  if (Storage.readFileToBuffer(kSavePath, buffer, sizeof(buffer)) == 0) return;

  gosave::Save save;
  if (!gosave::unpack(buffer, save)) {
    LOG_ERR("GO", "Save file did not parse; record and game both left alone");
    return;
  }
  wins = save.wins;
  losses = save.losses;
  hasHistory = save.hasHistory;
  lastWon = save.lastWon;
  lastMarginHalves = save.lastMarginHalves;
  for (int i = 0; i < go::kPoints; ++i) lastPoints[i] = save.lastPoints[i];
  opponent = save.opponent;
  level = save.level;
  playAs = save.playAs;
  inProgress = save.inProgress;
  if (inProgress) {
    game = save.game;
    seat = save.seat;
    resultRecorded = false;
  }
#endif
}

void GoActivity::writeSave() {
  // Never write the save during a match: the position on screen is the shared
  // game, and this file is what a solo game resumes from. The check lives here
  // rather than at each call site, because no caller ever wants the other
  // behaviour and chess had this wrong in two separate doors.
  if (inMatch()) return;

#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
  gosave::Save save;
  save.wins = wins;
  save.losses = losses;
  save.hasHistory = hasHistory;
  save.lastWon = lastWon;
  save.lastMarginHalves = lastMarginHalves;
  for (int i = 0; i < go::kPoints; ++i) save.lastPoints[i] = lastPoints[i];
  save.opponent = opponent;
  save.level = level;
  save.playAs = playAs;
  save.inProgress = inProgress;
  save.game = game;
  save.seat = seat;

  char line[1400];
  const int bytes = gosave::pack(save, line, sizeof(line));
  if (bytes <= 0) {
    LOG_ERR("GO", "Save line did not fit %d bytes", static_cast<int>(sizeof(line)));
    return;
  }

  // Temp file then rename. openFileForWrite carries O_TRUNC, so writing in
  // place empties the file at open and power lost in that window leaves
  // nothing at all -- and this app writes on every move, which multiplies that
  // window by the length of a game.
  Storage.writeFile(kSaveTempPath, String(line));
  Storage.remove(kSavePath);
  Storage.rename(kSaveTempPath, kSavePath);
#endif
}

void GoActivity::goTo(const go::Screen next) {
  screen = next;
  requestUpdate();
}

void GoActivity::clearAim() {
  aimed = go::kNothingAimed;
  caution = go::Caution::None;
}

bool GoActivity::myMove() const {
  if (inMatch()) return linkYourTurn();
  // Two people sharing the device are both "me": the board accepts whoever is
  // to move, because there is only one pair of hands.
  if (opponent == go::Opponent::Human) return true;
  return game.toMove == seat;
}

bool GoActivity::computerToMove() const {
  if (inMatch()) return false;
  if (opponent != go::Opponent::Computer) return false;
  return game.stage == static_cast<uint8_t>(go::Stage::Playing) && game.toMove != seat;
}

void GoActivity::beginSoloGame() {
  int handicap = 0;
  int16_t komi = go::kDefaultKomiHalves;
  if (opponent == go::Opponent::Computer) goengine::openingFor(level, handicap, komi);
  go::reset(game, handicap, komi);
  // In a handicap game the weaker player takes Black -- that is what a handicap
  // IS, and the alternative is placing White stones and letting Black open,
  // which is not a game anybody plays. So the colour is not the player's to
  // choose at a level that spots them stones, and the front door says so.
  seat = opponent != go::Opponent::Computer ? go::kBlack : (handicap > 0 ? go::kBlack : playAs);
  resultRecorded = false;
  youAccepted = false;
  theyAccepted = false;
  inProgress = true;
  thinking = false;
  clearAim();
  writeSave();
  goTo(go::Screen::Board);
}

void GoActivity::takeComputerTurn() {
  if (!computerToMove()) return;
  // The search runs on a COPY. The render task reads `game` on its own task,
  // and a search that walked the live board would put thousands of imagined
  // positions on the panel -- invisible here, half a second of garbage on the
  // device. See the chess note in docs/building-apps.md.
  const go::Game snapshot = game;
  const int move = goengine::chooseMove(snapshot, level, seed);
  thinking = false;
  if (!go::play(game, move)) {
    // Belt and braces: chooseMove promises a legal move, and if it ever breaks
    // that promise the game passes rather than freezing on a turn nobody can
    // take.
    LOG_ERR("GO", "Engine offered an illegal move %d; passing", move);
    go::play(game, go::kPass);
  }
  clearAim();
  inProgress = true;
  writeSave();
  if (game.stage == static_cast<uint8_t>(go::Stage::Scoring)) {
    enterCounting();
    return;
  }
  requestUpdate();
}

void GoActivity::handlePointActivated(const int point) {
  if (game.stage != static_cast<uint8_t>(go::Stage::Playing)) return;
  if (!myMove()) return;

  const uint8_t colour = game.toMove;
  const bool legalHere = go::legal(game, point, colour);
  switch (go::tapMeaning(game, aimed, point, true, legalHere)) {
    case go::Tap::Ignore:
      // A tap on a point that cannot be played changes nothing, so it must not
      // repaint. A refresh that comes back identical is what a bug looks like
      // on e-ink: the panel visibly blinks and says the same thing.
      return;
    case go::Tap::Aim:
      aimed = point;
      caution = go::cautionFor(game, point, colour);
      requestUpdate();
      return;
    case go::Tap::Commit:
      break;
  }

  if (!go::play(game, point)) return;
  clearAim();
  inProgress = true;
  if (inMatch()) {
    play.play(game);
  } else {
    writeSave();
  }
  if (game.stage == static_cast<uint8_t>(go::Stage::Scoring)) {
    enterCounting();
    return;
  }
  // The computer's reply is started one pass later, so the repaint carrying
  // your own stone and the word THINKING lands before the search begins.
  thinking = computerToMove();
  requestUpdate();
}

void GoActivity::refreshCount() {
  go::territory(game, owner);
  const go::Score counted = go::score(game);
  blackHalves = counted.blackHalves;
  whiteHalves = counted.whiteHalves;
}

void GoActivity::enterCounting() {
  goengine::estimateDead(game, seed, game.dead);
  youAccepted = false;
  theyAccepted = false;
  refreshCount();
  clearAim();
  if (!inMatch()) writeSave();
  goTo(go::Screen::Count);
}

void GoActivity::toggleDeadAt(const int point) {
  if (!go::isStone(game.point[point])) return;

  // A whole group flips, never one stone of it: a group is alive or dead as a
  // unit, and asking a player to tap eleven stones of a dead dragon is asking
  // them to get it wrong.
  uint8_t stones[(go::kPoints + 7) / 8];
  int size = 0;
  int liberties = 0;
  go::group(game, point, stones, size, liberties);
  const bool nowDead = !go::marked(game.dead, point);
  for (int p = 0; p < go::kPoints; ++p) {
    if (!go::marked(stones, p)) continue;
    if (nowDead) {
      go::mark(game.dead, p);
    } else {
      go::unmark(game.dead, p);
    }
  }

  // Both seats have to agree again after either of them changes their mind.
  youAccepted = false;
  theyAccepted = false;
  refreshCount();
  if (inMatch()) {
    play.play(game);
  } else {
    writeSave();
  }
  requestUpdate();
}

void GoActivity::finishCounting() {
  game.stage = static_cast<uint8_t>(go::Stage::Over);
  refreshCount();
  inProgress = false;
  if (inMatch()) {
    play.play(game);
  } else {
    recordResult();
    writeSave();
    goTo(go::Screen::Result);
  }
}

void GoActivity::recordResult() {
  if (resultRecorded) return;
  resultRecorded = true;

  const bool blackWon = blackHalves > whiteHalves;
  // Two people sharing the device have no "you", so the record counts Black's
  // result. Calling one of them the device's own player would be a lie the
  // ornament then repeats every time the front door is drawn.
  const uint8_t winner = blackWon ? go::kBlack : go::kWhite;
  lastWon = opponent == go::Opponent::Human ? blackWon : (winner == seat);
  if (lastWon) {
    ++wins;
  } else {
    ++losses;
  }
  lastMarginHalves = blackHalves > whiteHalves ? blackHalves - whiteHalves : whiteHalves - blackHalves;
  for (int i = 0; i < go::kPoints; ++i) lastPoints[i] = game.point[i];
  hasHistory = true;
}

const char* GoActivity::linkHeadline() const {
  if (linkPhase() == linkplay::PlayBase::Phase::Searching) return "LOOKING FOR A PLAYER";
  if (game.stage != static_cast<uint8_t>(go::Stage::Over)) return "GO";
  const bool blackWon = blackHalves > whiteHalves;
  return ((blackWon ? go::kBlack : go::kWhite) == seat) ? "YOU WIN" : "THEY WIN";
}

void GoActivity::onMatchStart(const bool goesFirst) {
  // reset() puts Black to move, so whoever goes first IS Black. Both sides
  // deal: there is no randomness in an opening go position, so reset() is
  // identical on both devices and there is nothing to wait for. A follower that
  // started from a zeroed struct would have an EMPTY board with stage 0, which
  // reads as a legal game nobody can score.
  seat = goesFirst ? go::kBlack : go::kWhite;
  resultRecorded = false;
  youAccepted = false;
  theyAccepted = false;
  thinking = false;
  clearAim();
  go::reset(game);
  goTo(go::Screen::Board);
}

bool GoActivity::takeOpponentState() {
  const bool wasCounting = game.stage == static_cast<uint8_t>(go::Stage::Scoring);
  const bool took = play.takeOpponent(game);
  if (!took) return false;

  // Their move landing invalidates the aim: the point you were about to play
  // may now hold one of their stones.
  clearAim();

  if (game.stage == static_cast<uint8_t>(go::Stage::Scoring)) {
    // They passed a second time, or they changed a dead-stone mark. Either way
    // this seat has to look again and say yes again.
    youAccepted = false;
    refreshCount();
    if (!wasCounting) goengine::estimateDead(game, seed, game.dead);
    goTo(go::Screen::Count);
    return true;
  }
  if (game.stage == static_cast<uint8_t>(go::Stage::Over)) {
    refreshCount();
    return true;
  }
  if (wasCounting) {
    // They chose PLAY ON, so the board is live again.
    goTo(go::Screen::Board);
  }
  return true;
}

void GoActivity::onRematch() { onMatchStart(play.goesFirst()); }

void GoActivity::onLinkEnded() {
  seat = playAs;
  clearAim();
  goTo(go::Screen::Menu);
}

// `seat` is here because it decides whose move it is, and the live bit because
// driveLink() runs ahead of gameLoop() on every pass and can hand the turn over
// with no tap from this player: a board that was inert one pass ago starts
// accepting moves while the panel still shows the position before their move.
//
// `aimed` is deliberately absent. Aiming repaints, and hashing it would eat the
// second tap of every move -- which is every move in this game.
uint32_t GoActivity::surfaceMeaning() const {
  const uint32_t withScreen = paintclock::mixMeaning(paintclock::kMeaningSeed, static_cast<uint32_t>(screen));
  const uint32_t withSeat = paintclock::mixMeaning(withScreen, seat);
  const bool live = myMove() && game.stage != static_cast<uint8_t>(go::Stage::Over);
  return paintclock::mixMeaning(withSeat, live ? 1u : 0u);
}

void GoActivity::onMatchEnded() {
  recordResult();
  goTo(go::Screen::Result);
}

void GoActivity::gameLoop() {
  namespace fui = freeink::ui;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (inMatch()) {
      leaveLink();
      return;
    }
    if (go::leavesApp(screen)) {
      writeSave();
      shelf::leave(renderer, mappedInput);
      return;
    }
    goTo(go::back(screen));
    return;
  }

  // HOW TO PLAY pages on the two side keys: the case labels them previous and
  // next page, and a page of a how-to is a page. NEXT stays tappable, because a
  // physical button is never the only route to anything here.
  if (screen == go::Screen::HowTo) {
    const bool forward = mappedInput.wasReleased(MappedInputManager::Button::Down);
    const bool backward = mappedInput.wasReleased(MappedInputManager::Button::Up);
    if (forward || backward) {
      const int pages = goui::howToPages();
      howToPage = (howToPage + (forward ? 1 : pages - 1)) % pages;
      requestUpdate();
      return;
    }
  }

  if (screen == go::Screen::Board) {
    if (inMatch()) {
      if (!linkYourTurn()) {
        if (takeOpponentState()) requestUpdate();
        return;
      }
    } else if (thinking) {
      // One pass later than the repaint that announced it. See the member's
      // comment: requestUpdate() only notifies the render task.
      takeComputerTurn();
      return;
    } else if (computerToMove()) {
      thinking = true;
      requestUpdate();
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

  // Eighty-one points against a twenty-four slot interaction buffer, so the
  // board is hit-tested from the geometry that drew it rather than registered
  // point by point. Tried before the registered controls, because it covers
  // most of the screen.
  if (screen == go::Screen::Board || screen == go::Screen::Count) {
    const fui::DeviceContext device = toybox::makeTarget(renderer).deviceContext();
    int point = 0;
    if (goui::pointAt(device, tapX, tapY, point)) {
      if (!surfaceRevealed()) return;
      if (screen == go::Screen::Count) {
        toggleDeadAt(point);
      } else {
        handlePointActivated(point);
      }
      return;
    }
  }

  const fui::ActionEvent event = interactions.route(input);
  switch (event.action) {
    case goui::ActionMenuRow:
      switch (static_cast<goui::MenuRow>(event.value)) {
        case goui::MenuRow::Play:
          // RESUME rather than a new game when one is part-played: throwing away
          // a position from the front door with no warning is how a player
          // loses a game they left on the train.
          if (inProgress && game.stage != static_cast<uint8_t>(go::Stage::Over)) {
            thinking = false;
            clearAim();
            // A game saved mid-count comes back with its dead marks but with
            // nothing derived from them: the count is not in the save because
            // it is not a fact, it is a view of one.
            if (game.stage == static_cast<uint8_t>(go::Stage::Scoring)) refreshCount();
            goTo(go::screenFor(static_cast<go::Stage>(game.stage)));
            return;
          }
          beginSoloGame();
          return;
        case goui::MenuRow::Opponent:
          // The row changes in place and the headline above it relabels itself;
          // leaving the menu is what applies it. A destructive setting that
          // jumps you somewhere else makes you guess whether it worked.
          opponent = opponent == go::Opponent::Computer ? go::Opponent::Human : go::Opponent::Computer;
          inProgress = false;
          menuSelected = static_cast<int>(goui::MenuRow::Opponent);
          writeSave();
          requestUpdate();
          return;
        case goui::MenuRow::Level:
          if (opponent != go::Opponent::Computer) return;
          level = go::nextLevel(level);
          // The level decides the opening -- how many stones and what komi --
          // so it cannot be changed under a game in progress without the board
          // and the menu disagreeing about what is being played. Dropping the
          // game here is what makes the headline relabel itself from RESUME to
          // PLAY, which is the confirmation, rather than a dialog.
          inProgress = false;
          menuSelected = static_cast<int>(goui::MenuRow::Level);
          writeSave();
          requestUpdate();
          return;
        case goui::MenuRow::PlayAs: {
          if (opponent != go::Opponent::Computer) return;
          int handicap = 0;
          int16_t komi = go::kDefaultKomiHalves;
          goengine::openingFor(level, handicap, komi);
          // Dimmed rather than gone at a level that spots stones, so the row
          // still says what it would do and the layout does not jump.
          if (handicap > 0) return;
          playAs = go::other(playAs);
          inProgress = false;
          menuSelected = static_cast<int>(goui::MenuRow::PlayAs);
          writeSave();
          requestUpdate();
          return;
        }
        case goui::MenuRow::PlayNearby:
          enterLink(linkplay::GameId::Go);
          return;
        case goui::MenuRow::HowTo:
          howToPage = 0;
          goTo(go::Screen::HowTo);
          return;
        case goui::MenuRow::Count:
          return;
      }
      return;

    case goui::ActionHowToNext:
      if (howToPage + 1 < goui::howToPages()) {
        ++howToPage;
        requestUpdate();
        return;
      }
      goTo(go::Screen::Menu);
      return;

    case goui::ActionPass:
      if (!myMove()) return;
      if (!go::play(game, go::kPass)) return;
      clearAim();
      if (inMatch()) {
        play.play(game);
      } else {
        writeSave();
      }
      if (game.stage == static_cast<uint8_t>(go::Stage::Scoring)) {
        enterCounting();
        return;
      }
      thinking = computerToMove();
      requestUpdate();
      return;

    case goui::ActionResume:
      // Disagreeing about what is dead is resolved by playing it out, which is
      // what the rules say and what a Go player expects. The marks are dropped
      // so nobody carries half an argument back onto the board.
      game.stage = static_cast<uint8_t>(go::Stage::Playing);
      game.passes = 0;
      go::clearMask(game.dead);
      youAccepted = false;
      theyAccepted = false;
      if (inMatch()) {
        play.play(game);
      } else {
        writeSave();
      }
      goTo(go::Screen::Board);
      return;

    case goui::ActionAccept:
      youAccepted = true;
      if (!inMatch()) {
        finishCounting();
        return;
      }
      // In a match the other seat has to agree too. Sending the state hands
      // them the turn, and their ACCEPT comes back as a finished game.
      theyAccepted = theyAccepted || false;
      finishCounting();
      requestUpdate();
      return;

    case goui::ActionAgain:
      if (inMatch()) {
        proposeRematch();
        return;
      }
      beginSoloGame();
      return;

    case goui::ActionDone:
      // DONE on a finished MATCH means done with the match, not with the
      // screen: the radio is still up and the link screen would slam over the
      // menu the moment the hold ended.
      if (inMatch()) {
        leaveLink();
        return;
      }
      goTo(go::Screen::Menu);
      return;

    default:
      return;
  }
}

void GoActivity::gameRender() {
  namespace fui = freeink::ui;

  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, device, noInput, interactions);
  toybox::Screen surface(frame);

  switch (screen) {
    case go::Screen::Menu: {
      goui::MenuModel model;
      model.selected = menuSelected;
      model.opponent = opponent;
      model.level = level;
      model.playAs = playAs;
      {
        int handicap = 0;
        int16_t komi = go::kDefaultKomiHalves;
        goengine::openingFor(level, handicap, komi);
        model.handicap = handicap;
      }
      model.inProgress = inProgress && game.stage != static_cast<uint8_t>(go::Stage::Over);
      model.hasHistory = hasHistory;
      model.lastPoints = lastPoints;
      model.lastWon = lastWon;
      model.lastMarginHalves = lastMarginHalves;
      model.wins = wins;
      model.losses = losses;
      goui::buildMenu(surface, model);
      break;
    }
    case go::Screen::HowTo: {
      goui::HowToModel model;
      model.page = howToPage;
      goui::buildHowTo(surface, model);
      break;
    }
    case go::Screen::Board: {
      goui::BoardModel model;
      model.game = game;
      model.aimed = aimed;
      model.caution = caution;
      model.seat = seat;
      model.yourTurn = myMove();
      model.theyPassed = game.lastMove == go::kPass;
      model.nothingLeft = !go::hasUsefulMove(game, game.toMove);
      model.opponentName = inMatch() ? opponentName() : nullptr;
      model.sharedDevice = !inMatch() && opponent == go::Opponent::Human;
      model.thinking = thinking;
      goui::buildBoard(surface, model);
      break;
    }
    case go::Screen::Count: {
      goui::CountModel model;
      model.game = game;
      model.seat = seat;
      for (int i = 0; i < go::kPoints; ++i) model.owner[i] = owner[i];
      model.blackHalves = blackHalves;
      model.whiteHalves = whiteHalves;
      model.youAccepted = youAccepted;
      model.theyAccepted = theyAccepted;
      model.sharedDevice = !inMatch() && opponent == go::Opponent::Human;
      goui::buildCount(surface, model);
      break;
    }
    case go::Screen::Result: {
      goui::ResultModel model;
      model.game = game;
      model.seat = seat;
      for (int i = 0; i < go::kPoints; ++i) model.owner[i] = owner[i];
      model.blackHalves = blackHalves;
      model.whiteHalves = whiteHalves;
      model.opponentName = inMatch() ? opponentName() : nullptr;
      model.sharedDevice = !inMatch() && opponent == go::Opponent::Human;
      goui::buildResult(surface, model);
      break;
    }
  }

  interactionsReady = true;
  toybox::reportOverflow(interactions, "Go");

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

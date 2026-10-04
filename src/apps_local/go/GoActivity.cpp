#include "GoActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#if defined(ARDUINO_ARCH_ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

#include <cstdlib>

#include "../Shelf.h"
#include "../player/PlayerName.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "GoEngine.h"
#include "GoMichi.h"
#include "GoSave.h"
#include "GoScreens.h"

// How much stack michi's deepest path needs, and why it is not the loop task's.
//
// The search descends tree_search -> tree_descend -> expand, and from the
// playout policy into fix_atari -> read_ladder_attack, which recurses through
// fix_atari_r back into itself once per ply of a ladder. Upstream runs that on
// a desktop with an eight megabyte stack; the loop task here gets 24KB
// (main.cpp), under the worst case below, and one frame of expand() alone was
// 8.5KB before the vendored copy was trimmed.
//
// So the search gets a task of its own, created when the app opens and ended
// when it closes, and the loop waits on it.
//
// The number is measured. scripts_local/stack_budget.py can only do half of
// the arithmetic -- it reports cycles and never sums them -- so the other half
// is written at MICHI_LADDER_MAX in michi.c, at N=19: 26,848 of 32,768 worst
// case. Re-measure rather than trust it if any michi frame or N moves:
//   ./scripts_local/stack-budget.sh --verbose
#define GO_SEARCH_TASK_STACK 32768

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
  // A VALID empty board before anything else. `go::Game{}` is zeroed, which is
  // a board of size 0 with komi 0 and nobody to move -- a state the save file's
  // own reader refuses. Without this, changing a setting and backing out of a
  // device that had never started a game wrote a line that would not load, and
  // the settings came back as the defaults on the next launch.
  go::reset(game, boardSize);
  loadSave();
#if defined(ARDUINO_ARCH_ESP32)
  searchEnding = false;
  TaskHandle_t handle = nullptr;
  // Core 1 where there is one, for the same reason the render task is pinned
  // there: a four second compute-bound task starves whichever core's idle task
  // it shares, and core 0's is the one the system watches.
#if defined(configNUM_CORES) && configNUM_CORES > 1
  constexpr BaseType_t searchCore = 1;
#else
  constexpr BaseType_t searchCore = 0;
#endif
  if (xTaskCreatePinnedToCore(&GoActivity::searchTrampoline, "go_search", GO_SEARCH_TASK_STACK, this, 1, &handle,
                              searchCore) == pdPASS) {
    searchTask = handle;
  } else {
    // Not fatal: the search then runs on the loop task, which is where it used
    // to run and where it may overflow. Saying so is the point -- a fallback
    // nobody can see is a crash nobody can explain.
    searchTask = nullptr;
    LOG_ERR("GO", "No search task (%d bytes); searching on the loop task", GO_SEARCH_TASK_STACK);
  }
#endif
  // The seed has to differ between boots or the computer plays the same game
  // every time. millis() at entry is the only entropy this app can reach, and
  // it is enough: nothing here is a secret.
  seed ^= static_cast<uint32_t>(millis()) * 2654435761u;
  if (seed == 0) seed = 0x9E3779B9u;
  requestUpdate();
}

void GoActivity::onExit() {
  writeSave();
#if defined(ARDUINO_ARCH_ESP32)
  // The task has to be GONE before this object is, because its loop reads this
  // object's members. It is parked on a notification, so waking it with
  // `searchEnding` set is what ends it, and it acknowledges before it deletes
  // itself.
  if (searchTask != nullptr) {
    searchEnding = true;
    searchWaiter = xTaskGetCurrentTaskHandle();
    xTaskNotifyGive(static_cast<TaskHandle_t>(searchTask));
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000));
    searchTask = nullptr;
  }
#endif
  // Hundreds of kilobytes of PSRAM that nothing reads between games.
  gomichi::forget();
  Activity::onExit();
}

#if defined(ARDUINO_ARCH_ESP32)
void GoActivity::searchTrampoline(void* self) { static_cast<GoActivity*>(self)->searchLoop(); }

void GoActivity::searchLoop() {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (searchEnding) break;
    searchResult = gomichi::chooseMove(searchBoard, searchLevel, seed, []() -> uint32_t { return millis(); });
    xTaskNotifyGive(static_cast<TaskHandle_t>(searchWaiter));
  }
  // Nothing below this line may touch the activity: the waiter is free to
  // delete it the moment it is notified.
  TaskHandle_t waiter = static_cast<TaskHandle_t>(searchWaiter);
  xTaskNotifyGive(waiter);
  vTaskDelete(nullptr);
}
#endif

int GoActivity::chooseComputerMove(const go::Game& snapshot) {
#if defined(ARDUINO_ARCH_ESP32)
  if (searchTask != nullptr) {
    searchBoard = snapshot;
    searchLevel = level;
    searchResult = go::kPass;
    searchWaiter = xTaskGetCurrentTaskHandle();
    xTaskNotifyGive(static_cast<TaskHandle_t>(searchTask));
    // No timeout. The search has its own clock and stops on it; a timeout here
    // would leave a task writing into searchResult while the loop moved on.
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    return searchResult;
  }
#endif
  // The clock the engine measures itself against. It is lent rather than
  // called, because GoMichi is freestanding and must stay that way: the host
  // tests pass none and get a search bounded by simulations alone, which is
  // what makes them deterministic.
  return gomichi::chooseMove(snapshot, level, seed, []() -> uint32_t { return millis(); });
}

void GoActivity::loadSave() {
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
  if (!Storage.exists(kSavePath)) return;
  char buffer[gosave::kMaxLine] = {};
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
  for (int i = 0; i < go::kMaxPoints; ++i) lastPoints[i] = save.lastPoints[i];
  lastSize = save.lastSize;
  opponent = save.opponent;
  level = save.level;
  playAs = save.playAs;
  handicap = save.handicap >= 2 && save.handicap <= go::kMaxHandicap ? save.handicap : 0;
  boardSize = go::isBoardSize(save.boardSize) ? save.boardSize : go::kSmallSize;
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
  for (int i = 0; i < go::kMaxPoints; ++i) save.lastPoints[i] = lastPoints[i];
  save.lastSize = lastSize;
  save.opponent = opponent;
  save.level = level;
  save.playAs = playAs;
  save.handicap = handicap;
  save.boardSize = boardSize;
  save.inProgress = inProgress;
  save.game = game;
  save.seat = seat;

  char line[gosave::kMaxLine];
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
  // The handicap is its own setting now, not a property of the level. Komi
  // follows it, because those two are one decision: a handicap game is played
  // at half a point, an even one at seven and a half.
  go::reset(game, boardSize, handicap, go::komiForHandicap(handicap));
  // A handicap is Black's by definition, so it settles the colour and the
  // YOU PLAY row goes dim rather than lying.
  seat = opponent != go::Opponent::Computer ? go::kBlack : (handicap > 0 ? go::kBlack : playAs);
  resultRecorded = false;
  inProgress = true;
  thinking = false;
  disagreed = false;
  passWasPlayedThrough = false;
  clearAim();
  writeSave();
  goTo(go::Screen::Board);
}

void GoActivity::discardGame() {
  // The one destructive control in the app, and it is deliberately NOT
  // confirmed. A game in progress is drawn on the front door right above the
  // button, so what is being thrown away is on screen while the finger is over
  // it -- which is a better guard than a dialog nobody reads. What it costs if
  // it is ever tapped by accident is one game, and PLAY starts another.
  inProgress = false;
  thinking = false;
  resultRecorded = false;
  clearAim();
  go::reset(game, boardSize);
  writeSave();
  requestUpdate();
}

void GoActivity::takeComputerTurn() {
  if (!computerToMove()) return;
  // The search runs on a COPY. The render task reads `game` on its own task,
  // and a search that walked the live board would put thousands of imagined
  // positions on the panel -- invisible here, half a second of garbage on the
  // device. See the chess note in docs/building-apps.md.
  const go::Game snapshot = game;
  const uint32_t began = millis();
  const int move = chooseComputerMove(snapshot);
  const uint32_t took = millis() - began;
  thinking = false;

  // How long the search actually took, on the actual chip. Every number in
  // docs/apps/go.md is a laptop measurement scaled by a published CoreMark
  // ratio, and the spread in that estimate is a rank and a half -- so this is
  // the one line that turns an estimate into a fact, and it is also the line
  // somebody needs if a move ever takes long enough to trip the watchdog.
  //
  // And how close the search came to the end of its stack, lowest since the app
  // opened: michi's ladder reader recurses, the budget for it is arithmetic
  // rather than a measurement (see MICHI_LADDER_MAX), and a losing machine runs
  // every simulation and so reads the most ladders. -1 means the search ran on
  // the loop task because its own could not be created (onEnter logs why).
  const gomichi::Settings settings = gomichi::settingsFor(level);
  int stackFree = -1;
#if defined(ARDUINO_ARCH_ESP32)
  if (searchTask != nullptr) {
    stackFree = static_cast<int>(uxTaskGetStackHighWaterMark(static_cast<TaskHandle_t>(searchTask)));
  }
#endif
  LOG_INF("GO", "search: level %d, %ux%u, %u ms of %u, %d of %u sims (move %u), stack free %d of %d",
          static_cast<int>(level), static_cast<unsigned>(game.size), static_cast<unsigned>(game.size),
          static_cast<unsigned>(took), static_cast<unsigned>(settings.budgetMs), gomichi::lastSimulations(),
          static_cast<unsigned>(settings.simulations), static_cast<unsigned>(game.moveNumber), stackFree,
          GO_SEARCH_TASK_STACK);
  // Before the move lands, because playing it resets the pass count.
  passWasPlayedThrough = game.passes >= 1 && move != go::kPass;
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
  disagreed = false;
  passWasPlayedThrough = false;
  if (inMatch()) {
    play.play(game);
  } else {
    // Only a SOLO game is in progress. A match has its own board, and marking
    // the device as having a game to resume while playing one leaves the front
    // door offering RESUME for a game that was overwritten the moment the match
    // started.
    inProgress = true;
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

void GoActivity::resumeFromDisagreement() {
  game.stage = static_cast<uint8_t>(go::Stage::Playing);
  game.passes = 0;
  go::clearMask(game.dead);
  go::withdrawAcceptance(game);
  // Said on the board rather than in a dialog: the player needs to know why the
  // game came back, and the answer is one line on the screen they returned to.
  disagreed = true;
  thinking = false;
  clearAim();
  writeSave();
  goTo(go::Screen::Board);
}

void GoActivity::enterCounting() {
  // The machine's own reading, offered as the starting point. The players may
  // change it; against the machine, changing it is a disagreement and has to be
  // settled on the board rather than by assertion. See ActionAccept.
  goengine::opinionOnDead(game, game.dead);
  refreshCount();
  clearAim();
  if (!inMatch()) writeSave();
  goTo(go::Screen::Count);
}

void GoActivity::toggleDeadAt(const int point) {
  if (!go::isStone(game.at(point))) return;
  // The counting screen has a turn, exactly like the board does. A mark is a
  // state change, so the link refuses to send one out of turn -- and the first
  // version mutated the board first and threw the refusal away, which left one
  // device showing a mark the other had never heard of, until their next send
  // silently wiped it.
  if (!myMove()) return;

  // A whole group flips, never one stone of it: a group is alive or dead as a
  // unit, and asking a player to tap eleven stones of a dead dragon is asking
  // them to get it wrong.
  uint8_t stones[go::kMaskBytes];
  int size = 0;
  int liberties = 0;
  go::group(game, point, stones, size, liberties);
  const bool nowDead = !go::marked(game.dead, point);
  const int points = game.points();
  for (int p = 0; p < points; ++p) {
    if (!go::marked(stones, p)) continue;
    if (nowDead) {
      go::mark(game.dead, p);
    } else {
      go::unmark(game.dead, p);
    }
  }

  // Both seats have to agree again after either of them changes their mind.
  go::withdrawAcceptance(game);
  refreshCount();
  if (inMatch()) {
    if (!play.play(game)) LOG_ERR("GO", "The link refused a dead-stone mark the board allowed");
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
  // One byte a point here, unpacked: the front door draws a picture of this and
  // nothing plays on it.
  for (int i = 0; i < go::kMaxPoints; ++i) lastPoints[i] = go::kEmpty;
  const int points = game.points();
  for (int i = 0; i < points; ++i) lastPoints[i] = game.at(i);
  lastSize = game.size;
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
  thinking = false;
  clearAim();
  // On THIS device's board setting. The size crosses the wire inside the game,
  // so a match between two devices set differently settles on the first seat's
  // board as soon as its first move arrives -- which is before the second seat
  // can place anything, because it is not their turn until then.
  go::reset(game, boardSize);
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
    // They passed a second time, they changed a dead-stone mark, or they
    // accepted. Whichever it was, `accepted` came with the board, so who agrees
    // is a fact about the game rather than a flag this device kept.
    refreshCount();
    if (go::hasAccepted(game, go::kBlack) && go::hasAccepted(game, go::kWhite)) {
      finishCounting();
      return true;
    }
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
  // The solo game comes BACK. onMatchStart resets the board straight over it,
  // so without this the front door offers RESUME and opens the match's final
  // position with the wrong seat -- and the computer starts playing it. Chess
  // reloads here for the same reason.
  //
  // The card still holds the solo game: writeSave() refuses for the whole
  // length of a match, so nothing has overwritten it.
  clearAim();
  thinking = false;
  go::reset(game, boardSize, 0, go::kDefaultKomiHalves);
  inProgress = false;
  seat = playAs;
  loadSave();
  if (inProgress && game.stage == static_cast<uint8_t>(go::Stage::Scoring)) refreshCount();
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

  // Up to a hundred and sixty nine points against a twenty-four slot
  // interaction buffer, so the board is hit-tested from the geometry that drew
  // it rather than registered point by point. Tried before the registered
  // controls, because it covers most of the screen.
  if (screen == go::Screen::Board || screen == go::Screen::Count) {
    const fui::DeviceContext device = toybox::makeTarget(renderer).deviceContext();
    int point = 0;
    if (goui::pointAt(device, game.size, tapX, tapY, point)) {
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
        case goui::MenuRow::PlayNearby:
          enterLink(linkplay::GameId::Go);
          return;
        case goui::MenuRow::Settings:
          settingsSelected = -1;
          goTo(go::Screen::Settings);
          return;
        case goui::MenuRow::Count:
          return;
      }
      return;

    case goui::ActionDiscard:
      discardGame();
      return;

    case goui::ActionSettingsRow:
      switch (static_cast<goui::SettingsRow>(event.value)) {
        case goui::SettingsRow::Opponent:
          // The row changes in place and the front door's headline relabels
          // itself; leaving is what applies it. A destructive setting that
          // jumps you somewhere else makes you guess whether it worked.
          opponent = opponent == go::Opponent::Computer ? go::Opponent::Human : go::Opponent::Computer;
          inProgress = false;
          settingsSelected = static_cast<int>(goui::SettingsRow::Opponent);
          writeSave();
          requestUpdate();
          return;
        case goui::SettingsRow::Level:
          if (opponent != go::Opponent::Computer) return;
          level = go::nextLevel(level);
          // Strength only, so a game in progress could in principle survive it.
          // It is dropped anyway, because every other row here drops it and a
          // list where one row keeps your game and four throw it away is a list
          // nobody can predict.
          inProgress = false;
          settingsSelected = static_cast<int>(goui::SettingsRow::Level);
          writeSave();
          requestUpdate();
          return;
        case goui::SettingsRow::Handicap:
          if (opponent != go::Opponent::Computer) return;
          handicap = handicap >= go::kMaxHandicap ? 0 : (handicap == 0 ? 2 : handicap + 1);
          inProgress = false;
          settingsSelected = static_cast<int>(goui::SettingsRow::Handicap);
          writeSave();
          requestUpdate();
          return;
        case goui::SettingsRow::PlayAs: {
          if (opponent != go::Opponent::Computer) return;
          if (handicap > 0) return;
          playAs = go::other(playAs);
          inProgress = false;
          settingsSelected = static_cast<int>(goui::SettingsRow::PlayAs);
          writeSave();
          requestUpdate();
          return;
        }
        case goui::SettingsRow::Board:
          boardSize = go::nextBoardSize(boardSize);
          // The board cannot change under a game in progress: its stones are
          // laid out for the size they were played on. Dropping the resume is
          // what every other setting here already does.
          inProgress = false;
          settingsSelected = static_cast<int>(goui::SettingsRow::Board);
          writeSave();
          requestUpdate();
          return;
        case goui::SettingsRow::Count:
          return;
      }
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
      if (!myMove()) return;
      // Disagreeing about what is dead is resolved by playing it out, which is
      // what the rules say and what a Go player expects. The marks are dropped
      // so nobody carries half an argument back onto the board.
      game.stage = static_cast<uint8_t>(go::Stage::Playing);
      game.passes = 0;
      go::clearMask(game.dead);
      go::withdrawAcceptance(game);
      if (inMatch()) {
        if (!play.play(game)) LOG_ERR("GO", "The link refused PLAY ON");
      } else {
        writeSave();
      }
      goTo(go::Screen::Board);
      return;

    case goui::ActionAccept: {
      if (inMatch() && !myMove()) return;
      // Solo, there is nobody to wait for. Two people sharing one device are
      // sitting together and can say so out loud, so one tap settles it there
      // too; only a match has a second seat that has to agree in its own time.
      if (!inMatch()) {
        // The rule itself is in GoFlow.h, where the suite can reach it.
        uint8_t opinion[go::kMaskBytes];
        goengine::opinionOnDead(game, opinion);
        if (go::acceptEndsTheGame(opponent, go::sameMask(opinion, game.dead))) {
          go::accept(game, go::kBlack);
          go::accept(game, go::kWhite);
          finishCounting();
          return;
        }
        // It disagrees, so the BOARD settles it. That is what the rules say to
        // do about a disagreement over which stones are dead, and it is why
        // there is no way to talk the machine round: you prove it instead.
        resumeFromDisagreement();
        return;
      }
      const bool both = go::accept(game, seat);
      if (both) {
        finishCounting();
        return;
      }
      // Sending hands them the turn. The button relabels itself to WAITING and
      // means it: the game ends when their ACCEPT comes back, not now.
      if (!play.play(game)) LOG_ERR("GO", "The link refused an ACCEPT the screen allowed");
      requestUpdate();
      return;
    }

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
      model.inProgress = inProgress && game.stage != static_cast<uint8_t>(go::Stage::Over);
      model.hasHistory = hasHistory;
      model.lastWon = lastWon;
      model.lastMarginHalves = lastMarginHalves;
      model.wins = wins;
      model.losses = losses;

      // The GAME IN PROGRESS, or the last one finished. Unpacked here rather
      // than in the screen, because the screen draws a picture and has no
      // business knowing how a position is stored.
      uint8_t doorPoints[go::kMaxPoints] = {};
      if (model.inProgress) {
        const int points = game.points();
        for (int i = 0; i < points; ++i) doorPoints[i] = game.at(i);
        model.boardPoints = doorPoints;
        model.boardSize = game.size;
        model.moveNumber = game.moveNumber;
      } else if (hasHistory) {
        model.boardPoints = lastPoints;
        model.boardSize = lastSize;
      }
      goui::buildMenu(surface, model);
      break;
    }
    case go::Screen::Settings: {
      goui::SettingsModel model;
      model.selected = settingsSelected;
      model.opponent = opponent;
      model.level = level;
      model.playAs = playAs;
      model.handicap = opponent == go::Opponent::Computer ? handicap : 0;
      model.boardSize = boardSize;
      goui::buildSettings(surface, model);
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
      model.disagreed = disagreed;
      model.itPlayedOn = passWasPlayedThrough && opponent == go::Opponent::Computer;
      model.freePoints = go::freePoints(game, game.toMove);
      player::shortName(inMatch() ? opponentName() : nullptr, theirName, sizeof(theirName));
      model.opponentName = inMatch() ? theirName : nullptr;
      model.sharedDevice = !inMatch() && opponent == go::Opponent::Human;
      model.thinking = thinking;
      goui::buildBoard(surface, model);
      break;
    }
    case go::Screen::Count: {
      goui::CountModel model;
      model.game = game;
      model.seat = seat;
      for (int i = 0; i < go::kMaxPoints; ++i) model.owner[i] = owner[i];
      model.blackHalves = blackHalves;
      model.whiteHalves = whiteHalves;
      model.youAccepted = go::hasAccepted(game, seat);
      model.theyAccepted = go::hasAccepted(game, go::other(seat));
      model.yourTurn = myMove();
      model.sharedDevice = !inMatch() && opponent == go::Opponent::Human;
      goui::buildCount(surface, model);
      break;
    }
    case go::Screen::Result: {
      goui::ResultModel model;
      model.game = game;
      model.seat = seat;
      for (int i = 0; i < go::kMaxPoints; ++i) model.owner[i] = owner[i];
      model.blackHalves = blackHalves;
      model.whiteHalves = whiteHalves;
      player::shortName(inMatch() ? opponentName() : nullptr, theirName, sizeof(theirName));
      model.opponentName = inMatch() ? theirName : nullptr;
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

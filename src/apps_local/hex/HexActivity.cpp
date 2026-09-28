#include "HexActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#if defined(ARDUINO_ARCH_ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

#include "../Shelf.h"
#include "../player/PlayerName.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "HexSave.h"
#include "HexScreens.h"

// The search gets a task of its own, and the number is a budget rather than a
// guess. Nothing here recurses -- the tree descent is a loop, the playout is a
// loop, and the flood fill that scores it carries its own explicit stack -- so
// the frames are shallow and the arrays are what cost: one playout holds a 121
// byte board, a 121 byte order, two 121 byte bridge queues and a 121 byte seen
// map, about 700 bytes, on top of the descent's 244 byte path.
//
// scripts_local/stack-budget.sh measures the deepest path the compiler can see
// at 1792 bytes, so sixteen kilobytes is about nine times what it needs -- the
// estimate above is optimistic, and the measurement is the number to trust. It
// is not eight kilobytes, which is what the Arduino loop task gets, because the
// margin is the point: the loop task is also running the render path, and an
// overflow there is a reboot with no log line to say why.
#define HEX_SEARCH_TASK_STACK 16384

std::unique_ptr<Activity> HexActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<HexActivity>(renderer, mappedInput);
}

// Beside the reader's own state and the player's name. A fork-local fact in a
// fork-local file, the pattern knucklebones.sav set.
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
constexpr char kSavePath[] = "/.crosspoint/hex.sav";
constexpr char kSaveTempPath[] = "/.crosspoint/hex.sav.tmp";
#endif

void HexActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  screen = hex::Screen::Menu;
  menuSelected = -1;
  hex::reset(game);
  loadSave();

  // Null-checked rather than assumed: under -fno-exceptions a bare `new` calls
  // abort() on OOM, and makeUniqueNoThrow hands back nothing instead. With no
  // pool the app still plays -- the brain answers with a centre-weighted legal
  // move -- which is a worse opponent and not a crash.
  pool = makeUniqueNoThrow<hexbrain::Pool>();
  if (!pool) LOG_ERR("HEX", "No search pool (%d bytes); the computer will play at random", (int)sizeof(hexbrain::Pool));

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
  if (xTaskCreatePinnedToCore(&HexActivity::searchTrampoline, "hex_search", HEX_SEARCH_TASK_STACK, this, 1, &handle,
                              searchCore) == pdPASS) {
    searchTask = handle;
  } else {
    // Not fatal: the search then runs on the loop task, which is where it would
    // compete with the repaint. Saying so is the point -- a fallback nobody can
    // see is a stutter nobody can explain.
    searchTask = nullptr;
    LOG_ERR("HEX", "No search task (%d bytes); searching on the loop task", HEX_SEARCH_TASK_STACK);
  }
#endif
  // The seed has to differ between boots or the computer plays the same game
  // every time. millis() at entry is the only entropy this app can reach, and
  // it is enough: nothing here is a secret.
  seed ^= static_cast<uint32_t>(millis()) * 2654435761u;
  if (seed == 0) seed = 0x9E3779B9u;
  requestUpdate();
}

void HexActivity::onExit() {
  writeSave();
#if defined(ARDUINO_ARCH_ESP32)
  // The task has to be GONE before this object is, because its loop reads this
  // object's members and its pool. It is parked on a notification, so waking it
  // with `searchEnding` set is what ends it, and it acknowledges before it
  // deletes itself.
  if (searchTask != nullptr) {
    searchEnding = true;
    searchWaiter = xTaskGetCurrentTaskHandle();
    xTaskNotifyGive(static_cast<TaskHandle_t>(searchTask));
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000));
    searchTask = nullptr;
  }
#endif
  // Freed here and not in the destructor, because the task above is what reads
  // it and this is the line that has just proved the task is gone.
  pool.reset();
  Activity::onExit();
}

#if defined(ARDUINO_ARCH_ESP32)
void HexActivity::searchTrampoline(void* self) { static_cast<HexActivity*>(self)->searchLoop(); }

void HexActivity::searchLoop() {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (searchEnding) break;
    searchResult =
        pool ? hexbrain::chooseMove(searchBoard, searchLevel, seed, *pool, []() -> uint32_t { return millis(); })
             : hex::kNoCell;
    xTaskNotifyGive(static_cast<TaskHandle_t>(searchWaiter));
  }
  // Nothing below this line may touch the activity: the waiter is free to
  // delete it the moment it is notified.
  TaskHandle_t waiter = static_cast<TaskHandle_t>(searchWaiter);
  xTaskNotifyGive(waiter);
  vTaskDelete(nullptr);
}
#endif

int HexActivity::chooseComputerMove(const hex::Game& snapshot) {
  if (!pool) {
    // No pool, no tree. A legal move is still owed: the first empty cell
    // nearest the middle, which is what the brain itself falls back to.
    int best = hex::kNoCell;
    int bestDistance = 0;
    for (int cell = 0; cell < hex::kCells; ++cell) {
      if (snapshot.at(cell) != hex::kEmpty) continue;
      const int dr = hex::rowOf(cell) - hex::kSize / 2;
      const int dc = hex::colOf(cell) - hex::kSize / 2;
      const int distance = dr * dr + dc * dc;
      if (best == hex::kNoCell || distance < bestDistance) {
        best = cell;
        bestDistance = distance;
      }
    }
    return best;
  }
#if defined(ARDUINO_ARCH_ESP32)
  if (searchTask != nullptr) {
    searchBoard = snapshot;
    searchLevel = level;
    searchResult = hex::kNoCell;
    searchWaiter = xTaskGetCurrentTaskHandle();
    xTaskNotifyGive(static_cast<TaskHandle_t>(searchTask));
    // No timeout. The search has its own clock and stops on it; a timeout here
    // would leave a task writing into searchResult while the loop moved on.
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    return searchResult;
  }
#endif
  // The clock the brain measures itself against. It is lent rather than called,
  // because HexBrain is freestanding and must stay that way: the host tests
  // pass none and get a search bounded by simulations alone, which is what
  // makes them deterministic.
  return hexbrain::chooseMove(snapshot, level, seed, *pool, []() -> uint32_t { return millis(); });
}

void HexActivity::loadSave() {
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
  if (!Storage.exists(kSavePath)) return;
  char buffer[hexsave::kMaxLineBytes] = {};
  if (Storage.readFileToBuffer(kSavePath, buffer, sizeof(buffer)) == 0) return;

  hexsave::Save save;
  if (!hexsave::unpack(buffer, save)) {
    LOG_ERR("HEX", "Save file did not parse; record and game both left alone");
    return;
  }
  wins = save.wins;
  losses = save.losses;
  hasHistory = save.hasHistory;
  lastWon = save.lastWon;
  for (int i = 0; i < hex::kCellBytes; ++i) lastCells[i] = save.lastCells[i];
  opponent = save.opponent;
  level = save.level;
  playAs = save.playAs;
  seat = playAs;
  inProgress = save.inProgress;
  if (inProgress) {
    game = save.game;
    seat = save.seat;
    resultRecorded = false;
    if (hex::over(game)) hex::winningChain(game, chain);
  }
#endif
}

void HexActivity::writeSave() {
  // Never write the save during a match: the position on screen is the shared
  // game, and this file is what a solo game resumes from. The check lives here
  // rather than at each call site, because no caller ever wants the other
  // behaviour and chess had this wrong in two separate doors.
  if (inMatch()) return;

#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
  hexsave::Save save;
  save.wins = wins;
  save.losses = losses;
  save.opponent = opponent;
  save.level = level;
  save.playAs = playAs;
  save.hasHistory = hasHistory;
  save.lastWon = lastWon;
  for (int i = 0; i < hex::kCellBytes; ++i) save.lastCells[i] = lastCells[i];
  save.inProgress = inProgress;
  save.game = game;
  save.seat = seat;

  char line[hexsave::kMaxLineBytes];
  const int bytes = hexsave::pack(save, line, sizeof(line));
  if (bytes <= 0) {
    LOG_ERR("HEX", "Save line did not fit %d bytes", static_cast<int>(sizeof(line)));
    return;
  }

  // Temp file then rename. openFileForWrite carries O_TRUNC, so writing in
  // place empties the file at open and power lost in that window leaves nothing
  // at all -- and this app writes on every move, which multiplies that window
  // by the length of a game.
  //
  // The temp write's answer is CHECKED, and that is the half a temp file is
  // useless without: removing the live save before knowing the replacement
  // exists turns a full card into a lost record and a lost game, which is the
  // total loss the whole dance is here to avoid.
  if (!Storage.writeFile(kSaveTempPath, String(line))) {
    LOG_ERR("HEX", "Save temp write failed; the card keeps the last good save");
    Storage.remove(kSaveTempPath);
    return;
  }
  Storage.remove(kSavePath);
  if (!Storage.rename(kSaveTempPath, kSavePath)) {
    LOG_ERR("HEX", "Save rename failed; the save is gone from the card");
    Storage.remove(kSaveTempPath);
  }
#endif
}

void HexActivity::goTo(const hex::Screen next) {
  screen = next;
  requestUpdate();
}

bool HexActivity::myMove() const {
  if (inMatch()) return linkYourTurn();
  // Two people sharing the device are both "me": the board accepts whoever is
  // to move, because there is only one pair of hands.
  if (opponent == hex::Opponent::Human) return true;
  return game.toMove == seat;
}

bool HexActivity::computerToMove() const {
  if (inMatch()) return false;
  if (opponent != hex::Opponent::Computer) return false;
  return !hex::over(game) && game.toMove != seat;
}

void HexActivity::beginSoloGame() {
  hex::reset(game);
  for (int i = 0; i < hex::kMaskBytes; ++i) chain[i] = 0;
  // Black moves first and there is no swap rule, so the colour the player chose
  // is exactly the advantage they chose with it.
  seat = opponent != hex::Opponent::Computer ? hex::kBlack : playAs;
  resultRecorded = false;
  inProgress = true;
  thinking = false;
  writeSave();
  goTo(hex::Screen::Board);
}

void HexActivity::noteFinished() {
  hex::winningChain(game, chain);
  inProgress = false;
}

void HexActivity::takeComputerTurn() {
  if (!computerToMove()) return;
  // The search runs on a COPY. The render task reads `game` on its own task,
  // and a search that walked the live board would put thousands of imagined
  // positions on the panel -- invisible here, half a second of garbage on the
  // device.
  const hex::Game snapshot = game;
  const uint32_t began = millis();
  const int move = chooseComputerMove(snapshot);
  const uint32_t took = millis() - began;
  thinking = false;

  // Both of the brain's own numbers, beside the wall clock this task measured.
  // They are not the same measurement and the difference is the point: `took`
  // is what the player waited, `lastMs()` is what the search believed it spent
  // on the clock it was lent, and a gap between them is a search that was
  // stopped by something other than its own budget.
  const hexbrain::Settings settings = hexbrain::settingsFor(level);
  LOG_INF("HEX", "search: level %d, %u ms (%u on its own clock) of %u, %d of %u sims (move %u)",
          static_cast<int>(level), static_cast<unsigned>(took), static_cast<unsigned>(hexbrain::lastMs()),
          static_cast<unsigned>(settings.budgetMs), hexbrain::lastSimulations(),
          static_cast<unsigned>(settings.simulations), static_cast<unsigned>(game.moveNumber));

  if (!hex::play(game, move)) {
    // Belt and braces: chooseMove promises a legal cell, and if it ever breaks
    // that promise the first empty one is played rather than freezing on a turn
    // nobody can take.
    LOG_ERR("HEX", "Brain offered an unplayable cell %d; taking the first empty one", move);
    for (int cell = 0; cell < hex::kCells; ++cell) {
      if (hex::play(game, cell)) break;
    }
  }
  if (hex::over(game)) {
    noteFinished();
    recordResult();
    writeSave();
    goTo(hex::Screen::Result);
    return;
  }
  inProgress = true;
  writeSave();
  requestUpdate();
}

void HexActivity::handleCellActivated(const int cell) {
  if (hex::tapMeaning(game, cell, myMove()) == hex::Tap::Ignore) {
    // A tap on a cell that cannot be played changes nothing, so it must not
    // repaint. A refresh that comes back identical is what a bug looks like on
    // e-ink: the panel visibly blinks and says the same thing.
    return;
  }
  if (!hex::play(game, cell)) return;

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

  if (hex::over(game)) {
    noteFinished();
    if (!inMatch()) {
      recordResult();
      writeSave();
      goTo(hex::Screen::Result);
      return;
    }
    // In a match the layer owns the ending: onMatchEnded() is where it is
    // counted, on the pass it happened, on BOTH devices.
    requestUpdate();
    return;
  }
  // The computer's reply is started one pass later, so the repaint carrying
  // your own stone and the word THINKING lands before the search begins.
  thinking = computerToMove();
  requestUpdate();
}

void HexActivity::recordResult() {
  if (resultRecorded) return;
  resultRecorded = true;

  const uint8_t won = game.winner;
  // Two people sharing the device have no "you", so the record counts Black's
  // result. Calling one of them the device's own player would be a lie the
  // front door then repeats every time it is drawn.
  lastWon = opponent == hex::Opponent::Human && !inMatch() ? won == hex::kBlack : won == seat;
  if (lastWon) {
    ++wins;
  } else {
    ++losses;
  }
  for (int i = 0; i < hex::kCellBytes; ++i) lastCells[i] = game.cell[i];
  hasHistory = true;
}

const char* HexActivity::linkHeadline() const {
  if (linkPhase() == linkplay::PlayBase::Phase::Searching) return "LOOKING FOR A PLAYER";
  if (!hex::over(game)) return "HEX";
  return game.winner == seat ? "YOU WIN" : "THEY WIN";
}

void HexActivity::onMatchStart(const bool goesFirst) {
  // reset() puts Black to move, so whoever goes first IS Black. Both sides
  // deal: there is no randomness in an opening Hex position, so reset() is
  // identical on both devices and there is nothing to wait for. A follower that
  // started from a zeroed struct would hold a board whose union-find made every
  // cell its own root, which reads as a legal game nobody can ever win.
  seat = goesFirst ? hex::kBlack : hex::kWhite;
  resultRecorded = false;
  thinking = false;
  hex::reset(game);
  for (int i = 0; i < hex::kMaskBytes; ++i) chain[i] = 0;
  goTo(hex::Screen::Board);
}

bool HexActivity::takeOpponentState() {
  if (!play.takeOpponent(game)) return false;
  if (hex::over(game)) noteFinished();
  return true;
}

void HexActivity::onRematch() { onMatchStart(play.goesFirst()); }

void HexActivity::onLinkEnded() {
  // The solo game comes BACK. onMatchStart resets the board straight over it,
  // so without this the front door offers RESUME and opens the match's final
  // position with the wrong seat -- and the computer starts playing it. The
  // card still holds the solo game: writeSave() refuses for the whole length of
  // a match, so nothing has overwritten it.
  thinking = false;
  hex::reset(game);
  for (int i = 0; i < hex::kMaskBytes; ++i) chain[i] = 0;
  inProgress = false;
  seat = playAs;

  // The match's result is the one thing that must NOT come back off the card.
  // onMatchEnded() counted it in memory and could not write it -- writeSave()
  // refuses for the whole length of a match -- so the reload below would take
  // the pre-match tally straight back over it. The decision is
  // hexsave::recordAfterLink()'s rather than this file's, because it is a rule
  // and this is the layer that is not allowed to hold one.
  hexsave::Record counted;
  counted.wins = wins;
  counted.losses = losses;
  counted.hasHistory = hasHistory;
  counted.lastWon = lastWon;
  uint8_t countedCells[hex::kCellBytes];
  for (int i = 0; i < hex::kCellBytes; ++i) countedCells[i] = lastCells[i];

  loadSave();

  hexsave::Record onCard;
  onCard.wins = wins;
  onCard.losses = losses;
  onCard.hasHistory = hasHistory;
  onCard.lastWon = lastWon;

  const hexsave::Record kept = hexsave::recordAfterLink(counted, onCard);
  wins = kept.wins;
  losses = kept.losses;
  hasHistory = kept.hasHistory;
  lastWon = kept.lastWon;
  // The ornament follows whichever record was kept, or the front door draws one
  // game's board under another game's caption.
  if (kept.wins == counted.wins && kept.losses == counted.losses) {
    for (int i = 0; i < hex::kCellBytes; ++i) lastCells[i] = countedCells[i];
  }
  // Written now, because the refusal above has just stopped applying and this
  // is the first moment the counted match can reach the card.
  writeSave();

  goTo(hex::Screen::Menu);
}

// `seat` is here because it decides whose move it is, and the live bit because
// driveLink() runs ahead of gameLoop() on every pass and can hand the turn over
// with no tap from this player: a board that was inert one pass ago starts
// accepting stones while the panel still shows the position before their move.
//
// The board's CONTENTS are deliberately absent. They do not move which cell a
// pixel is -- the layout is a function of the panel alone -- and gating on them
// would gate every consecutive placement, which is every move in this game.
uint32_t HexActivity::surfaceMeaning() const {
  const uint32_t withScreen = paintclock::mixMeaning(paintclock::kMeaningSeed, static_cast<uint32_t>(screen));
  const uint32_t withSeat = paintclock::mixMeaning(withScreen, seat);
  return paintclock::mixMeaning(withSeat, myMove() && !hex::over(game) ? 1u : 0u);
}

void HexActivity::onMatchEnded() {
  recordResult();
  goTo(hex::Screen::Result);
}

void HexActivity::gameLoop() {
  namespace fui = freeink::ui;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (inMatch()) {
      leaveLink();
      return;
    }
    if (hex::leavesApp(screen)) {
      writeSave();
      shelf::leave(renderer, mappedInput);
      return;
    }
    goTo(hex::back(screen));
    return;
  }

  if (screen == hex::Screen::Board) {
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

  // A hundred and twenty one cells against a twenty-four slot interaction
  // buffer, so the board is hit-tested from the geometry that drew it rather
  // than registered cell by cell. Tried before the registered controls, because
  // it covers most of the screen -- and it answers no for the notches the
  // rhombus leaves, which is where the buttons are.
  if (screen == hex::Screen::Board) {
    const fui::DeviceContext device = toybox::makeTarget(renderer).deviceContext();
    int cell = 0;
    if (hexui::cellAt(hexui::boardLayout(device), tapX, tapY, cell)) {
      if (!surfaceRevealed()) return;
      handleCellActivated(cell);
      return;
    }
  }

  const fui::ActionEvent event = interactions.route(input);
  switch (event.action) {
    case hexui::ActionMenuRow:
      switch (static_cast<hexui::MenuRow>(event.value)) {
        case hexui::MenuRow::Play:
          // RESUME rather than a new game when one is part-played: throwing a
          // position away from the front door with no warning is how a player
          // loses a game they left on the train.
          if (inProgress && !hex::over(game)) {
            thinking = false;
            goTo(hex::Screen::Board);
            return;
          }
          beginSoloGame();
          return;
        case hexui::MenuRow::PlayNearby:
          enterLink(linkplay::GameId::Hex);
          return;
        case hexui::MenuRow::Settings:
          settingsSelected = -1;
          goTo(hex::Screen::Settings);
          return;
        case hexui::MenuRow::Count:
          return;
      }
      return;

    case hexui::ActionSettingsRow:
      switch (static_cast<hexui::SettingsRow>(event.value)) {
        case hexui::SettingsRow::Opponent:
          // The row changes in place and the front door relabels itself;
          // leaving is what applies it. A setting that jumps you somewhere else
          // makes you guess whether it worked.
          opponent = opponent == hex::Opponent::Computer ? hex::Opponent::Human : hex::Opponent::Computer;
          inProgress = false;
          settingsSelected = static_cast<int>(hexui::SettingsRow::Opponent);
          writeSave();
          requestUpdate();
          return;
        case hexui::SettingsRow::Level:
          if (opponent != hex::Opponent::Computer) return;
          level = hex::nextLevel(level);
          // Strength only, so a game in progress could in principle survive it.
          // It is dropped anyway, because the other rows here drop it and a
          // list where one row keeps your game and two throw it away is a list
          // nobody can predict.
          inProgress = false;
          settingsSelected = static_cast<int>(hexui::SettingsRow::Level);
          writeSave();
          requestUpdate();
          return;
        case hexui::SettingsRow::PlayAs:
          if (opponent != hex::Opponent::Computer) return;
          playAs = hex::other(playAs);
          seat = playAs;
          inProgress = false;
          settingsSelected = static_cast<int>(hexui::SettingsRow::PlayAs);
          writeSave();
          requestUpdate();
          return;
        case hexui::SettingsRow::Count:
          return;
      }
      return;

    case hexui::ActionAgain:
      if (inMatch()) {
        proposeRematch();
        return;
      }
      beginSoloGame();
      return;

    case hexui::ActionDone:
      // DONE on a finished MATCH means done with the match, not with the
      // screen: the radio is still up and the link screen would slam over the
      // menu the moment the hold ended.
      if (inMatch()) {
        leaveLink();
        return;
      }
      goTo(hex::Screen::Menu);
      return;

    default:
      return;
  }
}

void HexActivity::gameRender() {
  namespace fui = freeink::ui;

  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, device, noInput, interactions);
  toybox::Screen surface(frame);

  switch (screen) {
    case hex::Screen::Menu: {
      hexui::MenuModel model;
      model.selected = menuSelected;
      model.inProgress = inProgress && !hex::over(game);
      model.lastWon = lastWon;
      model.wins = wins;
      model.losses = losses;
      model.moveNumber = game.moveNumber;
      if (model.inProgress) {
        model.boardCells = game.cell;
      } else if (hasHistory) {
        model.boardCells = lastCells;
      }
      hexui::buildMenu(surface, model);
      break;
    }
    case hex::Screen::Settings: {
      hexui::SettingsModel model;
      model.selected = settingsSelected;
      model.opponent = opponent;
      model.level = level;
      model.playAs = playAs;
      hexui::buildSettings(surface, model);
      break;
    }
    case hex::Screen::Board: {
      hexui::BoardModel model;
      model.game = game;
      model.seat = seat;
      player::shortName(inMatch() ? opponentName() : nullptr, theirName, sizeof(theirName));
      model.opponentName = inMatch() ? theirName : nullptr;
      model.sharedDevice = !inMatch() && opponent == hex::Opponent::Human;
      model.thinking = thinking;
      hexui::buildBoard(surface, model);
      break;
    }
    case hex::Screen::Result: {
      hexui::ResultModel model;
      model.game = game;
      model.seat = seat;
      for (int i = 0; i < hex::kMaskBytes; ++i) model.chain[i] = chain[i];
      player::shortName(inMatch() ? opponentName() : nullptr, theirName, sizeof(theirName));
      model.opponentName = inMatch() ? theirName : nullptr;
      model.sharedDevice = !inMatch() && opponent == hex::Opponent::Human;
      hexui::buildResult(surface, model);
      break;
    }
  }

  interactionsReady = true;
  toybox::reportOverflow(interactions, "Hex");

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

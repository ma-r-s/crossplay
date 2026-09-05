#include "ToyBattleActivity.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>

#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxFormat.h"
#include "../ui/ToyboxTheme.h"
#include "ToyBattleScreens.h"

namespace tb = toybattle;

// The fork-local convention, the pattern knucklebones.sav set.
static constexpr char kSavePath[] = "/.crosspoint/toybattle.sav";
// A separate file for the record line, because kSavePath is removed the moment
// a game ends. Text and versioned, the way battleship writes bship.cfg.
static constexpr char kStatsPath[] = "/.crosspoint/toybattle.stats";
static constexpr int kStatsVersion = 1;

std::unique_ptr<Activity> ToyBattleActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return std::make_unique<ToyBattleActivity>(renderer, mappedInput);
}

void ToyBattleActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  // Open on whatever was left in the middle, so CONTINUE means what it says the
  // moment the app appears.
  hasSave = loadGame();
  dealt = hasSave;
  loadStats();
  screen = tb::Screen::Menu;
  menuSelected = 0;
  refreshSaveLine();
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
  // Seat 0 always moves first; SIDE says whether that is you. On the two boards
  // that are not symmetric it also decides which H.Q. is yours, and the screen
  // turns the board round so you are never playing upside down.
  game.newGame(static_cast<uint32_t>(millis()) * 2654435761u + 1u, options.terrain, 0, options.specialBases);
  draft.clear();
  notice = nullptr;
  seat = options.side;
  dealt = true;
  hasSave = true;
  recorded = false;
  goTo(tb::Screen::Board);
}

void ToyBattleActivity::takeOpponentTurn() {
  const tb::Observation obs = tb::observe(game, game.turn);
  game.apply(tb::chooseMove(obs, options.skill));
  requestUpdate();
}

tbui::MenuModel ToyBattleActivity::menuModel() const {
  tbui::MenuModel model;
  model.selected = menuSelected;
  model.hasSave = hasSave;
  model.saveDetail = saveDetail;
  model.played = played;
  model.won = won;
  model.options = options;
  model.preview = &preview;
  return model;
}

void ToyBattleActivity::openMenu() {
  menuSelected = 0;
  goTo(tb::Screen::Menu);
}

void ToyBattleActivity::cycleSetupRow(const tbui::SetupRow row) {
  switch (row) {
    case tbui::SetupRow::Map:
      mapPage = 0;
      goTo(tb::Screen::MapPick);
      return;
    case tbui::SetupRow::Opponent:
      options.skill = static_cast<tb::Skill>((static_cast<int>(options.skill) + 1) % tb::kSkillCount);
      break;
    case tbui::SetupRow::Side:
      options.side ^= 1;
      break;
    case tbui::SetupRow::Bases:
      options.specialBases = !options.specialBases;
      break;
    case tbui::SetupRow::Count:
      return;
  }
  requestUpdate();
}

void ToyBattleActivity::refreshSaveLine() {
  // The preview is the board the front door draws. With no save it is the map
  // you are about to play, empty, so the ornament is never a board from a
  // different game than the one START would begin.
  if (!hasSave) {
    preview = tb::Game{};
    preview.newGame(1u, options.terrain, 0, options.specialBases);
    saveDetail[0] = '\0';
    return;
  }
  // The board behind the glass is the game you would actually return to, not a
  // fresh one on the same map. A CONTINUE row that shows an empty board is
  // offering something it does not have.
  preview = game;
  std::snprintf(saveDetail, sizeof(saveDetail), "%d-%d", game.medals[seat], game.medals[seat ^ 1]);
}

void ToyBattleActivity::goTo(const tb::Screen next) {
  // Every road to the menu passes through here, so the front door is never
  // showing a save line from before the last move.
  if (next == tb::Screen::Menu) refreshSaveLine();
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
  if (!dealt) return "WAITING FOR THE DEAL";
  if (game.currentPhase() != tb::Phase::Playing) {
    // The verdict AND how it happened, because the three endings are genuinely
    // different games and "YOU LOSE" alone does not say which one you played.
    // This line is the ending now: the board stays up behind it rather than
    // being swept away for a screen that says the same thing.
    const bool mine = game.winner == seat;
    if (game.winner == tb::kNoSeat) return "A DRAW: NOBODY COULD MOVE";
    switch (game.endedBy()) {
      case tb::Ending::HqCaptured:
        return mine ? "YOU WIN: THEIR H.Q. IS TAKEN" : "YOU LOSE: YOUR H.Q. IS TAKEN";
      case tb::Ending::MedalsObjective:
        return mine ? "YOU WIN ON MEDALS" : "THEY WIN ON MEDALS";
      case tb::Ending::Stuck:
        return mine ? "YOU WIN: NOBODY COULD MOVE" : "YOU LOSE: NOBODY COULD MOVE";
      case tb::Ending::None:
        break;
    }
    return mine ? "YOU WIN" : "YOU LOSE";
  }
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

void ToyBattleActivity::recordResult() {
  if (recorded) return;
  recorded = true;
  ++played;
  if (game.winner == seat) ++won;
  // Written here, at the finish, rather than in onExit: a chip-reset wake loses
  // anything not on the card, so the count reaches disk the moment it changes.
  saveStats();
  hasSave = false;
  if (Storage.exists(kSavePath)) Storage.remove(kSavePath);
  requestUpdate();
}

// The first caller in a match, and for a long time there was no other: the link
// layer used to stop giving gameLoop() the pass the moment the battle ended, so
// the block below never ran and no link battle was ever counted. It runs again
// now, during the couple of seconds the finished board stays up, which is
// harmless because `recorded` latches. No screen to change either -- this game's
// board stays put by design, and that is already the thing worth looking at.
void ToyBattleActivity::onMatchEnded() { recordResult(); }

void ToyBattleActivity::gameLoop() {
  namespace fui = freeink::ui;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (screen == tb::Screen::HowTo && howToPage > 0) {
      // A paginated screen steps a page before it leaves, which is what Back
      // means to somebody who has been tapping NEXT.
      --howToPage;
      requestUpdate();
      return;
    }
    if (tb::leavesApp(screen)) {
      // No app names its own destination; the shelf decides where out is.
      shelf::leave(renderer, mappedInput);
      return;
    }
    if (screen == tb::Screen::Board) {
      // In a match, Back is leaving the match. Every other game on the link
      // layer says so; this one walked to its own menu with the radio up and
      // the opponent never told, and the link screen arrived over the menu a
      // moment later. saveGame() is a no-op in a match anyway.
      if (inMatch()) {
        leaveLink();
        return;
      }
      // Leaving the board writes it, the same as leaving the app does. onExit
      // is the call that matters because sleep makes it when the player does
      // nothing, but a board abandoned by Back and then never slept would
      // otherwise be a game the menu offers and cannot open.
      saveGame();
    }
    goTo(tb::back(screen));
    return;
  }

  if (screen == tb::Screen::MapPick && tbui::mapPages() > 1) {
    const bool down = mappedInput.wasReleased(MappedInputManager::Button::Down);
    const bool up = mappedInput.wasReleased(MappedInputManager::Button::Up);
    if (down || up) {
      mapPage += down ? 1 : -1;
      if (mapPage < 0) mapPage = tbui::mapPages() - 1;
      if (mapPage >= tbui::mapPages()) mapPage = 0;
      requestUpdate();
      return;
    }
  }

  if (screen == tb::Screen::Board && dealt && game.currentPhase() != tb::Phase::Playing) {
    // The board STAYS. Mario, 2026-08-12: winning used to sweep the whole
    // position away and replace it with a sentence, and what you want at that
    // moment is to look at the board -- to see the troop standing on their
    // H.Q., or the medal that finished it.
    //
    // This was tried once before and reverted because it "left nowhere to go
    // but Back". That was the real objection and it is answered by giving the
    // action bar a way on rather than by taking the board away.
    //
    // The one place a result is recorded and the save is cleared: a finished
    // game must not be offered as one to continue. recordResult() latches, so
    // running it every pass is a no-op after the first.
    recordResult();
    // No return: the finished board's action bar (HOW IT ENDED, ?) is the only
    // way on and has to answer a tap. Falling through reaches interactions.route
    // below; the brain and the board's own hit-testing are both gated on
    // Phase::Playing (and canAct()), so only the registered controls are live.
  }

  // The brain only plays when there is nobody on the other end. In a match the
  // opposite seat is a person, and their move arrives through the link.
  if (!inMatch() && screen == tb::Screen::Board && game.currentPhase() == tb::Phase::Playing && game.turn != seat) {
    notice = nullptr;
    takeOpponentTurn();
    return;
  }

  int tapX = 0, tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY) || !interactionsReady) return;

  // The board and the rack are 25 targets against a 24-slot buffer, so both are
  // hit-tested from the geometry that drew them, before the registered
  // controls get a look.
  if (screen == tb::Screen::Board && canAct()) {
    const fui::DeviceContext device = toybox::makeTarget(renderer).deviceContext();
    const tb::Ask ask = tb::pending(game, draft);

    const int kind = tbui::rackAt(device, game, draft, seat, tapX, tapY);
    if (kind >= 0) {
      const tb::Troop troop = static_cast<tb::Troop>(kind);
      // The row shows the DISCARD while Cursed Cemetery is asking, so a tap on
      // it answers that question rather than choosing something to place.
      // Without this the question could be asked and not answered.
      if (ask == tb::Ask::ExhumeKind) {
        if (tb::answerTarget(game, draft, kind)) {
          notice = nullptr;
          say(tbui::troopBlurb(troop));
          if (tb::pending(game, draft) == tb::Ask::Ready) commitMove();
          requestUpdate();
        }
        return;
      }
      if (tb::answerTroop(game, draft, troop)) {
        // Picking a card says what the card does, which is the moment the
        // player wants to know it.
        say(tbui::troopBlurb(troop));
      } else {
        say(tbui::refusalBlurb(tb::whyNotTroop(game, draft, troop)));
      }
      return;
    }
    const int slot = tbui::slotAt(device, game.board(), tapX, tapY, seat);
    if (slot >= 0) {
      const tb::Refusal why = tb::whyNotSlot(game, draft, slot);
      if (why != tb::Refusal::None) {
        say(tbui::refusalBlurb(why));
        return;
      }
      const bool took = ask == tb::Ask::Slot ? tb::answerSlot(game, draft, slot) : tb::answerTarget(game, draft, slot);
      if (took) {
        notice = nullptr;
        if (tb::pending(game, draft) == tb::Ask::Ready) commitMove();
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
    case tbui::ActionShellRow: {
      const tbui::ShellRow row = tbui::shellRowAt(menuModel(), event.value);
      switch (row) {
        case tbui::ShellRow::Continue:
          if (hasSave) goTo(tb::Screen::Board);
          return;
        case tbui::ShellRow::Play:
          options.mode = tb::Mode::Solo;
          setupSelected = 0;
          goTo(tb::Screen::Setup);
          return;
        case tbui::ShellRow::Nearby:
          options.mode = tb::Mode::Link;
          setupSelected = 0;
          goTo(tb::Screen::Setup);
          return;
        case tbui::ShellRow::HowTo:
          howToPage = 0;
          goTo(tb::Screen::HowTo);
          return;
        case tbui::ShellRow::Count:
          return;
      }
      return;
    }
    case tbui::ActionStart:
      if (options.mode == tb::Mode::Link) {
        // Nothing happens on the radio until the next pass, so a failure to
        // start lands the player back on a game they can play.
        goTo(tb::Screen::Lobby);
        enterLink(linkplay::GameId::ToyBattle);
        return;
      }
      requestNewGame();
      return;
    case tbui::ActionSetupRow: {
      tbui::SetupModel setup;
      setup.options = options;
      setup.forLink = options.mode == tb::Mode::Link;
      cycleSetupRow(tbui::setupRowAt(setup, event.value));
      return;
    }
    case tbui::ActionOpenMaps:
      mapPage = 0;
      goTo(tb::Screen::MapPick);
      return;
    case tbui::ActionMapRow:
      if (event.value >= 0 && event.value < tb::kTerrainCount) {
        options.terrain = static_cast<uint8_t>(event.value);
        refreshSaveLine();
      }
      goTo(tb::Screen::Setup);
      return;
    case tbui::ActionPickSkill:
      // The picker hands back which rung was tapped; the list hands back the
      // row and means "next one". Both land here.
      if (event.value >= 0 && event.value < tb::kSkillCount) {
        options.skill = static_cast<tb::Skill>(event.value);
      } else {
        cycleSetupRow(tbui::SetupRow::Opponent);
      }
      requestUpdate();
      return;
    case tbui::ActionPickBases:
      options.specialBases = !options.specialBases;
      requestUpdate();
      return;
    case tbui::ActionPageNext:
      if (howToPage + 1 >= tbui::howToPages()) {
        // The last page returns to the menu rather than dropping you into a
        // board: you have just been taught, and what you want next is to pick.
        openMenu();
        return;
      }
      ++howToPage;
      requestUpdate();
      return;
    case tbui::ActionPagePrev:
      if (howToPage == 0) {
        openMenu();
        return;
      }
      --howToPage;
      requestUpdate();
      return;
    case tbui::ActionAgain:
      requestNewGame();
      return;
    case tbui::ActionDone:
      goTo(tb::Screen::Menu);
      return;
    case tbui::ActionDraw:
      // The other half of a turn, and it had no way in before now.
      if (!canAct()) return;
      draft.move = tb::Move::draw();
      draft.slotChosen = true;
      commitMove();
      say("YOU DREW TWO");
      return;
    case tbui::ActionBrief:
      goTo(tb::Screen::Brief);
      return;
    case tbui::ActionResult:
      goTo(tb::Screen::Result);
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
        if (tb::pending(game, draft) == tb::Ask::Ready) commitMove();
        requestUpdate();
      }
      return;
    default:
      return;
  }
}

void ToyBattleActivity::gameRender() {
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
    case tb::Screen::HowTo: {
      tbui::HowToModel model;
      model.page = howToPage;
      tbui::buildHowTo(surface, model);
      break;
    }
    case tb::Screen::MapPick: {
      tbui::MapPickModel model;
      model.page = mapPage;
      tbui::buildMapPick(surface, model);
      break;
    }
    case tb::Screen::Setup: {
      tbui::SetupModel model;
      model.options = options;
      model.selected = setupSelected;
      model.forLink = options.mode == tb::Mode::Link;
      tbui::buildSetup(surface, model);
      break;
    }
    case tb::Screen::Lobby:
    case tb::Screen::Menu: {
      tbui::buildMenu(surface, menuModel());
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

// ---------------------------------------------------------------------------
// Playing a person
// ---------------------------------------------------------------------------

bool ToyBattleActivity::canAct() const {
  const bool mine = game.currentPhase() == tb::Phase::Playing && game.turn == seat;
  if (!inMatch()) return mine;
  // Asking whether the rules and the link AGREE is a different question, and it
  // is true on their turn as well. Both have to say yes.
  if (linkYourTurn() && !mine) {
    LOG_ERR("TB", "the link offered our turn and the rules disagree");
    return false;
  }
  return mine && linkYourTurn();
}

void ToyBattleActivity::commitMove() {
  if (!game.apply(draft.move)) {
    draft.clear();
    return;
  }
  draft.clear();
  if (inMatch() && !link.play(game)) LOG_ERR("TB", "the link refused a move on our own turn");
  requestUpdate();
}

void ToyBattleActivity::requestNewGame() {
  // Never restart a shared board unilaterally: in a match this is a question,
  // not a board that resets under the other player.
  if (inMatch()) {
    proposeRematch();
    return;
  }
  beginGame();
}

void ToyBattleActivity::onMatchStart(const bool goesFirst) {
  seat = goesFirst ? 0 : 1;
  draft.clear();
  notice = nullptr;
  recorded = false;
  if (goesFirst) {
    // The leader deals and passes at once, rather than dealing and holding the
    // turn. Holding it means the opening does not leave this device until the
    // dealer has decided on a move, and the other player spends that whole time
    // looking at an empty rack and a board with nothing on it -- which is
    // exactly what two simulators showed.
    //
    // So the deal names the OTHER seat as the starter and goes out immediately.
    // The coin toss decides who deals; it does not have to decide who moves.
    game.newGame(static_cast<uint32_t>(millis()) * 2654435761u + 1u, options.terrain, 1, options.specialBases);
    dealt = true;
    if (!link.play(game)) LOG_ERR("TB", "the link refused the opening on our own turn");
  } else {
    // The follower must not deal. A randomised opening would be a different
    // game on each device, and a zeroed one has no legal move and reads as
    // finished -- which latches the rematch screen permanently.
    game = tb::Game{};
    dealt = false;
  }
  screen = tb::Screen::Board;
  requestUpdate();
}

bool ToyBattleActivity::takeOpponentState() {
  tb::Game arriving{};
  if (!link.takeOpponent(arriving)) return false;
  // The layer checks the payload length and copies. Everything about the
  // contents is ours: a corrupt packet arrives here as a `turn` of 200 indexing
  // a two-seat array.
  if (arriving.terrain >= tb::kTerrainCount || arriving.turn >= tb::kSeats || !arriving.isWellFormed()) {
    LOG_ERR("TB", "rejected an implausible position from the wire");
    return false;
  }
  game = arriving;
  dealt = true;
  draft.clear();
  notice = nullptr;
  if (screen != tb::Screen::Board) screen = tb::Screen::Board;
  requestUpdate();
  return true;
}

void ToyBattleActivity::onRematch() { onMatchStart(link.goesFirst()); }

void ToyBattleActivity::onLinkEnded() {
  // Has to be safe on the very first pass after enterLink(), before any match
  // existed: the radio failing to start comes through here.
  options.mode = tb::Mode::Solo;
  seat = 0;
  draft.clear();
  notice = nullptr;
  dealt = loadGame();
  hasSave = dealt;
  refreshSaveLine();
  openMenu();
}

bool ToyBattleActivity::matchGameOver() const {
  // Polled every pass, and the layer latches the rematch screen the moment it
  // is true, irreversibly. "Nothing has been dealt yet" must not read as over.
  return dealt && game.currentPhase() != tb::Phase::Playing;
}

const char* ToyBattleActivity::linkHeadline() const {
  if (!dealt) return "LOOKING FOR A PLAYER";
  if (game.currentPhase() != tb::Phase::Playing) {
    if (game.winner == tb::kNoSeat) return "A DRAW";
    return game.winner == seat ? "YOU WIN" : "THEY WIN";
  }
  std::snprintf(headline, sizeof(headline), "%s", tb::terrainAt(game.terrain).name);
  return headline;
}

void ToyBattleActivity::drawLinkArt(const Rect& area) {
  // The board you have just finished, on the rematch screen. Different every
  // time and identical on nobody else's device, which is the test this fork
  // applies to anything decorative.
  if (!dealt) return;
  namespace fui = freeink::ui;
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  // A scratch buffer, not the shared one: the link chrome owns the screen and
  // its own hit table while this runs, and drawing through `interactions` would
  // rebuild the table under the buttons the player is looking at. Static
  // because a 24-entry buffer is well past this project's 256-byte stack rule.
  static toybox::Interactions scratch;
  toybox::Frame frame(target, device, noInput, scratch);
  toybox::Screen surface(frame);
  tbui::miniBoard(surface,
                  fui::makeRect(static_cast<int16_t>(area.x), static_cast<int16_t>(area.y),
                                static_cast<int16_t>(area.width), static_cast<int16_t>(area.height)),
                  game.board(), &game, 0);
}

// ---------------------------------------------------------------------------
// Putting it down
// ---------------------------------------------------------------------------

void ToyBattleActivity::saveGame() {
  // The guard lives in the callee rather than at each call site, because the
  // one that matters is onExit() -- which runs on sleep, which the player
  // triggers by doing nothing.
  if (linkRequested()) return;
  if (!dealt || game.currentPhase() != tb::Phase::Playing) {
    if (Storage.exists(kSavePath)) Storage.remove(kSavePath);
    return;
  }
  tb::Saved saved;
  saved.options = options;
  saved.game = game;
  saved.seat = seat;
  uint8_t bytes[tb::kSaveBytes];
  const int n = tb::encodeSave(saved, bytes);
  HalFile file;
  if (!Storage.openFileForWrite("TB", kSavePath, file)) return;
  file.write(bytes, static_cast<size_t>(n));
  file.flush();
}

bool ToyBattleActivity::loadGame() {
  if (!Storage.exists(kSavePath)) return false;
  HalFile file;
  if (!Storage.openFileForRead("TB", kSavePath, file)) return false;
  uint8_t bytes[tb::kSaveBytes];
  const size_t read = file.read(bytes, sizeof(bytes));
  tb::Saved saved;
  if (read != sizeof(bytes) || !tb::decodeSave(bytes, static_cast<int>(read), saved)) {
    LOG_INF("TB", "Ignoring a save this build cannot read");
    return false;
  }
  if (!tb::isResumable(saved)) return false;
  options = saved.options;
  game = saved.game;
  seat = saved.seat;
  preview = saved.game;
  return true;
}

void ToyBattleActivity::saveStats() const {
  // "%d %d %d\n"
  constexpr int kLineChars =
      toybox::kIntChars + toybox::kIntChars + toybox::kIntChars + toybox::literalChars("  \n") + 1;
  char line[kLineChars];
  std::snprintf(line, sizeof(line), "%d %d %d\n", kStatsVersion, played, won);
  Storage.writeFile(kStatsPath, String(line));
}

void ToyBattleActivity::loadStats() {
  // No file is the ordinary state on every device that ran a build before this
  // one: leave the counters at zero rather than treat its absence as an error.
  if (!Storage.exists(kStatsPath)) return;
  char buffer[48] = {};
  if (Storage.readFileToBuffer(kStatsPath, buffer, sizeof(buffer)) == 0) return;
  int version = 0;
  int storedPlayed = 0;
  int storedWon = 0;
  if (std::sscanf(buffer, "%d %d %d", &version, &storedPlayed, &storedWon) < 3) return;
  if (version != kStatsVersion) return;
  played = storedPlayed;
  won = storedWon;
}

void ToyBattleActivity::onExit() {
  saveGame();
  Activity::onExit();
}

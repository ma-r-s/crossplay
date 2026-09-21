#include "HeartsActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>

#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxTheme.h"

namespace ui = heartsui;
namespace fui = freeink::ui;
using namespace hearts;

namespace {

constexpr const char* kSavePath = "/.crosspoint/hearts.sav";
constexpr const char* kSkillPath = "/.crosspoint/hearts.skill";
constexpr const char* kStatsPath = "/.crosspoint/hearts.res";

// Bumped when the save layout changes, per the cache-format rule. An old save
// is discarded rather than misread, which for a card game means a table with
// two of the same card on it.
constexpr uint8_t kSaveVersion = 1;

// THE SAVE IS WRITTEN AT EVERY TRICK BOUNDARY, AND ON THE WAY OUT.
//
// The routine writes are at trick boundaries; leaving the board and exiting the
// activity write immediately, mid-trick included, because losing a hand to a
// closed app is worse than the write.
//
// The reason for the boundary is TRAFFIC, not correctness. Solitaire wrote
// ~340 bytes on every tap, 150+ times a session, and this struct is bigger; at
// a boundary it is thirteen writes a hand rather than one per card. Restoring
// mid-trick is perfectly sound -- the struct carries `turn`, the table and
// every hand, so nothing has to be reconstructed. An earlier version of this
// comment claimed the opposite and forbade something the code was already
// doing two lines away, which is worse than no comment.

// How long a completed trick sits on the table before it is swept.
//
// THIS IS THE ONE NUMBER THAT DECIDES WHETHER THE GAME IS FOLLOWABLE. A trick
// is four cards landing one at a time, and the fourth one decides it; swept on
// the next loop pass, that fourth card is drawn and removed inside a single
// panel settle and is never actually seen. 900ms is about two partial refreshes
// -- long enough to read four cards and the line saying who took them, short
// enough that thirteen of them do not feel like waiting.
constexpr uint32_t kTrickHoldMs = 900;

// A bot plays on a beat too, for the same reason: four cards appearing at once
// is a result, not a game.
constexpr uint32_t kBotThinkMs = 420;

// How many tricks of partial refreshes before one full one. Thirteen tricks is
// about sixty-five partials; four is three flashes a hand, at trick boundaries
// where the table is already pausing to be read, and never mid-decision.
constexpr uint8_t kTricksPerFlash = 4;

const char* seatName(const Seat seat) {
  switch (seat) {
    case Seat::South:
      return "YOU";
    case Seat::West:
      return "WEST";
    case Seat::North:
      return "NORTH";
    case Seat::East:
      return "EAST";
  }
  return "?";
}

// "YOU TAKES IT" was on the panel. The seat that is you is the only one that
// conjugates differently, and it is the one named most often.
const char* takesVerb(const Seat seat) { return seat == Seat::South ? "TAKE" : "TAKES"; }

const char* passName(const Pass pass) {
  switch (pass) {
    case Pass::Left:
      return "LEFT";
    case Pass::Right:
      return "RIGHT";
    case Pass::Across:
      return "ACROSS";
    case Pass::Hold:
      return "NOBODY";
  }
  return "?";
}

}  // namespace

std::unique_ptr<Activity> HeartsActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<HeartsActivity>(renderer, mappedInput);
}

void HeartsActivity::onEnter() {
  renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
  toybox::ensureFonts(renderer);
  rng = static_cast<uint32_t>(millis()) | 1u;
  // A PREFERENCE HAS TO SURVIVE LEAVING. Picking ROOKIE and coming back to a
  // Sharp table -- mid-saved-game -- is the setting silently undoing itself.
  {
    HalFile file;
    uint8_t byte = 0;
    if (Storage.openFileForRead("HEARTS", kSkillPath, file) && file.read(&byte, 1) == 1) {
      skill = byte == 0 ? Skill::Rookie : Skill::Sharp;
    }
  }
  hasGame = loadGame();
  view = View::Menu;
  requestUpdate();
}

void HeartsActivity::onExit() {
  if (hasGame) saveGame();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
}

// Deal, then skip the pass if this hand has none. WITHOUT THIS, every fourth
// hand paints one full frame reading "PASSING NOBODY" with a live PASS button
// before advance() notices and commits: render runs before the next loop pass,
// so the phase is briefly Passing on a hand that does not pass.
void HeartsActivity::startHand() {
  if (game.phase == Phase::Passing && game.passDirection() == Pass::Hold && passReady(game)) commitPass(game);
  std::memset(picked, 0, sizeof(picked));
  pickedCount = 0;
  hasLastWinner = false;
  trickShownAt = 0;
}

void HeartsActivity::newGame() {
  // A DEAL NEEDS ENTROPY IT DOES NOT GET FROM millis() ALONE. Seeded once at
  // activity entry, five separate simulator runs dealt the identical opening
  // hand, because the only thing separating them was how long boot took. The
  // games already played are persistent and monotonic, and the hand number
  // moves within a game, so mixing both makes a repeat need the same card
  // state AND the same millisecond.
  ui::MenuModel stats;
  fillStats(stats);
  rng ^= (static_cast<uint32_t>(millis()) * 2654435761u) + (static_cast<uint32_t>(stats.gamesPlayed) * 40503u) +
         (static_cast<uint32_t>(game.handNumber) * 2246822519u);
  if (rng == 0) rng = 0x9E3779B9u;
  hearts::newGame(game, rng);
  hasGame = true;
  startHand();
  view = View::Board;
  saveGame();
  requestUpdate();
}

// Everything that is not waiting on the player. One step per call, so each one
// gets its own repaint and the table deals rather than teleports.
bool HeartsActivity::advance() {
  switch (game.phase) {
    case Phase::Passing: {
      // The three brains choose in one pass -- their choices are simultaneous
      // and invisible, so there is nothing to watch.
      bool changed = false;
      for (int s = 0; s < kSeats; ++s) {
        if (s == seatIndex(Seat::South)) continue;
        if (game.passingCount[s] == kPassCount) continue;
        Observation obs;
        observe(game, static_cast<Seat>(s), obs);
        uint8_t three[kPassCount];
        decidePass(obs, skill, rng, three);
        setPass(game, static_cast<Seat>(s), three, kPassCount);
        changed = true;
      }
      // THE COMMIT LIVES WHERE THE PASS BECOMES READY, not in whichever actor
      // happens to act last. It used to sit only inside the human's confirm, so
      // a confirm processed before the brains had chosen left every seat ready
      // and nothing to notice it: the board sat in Passing with a dead PASS
      // button until the player picked three cards a second time.
      if (passReady(game)) {
        commitPass(game);
        // Nothing may stay picked across the commit. On a Hold hand the panel
        // can be tapped during the single frame before the skip, and a stale
        // `picked` slot raises whichever card later occupies that POSITION --
        // a different card after every trick.
        std::memset(picked, 0, sizeof(picked));
        pickedCount = 0;
        return true;
      }
      return changed;
    }

    case Phase::Playing: {
      if (game.turn == Seat::South) return false;  // the player owes a card
      Observation obs;
      observe(game, game.turn, obs);
      uint8_t card = decidePlay(obs, skill, rng);
      if (card == kNoCard || !isLegalPlay(game, game.turn, card)) {
        // The brain asks the rules for legality, so this needs the two to
        // disagree. RECOVER RATHER THAN RETURN: returning false is exactly the
        // wedge it was meant to avoid, because nothing else ever moves this
        // seat -- the panel says "WEST IS THINKING" until the player gives up
        // and leaves. The rules always have an answer on turn, so take theirs.
        LOG_ERR("HEARTS", "brain for seat %d gave card %u; falling back to the rules", seatIndex(game.turn), card);
        uint8_t legal[kHandSize];
        const int n = legalPlays(game, game.turn, legal);
        if (n == 0) {
          LOG_ERR("HEARTS", "seat %d has no legal card at all; hand is wedged", seatIndex(game.turn));
          return false;
        }
        card = legal[0];
      }
      if (!playCard(game, card)) {
        LOG_ERR("HEARTS", "seat %d could not play %u even after falling back", seatIndex(game.turn), card);
        return false;
      }
      if (game.phase == Phase::TrickTaken) {
        lastWinner = game.trick.winner();
        hasLastWinner = true;
        trickShownAt = static_cast<uint32_t>(millis());
      }
      return true;
    }

    case Phase::TrickTaken: {
      if (trickShownAt == 0) trickShownAt = static_cast<uint32_t>(millis());
      if (static_cast<uint32_t>(millis()) - trickShownAt < kTrickHoldMs) return false;
      trickShownAt = 0;
      sweepTrick(game);
      // The trick boundary: the one moment with nothing half-finished, and the
      // only point in a hand where a full refresh costs nobody a decision.
      if (++tricksSinceFlash >= kTricksPerFlash) {
        tricksSinceFlash = 0;
        flashOnNextPaint = true;
      }
      if (game.phase == Phase::Playing) saveGame();
      if (game.phase == Phase::HandOver || game.phase == Phase::GameOver) {
        view = View::Score;
        flashOnNextPaint = game.phase == Phase::GameOver;
        if (game.phase == Phase::GameOver) {
          // Place is one plus however many finished STRICTLY lower, so a tie
          // for lowest is a shared first and is recorded as a win. That is a
          // decision, not an accident: Hearts has no tiebreak, and the score
          // screen says TIED rather than naming one of them.
          int place = 1;
          for (int s = 0; s < kSeats; ++s) {
            if (s != seatIndex(Seat::South) && game.total[s] < game.total[seatIndex(Seat::South)]) ++place;
          }
          recordResult(place);
          clearSave();
          hasGame = false;
        } else {
          saveGame();
        }
      }
      return true;
    }

    case Phase::HandOver:
    case Phase::GameOver:
      return false;
  }
  return false;
}

void HeartsActivity::routeHandCard(const int index) {
  const Hand& hand = game.hands[seatIndex(Seat::South)];
  if (index < 0 || index >= hand.count) return;
  const uint8_t card = hand.at(index);

  if (game.phase == Phase::Passing) {
    if (picked[index]) {
      picked[index] = false;
      --pickedCount;
    } else if (pickedCount < kPassCount) {
      picked[index] = true;
      ++pickedCount;
    } else {
      // Four taps and the fourth replaces the first, which is what a hand of
      // cards does. Refusing the tap would be a dead control.
      for (int i = 0; i < hand.count; ++i) {
        if (picked[i]) {
          picked[i] = false;
          --pickedCount;
          break;
        }
      }
      picked[index] = true;
      ++pickedCount;
    }
    requestUpdate();
    return;
  }

  if (game.phase != Phase::Playing) return;
  if (game.turn != Seat::South) {
    rejected = ui::refusalText(ui::Refusal::NotYourTurn);
    turnNoteBuffer[0] = '\0';
    std::snprintf(turnNoteBuffer, sizeof(turnNoteBuffer), "%s IS THINKING", seatName(game.turn));
    turnNote = turnNoteBuffer;
    requestUpdate();
    return;
  }
  // One activation path: the tap asks the rules, exactly as the board asked
  // them to decide whether to dim the card. A card drawn dimmed cannot be
  // played, and a card drawn bright always can.
  if (!isLegalPlay(game, Seat::South, card)) {
    // SAY WHY. The dither already said "not this one"; this says which rule,
    // which is the difference between a game that refuses you and a game that
    // teaches you. The reasons are in the order the rules bite.
    // The words live in HeartsScreens so a suite can measure all six; this only
    // decides WHICH, in the order the rules bite.
    if (game.firstTrick() && game.trick.empty()) {
      rejected = ui::refusalText(ui::Refusal::TwoOfClubsOpens);
    } else if (!game.trick.empty() && game.hands[seatIndex(Seat::South)].hasSuit(game.trick.ledSuit()) &&
               cards::suitOf(card) != game.trick.ledSuit()) {
      rejected = ui::refusalText(ui::Refusal::MustFollowSuit);
    } else if (game.firstTrick() && penaltyOf(card) > 0) {
      rejected = ui::refusalText(ui::Refusal::NoPointsFirstTrick);
    } else if (game.trick.empty() && isHeart(card) && !game.heartsBroken) {
      rejected = ui::refusalText(ui::Refusal::HeartsNotBroken);
    } else {
      rejected = ui::refusalText(ui::Refusal::JustNo);
    }
    requestUpdate();
    return;
  }
  rejected = nullptr;
  playCard(game, card);
  if (game.phase == Phase::TrickTaken) {
    lastWinner = game.trick.winner();
    hasLastWinner = true;
    trickShownAt = static_cast<uint32_t>(millis());
  }
  requestUpdate();
}

void HeartsActivity::routeButton(const int button) {
  switch (button) {
    case ui::ButtonMenu:
      if (hasGame) saveGame();
      view = View::Menu;
      requestUpdate();
      break;
    case ui::ButtonConfirm:
      if (game.phase == Phase::Passing && pickedCount == kPassCount) {
        const Hand& hand = game.hands[seatIndex(Seat::South)];
        uint8_t three[kPassCount];
        int n = 0;
        for (int i = 0; i < hand.count && n < kPassCount; ++i) {
          if (picked[i]) three[n++] = hand.at(i);
        }
        if (setPass(game, Seat::South, three, kPassCount)) {
          std::memset(picked, 0, sizeof(picked));
          pickedCount = 0;
          if (passReady(game)) commitPass(game);
          saveGame();
          requestUpdate();
        }
      } else if (game.phase == Phase::HandOver) {
        nextHand(game, rng);
        startHand();
        view = View::Board;
        saveGame();
        requestUpdate();
      } else if (game.phase == Phase::GameOver) {
        newGame();
      }
      break;
    default:
      break;
  }
}

void HeartsActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (view == View::Menu) {
      shelf::leave(renderer, mappedInput);
    } else if (view == View::HowTo) {
      view = View::Menu;
      requestUpdate();
    } else {
      if (hasGame) saveGame();
      view = View::Menu;
      requestUpdate();
    }
    return;
  }

  fui::InputSnapshot input;
  int tapX = 0;
  int tapY = 0;
  if (mappedInput.wasScreenTapped(tapX, tapY)) {
    input.touchReleased = true;
    input.touchX = static_cast<int16_t>(tapX);
    input.touchY = static_cast<int16_t>(tapY);
  }

  if (input.touchReleased && !interactionsReady) {
    LOG_DBG("HEARTS", "tap (%d,%d) DROPPED: no interaction table yet", tapX, tapY);
  }
  if (input.touchReleased && interactionsReady) {
    const fui::ActionEvent action = interactions.route(input);
    // Every routed tap, with what it resolved to. This is not debug residue:
    // the rules button routed correctly for a whole render cycle while the
    // menu's switch had no case for it, so the control was live, drawn, hit,
    // and silently dropped. Nothing in a screenshot can tell that apart from a
    // dead button, and this line told them apart in one run.
    LOG_DBG("HEARTS", "tap (%d,%d) view=%d -> action=%d value=%d", tapX, tapY, static_cast<int>(view),
            static_cast<int>(action.action), action.value);
    if (action.action == ui::ActionHandCard && view == View::Board) {
      routeHandCard(action.value);
      return;
    }
    if (action.action == ui::ActionButton) {
      if (view == View::Menu) {
        switch (action.value) {
          case ui::ButtonConfirm:
            confirmingNew = false;
            if (hasGame) {
              view = View::Board;
              requestUpdate();
            } else {
              newGame();
            }
            break;
          case ui::ButtonMenu:
            // ONE TAP USED TO DESTROY A SAVED GAME, with NEW GAME sitting
            // directly beside RESUME and no way back. It asks once now, and
            // only when there is something to lose.
            if (hasGame && !confirmingNew) {
              confirmingNew = true;
              requestUpdate();
              // (any other menu action disarms it -- see below)
            } else {
              confirmingNew = false;
              newGame();
            }
            break;
          case ui::ButtonHint: {
            // AN ARMED CONFIRM DOES NOT SURVIVE AN UNRELATED ACTION. "DISCARD
            // IT?" stayed armed through the difficulty toggle, with no way to
            // disarm it and no button labelled NEW GAME on screen while it was.
            confirmingNew = false;
            skill = skill == Skill::Sharp ? Skill::Rookie : Skill::Sharp;
            HalFile file;
            if (Storage.openFileForWrite("HEARTS", kSkillPath, file)) {
              const uint8_t byte = skill == Skill::Sharp ? 1 : 0;
              file.write(&byte, 1);
              file.flush();
            }
            requestUpdate();
            break;
          }
          case ui::ButtonHowTo:
            confirmingNew = false;
            howToPage = 0;
            view = View::HowTo;
            requestUpdate();
            break;
          default:
            break;
        }
      } else if (view == View::HowTo) {
        if (action.value == ui::ButtonHowToNext) {
          if (howToPage + 1 < ui::howToPages()) {
            ++howToPage;
          } else {
            view = View::Menu;
          }
          requestUpdate();
        }
      } else {
        routeButton(action.value);
      }
      return;
    }
  }

  // The rules screen is not a game state, so nothing advances behind it.
  if (view == View::HowTo) return;

  // Nothing for the player to do: let the table play on. One step per pass so
  // each card gets its own repaint.
  if (view == View::Board || view == View::Score) {
    const uint32_t now = static_cast<uint32_t>(millis());
    const bool waitingOnTrick = game.phase == Phase::TrickTaken;
    if (waitingOnTrick || now - lastStep >= kBotThinkMs) {
      if (advance()) {
        lastStep = now;
        requestUpdate();
      }
    }
  }
}

void HeartsActivity::fillSeats(ui::SeatView* seats) const {
  for (int s = 0; s < kSeats; ++s) {
    const Seat seat = static_cast<Seat>(s);
    seats[s].name = seatName(seat);
    seats[s].initial = seatName(seat)[0];
    seats[s].total = game.total[s];
    // INCLUDING WHAT IS FACE UP ON THE TABLE. taken[] updates in sweepTrick(),
    // so between the fourth card landing and the sweep the plaque read "EAST 0"
    // while the status line directly under it said "EAST TAKES IT, 4 POINTS".
    // A projected total that excludes the trick being announced is a worse
    // claim than an honest stale number, which is what this replaced.
    seats[s].taken = game.taken[s];
    if (game.phase == Phase::TrickTaken && hasLastWinner && static_cast<Seat>(s) == lastWinner) {
      seats[s].taken += game.trick.points();
    }
    seats[s].cardsLeft = game.hands[s].count;
    seats[s].isMe = seat == Seat::South;
    seats[s].isTurn = game.phase == Phase::Playing && game.turn == seat;
    seats[s].tookLastTrick = hasLastWinner && lastWinner == seat;
  }
}

void HeartsActivity::fillLegal(ui::BoardModel& model) const {
  const Hand& hand = game.hands[seatIndex(Seat::South)];
  for (int i = 0; i < hand.count; ++i) {
    // THE DITHER MEANS EXACTLY ONE THING: THE RULES WILL REFUSE THIS CARD.
    //
    // It used to also mean "it is not your turn" -- legality ANDed with
    // `turn == South` -- so while three brains thought, which is most of the
    // wall clock, all thirteen cards went grey at once. That overloads the only
    // teaching device on the screen, and once the rules page started saying
    // "A GREY CARD IS ONE THE RULES WILL REFUSE" it made the board contradict
    // its own help for three quarters of every hand.
    //
    // Asking the rules WITHOUT the turn is also strictly more useful: once two
    // cards are down the led suit is settled, so the hand shows what you will
    // be allowed to play before your turn arrives.
    // AND NOTHING IS DIMMED WHILE A FINISHED TRICK IS BEING READ. In TrickTaken
    // the four cards are face up and about to be swept, so legality "into this
    // completed trick" answers a question nobody is being asked -- and the
    // answer changes the instant the sweep lands a new led suit. A flicker of
    // confident wrong information, in the one signal the rules screen has just
    // taught the player to trust.
    model.legal[i] = game.phase == Phase::Passing || game.phase == Phase::TrickTaken ||
                     isLegalCard(hand, game.trick, game.heartsBroken, game.firstTrick(), hand.at(i));
  }
}

const char* HeartsActivity::statusLine() {
  // A refusal outranks whatever the board would otherwise be saying, and it
  // lasts exactly one paint: the next thing that happens replaces it.
  if (rejected != nullptr) {
    const char* reason = rejected;
    rejected = nullptr;
    return reason;
  }
  // (subStatusLine carries the refusal's company; see the note there.)
  switch (game.phase) {
    case Phase::Passing:
      std::snprintf(statusBuffer, sizeof(statusBuffer), ui::statusTemplate(ui::Status::PickToPass),
                    passName(game.passDirection()));
      return statusBuffer;
    case Phase::Playing:
      if (game.turn != Seat::South) {
        std::snprintf(statusBuffer, sizeof(statusBuffer), ui::statusTemplate(ui::Status::Thinking),
                      seatName(game.turn));
        return statusBuffer;
      }
      if (game.trick.empty()) {
        if (game.firstTrick()) return ui::statusTemplate(ui::Status::YourLeadFirst);
        return ui::statusTemplate(ui::Status::YourLead);
      }
      std::snprintf(statusBuffer, sizeof(statusBuffer), ui::statusTemplate(ui::Status::FollowSuit),
                    cards::suitName(game.trick.ledSuit()));
      return statusBuffer;
    case Phase::TrickTaken:
      if (hasLastWinner) {
        const int points = game.trick.points();
        if (points > 0) {
          std::snprintf(statusBuffer, sizeof(statusBuffer), ui::statusTemplate(ui::Status::TakesItPoints),
                        seatName(lastWinner), takesVerb(lastWinner), points, points == 1 ? "" : "S");
        } else {
          std::snprintf(statusBuffer, sizeof(statusBuffer), ui::statusTemplate(ui::Status::TakesIt),
                        seatName(lastWinner), takesVerb(lastWinner));
        }
        return statusBuffer;
      }
      return "";
    case Phase::HandOver:
      return ui::statusTemplate(ui::Status::HandOver);
    case Phase::GameOver:
      return ui::statusTemplate(ui::Status::GameOver);
  }
  return "";
}

const char* HeartsActivity::subStatusLine() {
  if (game.phase == Phase::Passing) {
    std::snprintf(subStatusBuffer, sizeof(subStatusBuffer), "%d OF 3", pickedCount);
    return subStatusBuffer;
  }
  // A REFUSAL MUST NOT COST YOU THE TURN INDICATOR. The reason replaces the
  // status line, which is the line that says whose go it is, and paints happen
  // every 420ms -- so one stray tap and you no longer know who the table is
  // waiting for. The reason takes the status and the turn moves down here.
  if (turnNote != nullptr) {
    const char* note = turnNote;
    turnNote = nullptr;
    return note;
  }
  // Two facts a Hearts player tracks all hand and cannot see anywhere else.
  const bool queenGone = game.played[static_cast<int>(Suit::Spades) * cards::kRanks + cards::kQueen];
  // "QUEEN OUT" meant she had NOT been played and "QUEEN GONE" meant she had.
  // In cards "out" normally means out of play, so the two states were one word
  // apart, in the smallest type on the screen, and meant opposite things.
  // "HEARTS SHUT" was invented too: the game's own word is broken, and its
  // opposite is not broken.
  // SHORT ENOUGH TO FIT AT A FIXED CUT. "QUEEN STILL OUT" was shrunk a size
  // and then truncated anyway, to "QUEEN STILL...", which says neither out nor
  // played -- the one fact the line exists to carry. The longest combination
  // here is 33 characters and host-tests/ui measures every one of them.
  std::snprintf(subStatusBuffer, sizeof(subStatusBuffer), "%s   %s", ui::heartsStateText(game.heartsBroken),
                ui::queenStateText(queenGone));
  return subStatusBuffer;
}

void HeartsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer, toybox::toyboxFaces());
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions);

  // Built once, not per frame: a named local costs 2712 bytes of ThemeTokens on
  // the render task's stack every repaint, which overflowed it in v1.12.45.
  static const fui::ThemeTokens tokens = [] {
    fui::ThemeTokens t = toybox::themeTokens();
    t.headerHeight = ui::kHeaderBand;
    return t;
  }();
  toybox::Screen screen(frame, tokens);

  // `view` says which DOOR we came through -- menu, rules, or the game -- and
  // the game's own phase says which of its screens is showing. It used to say
  // both, and the two came apart: a frame landed with view still Board while
  // the phase had moved to HandOver, so the panel drew an empty table over an
  // empty hand with "HAND OVER" underneath it, a screen that exists in no
  // design. Two facts that must agree are one fact stored once.
  if (view == View::HowTo) {
    ui::HowToModel model;
    model.page = howToPage;
    ui::buildHowTo(screen, model);
  } else if (view == View::Menu) {
    ui::MenuModel model;
    model.hasSave = hasGame;
    model.savedHand = static_cast<int>(game.handNumber) + 1;
    // "HAND 1, PART PLAYED" was shown for a hand that had finished and scored.
    model.savedHandDone = game.phase == Phase::HandOver || game.phase == Phase::GameOver;
    model.sharp = skill == Skill::Sharp;
    model.confirmingNew = confirmingNew;
    fillStats(model);
    ui::buildMenu(screen, model);
  } else if (game.phase == Phase::HandOver || game.phase == Phase::GameOver) {
    ui::ScoreModel model;
    model.game = &game;
    fillSeats(model.seats);
    model.gameOver = game.phase == Phase::GameOver;
    ui::buildScore(screen, model);
  } else {
    ui::BoardModel model;
    model.game = &game;
    fillSeats(model.seats);
    fillLegal(model);
    for (int i = 0; i < kHandSize; ++i) model.picked[i] = picked[i];
    model.pickedCount = pickedCount;
    model.status = statusLine();
    model.subStatus = subStatusLine();
    model.showConfirm = game.phase == Phase::Passing;
    model.confirmLabel = "PASS";
    model.confirmEnabled = pickedCount == kPassCount;
    ui::buildBoard(screen, model, layout);
  }

  interactionsReady = true;
  toybox::reportOverflow(interactions, "Hearts");

  // A SCREEN CHANGE IS ALWAYS A FULL REFRESH. Moving between the menu, the
  // rules, the table and the scoreboard replaces essentially every pixel, which
  // is the worst thing to ask a partial refresh to do and the best moment to
  // spend a flash: it reads as a page turn.
  const uint8_t screenNow = view == View::Menu                                                 ? 0
                            : view == View::HowTo                                              ? 1
                            : (game.phase == Phase::HandOver || game.phase == Phase::GameOver) ? 2
                                                                                               : 3;
  if (screenNow != lastPaintedScreen) {
    flashOnNextPaint = true;
    lastPaintedScreen = screenNow;
    tricksSinceFlash = 0;
  }

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(flashOnNextPaint ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
  flashOnNextPaint = false;
}

// ---------------------------------------------------------------------------
// Persistence. The board itself, not a seed and a move list: replaying moves to
// rebuild a position is a second implementation of the rules that has to agree
// with the first one forever.

void HeartsActivity::saveGame() const {
  HalFile file;
  if (!Storage.openFileForWrite("HEARTS", kSavePath, file)) return;
  const uint8_t version = kSaveVersion;
  file.write(&version, 1);
  file.write(reinterpret_cast<const uint8_t*>(&game), sizeof(game));
  file.flush();
}

bool HeartsActivity::loadGame() {
  HalFile file;
  if (!Storage.openFileForRead("HEARTS", kSavePath, file)) return false;
  uint8_t version = 0;
  if (file.read(&version, 1) != 1 || version != kSaveVersion) return false;
  Game loaded;
  if (file.read(reinterpret_cast<uint8_t*>(&loaded), sizeof(loaded)) != static_cast<int>(sizeof(loaded))) return false;
  // A VERSION BYTE AND A LENGTH PROVE NOTHING ABOUT A TORN WRITE. Losing power
  // during the save leaves a file of exactly the right size holding a mix of
  // two states, which passes both gates and then restores a seat index of 5 or
  // a hand of twenty cards -- an out-of-bounds read on the next repaint, and an
  // out-of-bounds WRITE the first time that hand is tapped. isConsistent asks
  // the rules whether the position is possible at all.
  if (!isConsistent(loaded)) {
    LOG_ERR("HEARTS", "save failed the consistency check; discarding it");
    return false;
  }
  if (loaded.phase == Phase::GameOver) return false;
  game = loaded;
  return true;
}

void HeartsActivity::clearSave() const { Storage.remove(kSavePath); }

void HeartsActivity::recordResult(const int place) const {
  HalFile file;
  if (!Storage.openFileForAppend("HEARTS", kStatsPath, file)) return;
  const uint8_t byte = static_cast<uint8_t>(place);
  file.write(&byte, 1);
  file.flush();
}

void HeartsActivity::fillStats(ui::MenuModel& model) const {
  model.gamesPlayed = 0;
  model.gamesWon = 0;
  model.bestPlace = 0;
  HalFile file;
  if (!Storage.openFileForRead("HEARTS", kStatsPath, file)) return;
  uint8_t byte = 0;
  while (file.read(&byte, 1) == 1) {
    if (byte < 1 || byte > kSeats) continue;
    ++model.gamesPlayed;
    if (byte == 1) ++model.gamesWon;
    if (model.bestPlace == 0 || byte < model.bestPlace) model.bestPlace = byte;
  }
}

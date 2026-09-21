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
constexpr const char* kStatsPath = "/.crosspoint/hearts.res";

// Bumped when the save layout changes, per the cache-format rule. An old save
// is discarded rather than misread, which for a card game means a table with
// two of the same card on it.
constexpr uint8_t kSaveVersion = 1;

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

const char* seatName(const Seat seat) {
  switch (seat) {
    case Seat::South: return "YOU";
    case Seat::West: return "WEST";
    case Seat::North: return "NORTH";
    case Seat::East: return "EAST";
  }
  return "?";
}

const char* passName(const Pass pass) {
  switch (pass) {
    case Pass::Left: return "LEFT";
    case Pass::Right: return "RIGHT";
    case Pass::Across: return "ACROSS";
    case Pass::Hold: return "NOBODY";
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
  hasGame = loadGame();
  view = View::Menu;
  requestUpdate();
}

void HeartsActivity::onExit() {
  if (hasGame) saveGame();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
}

void HeartsActivity::newGame() {
  hearts::newGame(game, rng);
  hasGame = true;
  pickedCount = 0;
  std::memset(picked, 0, sizeof(picked));
  hasLastWinner = false;
  trickShownAt = 0;
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
      if (game.passDirection() != Pass::Hold) {
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
      } else if (passReady(game)) {
        commitPass(game);
        return true;
      }
      return changed;
    }

    case Phase::Playing: {
      if (game.turn == Seat::South) return false;  // the player owes a card
      Observation obs;
      observe(game, game.turn, obs);
      const uint8_t card = decidePlay(obs, skill, rng);
      if (card == kNoCard) {
        LOG_ERR("HEARTS", "brain for seat %d returned no card", seatIndex(game.turn));
        return false;
      }
      if (!playCard(game, card)) {
        // The brain asks the rules for legality, so this cannot happen without
        // the two disagreeing. Log it loudly rather than wedging the hand.
        LOG_ERR("HEARTS", "brain for seat %d played an illegal card %u", seatIndex(game.turn), card);
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
      if (game.phase == Phase::HandOver || game.phase == Phase::GameOver) {
        view = View::Score;
        flashOnNextPaint = game.phase == Phase::GameOver;
        if (game.phase == Phase::GameOver) {
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

  if (game.phase != Phase::Playing || game.turn != Seat::South) return;
  // One activation path: the tap asks the rules, exactly as the board asked
  // them to decide whether to dim the card. A card drawn dimmed cannot be
  // played, and a card drawn bright always can.
  if (!isLegalPlay(game, Seat::South, card)) return;
  playCard(game, card);
  if (game.phase == Phase::TrickTaken) {
    lastWinner = game.trick.winner();
    hasLastWinner = true;
    trickShownAt = static_cast<uint32_t>(millis());
  }
  saveGame();
  requestUpdate();
}

void HeartsActivity::routeButton(const int button) {
  switch (button) {
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
        std::memset(picked, 0, sizeof(picked));
        pickedCount = 0;
        hasLastWinner = false;
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

  if (input.touchReleased && interactionsReady) {
    const fui::ActionEvent action = interactions.route(input);
    if (action.action == ui::ActionHandCard && view == View::Board) {
      routeHandCard(action.value);
      return;
    }
    if (action.action == ui::ActionButton) {
      if (view == View::Menu) {
        switch (action.value) {
          case ui::ButtonConfirm:
            if (hasGame) {
              view = View::Board;
              requestUpdate();
            } else {
              newGame();
            }
            break;
          case ui::ButtonMenu: newGame(); break;
          case ui::ButtonHint:
            skill = skill == Skill::Sharp ? Skill::Rookie : Skill::Sharp;
            requestUpdate();
            break;
          default: break;
        }
      } else {
        routeButton(action.value);
      }
      return;
    }
  }

  // Nothing for the player to do: let the table play on. One step per pass so
  // each card gets its own repaint.
  if (view == View::Board || view == View::Score) {
    static uint32_t lastStep = 0;
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
    seats[s].taken = game.taken[s];
    seats[s].cardsLeft = game.hands[s].count;
    seats[s].isMe = seat == Seat::South;
    seats[s].isTurn = game.phase == Phase::Playing && game.turn == seat;
    seats[s].tookLastTrick = hasLastWinner && lastWinner == seat;
  }
}

void HeartsActivity::fillLegal(ui::BoardModel& model) const {
  const Hand& hand = game.hands[seatIndex(Seat::South)];
  for (int i = 0; i < hand.count; ++i) {
    // In the pass everything is choosable; in play the rules decide. Asked of
    // the same function the tap asks, so the board cannot offer what the tap
    // then refuses.
    model.legal[i] = game.phase == Phase::Passing
                         ? true
                         : (game.turn == Seat::South && isLegalPlay(game, Seat::South, hand.at(i)));
  }
}

const char* HeartsActivity::statusLine() const {
  auto* self = const_cast<HeartsActivity*>(this);
  switch (game.phase) {
    case Phase::Passing:
      std::snprintf(self->statusBuffer, sizeof(self->statusBuffer), "PICK THREE CARDS TO PASS %s",
                    passName(game.passDirection()));
      return self->statusBuffer;
    case Phase::Playing:
      if (game.turn != Seat::South) {
        std::snprintf(self->statusBuffer, sizeof(self->statusBuffer), "%s IS THINKING", seatName(game.turn));
        return self->statusBuffer;
      }
      if (game.trick.empty()) {
        if (game.firstTrick()) return "YOUR LEAD: THE TWO OF CLUBS OPENS";
        std::snprintf(self->statusBuffer, sizeof(self->statusBuffer), "YOUR LEAD");
        return self->statusBuffer;
      }
      std::snprintf(self->statusBuffer, sizeof(self->statusBuffer), "FOLLOW %s",
                    cards::suitName(game.trick.ledSuit()));
      return self->statusBuffer;
    case Phase::TrickTaken:
      if (hasLastWinner) {
        const int points = game.trick.points();
        if (points > 0) {
          std::snprintf(self->statusBuffer, sizeof(self->statusBuffer), "%s TAKES IT, %d POINT%s",
                        seatName(lastWinner), points, points == 1 ? "" : "S");
        } else {
          std::snprintf(self->statusBuffer, sizeof(self->statusBuffer), "%s TAKES IT", seatName(lastWinner));
        }
        return self->statusBuffer;
      }
      return "";
    case Phase::HandOver: return "HAND OVER";
    case Phase::GameOver: return "GAME OVER";
  }
  return "";
}

const char* HeartsActivity::subStatusLine() const {
  auto* self = const_cast<HeartsActivity*>(this);
  if (game.phase == Phase::Passing) {
    std::snprintf(self->subStatusBuffer, sizeof(self->subStatusBuffer), "%d OF 3", pickedCount);
    return self->subStatusBuffer;
  }
  // Two facts a Hearts player tracks all hand and cannot see anywhere else.
  const bool queenGone = game.played[static_cast<int>(Suit::Spades) * cards::kRanks + cards::kQueen];
  std::snprintf(self->subStatusBuffer, sizeof(self->subStatusBuffer), "%s   %s",
                game.heartsBroken ? "HEARTS BROKEN" : "HEARTS SHUT", queenGone ? "QUEEN GONE" : "QUEEN OUT");
  return self->subStatusBuffer;
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

  if (view == View::Board) {
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
  } else if (view == View::Score) {
    ui::ScoreModel model;
    model.game = &game;
    fillSeats(model.seats);
    model.gameOver = game.phase == Phase::GameOver;
    ui::buildScore(screen, model);
  } else {
    ui::MenuModel model;
    model.hasSave = hasGame;
    model.savedHand = static_cast<int>(game.handNumber) + 1;
    model.sharp = skill == Skill::Sharp;
    fillStats(model);
    ui::buildMenu(screen, model);
  }

  interactionsReady = true;
  toybox::reportOverflow(interactions, "Hearts");

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

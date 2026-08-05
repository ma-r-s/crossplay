#include "InsiderActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstring>

// GUI, for drawButtonHints. Included directly rather than inherited from
// Toybox.h: a header pulled in only for a macro reads as unused to clangd.
#include "../../components/UITheme.h"
#include "../Shelf.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"

namespace {

namespace fui = freeink::ui;
namespace ui = insiderui;

constexpr char kSavePath[] = "/.crosspoint/insider.sav";

// Bumped whenever the layout below changes, per the cache-format rule. An old
// save is discarded rather than misread, which here would mean a deck that
// thinks it has dealt words it has not.
constexpr uint8_t kSaveVersion = 1;

struct SaveState {
  uint8_t players;
  uint16_t rounds;
  uint16_t won;
  uint16_t lost;
  uint16_t outOfTime;
  uint32_t recent;
  uint8_t deck[insider::Deck::kMaskBytes];
} __attribute__((packed));

}  // namespace

std::unique_ptr<Activity> InsiderActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<InsiderActivity>(renderer, mappedInput);
}

void InsiderActivity::onEnter() {
  toybox::ensureFonts(renderer);
  if (!loadState()) {
    deck.reset();
    record = insider::Record{};
    players = 5;
  }
  view = View::Menu;
  dirty = false;
  requestUpdate();
}

void InsiderActivity::onExit() { flushSave(); }

void InsiderActivity::goToMenu() {
  view = View::Menu;
  requestUpdate();
}

void InsiderActivity::dealRound() {
  // millis() is the only entropy on this device that differs between two boots,
  // and by the time anybody taps DEAL it has been running for seconds, so the
  // low bits are genuinely unpredictable. Mixed rather than used raw: the timer
  // advances in even steps and the bottom bit of a raw reading is not random.
  insider::Rng rng(static_cast<uint32_t>(millis()) * 2654435761u + 1u);
  const int word = deck.draw(rng);
  round.deal(players, word, rng);
  seat = 0;
  revealed = false;
  chosen = ui::VoteModel::kNothingChosen;
  accused = insider::kNoInsider;
  // The deck moved on, so the mark has to reach the card even if the round is
  // abandoned halfway through -- otherwise a word could be dealt twice.
  dirty = true;
  flushSave();
  view = View::Pass;
  requestUpdate();
}

// One tap drives the whole pass-around: it turns the card face up, then face
// down and on to the next person, and after the last one it starts the clock.
// Modelling it as one action is what stops the reveal and the pass from ever
// disagreeing about whose turn it is.
void InsiderActivity::advancePass() {
  if (!revealed) {
    revealed = true;
    requestUpdate();
    return;
  }
  revealed = false;
  if (seat + 1 < round.players()) {
    ++seat;
    requestUpdate();
    return;
  }
  startClock();
}

void InsiderActivity::startClock() {
  clockStartMs = static_cast<uint32_t>(millis());
  lastClockTick = insider::clockTick(insider::kQuestionSeconds);
  view = View::Questions;
  requestUpdate();
}

void InsiderActivity::settle(const insider::Outcome result) {
  outcome = result;
  record.push(result);
  dirty = true;
  flushSave();
  view = View::Reveal;
  // The refresh flash as punctuation, per docs/design-language.md. This is the
  // one moment in a round that has earned the full blink: everybody is looking
  // at the device at exactly this instant and nothing else is happening.
  flashOnNextPaint = true;
  requestUpdate();
}

void InsiderActivity::routeAction(const int action, const int value) {
  switch (action) {
    case ui::ActionPlayers: {
      const int next = players + value;
      if (next < insider::kMinPlayers || next > insider::kMaxPlayers) return;
      players = next;
      dirty = true;
      requestUpdate();
      break;
    }
    case ui::ActionDeal:
      dealRound();
      break;
    case ui::ActionRules:
      view = View::Rules;
      requestUpdate();
      break;
    case ui::ActionBack:
      goToMenu();
      break;
    case ui::ActionAdvance:
      if (view == View::Pass) advancePass();
      break;
    case ui::ActionFoundWord:
      if (view == View::Questions) {
        view = View::Vote;
        requestUpdate();
      }
      break;
    case ui::ActionAccuse:
      if (view == View::Vote) {
        chosen = value;
        requestUpdate();
      }
      break;
    case ui::ActionConfirmVote:
      if (view == View::Vote && chosen != ui::VoteModel::kNothingChosen) {
        accused = chosen;
        settle(round.judge(accused));
      }
      break;
    case ui::ActionPlayAgain:
      dealRound();
      break;
    case ui::ActionDone:
      goToMenu();
      break;
    default:
      break;
  }
}

void InsiderActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // See src/apps_local/Shelf.h: no app names its own destination. Every other
    // view backs out to the menu, abandoning the round -- nothing is recorded,
    // because nothing was finished.
    if (view == View::Menu) {
      flushSave();
      shelf::leave(renderer, mappedInput);
    } else {
      goToMenu();
    }
    return;
  }

  // The clock. Repaints are driven by clockTick() moving, never by the second
  // hand: see InsiderCore.h for why that is a rule rather than an optimisation.
  if (view == View::Questions) {
    const uint32_t elapsed = static_cast<uint32_t>(millis()) - clockStartMs;
    const int secondsLeft = insider::kQuestionSeconds - static_cast<int>(elapsed / 1000);
    if (secondsLeft <= 0) {
      // Nobody said the word. The Insider does not win this either; it is the
      // one result where the whole table loses together.
      accused = insider::kNoInsider;
      settle(insider::Outcome::OutOfTime);
      return;
    }
    const int tick = insider::clockTick(secondsLeft);
    if (tick != lastClockTick) {
      lastClockTick = tick;
      requestUpdate();
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
  // Touch only, like the rest of this fork's games. See the note in
  // ConnectionsActivity for why there is no cursor.
  if (!input.touchReleased || !interactionsReady) return;

  const fui::ActionEvent action = interactions.route(input);
  routeAction(static_cast<int>(action.action), static_cast<int>(action.value));
}

void InsiderActivity::render(RenderLock&&) {
  renderer.clearScreen();
  // The rules page is the only screen in this game that is prose rather than
  // chrome, and Jersey at 20px is a display cut: a screenful of it is a wall,
  // and it does not fit the page. The small slot takes the 14px cut there and
  // nowhere else. Rebinding a slot is one assignment -- that is what the three
  // of them are for. See docs/design-language.md.
  const toybox::Faces faces = view == View::Rules
                                  ? toybox::Faces{toybox::kButtonFontId, toybox::kUiFontId, toybox::kDisplayFontId}
                                  : toybox::toyboxFaces();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer, faces);
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions);
  toybox::Screen screen(frame, toybox::themeTokens());

  switch (view) {
    case View::Rules:
      ui::buildRules(screen);
      break;

    case View::Pass: {
      ui::PassModel model;
      model.seat = seat;
      model.players = round.players();
      model.revealed = revealed;
      model.role = round.roleOf(seat);
      model.word = round.secretWord();
      model.lastSeat = seat + 1 >= round.players();
      ui::buildPass(screen, model);
      break;
    }

    case View::Questions: {
      ui::QuestionsModel model;
      const uint32_t elapsed = static_cast<uint32_t>(millis()) - clockStartMs;
      const int left = insider::kQuestionSeconds - static_cast<int>(elapsed / 1000);
      model.secondsLeft = left < 0 ? 0 : left;
      model.masterSeat = round.masterSeat();
      model.players = round.players();
      ui::buildQuestions(screen, model);
      break;
    }

    case View::Vote: {
      ui::VoteModel model;
      model.players = round.players();
      model.masterSeat = round.masterSeat();
      model.chosen = chosen;
      ui::buildVote(screen, model);
      break;
    }

    case View::Reveal: {
      ui::RevealModel model;
      model.outcome = outcome;
      model.insiderSeat = round.insiderSeat();
      model.accused = accused;
      model.players = round.players();
      model.word = round.secretWord();
      ui::buildReveal(screen, model);
      break;
    }

    case View::Menu:
    default: {
      ui::MenuModel model;
      model.players = players;
      model.record = &record;
      ui::buildMenu(screen, model);
      break;
    }
  }

  interactionsReady = true;
  toybox::reportOverflow(interactions, "Insider");

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(flashOnNextPaint ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
  flashOnNextPaint = false;
}

void InsiderActivity::flushSave() {
  if (!dirty) return;
  dirty = false;
  saveState();
}

void InsiderActivity::saveState() {
  SaveState state{};
  state.players = static_cast<uint8_t>(players);
  state.rounds = record.rounds;
  state.won = record.won;
  state.lost = record.lost;
  state.outOfTime = record.outOfTime;
  state.recent = record.recent;
  std::memcpy(state.deck, deck.mask(), sizeof(state.deck));

  HalFile file;
  if (!Storage.openFileForWrite("INSD", kSavePath, file)) return;
  const uint8_t version = kSaveVersion;
  file.write(&version, 1);
  file.write(reinterpret_cast<const uint8_t*>(&state), sizeof(state));
  file.flush();
}

bool InsiderActivity::loadState() {
  if (!Storage.exists(kSavePath)) return false;
  HalFile file;
  if (!Storage.openFileForRead("INSD", kSavePath, file)) return false;
  uint8_t version = 0;
  if (file.read(&version, 1) != 1 || version != kSaveVersion) return false;
  SaveState state{};
  if (file.read(reinterpret_cast<uint8_t*>(&state), sizeof(state)) != sizeof(state)) return false;

  players = state.players;
  if (players < insider::kMinPlayers || players > insider::kMaxPlayers) {
    LOG_ERR("INSD", "Save names %d players; clamping", state.players);
    players = 5;
  }
  record.rounds = state.rounds;
  record.won = state.won;
  record.lost = state.lost;
  record.outOfTime = state.outOfTime;
  record.recent = state.recent;
  // setMask clears the bits above the last real word, so a save written by a
  // build with a longer deck cannot leave this one permanently short of a lap.
  deck.setMask(state.deck);
  return true;
}

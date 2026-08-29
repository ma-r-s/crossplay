#include "ForeheadActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstring>

// GUI, for drawButtonHints. Included directly rather than inherited from
// Toybox.h: a header pulled in only for a macro reads as unused to clangd.
#include "../../components/UITheme.h"
#include "../Shelf.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxSeed.h"
#include "../ui/ToyboxTheme.h"

namespace {

namespace fui = freeink::ui;
namespace fh = forehead;
namespace ui = foreheadui;

constexpr char kSavePath[] = "/.crosspoint/forehead.sav";

// Bumped whenever the layout below changes, per the cache-format rule. An old
// save is discarded rather than misread, which here would mean a deck that
// believes it has dealt words it has not.
constexpr uint8_t kSaveVersion = 1;

struct SaveState {
  uint8_t category;
  uint8_t roundSeconds;  // seconds / 10, so the four lengths fit a byte
  uint16_t rounds;
  uint16_t words;
  uint8_t best;
  uint8_t recentCount;
  uint8_t recent[fh::Record::kRecentCount];
  uint8_t bestIn[fh::kCategoryCount];
  uint32_t played;
  uint8_t deck[fh::Deck::kMaskBytes];
} __attribute__((packed));

// THE ONE FACT IN THIS APP THAT THE SIMULATOR CANNOT CHECK.
//
// The X4 Pro has two keys and they are on the long edges: GPIO0 on the physical
// left in portrait (logical Up) and GPIO7 on the physical right (logical Down).
// Turned into LandscapeCounterClockwise the portrait right edge becomes the
// TOP and the portrait left edge becomes the BOTTOM -- follow
// rotateCoordinates() in GfxRenderer.cpp, which maps portrait logical y onto
// panel x and so rotates the device a quarter turn anticlockwise.
//
// So Up is the bottom key and Down is the top key, and the game reads them as
// the phone version reads a tilt: down is GOT IT, up is PASS.
//
// If this is backwards on a real unit, that is the entire fix -- swap these two
// lines. The screen labels the edges, so a wrong mapping is visible in the
// first three seconds of the first round rather than being subtle.
constexpr auto kGotItKey = MappedInputManager::Button::Up;
constexpr auto kPassKey = MappedInputManager::Button::Down;

// ScreenUp/ScreenDown are deliberately NOT used here even though they exist:
// in LandscapeCounterClockwise they resolve to Button::Left and Button::Right,
// which are PIN_UNASSIGNED on this board and can never fire, and the mapping is
// gated on a reader setting the player has probably never opened.

}  // namespace

std::unique_ptr<Activity> ForeheadActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<ForeheadActivity>(renderer, mappedInput);
}

void ForeheadActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  if (!loadState()) {
    deck.reset();
    record = fh::Record{};
    category = 0;
    roundSeconds = fh::kDefaultRoundSeconds;
  }
  view = View::Menu;
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  dirty = false;
  requestUpdate();
}

void ForeheadActivity::onExit() {
  // The orientation is global. Leaving it turned would rotate the shelf.
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  flushSave();
  Activity::onExit();
}

void ForeheadActivity::go(const View next) {
  const bool turning = landscape(view) != landscape(next);
  view = next;
  if (turning) {
    renderer.setOrientation(landscape(next) ? GfxRenderer::Orientation::LandscapeCounterClockwise
                                            : GfxRenderer::Orientation::Portrait);
  }
  requestUpdate();
}

int ForeheadActivity::secondsLeft() const {
  const uint32_t elapsed = static_cast<uint32_t>(millis()) - startMs;
  const int left = roundSeconds - static_cast<int>(elapsed / 1000);
  return left < 0 ? 0 : left;
}

void ForeheadActivity::startRound() {
  // millis() is the only entropy on this device that differs between two boots,
  // and by the time anybody has walked through a menu it has been running for
  // seconds. Mixed rather than used raw: the timer advances in even steps and
  // the bottom bit of a raw reading is not random.
  fh::Rng rng(toybox::seed());
  round.begin(category, roundSeconds, deck, rng);
  startMs = static_cast<uint32_t>(millis());
  lastTick = -1;
  swallowKeyRelease = false;
  resultPage = 0;
  go(View::Play);
}

void ForeheadActivity::endRound() {
  round.expire();
  // The ROUND's category, not the activity's. They cannot disagree today, but
  // the round is the thing that was played and the field beside it is a copy.
  record.push(round.category(), round.score());
  dirty = true;
  flushSave();
  resultPage = 0;
  // The refresh flash as punctuation, per docs/design-language.md. This device
  // has no speaker, so the full-refresh blink IS the buzzer -- and it is a
  // better one than a beep, because everybody in the room is already looking at
  // the panel when it goes off.
  flashOnNextPaint = true;
  // A key may still be down: the round read its press, and its release is about
  // to arrive on the results screen, where the same key means "next page".
  swallowKeyRelease = true;
  go(View::Result);
}

void ForeheadActivity::routeAction(const int action, const int value) {
  switch (action) {
    case ui::ActionReady:
      go(View::Ready);
      break;
    case ui::ActionStart:
      startRound();
      break;
    case ui::ActionMenuRow:
      switch (static_cast<ui::MenuRow>(value)) {
        case ui::MenuRow::Category:
          // Open on the page the current category is on, not on page one: with
          // seventeen lists, landing where the current one is not even visible
          // is the paging bug docs/shelf.md warns about in other clothes.
          pickerPage = category / ui::pickerRowsPerPage();
          go(View::Picker);
          break;
        case ui::MenuRow::Length: {
          int index = 0;
          for (int i = 0; i < fh::kRoundLengthCount; ++i) {
            if (fh::kRoundLengths[i] == roundSeconds) index = i;
          }
          roundSeconds = fh::kRoundLengths[(index + 1) % fh::kRoundLengthCount];
          dirty = true;
          // Changes in place, with the menu still open. The confirmation is a
          // label the player was going to read anyway, not a dialog.
          requestUpdate();
          break;
        }
        case ui::MenuRow::HowTo:
          howToPage = 0;
          go(View::HowTo);
          break;
        default:
          break;
      }
      break;
    case ui::ActionCategoryRow:
      if (value >= 0 && value < fh::kCategoryCount) {
        category = value;
        dirty = true;
        go(View::Menu);
      }
      break;
    case ui::ActionPage:
      if (view == View::Picker) {
        pickerPage = value;
      } else if (view == View::HowTo) {
        howToPage = value;
      }
      requestUpdate();
      break;
    case ui::ActionResultsPage:
      resultPage = value;
      requestUpdate();
      break;
    case ui::ActionHowToNext:
      if (howToPage + 1 < ui::howToPages()) {
        ++howToPage;
        requestUpdate();
      } else {
        go(View::Menu);
      }
      break;
    case ui::ActionGot:
    case ui::ActionMissed: {
      if (view != View::Play || !round.live()) break;
      fh::Rng rng(toybox::seed());
      if (action == ui::ActionGot) {
        round.got(deck, rng);
      } else {
        round.missed(deck, rng);
      }
      if (!round.live()) {
        endRound();
      } else {
        requestUpdate();
      }
      break;
    }
    case ui::ActionAgain:
      // Back to the ready card, not straight into a round: PLAY AGAIN is
      // usually somebody handing the device to the next person, and they need
      // the same three seconds to get it onto their head.
      go(View::Ready);
      break;
    case ui::ActionDone:
      go(View::Menu);
      break;
    default:
      break;
  }
}

void ForeheadActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (view == View::Menu) {
      // See src/apps_local/Shelf.h: no app names its own destination.
      flushSave();
      shelf::leave(renderer, mappedInput);
    } else {
      // Every other view backs out to the menu. A round abandoned this way
      // records nothing, because nothing was finished -- but the cards it dealt
      // stay dealt, which is what the deck mask is for.
      dirty = true;
      go(View::Menu);
    }
    return;
  }

  // The two keys, read as PRESSES rather than releases. Every other app in the
  // fork uses the release edge and should: a page turn is not a race. This one
  // is -- the card cannot change until a partial refresh has run, so adding the
  // release to that latency is the difference between a game that keeps up with
  // a shouting room and one that does not.
  if (view == View::Play) {
    // The clock is read FIRST, so a key press that lands after time is up
    // cannot score a card. The window is milliseconds -- rendering is its own
    // FreeRTOS task, so loop() is not blocked by the refresh -- but the whole
    // point of Mark::Unanswered is that the card in hand at the buzzer is
    // neither got nor given up on, and a press that beats the check would make
    // it one of them.
    const int left = secondsLeft();
    if (left <= 0) {
      endRound();
      return;
    }

    if (mappedInput.wasPressed(kGotItKey)) {
      routeAction(ui::ActionGot, 0);
      return;
    }
    if (mappedInput.wasPressed(kPassKey)) {
      routeAction(ui::ActionMissed, 0);
      return;
    }

    // Repaints are driven by the paint schedule moving, never by the second
    // hand: a per-second countdown is sixty partial refreshes a round on the
    // one screen somebody across the room is trying to read.
    // The schedule has to match what the round screen actually draws, or the
    // clock on the panel goes stale: the screen draws a bar, so a repaint is
    // owed exactly when a segment changes.
    const int tick = fh::barSegments(left, roundSeconds);
    if (tick != lastTick) {
      lastTick = tick;
      requestUpdate();
    }
  }

  // Either key pages a paged screen, which is what the case's moulded page keys
  // already mean on this device. Touch reaches every page through the pips, so
  // the buttons are never the only route.
  if (view == View::HowTo || view == View::Picker || view == View::Result) {
    const bool forward = mappedInput.wasReleased(MappedInputManager::Button::Down);
    const bool backward = mappedInput.wasReleased(MappedInputManager::Button::Up);
    // The round consumes key PRESSES; the results screen pages on RELEASES. A
    // key still held when the clock ran out therefore delivers its release to a
    // screen that never saw the press, and the results open on page two. One
    // stale release is eaten, and only the one.
    if (swallowKeyRelease && (forward || backward)) {
      swallowKeyRelease = false;
      return;
    }
    if (forward || backward) {
      const int step = forward ? 1 : -1;
      int* page = view == View::HowTo ? &howToPage : (view == View::Picker ? &pickerPage : &resultPage);
      const int pages = view == View::HowTo    ? ui::howToPages()
                        : view == View::Picker ? ui::pickerPages()
                                               : ui::resultPages(round.cards());
      const int next = *page + step;
      if (next >= 0 && next < pages) {
        *page = next;
        requestUpdate();
      }
      return;
    }
  }

  // Either key starts the round from the ready card: the guesser has it against
  // their forehead and cannot see which is which yet.
  if (view == View::Ready && (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
                              mappedInput.wasReleased(MappedInputManager::Button::Down))) {
    startRound();
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
  // Touch only, like the rest of this fork's games. See the note in
  // ConnectionsActivity for why there is no cursor.
  if (!input.touchReleased || !interactionsReady) return;

  const fui::ActionEvent action = interactions.route(input);
  routeAction(static_cast<int>(action.action), static_cast<int>(action.value));
}

void ForeheadActivity::render(RenderLock&&) {
  renderer.clearScreen();

  // The round and the ready card bind three sizes of Jersey at once, largest in
  // the title slot, so the card can measure all three and pick the biggest the
  // word fits in without a rebind. Everywhere else is the fork's usual faces.
  // One line per screen rather than one shared set that suits none of them.
  // Ready and Play share cardFaces because Ready's whole job is to show the
  // round's own layout before the round starts.
  const toybox::Faces faces = (view == View::Play || view == View::Ready) ? toybox::cardFaces()
                              : view == View::Result                     ? toybox::bigNumberFaces()
                                                                         : toybox::proseMenuFaces();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer, faces);
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions);
  toybox::Screen screen(frame);

  switch (view) {
    case View::Picker: {
      ui::PickerModel model;
      model.page = pickerPage;
      model.current = category;
      model.record = &record;
      ui::buildPicker(screen, model);
      break;
    }
    case View::HowTo: {
      ui::HowToModel model;
      model.page = howToPage;
      ui::buildHowTo(screen, model);
      break;
    }
    case View::Ready: {
      ui::ReadyModel model;
      model.category = category;
      model.roundSeconds = roundSeconds;
      ui::buildReady(screen, model);
      break;
    }
    case View::Play: {
      ui::PlayModel model;
      model.word = round.cardText();
      model.score = round.score();
      model.secondsLeft = secondsLeft();
      model.lengthSeconds = roundSeconds;
      model.category = category;
      ui::buildPlay(screen, model);
      break;
    }
    case View::Result: {
      ui::ResultModel model;
      model.category = category;
      model.score = round.score();
      model.page = resultPage;
      model.round = &round;
      ui::buildResult(screen, model);
      break;
    }
    case View::Menu:
    default: {
      ui::MenuModel model;
      model.category = category;
      model.roundSeconds = roundSeconds;
      model.record = &record;
      ui::buildMenu(screen, model);
      break;
    }
  }

  interactionsReady = true;
  toybox::reportOverflow(interactions, "Forehead");

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(flashOnNextPaint ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
  flashOnNextPaint = false;
}

void ForeheadActivity::flushSave() {
  if (!dirty) return;
  dirty = false;
  saveState();
}

void ForeheadActivity::saveState() {
  SaveState state{};
  state.category = static_cast<uint8_t>(category);
  state.roundSeconds = static_cast<uint8_t>(roundSeconds / 10);
  state.rounds = record.rounds;
  state.words = record.words;
  state.best = record.best;
  state.recentCount = record.recentCount;
  std::memcpy(state.recent, record.recent, sizeof(state.recent));
  std::memcpy(state.bestIn, record.bestIn, sizeof(state.bestIn));
  state.played = record.played;
  std::memcpy(state.deck, deck.mask(), sizeof(state.deck));

  HalFile file;
  if (!Storage.openFileForWrite("FRHD", kSavePath, file)) return;
  const uint8_t version = kSaveVersion;
  file.write(&version, 1);
  file.write(reinterpret_cast<const uint8_t*>(&state), sizeof(state));
  file.flush();
}

bool ForeheadActivity::loadState() {
  if (!Storage.exists(kSavePath)) return false;
  HalFile file;
  if (!Storage.openFileForRead("FRHD", kSavePath, file)) return false;
  uint8_t version = 0;
  if (file.read(&version, 1) != 1 || version != kSaveVersion) return false;
  SaveState state{};
  if (file.read(reinterpret_cast<uint8_t*>(&state), sizeof(state)) != sizeof(state)) return false;

  // Everything from the file is clamped against the tables as they are NOW, not
  // as they were when it was written. A build with fewer categories must not be
  // able to select one that no longer exists.
  category = state.category < fh::kCategoryCount ? state.category : 0;
  roundSeconds = fh::kDefaultRoundSeconds;
  for (const int length : fh::kRoundLengths) {
    if (length == state.roundSeconds * 10) roundSeconds = length;
  }

  record = fh::Record{};
  record.rounds = state.rounds;
  record.words = state.words;
  record.best = state.best;
  record.recentCount =
      state.recentCount > fh::Record::kRecentCount ? fh::Record::kRecentCount : state.recentCount;
  std::memcpy(record.recent, state.recent, sizeof(record.recent));
  std::memcpy(record.bestIn, state.bestIn, sizeof(record.bestIn));
  record.played = state.played;
  // setMask clears the bits above the last real entry, so a save written by a
  // build with a longer word list cannot leave this one short of a lap.
  deck.setMask(state.deck);
  return true;
}

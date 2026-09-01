#include "WavelengthActivity.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Memory.h>

#include <cstring>

#include "../../components/UITheme.h"
#include "../Shelf.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "WavelengthPairs.h"

namespace fui = freeink::ui;
namespace wl = wavelength;

namespace {

constexpr char kSavePath[] = "/.crosspoint/wavelength.sav";
constexpr uint8_t kSaveVersion = 1;

// Everything the front door draws plus the seen set, so a spectrum somebody
// remembers the target of does not come back next week.
struct SaveState {
  uint16_t rounds;
  uint16_t points;
  uint16_t buckets[wavelength::kBucketCount];
  uint16_t bestRoundTenths;
  uint32_t deck[wavelength::kSeenWords];
};

}  // namespace

std::unique_ptr<Activity> WavelengthActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<WavelengthActivity>(renderer, mappedInput);
}

void WavelengthActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  deck = wl::Deck(wl::kPairCountEn);
  rng = wl::Rng(static_cast<uint32_t>(millis()));
  session = wl::Session{};
  record = wl::Record{};
  sessionStarted = false;
  if (!loadState()) {
    deck.forgetSeen();
    record = wl::Record{};
  }
  view = View::Menu;
  practiceRound = session.isPractice();
  guess = wl::kSlots / 2;
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  requestUpdate();
}

void WavelengthActivity::onExit() {
  flushSave();
  Activity::onExit();
}

bool WavelengthActivity::loadState() {
  if (!Storage.exists(kSavePath)) return false;
  HalFile file;
  if (!Storage.openFileForRead("WAVE", kSavePath, file)) return false;
  uint8_t version = 0;
  if (file.read(&version, 1) != 1 || version != kSaveVersion) return false;
  SaveState state{};
  if (file.read(reinterpret_cast<uint8_t*>(&state), sizeof(state)) != sizeof(state)) return false;

  record = wl::Record{};
  record.rounds = state.rounds;
  record.points = state.points;
  std::memcpy(record.buckets, state.buckets, sizeof(record.buckets));
  record.bestRoundTenths = state.bestRoundTenths;

  // The seen set is clamped against the deck as it is NOW. A build with fewer
  // pairs must not carry marks for indices that no longer exist.
  deck.forgetSeen();
  for (int i = 0; i < wl::kPairCountEn && i < wl::kMaxPairs; ++i)
    if (state.deck[i / 32] & (1u << (i % 32))) deck.markSeen(i);
  return true;
}

void WavelengthActivity::saveState() {
  SaveState state{};
  state.rounds = record.rounds;
  state.points = record.points;
  std::memcpy(state.buckets, record.buckets, sizeof(state.buckets));
  state.bestRoundTenths = record.bestRoundTenths;
  for (int i = 0; i < wl::kPairCountEn && i < wl::kMaxPairs; ++i)
    if (deck.isSeen(i)) state.deck[i / 32] |= (1u << (i % 32));

  HalFile file;
  if (!Storage.openFileForWrite("WAVE", kSavePath, file)) return;
  const uint8_t version = kSaveVersion;
  file.write(&version, 1);
  file.write(reinterpret_cast<const uint8_t*>(&state), sizeof(state));
}

void WavelengthActivity::flushSave() {
  if (!dirty) return;
  dirty = false;
  saveState();
}

wavelengthui::Spectrum WavelengthActivity::spectrumAt(const int index) const {
  wavelengthui::Spectrum s;
  if (index >= 0 && index < wl::kPairCountEn) {
    s.top = wl::kPairsEn[index].top;
    s.bottom = wl::kPairsEn[index].bottom;
  }
  return s;
}

void WavelengthActivity::go(const View next) {
  view = next;
  peeking = false;
  nudgeHold = false;
  viewEnteredMs = millis();
  requestUpdate();
}

void WavelengthActivity::deal() {
  dealt = deck.dealChoice(rng, choice);
  if (dealt == 0) {
    // The deck is spent. Rather than silently re-dealing a spectrum somebody
    // remembers the target of, start it over: within one evening this cannot
    // happen at 120 pairs, and saying so beats pretending.
    deck.forgetSeen();
    dealt = deck.dealChoice(rng, choice);
  }
  go(View::Pick);
}

void WavelengthActivity::choose(const int which) {
  if (which >= dealt) return;
  spectrum = choice[which];
  deck.markSeen(spectrum);  // the one passed over goes back in the pool
  dirty = true;
  target = wl::drawTarget(rng);
  hasPeeked = false;
  abandoned = false;
  guess = wl::kSlots / 2;
  practiceRound = session.isPractice();
  go(View::Peek);
}

void WavelengthActivity::step(const int delta) {
  const int next = guess + delta;
  if (next < 1 || next > wl::kSlots) return;
  guess = next;
  requestUpdate();
}

void WavelengthActivity::lockIn() { go(View::Call); }

void WavelengthActivity::makeCall(const wl::Call call) {
  callWasRight = wl::endCallCorrect(guess, target, call);
  const bool wasPractice = session.isPractice();
  lastPoints = session.record(guess, target, call);
  // The practice round is played in full and simply does not count, in the
  // record as well as in the session.
  if (!wasPractice) {
    record.add(guess, target, call);
    const int avg = session.averageTenths();
    if (avg > record.bestRoundTenths) record.bestRoundTenths = static_cast<uint16_t>(avg);
    dirty = true;
    flushSave();
  }
  flashOnNextPaint = true;  // the reveal is the payoff
  go(View::Reveal);
}

void WavelengthActivity::routeAction(const int action) {
  switch (action) {
    case wavelengthui::ActionReady:
      deal();
      break;
    case wavelengthui::ActionPickFirst:
      choose(0);
      break;
    case wavelengthui::ActionPickSecond:
      choose(1);
      break;
    case wavelengthui::ActionClueGiven:
      // A quick TAP on the peek pad reveals nothing, so a player who taps
      // instead of holding could walk off this screen never having seen the
      // target and invent a clue from nothing. They only find out at the
      // reveal. Refuse to leave until they have actually looked.
      if (view == View::Peek && !hasPeeked) return;
      // The peek is one-way. Once the clue screen is passed there is no route
      // back to the target, or someone swipes back to it as a joke on round
      // five.
      go(view == View::Peek ? View::Clue : View::Dial);
      break;
    case wavelengthui::ActionLock:
      lockIn();
      break;
    case wavelengthui::ActionCallTop:
      makeCall(wl::Call::TowardTop);
      break;
    case wavelengthui::ActionCallBottom:
      makeCall(wl::Call::TowardBottom);
      break;
    case wavelengthui::ActionNextRound:
      practiceRound = session.isPractice();
      go(View::PassLeft);
      break;
    case wavelengthui::ActionStartRound:
      sessionStarted = true;
      practiceRound = session.isPractice();
      go(View::PassLeft);
      break;
    case wavelengthui::ActionHowTo:
      go(View::HowTo);
      break;
    case wavelengthui::ActionResume:
      go(pausedFrom);
      break;
    case wavelengthui::ActionAbandon:
      flashOnNextPaint = true;
      ++abandonedCount;
      go(View::PassLeft);
      abandoned = true;
      break;
    case wavelengthui::ActionBackToMenu:
      go(View::Menu);
      break;
    case wavelengthui::ActionEndSession:
      flushSave();
      go(View::Summary);
      break;
    case wavelengthui::ActionKeepPlaying:
      go(View::PassLeft);
      break;
    case wavelengthui::ActionNewSession:
      // The session resets; the record and the seen set do not. Those are the
      // table's history and the reason the front door is worth looking at.
      session = wl::Session{};
      abandonedCount = 0;
      sessionStarted = false;
      practiceRound = true;
      guess = wl::kSlots / 2;
      flushSave();
      go(View::Menu);
      break;
    default:
      break;
  }
}

void WavelengthActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (view == View::Reveal) {
      // Checked BEFORE committed(), which is true here: the reveal is
      // committed for the purpose of not sleeping, and must not be for the
      // purpose of backing out. Back went FORWARD into the next round, which
      // is the one direction a back gesture must never go.
      go(View::Menu);
    } else if (view == View::Paused) {
      // Back out of the pause resumes. The safe direction is the default.
      go(pausedFrom);
    } else if (committed(view)) {
      pausedFrom = view;
      go(View::Paused);
    } else if (view != View::Menu) {
      go(View::Menu);
    } else {
      // See src/apps_local/Shelf.h: no app names its own destination.
      flushSave();
      shelf::leave(renderer, mappedInput);
    }
    return;
  }

  // The peek's band shows only while a thumb is down. Tracked every loop rather
  // than on an edge, so lifting the finger hides it even if the release event
  // is missed.
  if (view == View::Peek) {
    int hx = 0;
    int hy = 0;
    const bool held = mappedInput.isScreenTouchHeld(hx, hy);
    const fui::Rect pad = wavelengthui::peekPadRect(static_cast<int16_t>(renderer.getScreenWidth()),
                                                    static_cast<int16_t>(renderer.getScreenHeight()));
    const bool onPad = held && hx >= pad.x && hx < pad.x + pad.width && hy >= pad.y && hy < pad.y + pad.height;
    if (onPad) {
      if (peekStartMs == 0) peekStartMs = millis();
      if (millis() - peekStartMs >= kSeenMs) hasPeeked = true;
    } else {
      peekStartMs = 0;
    }
    if (onPad != peeking) {
      const bool hiding = peeking && !onPad;
      peeking = onPad;
      if (onPad) nudgeHold = false;
      // A full refresh on the way DOWN, so no ghost of the band survives it.
      if (hiding) flashOnNextPaint = true;
      requestUpdate();
    }
    // Deliberately no early return. Returning here ate every tap on this
    // screen, because a tap is a touch-down before it is a release: the hold
    // check fired first and the button underneath never saw the release, so
    // the peek could be entered and never left.
  }

  if (view == View::Dial) {
    int tx = 0;
    int ty = 0;
    if (mappedInput.wasScreenTapped(tx, ty)) {
      const int slot = wavelengthui::dialSlotAt(static_cast<int16_t>(renderer.getScreenWidth()),
                                                static_cast<int16_t>(renderer.getScreenHeight()),
                                                static_cast<int16_t>(tx), static_cast<int16_t>(ty));
      if (slot != 0) {
        if (slot != guess) {
          guess = slot;
          requestUpdate();
        }
        return;
      }
    }
  }

  // A bare tap on a control that only answers to a hold says so, rather than
  // going silent. Silence on these two reads as a broken button: a cold player
  // tapped both, twice each, and concluded the device had died.
  {
    int tx = 0;
    int ty = 0;
    if ((view == View::Peek || view == View::Dial) && mappedInput.wasScreenTapped(tx, ty)) {
      const int16_t w = static_cast<int16_t>(renderer.getScreenWidth());
      const int16_t h = static_cast<int16_t>(renderer.getScreenHeight());
      const fui::Rect r = view == View::Peek ? wavelengthui::peekPadRect(w, h) : wavelengthui::lockBarRect(w, h);
      if (tx >= r.x && tx < r.x + r.width && ty >= r.y && ty < r.y + r.height && !nudgeHold) {
        nudgeHold = true;
        requestUpdate();
        return;
      }
    }
  }

  // HOLD TO LOCK means hold. Tracked against the same rect the screen drew, and
  // reset the moment the finger leaves it, so sliding off cancels rather than
  // committing. Shipped in v1.12.0 as a tap, which was the label lying.
  if (view == View::Dial) {
    int hx = 0;
    int hy = 0;
    const fui::Rect bar = wavelengthui::lockBarRect(static_cast<int16_t>(renderer.getScreenWidth()),
                                                    static_cast<int16_t>(renderer.getScreenHeight()));
    const bool onBar = mappedInput.isScreenTouchHeld(hx, hy) && hx >= bar.x && hx < bar.x + bar.width && hy >= bar.y &&
                       hy < bar.y + bar.height;
    if (onBar) {
      const uint32_t now = millis();
      if (lockHoldStartMs == 0) lockHoldStartMs = now;
      if (now - lockHoldStartMs >= static_cast<uint32_t>(wavelengthui::kLockHoldMs)) {
        lockHoldStartMs = 0;
        lockIn();
        return;
      }
    } else {
      lockHoldStartMs = 0;
    }
  }

  if (view == View::Dial) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      step(1);
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      step(-1);
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      lockIn();
      return;
    }
  }

  // The panel takes about half a second to show a new screen, so a tap landing
  // inside that window was aimed at the screen BEFORE it. Swallowing those is
  // what stops a double tap from crossing a screen boundary.
  if (millis() - viewEnteredMs < kSettleMs) {
    int sx = 0;
    int sy = 0;
    if (mappedInput.wasScreenTapped(sx, sy)) return;
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

  const fui::ActionEvent action = interactions.route(input);
  routeAction(static_cast<int>(action.action));
}

void WavelengthActivity::render(RenderLock&&) {
  renderer.clearScreen();

  // Three cuts at once: the huge one for the number the table reads across a
  // table, the display cut for the end words, the button cut for everything
  // else. End words are never drawn larger than the display cut; measured
  // against the deck, a quarter of them are wider than the panel at the next
  // size up.
  const toybox::Faces faces{toybox::kButtonFontId, toybox::kDisplayFontId, toybox::kHugeFontId};
  fui::GfxRendererTarget target_ = toybox::makeTarget(renderer, faces);
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target_, target_.deviceContext(), noInput, interactions);
  toybox::Screen screen(frame);

  const wavelengthui::Spectrum current = spectrumAt(spectrum);

  switch (view) {
    case View::Pick: {
      wavelengthui::PickModel model;
      model.first = spectrumAt(choice[0]);
      model.second = spectrumAt(choice[1]);
      model.onlyOne = dealt < 2;
      wavelengthui::renderPick(screen, model);
      break;
    }
    case View::Peek: {
      wavelengthui::PeekModel model;
      model.spectrum = current;
      model.target = target;
      model.revealed = peeking;
      model.everRevealed = hasPeeked;
      model.nudgeHold = nudgeHold;
      wavelengthui::renderPeek(screen, model);
      break;
    }
    case View::Clue: {
      wavelengthui::ClueModel model;
      model.spectrum = current;
      wavelengthui::renderClue(screen, model);
      break;
    }
    case View::Dial: {
      wavelengthui::DialModel model;
      model.spectrum = current;
      model.guess = guess;
      model.nudgeHold = nudgeHold;
      wavelengthui::renderDial(screen, model);
      break;
    }
    case View::Call: {
      wavelengthui::CallModel model;
      model.spectrum = current;
      model.guess = guess;
      model.practice = practiceRound;
      wavelengthui::renderCall(screen, model);
      break;
    }
    case View::Reveal: {
      wavelengthui::RevealModel model;
      model.spectrum = current;
      model.guess = guess;
      model.target = target;
      model.points = lastPoints;
      model.callWasRight = callWasRight;
      model.practice = practiceRound;
      model.roundNumber = session.round - 1;
      model.total = session.total;
      wavelengthui::renderReveal(screen, model);
      break;
    }
    case View::HowTo:
      wavelengthui::renderHowTo(screen);
      break;
    case View::Paused: {
      wavelengthui::PauseModel model;
      model.roundNumber = session.round;
      model.total = session.total;
      model.practice = practiceRound;
      model.abandoned = abandonedCount;
      wavelengthui::renderPause(screen, model);
      break;
    }
    case View::Summary: {
      wavelengthui::SummaryModel model;
      model.record = &record;
      model.rounds = session.scoredRounds;
      model.total = session.total;
      model.averageTenths = session.averageTenths();
      model.abandoned = abandonedCount;
      wavelengthui::renderSummary(screen, model);
      break;
    }
    case View::PassLeft: {
      wavelengthui::PassModel model;
      model.roundNumber = session.round;
      model.total = session.total;
      model.practice = session.isPractice();
      model.abandoned = abandoned;
      model.abandonedCount = abandonedCount;
      wavelengthui::renderPassLeft(screen, model);
      break;
    }
    case View::Menu:
    default: {
      wavelengthui::MenuModel model;
      model.record = &record;
      model.sessionInProgress = sessionStarted && session.round > 1;
      model.sessionRound = session.round;
      model.sessionTotal = session.total;
      model.sessionScored = session.scoredRounds;
      wavelengthui::renderMenu(screen, model);
      break;
    }
  }

  interactionsReady = true;
  toybox::reportOverflow(interactions, "Wavelength");

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // An activity publishes its own frame. Without this the screen is drawn into
  // the buffer and never shown, the previous activity's frame stays up, and it
  // looks exactly as though this one never started. The tell is the absence of
  // the "[GFX] Time = N ms from clearScreen to displayBuffer" log line.
  renderer.displayBuffer(flashOnNextPaint ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
  flashOnNextPaint = false;
}

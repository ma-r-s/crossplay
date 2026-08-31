#include "WavelengthActivity.h"

#include <Arduino.h>
#include <Memory.h>

#include "../../components/UITheme.h"
#include "../Shelf.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "WavelengthPairs.h"

namespace fui = freeink::ui;
namespace wl = wavelength;

std::unique_ptr<Activity> WavelengthActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<WavelengthActivity>(renderer, mappedInput);
}

void WavelengthActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  deck = wl::Deck(wl::kPairCountEn);
  rng = wl::Rng(static_cast<uint32_t>(millis()));
  session = wl::Session{};
  view = View::PassLeft;
  practiceRound = session.isPractice();
  guess = wl::kSlots / 2;
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  requestUpdate();
}

void WavelengthActivity::onExit() { Activity::onExit(); }

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
  target = wl::drawTarget(rng);
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
  lastPoints = session.record(guess, target, call);
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
      // The peek is one-way. Once the clue screen is passed there is no route
      // back to the target, or someone swipes back to it as a joke on round
      // five.
      go(view == View::Peek ? View::Clue : View::Dial);
      break;
    case wavelengthui::ActionStepTowardTop:
      step(1);
      break;
    case wavelengthui::ActionStepTowardBottom:
      step(-1);
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
    default:
      break;
  }
}

void WavelengthActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (committed(view)) {
      // Abandoning costs the clue-giver their turn, which is the whole point:
      // if backing out re-dealt for the same person, they could hunt for an
      // easy axis and the deck's strangest cards would never be played.
      flashOnNextPaint = true;
      go(View::PassLeft);
    } else {
      // See src/apps_local/Shelf.h: no app names its own destination.
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
    if (onPad != peeking) {
      const bool hiding = peeking && !onPad;
      peeking = onPad;
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
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      step(1);
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      step(-1);
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
      wavelengthui::renderDial(screen, model);
      break;
    }
    case View::Call: {
      wavelengthui::CallModel model;
      model.spectrum = current;
      model.guess = guess;
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
    case View::PassLeft:
    default: {
      wavelengthui::PassModel model;
      model.roundNumber = session.round;
      model.total = session.total;
      model.practice = session.isPractice();
      wavelengthui::renderPassLeft(screen, model);
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

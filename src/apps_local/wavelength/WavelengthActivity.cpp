#include "WavelengthActivity.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Memory.h>
#include <esp_random.h>

#include "../../components/UITheme.h"
#include "../Shelf.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "WavelengthPairs.h"

namespace fui = freeink::ui;
namespace wl = wavelength;

namespace {

constexpr char kSavePath[] = "/.crosspoint/wavelength.sav";
// Written first, renamed over the real file only once the bytes are down.
// openFileForWrite carries O_TRUNC, so writing in place empties the file at
// open: power lost in that window leaves nothing, and this app now writes on
// every screen change and every move of the marker rather than once a round.
// Trading one loss mode for a fifteen-times-more-likely one is not a fix.
constexpr char kSaveTmpPath[] = "/.crosspoint/wavelength.tmp";

}  // namespace

std::unique_ptr<Activity> WavelengthActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<WavelengthActivity>(renderer, mappedInput);
}

void WavelengthActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  deck = wl::Deck(wl::kPairCountEn);
  // The hardware RNG, not millis(). A wake from deep sleep is a chip reset, so
  // millis() is small and near-constant at exactly the moment a session now
  // resumes across one -- which would correlate the targets a device draws
  // from one power cycle to the next. The seed has to survive nothing, so it
  // may as well come from the one source that is actually unpredictable.
  rng = wl::Rng(esp_random());
  session = wl::Session{};
  record = wl::Record{};
  sessionStarted = false;
  view = View::Menu;
  guess = wl::kSlots / 2;
  practiceRound = session.isPractice();
  loadState();
  viewEnteredMs = millis();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  requestUpdate();
}

void WavelengthActivity::onExit() {
  saveState();
  Activity::onExit();
}

bool WavelengthActivity::resumable(const View v, const wl::Saved& saved) const {
  switch (v) {
    // The five screens of a live round all draw this round's spectrum, and the
    // last four of them are meaningless without the number behind them.
    case View::Peek:
    case View::Clue:
    case View::Dial:
    case View::Call:
    case View::Reveal:
      return saved.spectrum >= 0 && saved.spectrum < wl::kPairCountEn && saved.target >= 1 &&
             saved.target <= wl::kSlots && saved.guess >= 1 && saved.guess <= wl::kSlots;
    // The choice is dealt but nothing is drawn yet, so what has to survive is
    // the pair of spectra on offer rather than a number.
    case View::Pick:
      // BOTH offered pairs, not just the first. The screen draws the second
      // whenever `dealt` says two, so validating one of them leaves the other
      // free to render the placeholder spectrum and be chosen.
      return saved.dealt > 0 && saved.choice[0] >= 0 && saved.choice[0] < wl::kPairCountEn &&
             (saved.dealt < 2 || (saved.choice[1] >= 0 && saved.choice[1] < wl::kPairCountEn));
    // The pause is only as resumable as the screen underneath it.
    case View::Paused:
      return saved.resumeScreen < kViewCount && saved.resumeScreen != static_cast<uint8_t>(View::Paused) &&
             resumable(static_cast<View>(saved.resumeScreen), saved);
    case View::PassLeft:
    case View::Menu:
      return true;
    // HOW TO PLAY and the score sheet are each one tap from the front door and
    // neither is a position anybody is in the middle of. Coming back to the
    // front door with the session intact is what they resume to.
    case View::HowTo:
    case View::Summary:
    default:
      return false;
  }
}

void WavelengthActivity::loadState() {
  if (!Storage.exists(kSavePath)) return;
  HalFile file;
  if (!Storage.openFileForRead("WAVE", kSavePath, file)) return;
  uint8_t bytes[wl::kSaveBytes] = {};
  const int read = file.read(bytes, sizeof(bytes));
  wl::Saved saved;
  if (read <= 0 || !wl::unpack(bytes, static_cast<size_t>(read), saved)) {
    LOG_ERR("WAVE", "%s is not a save this build reads; starting fresh", kSavePath);
    return;
  }

  record = saved.record;
  // The seen set is clamped against the deck as it is NOW. A build with fewer
  // pairs must not carry marks for indices that no longer exist.
  deck.forgetSeen();
  for (int i = 0; i < wl::kPairCountEn && i < wl::kMaxPairs; ++i)
    if (saved.seen[i / 32] & (1u << (i % 32))) deck.markSeen(i);

  session = saved.session;
  sessionStarted = saved.sessionStarted;
  abandonedCount = saved.abandoned;

  const View wanted = saved.screen < kViewCount ? static_cast<View>(saved.screen) : View::Menu;
  if (!resumable(wanted, saved)) {
    // The evening survives even when the screen does not: the score, the round
    // number and the seen deck are still the table's, and the front door shows
    // them.
    view = View::Menu;
    practiceRound = session.isPractice();
    return;
  }

  view = wanted;
  pausedFrom = saved.resumeScreen < kViewCount ? static_cast<View>(saved.resumeScreen) : View::Dial;
  spectrum = saved.spectrum;
  choice[0] = saved.choice[0];
  choice[1] = saved.choice[1];
  dealt = saved.dealt;
  target = saved.target;
  guess = saved.guess >= 1 ? saved.guess : wl::kSlots / 2;
  lastPoints = saved.lastPoints;
  hasPeeked = saved.hasPeeked;
  practiceRound = saved.practiceRound;
  callWasRight = saved.callWasRight;
  abandoned = saved.abandonedRound;
  // A resumed round arrives over whatever the shelf left on the panel, and one
  // of the screens it can arrive on is the peek. Spend the full refresh: a
  // partial one there could leave a ghost of the only secret in the game, which
  // is the same reason hiding the band costs one.
  flashOnNextPaint = true;
  LOG_INF("WAVE", "Resuming round %d on screen %u", session.round, static_cast<unsigned>(saved.screen));
}

void WavelengthActivity::saveState() {
  wl::Saved saved;
  saved.record = record;
  for (int i = 0; i < wl::kPairCountEn && i < wl::kMaxPairs; ++i)
    if (deck.isSeen(i)) saved.seen[i / 32] |= (1u << (i % 32));

  saved.session = session;
  saved.sessionStarted = sessionStarted;
  saved.abandoned = static_cast<uint16_t>(abandonedCount);

  saved.screen = static_cast<uint8_t>(view);
  saved.resumeScreen = static_cast<uint8_t>(pausedFrom);
  saved.spectrum = static_cast<int16_t>(spectrum);
  saved.choice[0] = static_cast<int16_t>(choice[0]);
  saved.choice[1] = static_cast<int16_t>(choice[1]);
  saved.dealt = static_cast<uint8_t>(dealt);
  saved.target = static_cast<uint8_t>(target);
  saved.guess = static_cast<uint8_t>(guess);
  saved.lastPoints = static_cast<uint8_t>(lastPoints);
  saved.hasPeeked = hasPeeked;
  saved.practiceRound = practiceRound;
  saved.callWasRight = callWasRight;
  saved.abandonedRound = abandoned;

  uint8_t bytes[wl::kSaveBytes] = {};
  const size_t written = wl::pack(saved, bytes, sizeof(bytes));
  if (written == 0) return;

  // Temp, flush, release, then rename. The old file survives intact until the
  // new one is complete on the card, so the worst a power cut can cost is the
  // one screen being written -- which is what the doc claims and what writing
  // in place did not deliver.
  HalFile file;
  if (!Storage.openFileForWrite("WAVE", kSaveTmpPath, file)) return;
  const bool ok = file.write(bytes, written) == static_cast<int>(written);
  file.flush();
  file = HalFile{};
  if (!ok) {
    LOG_ERR("WAVE", "Short write to %s; the previous save is left alone", kSaveTmpPath);
    Storage.remove(kSaveTmpPath);
    return;
  }
  Storage.remove(kSavePath);
  Storage.rename(kSaveTmpPath, kSavePath);
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
  // Every screen change is a position worth coming back to, so the card is
  // written here rather than on the way out. A round survives the Home key and
  // survives the chip reset that a deep sleep really is, and the ~120 bytes
  // cost nothing beside the panel repaint this same call is about to order.
  saveState();
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
  saveState();
  requestUpdate();
}

void WavelengthActivity::lockIn() {
  if (kMode == wl::Mode::Teams) {
    go(View::Call);
    return;
  }
  // The call value is never read in co-op; testCoOpNeverConsultsTheCall proves
  // that by scoring every position both ways and requiring the same answer.
  makeCall(wl::Call::TowardTop);
}

void WavelengthActivity::makeCall(const wl::Call call) {
  callWasRight = wl::endCallCorrect(guess, target, call);
  const bool wasPractice = session.isPractice();
  lastPoints = session.record(guess, target, call, kMode);
  // The practice round is played in full and simply does not count, in the
  // record as well as in the session.
  if (!wasPractice) {
    record.add(guess, target, call, kMode);
    const int avg = session.averageTenths();
    if (avg > record.bestRoundTenths) record.bestRoundTenths = static_cast<uint16_t>(avg);
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
      // A round that was PLAYED is not an abandoned one. The flag used to be
      // cleared only by the next deal, so walking out to the front door and
      // back put the abandon note on a pass screen that had earned none -- and
      // it survives a power cycle now.
      abandoned = false;
      go(View::PassLeft);
      break;
    case wavelengthui::ActionStartRound:
      sessionStarted = true;
      practiceRound = session.isPractice();
      abandoned = false;
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
      // Set BEFORE go(), which is what writes the card: the pass screen has to
      // announce the abandon, and a resume that lost the flag would show a
      // normal pass instead -- exactly the silence the count exists to break.
      abandoned = true;
      go(View::PassLeft);
      break;
    case wavelengthui::ActionBackToMenu:
      go(View::Menu);
      break;
    case wavelengthui::ActionEndSession:
      go(View::Summary);
      break;
    case wavelengthui::ActionKeepPlaying:
      abandoned = false;
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
      // And the round in flight goes with it. A round that outlived the
      // session it belonged to would resume onto a board whose score had been
      // cleared out from under it -- persistence causing its own bug, which is
      // the failure this whole mechanism is one wrong line away from.
      spectrum = -1;
      target = 0;
      dealt = 0;
      choice[0] = -1;
      choice[1] = -1;
      hasPeeked = false;
      abandoned = false;
      lastPoints = 0;
      callWasRight = false;
      pausedFrom = View::Dial;
      go(View::Menu);
      break;
    default:
      break;
  }
}

void WavelengthActivity::loop() {
  if (awaitingRelease) {
    int rx = 0;
    int ry = 0;
    if (mappedInput.isScreenTouchHeld(rx, ry)) return;
    awaitingRelease = false;
    int sx = 0;
    int sy = 0;
    mappedInput.wasScreenTapped(sx, sy);  // consume the release that ended the hold
    return;
  }

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
      saveState();
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
          saveState();
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
        awaitingRelease = true;
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
    if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      const uint32_t now = millis();
      if (confirmHoldStartMs == 0) confirmHoldStartMs = now;
      if (now - confirmHoldStartMs >= static_cast<uint32_t>(wavelengthui::kLockHoldMs)) {
        confirmHoldStartMs = 0;
        lockIn();
        return;
      }
    } else {
      confirmHoldStartMs = 0;
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
      model.roundNumber = session.round;
      model.practice = practiceRound;
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
      model.roundNumber = session.round;
      model.practice = practiceRound;
      wavelengthui::renderClue(screen, model);
      break;
    }
    case View::Dial: {
      wavelengthui::DialModel model;
      model.spectrum = current;
      model.guess = guess;
      model.nudgeHold = nudgeHold;
      model.roundNumber = session.round;
      model.practice = practiceRound;
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
      model.showCall = kMode == wl::Mode::Teams;
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
      model.nextRound = session.round;
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

#pragma once

// WAVELENGTH: the spectrum guessing game, one device passed round a table.
//
// The activity is the thin layer: it owns the input, the deck and the running
// session. The rules are in WavelengthCore.h (freestanding) and the drawing is
// in WavelengthScreens.cpp (freestanding).

#include <memory>

#include "../../activities/Activity.h"
#include "WavelengthCore.h"
#include "WavelengthScreens.h"

class WavelengthActivity final : public Activity {
 public:
  WavelengthActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Wavelength", renderer, mappedInput), deck(0), rng(1) {}
  ~WavelengthActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  // Suppressed for the whole round, peek through reveal, and released only at
  // PASS LEFT. There is a long stretch where the table argues and nobody
  // touches the glass, and a device that sleeps mid-argument has to be woken by
  // somebody who then sees the screen. INSIDER only suppressed sleep while its
  // clock ran; this game has no clock and needs it anyway.
  bool preventAutoSleep() override { return committed(view) || view == View::PassLeft || view == View::Pick; }

 private:
  // The round, in the order the device is passed. Everything from Peek onward
  // is committed: backing out abandons the round and passes left, which is what
  // stops a clue-giver quietly re-dealing until an easy axis turns up.
  enum class View : uint8_t { Menu, HowTo, PassLeft, Pick, Peek, Clue, Dial, Call, Reveal, Summary };

  static bool committed(const View v) {
    return v != View::Menu && v != View::HowTo && v != View::PassLeft && v != View::Pick && v != View::Summary;
  }

  void go(View next);
  void deal();
  void choose(int which);
  void lockIn();
  void makeCall(wavelength::Call call);
  void step(int delta);
  void routeAction(int action);
  wavelengthui::Spectrum spectrumAt(int index) const;

  wavelength::Deck deck;
  wavelength::Rng rng;
  wavelength::Session session;
  wavelength::Record record;

  bool loadState();
  void saveState();
  void flushSave();
  bool dirty = false;
  bool sessionStarted = false;

  View view = View::Menu;
  int choice[2] = {-1, -1};
  int dealt = 0;
  int spectrum = 0;
  int target = 10;
  int guess = 10;
  int lastPoints = 0;
  bool callWasRight = false;
  bool practiceRound = true;
  bool peeking = false;
  uint32_t lockHoldStartMs = 0;
  uint32_t stepHoldStartMs = 0;
  uint32_t lastRepeatMs = 0;
  bool hasPeeked = false;
  bool abandoned = false;
  bool nudgeHold = false;
  uint32_t peekStartMs = 0;
  // How long the band must actually have been on the panel before the clue
  // screen will open. Longer than a tap and longer than one refresh, so it
  // means "was seen" rather than "was touched".
  static constexpr uint32_t kSeenMs = 400;
  uint32_t viewEnteredMs = 0;
  // How long after a screen change a tap is treated as aimed at the previous
  // screen. Roughly one panel refresh: until then the new screen is not
  // actually visible, so nobody can have meant to press it.
  static constexpr uint32_t kSettleMs = 500;

  // A full refresh is spent deliberately: on the reveal, which is the payoff,
  // and on hiding the peek, where a partial refresh could leave a ghost of the
  // only secret in the game.
  bool flashOnNextPaint = false;

  bool interactionsReady = false;
  toybox::Interactions interactions;
};

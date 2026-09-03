#pragma once

// WAVELENGTH: the spectrum guessing game, one device passed round a table.
//
// The activity is the thin layer: it owns the input, the deck and the running
// session. The rules are in WavelengthCore.h (freestanding) and the drawing is
// in WavelengthScreens.cpp (freestanding).

#include <memory>

#include "../../activities/Activity.h"
#include "WavelengthCore.h"
#include "WavelengthSave.h"
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
  // Co-op for now. Teams mode is a designed feature, not a flag to flip: see
  // docs/apps/wavelength-teams.md for the four questions it must answer first.
  static constexpr wavelength::Mode kMode = wavelength::Mode::CoOp;

  bool preventAutoSleep() override {
    return committed(view) || view == View::PassLeft || view == View::Pick || view == View::Paused;
  }

 private:
  // The round, in the order the device is passed. Everything from Peek onward
  // is committed: backing out abandons the round and passes left, which is what
  // stops a clue-giver quietly re-dealing until an easy axis turns up.
  //
  // These values are WRITTEN TO THE CARD, so reordering them makes an old save
  // resume onto a different screen. Append; do not renumber. Menu is 0 because
  // that is also what an empty save block means.
  enum class View : uint8_t {
    Menu,
    HowTo,
    PassLeft,
    Pick,
    Peek,
    Clue,
    Dial,
    Call,
    Reveal,
    Summary,
    Paused,
    // WHOSE GAME IS THIS? The front door for a launch that found somebody
    // else's evening on the card. NEVER WRITTEN TO THE CARD -- it is a question
    // about the file, not a position in the game, so saveState() refuses to
    // write while it is up and an undecided question stays undecided.
    Resume,
    // Not a screen, and it stays last. A load has to know whether the screen
    // number on the card is one this build has, and written as a literal that
    // was a derived fact kept where the enum could not update it: append a
    // screen and every save written on it would silently resume to the front
    // door instead.
    Count,
  };
  static constexpr uint8_t kViewCount = static_cast<uint8_t>(View::Count);

  // The screens that hold a round somebody is in the middle of. Written as an
  // exclusion list, so a screen appended to the enum joins it by default -- and
  // Resume, which is a question in front of the front door and not a round at
  // all, has to say so or it silently stops the device sleeping and reads as a
  // round that cannot be backed out of.
  static bool committed(const View v) {
    return v != View::Menu && v != View::HowTo && v != View::PassLeft && v != View::Pick && v != View::Summary &&
           v != View::Paused && v != View::Resume;
  }

  void go(View next);
  void deal();
  void choose(int which);
  void lockIn();
  void makeCall(wavelength::Call call);
  void startNewSession();
  void step(int delta);
  void routeAction(int action);
  wavelengthui::Spectrum spectrumAt(int index) const;

  wavelength::Deck deck;
  wavelength::Rng rng;
  wavelength::Session session;
  wavelength::Record record;

  // The card is written at every point the game reaches a new position, not on
  // the way out of the app. Home destroys the activity and deep sleep resets
  // the chip, so anything only written by onExit() is a round the table loses.
  void loadState();
  void saveState();
  // Whether a saved screen can be resumed onto, given what the save actually
  // carries. A round screen with no spectrum behind it is a corrupt file, not
  // a game.
  bool resumable(View v, const wavelength::Saved& saved) const;
  // Unix seconds, or 0 when this device cannot say. No RTC on some boards, and
  // on the rest the clock is only ever set by an NTP sync over Wi-Fi.
  static uint32_t nowOrZero();
  // Which run of the chip this is. Deep sleep is a chip reset and there is no
  // clock to rely on, so this is the only continuity question the fork can
  // actually answer: see wavelength::resumeFor.
  static uint32_t bootId();
  bool sessionStarted = false;

  // Set only while View::Resume is up: the screen the table would return to if
  // it says the game is theirs, and the two facts that screen puts on the
  // panel. pendingView is never View::Resume.
  View pendingView = View::Menu;
  int resumeMinutesAgo = -1;
  bool resumeRoundInFlight = false;

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
  bool hasPeeked = false;
  bool abandoned = false;
  int abandonedCount = 0;
  View pausedFrom = View::Dial;
  // Set when a bare tap lands on the PEEK pad, which is the one control left in
  // this app that answers only to a held finger -- and it holds because the band
  // shows WHILE the thumb is down, not after a duration. Silence there read as a
  // dead device to a cold player. The lock had one of these too and no longer
  // needs it: it is an ordinary button and a tap simply works.
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
  static constexpr uint32_t kSettleMs = 1100;
  static_assert(kSettleMs >= 1000,
                "a settle shorter than a second is one panel refresh, and a realistic second tap "
                "punches through to the next screen's button");

  // A full refresh is spent deliberately: on the reveal, which is the payoff,
  // and on hiding the peek, where a partial refresh could leave a ghost of the
  // only secret in the game.
  bool flashOnNextPaint = false;

  bool interactionsReady = false;
  toybox::Interactions interactions;
};

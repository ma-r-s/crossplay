#pragma once

// Insider: a party game for four to eight people and one device.
//
// The thing that makes this port work is that the device is never a screen you
// play on. It is a prop that gets handed round a table: it keeps the secret,
// it holds the clock, and it settles the argument at the end. Everything
// interesting happens between people, out loud, while it sits there.
//
// That is also why it needs no radio. The link layer is for two devices playing
// each other; this is one device and N humans, which is a different game shape
// entirely and a much older one.

#include <memory>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "InsiderCore.h"
#include "InsiderScreens.h"

class InsiderActivity final : public Activity {
 public:
  InsiderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Insider", renderer, mappedInput) {}
  ~InsiderActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  // A round is five minutes in which nobody necessarily touches the device, and
  // the whole point is that it sits in the middle of the table counting down.
  // Without this it goes to sleep at minute two and the game stops.
  bool preventAutoSleep() override { return view == View::Questions; }

 private:
  enum class View : uint8_t {
    Menu,
    Rules,
    Pass,
    Questions,
    Vote,
    Reveal,
  };

  void dealRound();
  void advancePass();
  void startClock();
  void settle(insider::Outcome outcome);
  void goToMenu();
  void routeAction(int action, int value);

  void saveState();
  bool loadState();
  void flushSave();

  insider::Deck deck;
  insider::Record record;
  insider::Round round;

  View view = View::Menu;
  int players = 5;
  int seat = 0;
  bool revealed = false;

  // Wall-clock start of the questions phase, and the last clock face drawn.
  // Repainting is driven by the second of these changing, never by the first.
  uint32_t clockStartMs = 0;
  int lastClockTick = -1;

  // Which page of the tutorial deck is showing.
  int tutorialPage = 0;

  int chosen = insiderui::VoteModel::kNothingChosen;
  int accused = insider::kNoInsider;
  insider::Outcome outcome = insider::Outcome::Won;

  bool dirty = false;
  bool flashOnNextPaint = false;
  bool interactionsReady = false;
  toybox::Interactions interactions;
};

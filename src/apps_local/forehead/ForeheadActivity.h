#pragma once

// FOREHEAD: the party game where the person holding the device is the only one
// who cannot see it.
//
// The activity is the thin layer: it owns the clock, the orientation, the save
// file and the two keys. The rules are in ForeheadCore.h (freestanding) and the
// drawing is in ForeheadScreens.cpp (freestanding). See docs/apps/forehead.md.

#include <memory>

#include "../../activities/Activity.h"
#include "ForeheadCore.h"
#include "ForeheadScreens.h"

class ForeheadActivity final : public Activity {
 public:
  ForeheadActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Forehead", renderer, mappedInput) {}
  ~ForeheadActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class View : uint8_t { Menu, Picker, HowTo, Ready, Play, Result };

  // Landscape for everything you hold against your head, portrait for
  // everything you hold in your hand. The orientation is global, so it is set
  // on every view change and put back in onExit -- leaving it turned rotates
  // whatever activity comes next.
  static bool landscape(View view) { return view == View::Ready || view == View::Play || view == View::Result; }
  void go(View next);

  void startRound();
  void endRound();
  void routeAction(int action, int value);
  int secondsLeft() const;

  bool loadState();
  void saveState();
  void flushSave();

  forehead::Deck deck;
  forehead::Round round;
  forehead::Record record;

  View view = View::Menu;
  int category = 0;
  int roundSeconds = forehead::kDefaultRoundSeconds;
  int pickerPage = 0;
  int howToPage = 0;
  int resultPage = 0;

  uint32_t startMs = 0;
  int lastTick = -1;

  bool dirty = false;
  bool flashOnNextPaint = false;
  bool interactionsReady = false;
  toybox::Interactions interactions;
};

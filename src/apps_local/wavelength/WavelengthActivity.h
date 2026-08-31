#pragma once

// WAVELENGTH: the spectrum guessing game, one device passed round a table.
//
// The activity is the thin layer: it owns the input and the orientation. The
// rules are in WavelengthCore.h (freestanding) and the drawing is in
// WavelengthScreens.cpp (freestanding).
//
// Only the DIAL exists so far, so that the three board arrangements can be shot
// side by side before the rest is built on whichever one wins.

#include <memory>

#include "../../activities/Activity.h"
#include "WavelengthCore.h"
#include "WavelengthScreens.h"

class WavelengthActivity final : public Activity {
 public:
  WavelengthActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Wavelength", renderer, mappedInput) {}
  ~WavelengthActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void step(int delta);
  void routeAction(int action);

  int guess = 10;

  bool interactionsReady = false;
  toybox::Interactions interactions;
};

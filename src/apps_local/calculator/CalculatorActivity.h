#pragma once

// Calculator on the device. The thin layer: chrome, drawing, taps.
//
// The pad is drawn by hand into the body rect rather than built from
// fui::keyGrid, and that is not a preference. toybox::kMaxInteractions is 24;
// twenty keys fits, but a screen that goes past it loses the LAST ones
// registered, on the device only, silently -- they draw, they look live, and
// they answer nothing. The fix for any grid near that ceiling is one geometry
// function used for BOTH the drawing and the hit test, which is what
// calc::keyRect and calc::keyAt are. See the Connections archive, which shipped
// with every date from the 20th onward dead.

#include <memory>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "CalcEngine.h"
#include "CalcStyle.h"

class CalculatorActivity final : public Activity {
 public:
  CalculatorActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Calculator", renderer, mappedInput) {}
  ~CalculatorActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void drawChrome();
  void drawDisplay(const calc::PadGeom& g);
  void drawPad(const calc::PadGeom& g);
  void drawKey(const calc::KeyDef& key, const calc::Rect16& r);

  calc::Engine engine;
  toybox::Interactions interactions;
  bool interactionsReady = false;
};

#pragma once

#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>

#include <atomic>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Frontlight quick panel (boards with a frontlight + home key, e.g. X4 Pro).
// Opened globally by the top-edge down-swipe: brightness and warmth sliders
// driving the light live, plus an on/off toggle with a sun icon. State is
// persisted once on exit (SPIFFS write throttling), not per slider tick.
class FrontlightPanelActivity final : public Activity {
  using UiApp = freeink::ui::FreeInkApp<8, 4>;

  ButtonNavigator buttonNavigator;

  freeink::ui::GfxRendererTarget uiTarget;  // must precede `app`: the app holds a reference to it
  UiApp app;
  std::atomic<bool> uiReady{false};

  uint8_t brightness = 60;
  uint8_t warmth = 50;
  bool lightOn = false;
  // Swallow the swipe/tap fallout of a slider drag so its release can't
  // trigger the back gesture and close the panel mid-adjustment.
  bool draggingSlider = false;

  static void panelScreen(UiApp::ScreenType& screen, void* user);
  static void onBrightnessEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onWarmthEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onToggleEvent(const freeink::ui::ActionEvent& event, void* user);
  void buildPanelScreen(UiApp::ScreenType& screen);
  void adjustBrightness(int delta);
  void toggleLight();
  void close();

 public:
  explicit FrontlightPanelActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  // The home key closes the panel back to whatever was underneath.
  bool handleHomeGesture() override;
};

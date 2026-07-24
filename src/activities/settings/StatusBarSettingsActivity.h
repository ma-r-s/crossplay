#pragma once
#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>

#include <atomic>
#include <string>

#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"

// Reader status bar configuration activity
class StatusBarSettingsActivity final : public Activity {
 public:
  explicit StatusBarSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // FreeInkApp hosts the settings list (themed rows, touch routing); the header
  // stays on GUI.drawHeader for the battery indicator and the live status-bar
  // preview renders raw below the list.
  using UiApp = freeink::ui::FreeInkApp<12, 4>;

  ButtonNavigator buttonNavigator;
  OptionPopup optionPopup;

  int selectedIndex = 0;
  // Decided in onEnter() based on halClock.isAvailable() so clock entries are hidden on X4.
  int visibleItemCount = 0;

  freeink::ui::GfxRendererTarget uiTarget;  // must precede `app`: the app holds a reference to it
  UiApp app;
  // render() rebuilds the app's interaction table; loop() only routes touch
  // snapshots against it while this is true (the two run on different tasks).
  std::atomic<bool> uiReady{false};
  int visibleRows = 1;  // rows per page at the current scale; set by the screen builder
  int topIndex = 0;     // viewport scroll position, decoupled from the selection

  static void listScreen(UiApp::ScreenType& screen, void* user);
  static void onRowEvent(const freeink::ui::ActionEvent& event, void* user);
  void buildListScreen(UiApp::ScreenType& screen);
  std::string rowValueText(int index);

  void handleSelection();
};

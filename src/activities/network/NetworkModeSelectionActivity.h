#pragma once

#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>

#include <atomic>
#include <functional>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

enum class NetworkMode { JOIN_NETWORK, CONNECT_CALIBRE, CREATE_HOTSPOT };

/**
 * NetworkModeSelectionActivity presents the user with a choice:
 * - "Join a Network" - Connect to an existing WiFi network (STA mode)
 * - "Connect to Calibre" - Use Calibre wireless device transfers
 * - "Create Hotspot" - Create an Access Point that others can connect to (AP mode)
 *
 * The onModeSelected callback is called with the user's choice.
 * The onCancel callback is called if the user presses back.
 */
class NetworkModeSelectionActivity final : public Activity {
  // FreeInkApp hosts the mode list (themed rows, icons, touch routing); the
  // header stays on GUI.drawHeader for the battery indicator.
  using UiApp = freeink::ui::FreeInkApp<12, 4>;

  ButtonNavigator buttonNavigator;

  int selectedIndex = 0;

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
  void selectCurrent();

 public:
  explicit NetworkModeSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  void onModeSelected(NetworkMode mode);
  void onCancel();
};

#pragma once

#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "components/UiAppHelpers.h"
#include "util/ButtonNavigator.h"

class FileBrowserActivity final : public Activity {
 public:
  // Books = standard reader browser; PickFirmware = filter to .bin only and return path via ActivityResult.
  enum class Mode { Books, PickFirmware };

 private:
  // FreeInkApp hosts the file list (themed rows, icons, touch routing); the
  // header stays on GUI.drawHeader for the battery indicator.
  using UiApp = freeink::ui::FreeInkApp<20, 4>;

  // Deletion
  bool removeDirFile(const std::string& fullPath);

  ButtonNavigator buttonNavigator;

  size_t selectorIndex = 0;

  bool lockLongPressBack = false;
  // True when this activity was entered while Confirm was already held; we must swallow the next
  // release so we don't immediately auto-open the first entry.
  bool lockNextConfirmRelease = false;

  Mode mode = Mode::Books;

  // Files state
  std::string basepath = "/";
  std::vector<std::string> files;
  std::unique_ptr<char[]> fileNameBuffer;

  freeink::ui::GfxRendererTarget uiTarget;  // must precede `app`: the app holds a reference to it
  UiApp app;
  // render() rebuilds the app's interaction table; loop() only routes touch
  // snapshots against it while this is true (the two run on different tasks).
  std::atomic<bool> uiReady{false};
  // Detects a hold on a file row and fires "delete" while the finger is down.
  TouchLongPressRouter longPressTouch;
  int visibleRows = 1;  // rows per page at the current scale; set by the screen builder
  int topIndex = 0;     // viewport scroll position, decoupled from the selection

  static void listScreen(UiApp::ScreenType& screen, void* user);
  static void onRowEvent(const freeink::ui::ActionEvent& event, void* user);
  void buildListScreen(UiApp::ScreenType& screen);
  // forceDelete routes the touch long-press to the delete branch; button
  // navigation leaves it false and relies on getHeldTime() instead.
  void activateSelected(bool forceDelete = false);

  // Data loading
  void loadFiles();
  size_t findEntry(const std::string& name) const;

 public:
  explicit FileBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialPath = "/",
                               Mode mode = Mode::Books);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};

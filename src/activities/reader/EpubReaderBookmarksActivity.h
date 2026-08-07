#pragma once
#include <Epub.h>
#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "../../BookmarkEntry.h"
#include "../Activity.h"
#include "components/OptionPopup.h"
#include "components/UiAppHelpers.h"
#include "util/ButtonNavigator.h"

class EpubReaderBookmarksActivity final : public Activity {
  // FreeInkApp hosts the bookmark list (themed rows, touch routing); the title
  // keeps its legacy draw, and OptionPopup keeps its legacy overlay rendering
  // for the delete confirmation.
  using UiApp = freeink::ui::FreeInkApp<20, 2>;

  std::shared_ptr<Epub> epub;
  std::string epubPath;
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  std::vector<BookmarkEntry> bookmarks;
  bool confirmingDelete = false;
  OptionPopup confirmPopup;
  // True while the button press that closed the popup is still held; its release
  // must not fall through to the list's own Back/Confirm handlers.
  bool popupClosing = false;

  freeink::ui::GfxRendererTarget uiTarget;  // must precede `app`: the app holds a reference to it
  UiApp app;
  // render() rebuilds the app's interaction table; loop() only routes touch
  // snapshots against it while this is true (the two run on different tasks).
  std::atomic<bool> uiReady{false};
  // Detects a hold on a bookmark row and fires "delete" while the finger is down.
  TouchLongPressRouter longPressTouch;
  int visibleRows = 1;  // rows per page at the current scale; set by the screen builder
  int topIndex = 0;     // viewport scroll position, decoupled from the selection

 public:
  explicit EpubReaderBookmarksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                       const std::shared_ptr<Epub>& epub, const std::string& epubPath);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static void listScreen(UiApp::ScreenType& screen, void* user);
  static void onRowEvent(const freeink::ui::ActionEvent& event, void* user);
  void buildListScreen(UiApp::ScreenType& screen);

  // Open the selected bookmark: finishes with a ProgressChangeResult for the reader.
  void openSelectedBookmark();

  // Opens the Cancel/Delete confirmation for the selected bookmark; shared by
  // the physical Confirm hold and the touch row long-press.
  void showDeleteConfirmation();

  // Delete the currently selected bookmark and persist the list
  void deleteSelectedBookmark();
};

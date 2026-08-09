#pragma once

// One folder of the shelf, on screen. GAMES and APPS are the same activity with
// a different index: a folder is a title and a list, and nothing about drawing
// one depends on what kind of things are in it.
//
// Deliberately thin. The list, its scrolling and its hit-testing are a FreeInkUI
// list component, so this class is a registry read plus an action switch.

#include <Icon.h>

#include <memory>

#include "../activities/Activity.h"
#include "ui/ToyboxScreen.h"

class ShelfFolderActivity final : public Activity {
 public:
  ShelfFolderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, int folderIndex)
      : Activity("ShelfFolder", renderer, mappedInput), folder(folderIndex) {}
  ~ShelfFolderActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput, int folderIndex);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // Fills the two arrays with one page of the folder, starting at `first`.
  void buildPage(int first, int count);

  // One screen's worth of rows, not one folder's worth of items.
  //
  // This used to be sized by the registry, so the folder's size was bounded by
  // an activity's array and a seventeenth game could not exist. Now that the
  // screen is handed a page at a time, only the page is ever copied, and a
  // folder can hold as many games as Mario writes down. Nine rows fit a folder
  // with the player bar and ten without; the slack is there so a token change
  // cannot silently overflow it, and static_assert catches it if one does.
  static constexpr int kMaxRowsPerPage = 16;

  const int folder;
  freeink::ui::ListItem items[kMaxRowsPerPage] = {};
  const freeink::Icon* icons[kMaxRowsPerPage] = {};
  // The whole folder's count, not the page's.
  int itemCount = 0;
  // Where the shelf resumes, and therefore which page is shown. NOT a cursor:
  // nothing draws it as a selection and no button moves it as one. See
  // docs/buttons.md.
  int selected = 0;
  // Whether the cursor has been revealed. See onEnter().
  // How many rows a page holds, from the last render. Cached rather than derived
  // in loop() because it is a property of the screen's geometry and not of the
  // selection, so no input can make it disagree with what was drawn. The page
  // itself is deliberately not stored: that one would drift the moment a button
  // moved the cursor. A tap can only arrive after a render, which is what makes
  // this safe -- interactionsReady says so.
  int rowsPerPage = 1;

  toybox::Interactions interactions;
  bool interactionsReady = false;
};

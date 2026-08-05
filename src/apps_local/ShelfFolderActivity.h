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
  void buildItems();

  // Sized for the largest folder we would ever put on one screen without
  // paging. The list virtualizes, so this bounds the registry read, not the
  // scroll. A folder that outgrows it is a sign it wants splitting, not a
  // bigger number.
  static constexpr int kMaxItems = 16;

  const int folder;
  freeink::ui::ListItem items[kMaxItems] = {};
  const freeink::Icon* icons[kMaxItems] = {};
  int itemCount = 0;
  int selected = 0;
  int topIndex = 0;

  toybox::Interactions interactions;
  bool interactionsReady = false;
};

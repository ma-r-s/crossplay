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

  // Show `page` and remember it. Every route that changes page goes through here
  // -- the two side keys, a horizontal swipe, a tap on a page mark -- so no two
  // of them can disagree about where a step lands, about what the screen says
  // afterwards, or about whether the folder comes back here. Returns false when
  // there is nowhere to go, which is a folder of one page.
  bool showPage(int page);

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
  // Where the shelf resumes, and therefore which page is shown. NOT a cursor and
  // never drawn as one: no button moves it, nothing can open it, and the panel
  // shows only which PAGE it puts you on. The page keys carry it from page to
  // page and a tap opens whatever it hits. See docs/buttons.md.
  //
  // It is also what outlives this activity, through shelf::rememberRowIn: the
  // folder comes back to the page this row is on. Both things that leave a
  // folder standing somewhere write it -- opening a game, and turning the page
  // -- so browsing to page three and walking out to read a book comes back to
  // page three, which is the case that used to come back to page one.
  int selected = 0;
  // How many rows a page holds, from the last render. Cached rather than derived
  // in loop() because it is a property of the screen's geometry and not of the
  // selection, so no input can make it disagree with what was drawn. The page
  // number itself is deliberately kept in no member: that one would drift the
  // moment a button moved the cursor, and it is not what is written down either.
  // A tap can only arrive after a render, which is what makes this safe --
  // interactionsReady says so.
  int rowsPerPage = 1;

  toybox::Interactions interactions;
  bool interactionsReady = false;
};

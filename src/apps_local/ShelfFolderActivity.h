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
  // Fills the row arrays with one page of the list, starting at row `first`.
  void buildPage(int first, int count);

  // The registry item at row `row` of whatever list is on screen, or -1 when
  // there is no such row. THE one place a row becomes an item: everything else
  // here counts rows, and the two are the same number only while nothing is
  // hidden, which is the state this screen used to be permanently in.
  int itemAtRow(int row) const;

  // How many rows that list has: what the folder is SHOWING while browsing, and
  // everything it holds while choosing.
  int rowsInList() const;

  // Enter or leave choosing, keeping the same item under the finger. Both
  // directions go through here, because landing somewhere else is the failure
  // this screen is most prone to: every page draws its rows at the same eight
  // positions, so a list that moved under a mode switch looks exactly like one
  // that did not, until something opens.
  void setChoosing(bool on);

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
  // Whether each row of the page is on the list. Only read while choosing; it is
  // what puts the box on the row.
  bool checks[kMaxRowsPerPage] = {};
  // The whole LIST's count, not the page's -- and the list is what this mode
  // shows, which is not the registry once something is hidden.
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

  // Whether the corner chip has been tapped: every item listed, each with a box,
  // and a tap toggles instead of opening. Not persisted -- it is a mode, not a
  // setting, and a device that woke up in it would be a device that had changed
  // what its rows do while nobody was looking.
  bool choosing = false;

  toybox::Interactions interactions;
  bool interactionsReady = false;
};

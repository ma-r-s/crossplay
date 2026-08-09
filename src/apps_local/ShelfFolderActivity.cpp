#include "ShelfFolderActivity.h"

#include <FreeInkUIIcon.h>
#include <Logging.h>
#include <Memory.h>

#include "Shelf.h"
#include "ShelfScreen.h"
#include "player/PlayerName.h"
#include "ui/Toybox.h"
#include "ui/ToyboxFonts.h"
#include "ui/ToyboxTheme.h"

std::unique_ptr<Activity> ShelfFolderActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                      const int folderIndex) {
  return makeUniqueNoThrow<ShelfFolderActivity>(renderer, mappedInput, folderIndex);
}

void ShelfFolderActivity::buildPage(const int first, const int count) {
  const shelf::Folder& self = shelf::folders()[folder];
  // Cannot happen on this panel: the tallest band a folder can have fits ten
  // rows and the array holds sixteen. Logged rather than clamped in silence
  // because a page that quietly drew fewer rows than it was asked for is
  // exactly the failure the registry cap used to have.
  if (count > kMaxRowsPerPage) LOG_ERR("SHELF", "Page of %d exceeds %d rows", count, kMaxRowsPerPage);
  for (int i = 0; i < count && i < kMaxRowsPerPage; ++i) {
    items[i].label = self.items[first + i].title;
    icons[i] = self.items[first + i].icon;
    // The absolute index, so a tap says which game it is rather than which row
    // of which page. The list component emits actionValue rather than the row,
    // which is what lets the screen be handed a slice at all.
    items[i].actionValue = static_cast<int16_t>(first + i);
  }
}

void ShelfFolderActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  // Land on whatever was opened last. This activity is destroyed the moment it
  // launches something, so the selection cannot survive in a member.
  //
  // Landing on it is not the same as *showing* it. A row drawn inverted on
  // arrival reads as "this is what you are about to do", and on a panel driven
  // by touch you are not about to do it -- you are about to tap something else.
  // So the cursor exists from the first frame and is only drawn once a button
  // moves it, which is the only input that needs to see where it is.
  selected = shelf::lastItemIn(folder);
  cursorShown = false;
  itemCount = shelf::folders()[folder].count;
  if (selected >= itemCount) selected = itemCount > 0 ? itemCount - 1 : 0;
  // The page itself is built in render(), which is the only place that knows how
  // many rows fit.
  requestUpdate();
}

void ShelfFolderActivity::loop() {
  namespace fui = freeink::ui;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // A folder's parent is Home. It does not name Home; leave() knows.
    shelf::leave(renderer, mappedInput);
    return;
  }

  fui::InputSnapshot input;
  int tapX = 0;
  int tapY = 0;
  if (mappedInput.wasScreenTapped(tapX, tapY)) {
    input.touchReleased = true;
    input.touchX = static_cast<int16_t>(tapX);
    input.touchY = static_cast<int16_t>(tapY);
  }
  const bool next = mappedInput.wasReleased(MappedInputManager::Button::Down);
  const bool prev = mappedInput.wasReleased(MappedInputManager::Button::Up);

  if (itemCount > 0 && (next || prev)) {
    // Selection is app-owned state; the component only styles what it is told.
    // The first press reveals the cursor where it already is rather than moving
    // it, so an arrow key never skips the row you were looking at.
    if (cursorShown) selected = (selected + (next ? 1 : itemCount - 1)) % itemCount;
    cursorShown = true;
    requestUpdate();
    return;
  }
  if (itemCount > 0 && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    // Confirm with nothing shown would open a row the user cannot see. Show it
    // instead; the second press opens it.
    if (!cursorShown) {
      cursorShown = true;
      requestUpdate();
      return;
    }
    shelf::openItem(folder, selected, renderer, mappedInput);
    return;
  }
  if (!input.touchReleased || !interactionsReady) return;

  const fui::ActionEvent event = interactions.route(input);
  if (event.action == shelfui::ActionOpen) {
    selected = event.value;
    shelf::openItem(folder, selected, renderer, mappedInput);
    return;
  }
  if (event.action == shelfui::ActionGoToPage) {
    // The page follows the selection, so changing page means moving the
    // selection onto that page rather than storing a page anywhere.
    //
    // It lands on the page's first row and the cursor stays hidden. Revealing it
    // here would draw an inverted row on arrival, which reads as "this is what
    // you are about to do" -- and someone who just tapped a pip is about to tap
    // a game, not open whatever happens to be at the top.
    const int landing = event.value * rowsPerPage;
    if (landing >= 0 && landing < itemCount) {
      selected = landing;
      requestUpdate();
    }
    return;
  }
  if (event.action == shelfui::ActionOpenPlayer) {
    // A destination, not an edit. Nothing about the name changes here any more;
    // PLAYER owns it, and this bar is only how you get there.
    shelf::openPlayer(renderer, mappedInput);
  }
}

void ShelfFolderActivity::render(RenderLock&&) {
  namespace fui = freeink::ui;

  const shelf::Folder& self = shelf::folders()[folder];

  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::DeviceContext device = target.deviceContext();
  const fui::ThemeTokens& tokens = toybox::themeTokens();
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, device, noInput, interactions);
  toybox::Screen screen(frame);

  // Keep the selection on screen. The list is virtualized, so a selection below
  // the fold would otherwise be styled on a row that is never drawn.
  //
  // The page is derived from the selection rather than stored beside it. Two
  // facts that must agree are one fact stored once: a page member would drift
  // the moment a button moved the cursor off it, and the screen would style a
  // row it was not showing -- which is the bug the icons had, in a second place.
  const shelfui::Paging paging = shelfui::pagingFor(device, tokens, self.showsDeviceName, itemCount);
  rowsPerPage = paging.rowsPerPage;
  const int page = shelfui::pageFor(selected, rowsPerPage);
  const int first = page * rowsPerPage;
  // Short on the last page, which is the whole reason the screen is handed a
  // slice rather than the folder plus an offset. See MenuModel::items.
  const int onThisPage = itemCount - first < rowsPerPage ? itemCount - first : rowsPerPage;
  buildPage(first, onThisPage);

  // Toybox chrome is capitals; upstream's Home list is Title Case. The registry
  // stores the Title Case name because that is the one a person reads on Home,
  // and the shout happens here, where our own design language starts.
  char shouted[24];
  size_t n = 0;
  for (const char* c = self.title; *c != '\0' && n + 1 < sizeof(shouted); ++c, ++n) {
    shouted[n] = *c >= 'a' && *c <= 'z' ? static_cast<char>(*c - 'a' + 'A') : *c;
  }
  shouted[n] = '\0';

  shelfui::MenuModel model;
  model.title = shouted;
  model.mark = self.mark;
  model.items = items;
  model.icons = icons;
  model.count = onThisPage;
  // Page-relative, because the model is one page. The cursor is always on the
  // page being drawn: the page is derived from it.
  model.selected = cursorShown ? selected - first : -1;
  model.playerName = self.showsDeviceName ? player::name() : nullptr;
  model.page = page;
  model.pageCount = paging.pageCount;
  shelfui::buildMenu(screen, model);

  interactionsReady = true;
  toybox::reportOverflow(interactions, self.title);

  const auto labels = mappedInput.mapLabels("Back", "Open", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

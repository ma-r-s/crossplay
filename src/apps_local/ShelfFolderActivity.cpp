#include "ShelfFolderActivity.h"

#include <FreeInkUIIcon.h>
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

void ShelfFolderActivity::buildItems() {
  const shelf::Folder& self = shelf::folders()[folder];
  itemCount = self.count < kMaxItems ? self.count : kMaxItems;
  for (int i = 0; i < itemCount; ++i) {
    items[i].label = self.items[i].title;
    icons[i] = self.items[i].icon;
    items[i].actionValue = static_cast<int16_t>(i);
  }
  if (selected >= itemCount) selected = itemCount > 0 ? itemCount - 1 : 0;
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
  buildItems();
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
  const fui::ThemeTokens tokens = toybox::themeTokens();
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, device, noInput, interactions);
  toybox::Screen screen(frame, tokens);

  // Keep the selection on screen. The list is virtualized, so a selection below
  // the fold would otherwise be styled on a row that is never drawn.
  topIndex =
      shelfui::topIndexFor(shelfui::listBand(device, self.showsDeviceName), tokens, selected, topIndex, itemCount);

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
  model.count = itemCount;
  model.selected = cursorShown ? selected : -1;
  model.topIndex = topIndex;
  model.playerName = self.showsDeviceName ? player::name() : nullptr;
  shelfui::buildMenu(screen, model);

  interactionsReady = true;
  toybox::reportOverflow(interactions, self.title);

  const auto labels = mappedInput.mapLabels("Back", "Open", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

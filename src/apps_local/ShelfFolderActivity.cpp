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
    if (self.items[i].icon != nullptr) {
      items[i].icon = freeink::ui::bitmapFromIcon(*self.items[i].icon);
    }
    items[i].actionValue = static_cast<int16_t>(i);
  }
  if (selected >= itemCount) selected = itemCount > 0 ? itemCount - 1 : 0;
}

void ShelfFolderActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  // Land on whatever was opened last. This activity is destroyed the moment it
  // launches something, so the selection cannot survive in a member.
  selected = shelf::lastItemIn(folder);
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
    selected = (selected + (next ? 1 : itemCount - 1)) % itemCount;
    requestUpdate();
    return;
  }
  if (itemCount > 0 && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
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
  if (event.action == shelfui::ActionRerollName) {
    // Instant and reversible: the next tap is another name, so there is nothing
    // to confirm and nothing to undo.
    player::reroll();
    requestUpdate();
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
  model.items = items;
  model.count = itemCount;
  model.selected = selected;
  model.topIndex = topIndex;
  model.playerName = self.showsDeviceName ? player::name() : nullptr;
  shelfui::buildMenu(screen, model);

  interactionsReady = true;
  toybox::reportOverflow(interactions, self.title);

  const auto labels = mappedInput.mapLabels("Back", "Open", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

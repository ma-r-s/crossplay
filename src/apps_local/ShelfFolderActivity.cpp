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
  // Land where the folder was left standing: on the game that was opened from
  // it, or on the first row of the page it was showing when it was walked out
  // of. This activity is destroyed the moment it launches something and again
  // the moment you leave, so neither can survive in a member.
  itemCount = shelf::folders()[folder].count;
  // resumeRowFor is the whole answer to "the folder shrank since": it pins a row
  // past the end to the last one, so the folder opens on its last page rather
  // than dropping the memory and opening on its first. Named there rather than
  // clamped here, so it is one rule with one test instead of an `if`.
  selected = shelfui::resumeRowFor(shelf::resumeRowIn(folder), itemCount);
  // Only when there is something to explain. Row zero is the top of page one,
  // which is where an unvisited folder opens anyway, so marking it would be
  // furniture on the one screen that needs none.
  showingResumedRow = selected > 0;
  // The page itself is built in render(), which is the only place that knows how
  // many rows fit.
  requestUpdate();
}

bool ShelfFolderActivity::showPage(const int page) {
  const int landing = shelfui::rowForPage(page, rowsPerPage);
  // Which page a step actually landed on, because "it moved by two" and "it
  // started somewhere else" look identical from outside and cost three cold
  // testers an evening between them.
  LOG_DBG("SHELF", "Page %d -> %d of %d (row %d of %d)", shelfui::pageFor(selected, rowsPerPage), page,
          shelfui::pageCountFor(itemCount, rowsPerPage), landing, itemCount);
  if (landing < 0 || landing >= itemCount) return false;
  // Within this screen the page is DERIVED from the selection and held nowhere
  // else, so changing page means moving the selection onto the page's first row.
  selected = landing;
  // And that row is what the folder comes back to. Written here rather than on
  // the way out because there is no way out to hook: Back destroys this
  // activity, and the idle timeout deep-sleeps wherever you happen to be, with
  // wake a chip reset. Paging is a deliberate act a handful of times a session,
  // and the write is twenty bytes beside a full-panel repaint.
  shelf::rememberRowIn(folder, landing);
  showingResumedRow = false;
  requestUpdate();
  return true;
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
  // The two side keys PAGE. They are the only physical buttons the X4 Pro has,
  // the case labels them previous and next page, and the reader turns pages with
  // them -- so paging the shelf with them is consistency with what the hardware
  // already says, not a new thing to learn. That is also why there is no
  // on-screen hint for it: the affordance is moulded into the case.
  //
  // They used to move a CURSOR, opened with Confirm. On this device that was a
  // dead end in the most literal way: `frontButtonConfirm` resolves to
  // PIN_UNASSIGNED, which InputManager::begin skips entirely, so Confirm can
  // never fire. You could move a selection you had no way to act on. The
  // design language had already removed this exact input model from Chess and
  // Connections for being a second, worse one running beside the real one;
  // here it was second, worse, and broken.
  //
  // The page marks stay tappable. A button must never be the only route to
  // something, or the invisible input model wins arguments it should not.
  const bool keyNext = mappedInput.wasReleased(MappedInputManager::Button::Down);
  const bool keyPrev = mappedInput.wasReleased(MappedInputManager::Button::Up);

  // And a horizontal swipe pages too, which is the first thing every hand
  // reaches for on a touch panel showing a page indicator. Left carries the
  // list leftwards to the next page, matching the reader's own swipe page
  // turns; Back is a left-edge swipe and has already returned above, so a
  // rightward swipe reaching here is a page and not an exit.
  const MappedInputManager::SwipeDir swipe = mappedInput.wasSwipe();
  const bool next = keyNext || swipe == MappedInputManager::SwipeDir::Left;
  const bool prev = keyPrev || swipe == MappedInputManager::SwipeDir::Right;

  if (itemCount > 0 && (next || prev)) {
    // A folder that fits on one page has no page to step to, and moving the
    // resumed row to the top instead would be a step that changed something
    // without going anywhere.
    const int pages = shelfui::pageCountFor(itemCount, rowsPerPage);
    if (pages > 1) showPage(shelfui::pageStep(shelfui::pageFor(selected, rowsPerPage), pages, next ? 1 : -1));
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
    showPage(event.value);
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
  // The page is derived from the selection rather than held beside it in a
  // member. Two facts that must agree are one fact stored once: a page member
  // would drift the moment a button moved the cursor off it, and the screen
  // would style a row it was not showing -- which is the bug the icons had, in a
  // second place. What crosses a reboot is that same one fact, the row; see
  // shelfui::rowForPage for why a row and not a page number.
  const shelfui::Paging paging = shelfui::pagingFor(device, tokens, self.showsDeviceName, itemCount);
  rowsPerPage = paging.rowsPerPage;
  const int page = shelfui::pageFor(selected, rowsPerPage);
  const int first = shelfui::rowForPage(page, rowsPerPage);
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
  // Page-relative, because the model is one page. The row is always on the page
  // being drawn: the page is derived from it.
  //
  // Drawn only while it is the row the shelf resumed on, which is what tells
  // you why the list did not open at the top. Once any page change has
  // happened, `selected` is just the page carrier and -1 draws no cursor.
  model.selected = showingResumedRow ? selected - first : -1;
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

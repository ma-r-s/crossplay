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

int ShelfFolderActivity::rowsInList() const {
  return choosing ? shelf::folders()[folder].count : shelf::shownCount(folder);
}

int ShelfFolderActivity::itemAtRow(const int row) const {
  if (row < 0) return -1;
  // While choosing, the list IS the registry: every item is on screen, in
  // registry order, so the row is the item. That identity is the reason the
  // chooser lists everything -- a chooser over the shown items could not show
  // you what you had hidden.
  if (choosing) return row < shelf::folders()[folder].count ? row : -1;
  return shelf::shownItem(folder, row);
}

void ShelfFolderActivity::buildPage(const int first, const int count) {
  const shelf::Folder& self = shelf::folders()[folder];
  // Cannot happen on this panel: the tallest band a folder can have fits ten
  // rows and the array holds sixteen. Logged rather than clamped in silence
  // because a page that quietly drew fewer rows than it was asked for is
  // exactly the failure the registry cap used to have.
  if (count > kMaxRowsPerPage) LOG_ERR("SHELF", "Page of %d exceeds %d rows", count, kMaxRowsPerPage);
  for (int i = 0; i < count && i < kMaxRowsPerPage; ++i) {
    const int item = itemAtRow(first + i);
    if (item < 0) {
      // A row the list does not have. Cannot happen -- render() slices against
      // the same count -- and is logged rather than drawn blank, because a row
      // with no label is the one thing a person cannot report.
      LOG_ERR("SHELF", "Row %d has no item in %s", first + i, self.title);
      items[i].label = "";
      icons[i] = nullptr;
      checks[i] = false;
      // And it must not still answer for whatever the last page put here: the
      // row is registered whatever its label says, so a stale value is a blank
      // row that opens a game.
      items[i].actionValue = -1;
      continue;
    }
    items[i].label = self.items[item].title;
    icons[i] = self.items[item].icon;
    checks[i] = !shelf::isHidden(folder, item);
    // The row's index in the WHOLE list, so a tap says which entry it is rather
    // than which row of which page. The list component emits actionValue rather
    // than the row, which is what lets the screen be handed a slice at all.
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
  itemCount = rowsInList();
  // resumeRowFor is the whole answer to "the folder shrank since": it pins a row
  // past the end to the last one, so the folder opens on its last page rather
  // than dropping the memory and opening on its first. Named there rather than
  // clamped here, so it is one rule with one test instead of an `if`.
  selected = shelfui::resumeRowFor(shelf::resumeRowIn(folder), itemCount);
  // The page itself is built in render(), which is the only place that knows how
  // many rows fit.
  requestUpdate();
}

void ShelfFolderActivity::setChoosing(const bool on) {
  if (choosing == on) return;
  // The table on the panel belongs to the mode that drew it, and every action
  // id on this screen changes meaning here: the row that opened CHESS a moment
  // ago now hides it. render() runs on the other core and only after this
  // loop() returns, so between the chip's tap and the repaint there is a window
  // where the OLD table is live and `choosing` is already the new value -- and
  // an unchanged table always routes, by design, which is what keeps touch
  // alive everywhere else. Disarmed here rather than left to the rebuild,
  // because the rebuild is the thing that has not happened yet.
  interactionsReady = false;
  // The item under the finger before the switch, kept through it. The two lists
  // are different lengths, so a row carried across straight would be a
  // different game -- and the page bar would say the same number either way.
  const int item = itemAtRow(selected);
  choosing = on;
  itemCount = rowsInList();
  if (choosing) {
    // The chooser's rows ARE the registry, so the item is the row.
    selected = item < 0 ? 0 : item;
    requestUpdate();
    return;
  }
  // Coming back out, the item may be the one that was just hidden, and then
  // there is no row for it: shownRowFor answers with the nearest surviving one
  // and resumeRowFor pins that to the end of a list that shrank underneath.
  selected = shelfui::resumeRowFor(shelf::shownRowFor(folder, item < 0 ? 0 : item), itemCount);
  // And that is where the folder comes back, the same write a page turn makes.
  // Skipped when the list is empty: there is no row to remember, and the empty
  // state is not a place.
  if (itemCount > 0) shelf::rememberRowIn(folder, selected);
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
  requestUpdate();
  // The chooser pages over a different list, so its rows are not the folder's
  // and writing one would move where the FOLDER resumes. Leaving the mode
  // writes the row it lands on instead, which is the same fact recorded once.
  if (choosing) return true;
  // And that row is what the folder comes back to. Written here rather than on
  // the way out because there is no way out to hook: Back destroys this
  // activity, and the idle timeout deep-sleeps wherever you happen to be, with
  // wake a chip reset. Paging is a deliberate act a handful of times a session,
  // and the write is twenty bytes beside a full-panel repaint.
  shelf::rememberRowIn(folder, landing);
  return true;
}

void ShelfFolderActivity::loop() {
  namespace fui = freeink::ui;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // Back has two rules in this fork -- an app returns to its folder, a folder
    // returns to Home -- and choosing adds the only exception: it closes the
    // mode and stays put. A mode that Back walked straight out of would make
    // Back mean "undo the thing I was doing" on the one screen where it is also
    // the only way out of the app, and the taps are already saved by then.
    if (choosing) {
      setChoosing(false);
      return;
    }
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
  //
  // Left and Right page as well. They are PIN_UNASSIGNED on both target boards
  // and can never fire there, but the simulator and the browser emulator wire
  // all six keys to the arrow keys, and on Home every arrow moves the cursor.
  // One level in, two of the four did nothing, which is the only place a person
  // meets these keys at all.
  const bool keyNext = mappedInput.wasReleased(MappedInputManager::Button::Down) ||
                       mappedInput.wasReleased(MappedInputManager::Button::Right);
  const bool keyPrev = mappedInput.wasReleased(MappedInputManager::Button::Up) ||
                       mappedInput.wasReleased(MappedInputManager::Button::Left);

  // And a VERTICAL swipe pages, both ways: up carries the list upwards to the
  // next page, the way the content moves under a finger, which is the same way
  // round as Hacker News's story list and the reader. Learn it once.
  //
  // Vertical because it is the only axis Back does not own. Back is a
  // left-to-right swipe anchored in the left 25% of the panel (120px of 480),
  // so on the horizontal axis the backward page turn and the exit are the SAME
  // visible gesture separated by an invisible boundary -- and this list paged
  // sideways for exactly that reason until a cold tester swiped back from page
  // two and landed on Home. A gesture whose meaning flips on a line nothing
  // draws cannot be made discoverable; it can only be moved off that axis. So
  // the horizontal axis carries Back and nothing else here, and the whole
  // paired step lives where it is symmetrical.
  //
  // One band is consumed above this activity and cannot be had: a down-swipe
  // STARTING in the top 14% (y <= 112) is the light-panel gesture, taken by
  // ActivityManager before any activity sees it. That band is the header, and
  // the list starts at 112, so a swipe that starts on a row is never eaten.
  //
  // The bottom edge is free on the X4 Pro and is not free on the Sticky. The
  // Home gesture is a bottom-edge UP swipe only where the board has no home
  // key (`MappedInputManager::wasHomeGesture`); the X4 Pro's profile carries
  // `hasHomeKey = true` for the capacitive key under the panel, so its Home
  // gesture is that key and an up-swipe from the bottom pages here. The Sticky
  // has no such key, so on that board an up-swipe starting below y=688 goes
  // Home instead. What sits below 688 on this screen is the page bar and the
  // player bar, so the cost is a swipe begun on chrome.
  const MappedInputManager::SwipeDir swipe = mappedInput.wasSwipe();
  const bool next = keyNext || swipe == MappedInputManager::SwipeDir::Up;
  const bool prev = keyPrev || swipe == MappedInputManager::SwipeDir::Down;

  if (itemCount > 0 && (next || prev)) {
    // A folder that fits on one page has no page to step to, and moving the
    // resumed row to the top instead would be a step that changed something
    // without going anywhere.
    const int pages = shelfui::pageCountFor(itemCount, rowsPerPage);
    if (pages > 1) {
      const int from = shelfui::pageFor(selected, rowsPerPage);
      const int to = shelfui::pageStepClamped(from, pages, next ? 1 : -1);
      // Nothing happens at the ends, and nothing repaints either: a full-panel
      // refresh that redraws the same eight rows is half a second of blink
      // saying a step was taken when none was. What says so instead is the page
      // bar, which reads 3 of 1 2 3.
      if (to != from) {
        showPage(to);
      } else {
        LOG_DBG("SHELF", "Page %d of %d is an end; step refused", from, pages);
      }
    }
    return;
  }

  if (!input.touchReleased || !interactionsReady) return;

  const fui::ActionEvent event = interactions.route(input);
  if (event.action == shelfui::ActionOpen) {
    selected = event.value;
    const int item = itemAtRow(selected);
    if (item >= 0) shelf::openItem(folder, item, renderer, mappedInput);
    return;
  }
  if (event.action == shelfui::ActionChoose) {
    setChoosing(!choosing);
    return;
  }
  if (event.action == shelfui::ActionToggleShown) {
    const int item = itemAtRow(event.value);
    if (item >= 0) {
      shelf::setHidden(folder, item, !shelf::isHidden(folder, item));
      // The row the finger is on stays the row the finger is on: the chooser's
      // list is the registry, so hiding something never moves it. The list that
      // shrinks is the other one, and it is not on screen.
      selected = event.value;
      requestUpdate();
    }
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
  //
  // The folder's own bar, in both modes: the screen puts the player bar in it
  // while browsing and the chooser's caption in it while choosing, so the two
  // modes fit the same games on the same page and entering the chooser adds
  // boxes to the list rather than reshuffling it.
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
  // Non-null is the mode, for the screen as well as for this file.
  model.checks = choosing ? checks : nullptr;
  model.playerName = self.showsDeviceName ? player::name() : nullptr;
  model.page = page;
  model.pageCount = paging.pageCount;
  shelfui::buildMenu(screen, model);

  interactionsReady = true;
  toybox::reportOverflow(interactions, self.title);

  // Only a board with no touch panel ever draws these (BaseTheme::drawButtonHints
  // returns immediately when gpio.hasTouch()), so they are the Sticky's copy of
  // what the chip in the corner says.
  const auto labels = choosing ? mappedInput.mapLabels("Done", "Show/Hide", "Up", "Down")
                               : mappedInput.mapLabels("Back", "Open", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

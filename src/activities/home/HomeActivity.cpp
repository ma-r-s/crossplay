#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Utf8.h>
#include <Xtc.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "../../apps_local/Shelf.h"  // fork-local seam
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

// --- fork-local seam ---------------------------------------------------
// How many rows indexToMenuItem() walks. NOT getMenuItemCount(), which also
// counts the recent-book tiles above the menu -- the dispatch has already
// subtracted those to get its menuIndex, so using it here subtracts them twice.
// With one book on the card that put Games out of range and made Apps open it.
int HomeActivity::upstreamMenuRows() const {
  // Browse Files, Recents, File transfer, Settings, plus OPDS when configured,
  // plus the Continue Reading row the RoundedRaff theme inserts at the top.
  //
  // (indexToMenuItem() does not know about that Continue Reading row, so
  // upstream's own dispatch is off by one under that theme. Not ours to fix,
  // but it is why this counts the row and that function does not.)
  const auto& metrics = UITheme::getInstance().getMetrics();
  const bool continueRow = metrics.homeContinueReadingInMenu && !recentBooks.empty();
  return 4 + (continueRow ? 1 : 0);
}

int HomeActivity::getMenuItemCount() const {
  // --- fork-local seam ---------------------------------------------------
  // The shelf's folders (GAMES, APPS) are appended after upstream's rows, so
  // upstream's indices never shift and indexToMenuItem()/menuItemToIndex() stay
  // untouched. Everything below returns NONE for our indices, which is what the
  // dispatch switch's default case picks up. See src/apps_local/Shelf.h.
  int count = 4 + shelf::folderCount();  // File Browser, Recents, File transfer, Settings, + ours
  if (!recentBooks.empty()) {
    count += recentBooks.size();
  }
  return count;
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // Skip if file no longer exists
    if (RecentBooksStore::isMissing(book)) {
      continue;
    }

    recentBooks.push_back(book);
  }
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    if (!book.coverBmpPath.empty()) {
      std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
      if (!Storage.exists(coverPath.c_str())) {
        // If epub, try to load the metadata for title/author and cover
        if (FsHelpers::hasEpubExtension(book.path)) {
          Epub epub(book.path, "/.crosspoint");
          // Skip loading css since we only need metadata here
          epub.load(false, true);

          // Try to generate thumbnail image for Continue Reading card
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
          bool success = epub.generateThumbBmp(coverHeight);
          if (!success) {
            RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
            book.coverBmpPath = "";
          }
          coverRendered = false;
          requestUpdate();
        } else if (FsHelpers::hasXtcExtension(book.path)) {
          // Handle XTC file
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            // Try to generate thumbnail image for Continue Reading card
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
            bool success = xtc.generateThumbBmp(coverHeight);
            if (!success) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            }
            coverRendered = false;
            requestUpdate();
          }
        }
      }
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  const auto& metrics = UITheme::getInstance().getMetrics();
  loadRecentBooks(metrics.homeRecentBooksCount);

  const auto base = static_cast<int>(recentBooks.size());
  selectorIndex =
      initialMenuItem == HomeMenuItem::NONE ? 0 : base + menuItemToIndex(initialMenuItem, /*hasOpdsUrl=*/false);

  // fork-local seam: goHome() restores the selection by matching the departing
  // activity's name against HomeMenuItem, which cannot know about shelf rows,
  // so leaving GAMES would otherwise drop the cursor on Browse Files.
  if (const int shelfRow = shelf::lastFolderOnHome(); shelfRow >= 0) {
    selectorIndex = base + upstreamMenuRows() + shelfRow;
  }

  // fork-local seam: boot straight into a named app when the environment asks
  // for one (the site's installer preview, CROSSPLAY_AUTOSTART=chess ./bin/sim).
  // Fires once per process; on hardware getenv finds nothing and this is free.
  // Safe from onEnter because replaceActivity defers to the end of the loop.
  shelf::autostartFromEnv(renderer, mappedInput);

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();

  // Free the stored cover buffer if any
  freeCoverBuffer();
}

bool HomeActivity::storeCoverBuffer() {
  // render() must have already set the cover rect; without it we'd be back to
  // cloning the whole framebuffer.
  if (coverRectW <= 0 || coverRectH <= 0) return false;
  freeCoverBuffer();
  const size_t needed = renderer.getRegionByteSize(coverRectX, coverRectY, coverRectW, coverRectH);
  if (needed == 0) return false;
  coverBuffer = static_cast<uint8_t*>(malloc(needed));
  if (!coverBuffer) {
    LOG_ERR("HOME", "OOM: cover buffer (%u bytes)", (unsigned)needed);
    return false;
  }
  coverBufferSize = needed;
  if (!renderer.copyRegionToBuffer(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize)) {
    free(coverBuffer);
    coverBuffer = nullptr;
    coverBufferSize = 0;
    return false;
  }
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer || coverRectW <= 0 || coverRectH <= 0) return false;
  return renderer.copyBufferToRegion(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize);
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferSize = 0;
  coverBufferStored = false;
}

void HomeActivity::loop() {
  const int menuCount = getMenuItemCount();
  const auto& metrics = UITheme::getInstance().getMetrics();

  auto activateSelection = [this] {
    if (selectorIndex < recentBooks.size()) {
      onSelectBook(recentBooks[selectorIndex].path);
      return;
    }
    const int menuIndex = selectorIndex - static_cast<int>(recentBooks.size());
    // Get Books moved into the APPS folder, so Home never draws its row.
    // Upstream's helpers still take the flag; they stay byte-identical and
    // merge cleanly, and false simply removes the row from their arithmetic.
    switch (indexToMenuItem(menuIndex, /*hasOpdsUrl=*/false)) {
      case HomeMenuItem::FILE_BROWSER:
        onFileBrowserOpen();
        break;
      case HomeMenuItem::RECENTS:
        onRecentsOpen();
        break;
      case HomeMenuItem::OPDS_BROWSER:
        onOpdsBrowserOpen();
        break;
      case HomeMenuItem::FILE_TRANSFER:
        onFileTransferOpen();
        break;
      case HomeMenuItem::SETTINGS_MENU:
        onSettingsOpen();
        break;
      default: {
        // fork-local seam: anything past upstream's rows is a shelf folder.
        const int shelfRow = menuIndex - upstreamMenuRows();
        if (shelfRow >= 0 && shelfRow < shelf::folderCount()) {
          shelf::openFolder(shelfRow, renderer, mappedInput);
        }
        break;
      }
    }
  };

  buttonNavigator.onNext([this, menuCount] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, menuCount] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
    return;
  }

  // Back is otherwise unused on the home menu: open the most recently read
  // book directly (recentBooks is most-recent-first and already pruned of
  // files missing from the SD card).
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && !recentBooks.empty()) {
    onSelectBook(recentBooks[0].path);
    return;
  }

  // Hit areas follow the DRAWN geometry, not the metrics table: render() may
  // shrink the cover tile to fit the menu, which moves everything below it up
  // by the shrink. Before the first render (menuTopRendered == 0) fall back to
  // the static formula, which is what render() uses when nothing shrank.
  const int coverBottomDrawn =
      menuTopRendered > 0 ? coverRectY + coverRectH : metrics.homeTopPadding + metrics.homeCoverTileHeight;
  const int coverColumnCount = std::max(1, metrics.homeRecentBooksCount);
  const int recentCount = std::min(static_cast<int>(recentBooks.size()), coverColumnCount);
  const int coverColumnWidth = (renderer.getScreenWidth() - 2 * metrics.contentSidePadding) / coverColumnCount;
  int touchedBook = -1;
  const auto coverTouch = mappedInput.colTouch(touchedBook, metrics.contentSidePadding, coverColumnWidth, recentCount,
                                               metrics.homeTopPadding, coverBottomDrawn, coverColumnWidth);
  if (coverTouch != MappedInputManager::RowTouch::None) {
    if (coverTouch == MappedInputManager::RowTouch::Down) {
      if (selectorIndex != touchedBook) {
        selectorIndex = touchedBook;
        requestUpdate();
      }
    } else {
      selectorIndex = touchedBook;
      activateSelection();
    }
    return;
  }

  const int menuTop = menuTopRendered > 0
                          ? menuTopRendered
                          : metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset;
  const int renderedMenuSelection =
      metrics.homeContinueReadingInMenu ? selectorIndex : selectorIndex - recentBooks.size();
  const int renderedMenuCount =
      menuCount - (metrics.homeContinueReadingInMenu ? 0 : static_cast<int>(recentBooks.size()));
  int menuRow = -1;
  // Row height from the theme, not the metrics table: RoundedRaff draws
  // font-derived rows and the touch grid must match the visuals exactly.
  const int menuRowHeight = GUI.getMenuRowHeight(renderer);
  const int rowSpacing = menuSpacingRendered > 0 ? menuSpacingRendered : metrics.menuSpacing;
  const auto menuTouch = mappedInput.rowTouch(menuRow, menuTop, menuRowHeight + rowSpacing, renderedMenuCount, 0,
                                              INT32_MAX, menuRowHeight);
  if (menuTouch != MappedInputManager::RowTouch::None) {
    const int touchedIndex =
        metrics.homeContinueReadingInMenu ? menuRow : menuRow + static_cast<int>(recentBooks.size());
    if (menuTouch == MappedInputManager::RowTouch::Down) {
      if (selectorIndex != touchedIndex) {
        selectorIndex = touchedIndex;
        requestUpdate();
      }
    } else {
      selectorIndex = touchedIndex;
      activateSelection();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelection();
  }
}

void HomeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  bool bufferRestored = coverBufferStored && restoreCoverBuffer();

  // Band spans topPadding..homeTopPadding: the cover tile starts at the fixed
  // homeTopPadding, so the height must shrink by topPadding or the band (and a
  // centered title, e.g. RoundedRaff's book title) sinks into the tile.
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding - metrics.topPadding},
                 metrics.homeContinueReadingInMenu && !recentBooks.empty() ? recentBooks[0].title.c_str() : nullptr);

  // Build menu items dynamically
  std::vector<const char*> menuItems = {tr(STR_BROWSE_FILES), tr(STR_MENU_RECENT_BOOKS), tr(STR_FILE_TRANSFER),
                                        tr(STR_SETTINGS_TITLE)};
  std::vector<UIIcon> menuIcons = {Folder, Recent, Transfer, Settings};

  if (metrics.homeContinueReadingInMenu && !recentBooks.empty()) {
    // Insert Continue Reading at the top if enabled in theme
    menuItems.insert(menuItems.begin(), tr(STR_CONTINUE_READING));
    menuIcons.insert(menuIcons.begin(), Book);
  }

  // fork-local seam: the shelf's folders, appended last so upstream's indices
  // hold. Raw titles rather than tr(): routing them through i18n would mean
  // editing lib/I18n/translations/*.yaml per folder.
  for (int i = 0; i < shelf::folderCount(); ++i) {
    menuItems.push_back(shelf::folders()[i].title);
    menuIcons.push_back(shelf::folders()[i].icon);
  }

  // --- fork-local seam ---------------------------------------------------
  // The shelf's folders (GAMES, APPS) are appended to upstream's rows, and
  // RoundedRaff adds a Continue Reading row of its own once a book has been
  // opened. drawButtonMenu lays rows at a fixed pitch and ignores the rect
  // height, so a row that does not fit is drawn off-screen and simply is not
  // there -- and the home menu does not scroll, so it cannot be reached at all.
  // APPS is the last row, which is how Get Books (inside it) would vanish.
  //
  // Only the row GAPS give. The cover tile keeps its full height: its art is
  // the point of it, and the gaps are generous enough to lose a few pixels
  // each and read the same.
  // getMenuRowHeight() rather than metrics.menuRowHeight: RoundedRaff marks its
  // metric as non-authoritative and derives the drawn height from the renderer.
  const int menuRowHeight = GUI.getMenuRowHeight(renderer);
  const int rows = static_cast<int>(menuItems.size());
  const int coverTileHeight = metrics.homeCoverTileHeight;
  const int spaceForMenu = pageHeight - metrics.homeTopPadding - metrics.homeMenuTopOffset - metrics.buttonHintsHeight -
                           coverTileHeight - metrics.verticalSpacing;
  int menuSpacing = metrics.menuSpacing;
  if (rows > 0 && rows * (menuRowHeight + menuSpacing) > spaceForMenu) {
    // Row height is fixed by the theme, so only the gaps can give. Floored at
    // 1px: rows flush against each other read as one block, not a list.
    menuSpacing = std::max(1, spaceForMenu / rows - menuRowHeight);
  }
  // Drawn spacing and hit-test spacing must be the same number, or taps drift
  // further off with every row down the list.
  menuSpacingRendered = menuSpacing;
  const int gapBelowTile = metrics.verticalSpacing;
  const int tileBlock = coverTileHeight > 0 ? coverTileHeight + gapBelowTile : 0;

  // Recorded so storeCoverBuffer (called from the theme) knows which
  // sub-region of the framebuffer to snapshot, rather than all 48 KB.
  coverRectX = 0;
  coverRectY = metrics.homeTopPadding;
  coverRectW = pageWidth;
  coverRectH = coverTileHeight;
  // The menu top the touch grid in loop() must use; drawButtonMenu below draws
  // at exactly this y.
  menuTopRendered = metrics.homeTopPadding + tileBlock + metrics.homeMenuTopOffset;

  if (coverTileHeight > 0) {
    GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, coverTileHeight}, recentBooks,
                            selectorIndex, coverRendered, coverBufferStored, bufferRestored,
                            std::bind(&HomeActivity::storeCoverBuffer, this));
  }

  GUI.drawButtonMenu(
      renderer,
      Rect{0, menuTopRendered, pageWidth,
           pageHeight -
               (metrics.homeTopPadding + coverTileHeight + metrics.homeMenuTopOffset + metrics.buttonHintsHeight)},
      static_cast<int>(menuItems.size()),
      metrics.homeContinueReadingInMenu ? selectorIndex : selectorIndex - recentBooks.size(),
      [&menuItems](int index) { return std::string(menuItems[index]); },
      [&menuIcons](int index) { return menuIcons[index]; }, menuSpacing);

  const auto labels = mappedInput.mapLabels(recentBooks.empty() ? "" : tr(STR_RESUME), tr(STR_SELECT), tr(STR_DIR_UP),
                                            tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(cleanInitialRefresh && !firstRenderDone ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    loadRecentCovers(metrics.homeCoverHeight);
  }
}

void HomeActivity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onRecentsOpen() { activityManager.goToRecentBooks(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }

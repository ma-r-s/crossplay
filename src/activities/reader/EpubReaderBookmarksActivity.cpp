#include "EpubReaderBookmarksActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "../../util/BookmarkFile.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr int ENTER_DELETE_MODE_MS = 700;
constexpr fui::ActionId ACTION_ROW = 1;
}  // namespace

EpubReaderBookmarksActivity::EpubReaderBookmarksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                         const std::shared_ptr<Epub>& epub, const std::string& epubPath)
    : Activity("EpubReaderBookmarks", renderer, mappedInput),
      epub(epub),
      epubPath(epubPath),
      uiTarget(makeUiTarget(renderer)),
      app(uiTarget, uiTarget.deviceContext()) {}

void EpubReaderBookmarksActivity::onEnter() {
  Activity::onEnter();

  if (!epub) {
    return;
  }

  if (!BookmarkFile::load(epubPath, bookmarks)) {
    bookmarks.shrink_to_fit();
  }
  LOG_DBG("EPB", "Loaded %d bookmarks for book: %s", static_cast<int>(bookmarks.size()), epubPath.c_str());

  uiReady = false;
  visibleRows = 1;
  topIndex = 0;
  app.setTheme(uiThemeTokens(uiTarget));
  app.on(ACTION_ROW, &EpubReaderBookmarksActivity::onRowEvent, this);
  app.setScreen(&EpubReaderBookmarksActivity::listScreen, this);

  // Trigger first update
  requestUpdate();
}

void EpubReaderBookmarksActivity::onExit() { Activity::onExit(); }

void EpubReaderBookmarksActivity::openSelectedBookmark() {
  if (bookmarks.empty()) {
    return;
  }
  const auto& bookmark = bookmarks.at(selectorIndex);
  ProgressChangeResult result{};
  result.xpath = bookmark.xpath;
  result.percentage = bookmark.percentage;
  result.hasSavedProgress = true;
  result.hasVisibleTextOffset = bookmark.hasVisibleTextOffset;
  result.visibleTextOffset = bookmark.visibleTextOffset;
  // The offset is spine-relative, so carry its spine even when the legacy page hints below
  // are stale. The reader validates the index.
  result.spineIndex = bookmark.computedSpineIndex;
  if (bookmark.computedChapterPageCount > 0 && bookmark.computedChapterProgress < bookmark.computedChapterPageCount &&
      bookmark.computedSpineIndex < epub->getSpineItemsCount()) {
    result.page = bookmark.computedChapterProgress;
    result.totalPages = bookmark.computedChapterPageCount;
  }
  setResult(std::move(result));
  finish();
}

void EpubReaderBookmarksActivity::onRowEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<EpubReaderBookmarksActivity*>(user);
  if (self->confirmPopup.isActive()) return;
  if (event.value < 0 || event.value >= static_cast<int16_t>(self->bookmarks.size())) return;
  self->selectorIndex = event.value;
  // The tapped row leaves this screen or is deleted; a lingering flash would
  // gray an unrelated row on the next render.
  self->app.clearTapFlash();
  if (event.longPress) {
    // Touch long-press asks the same Cancel/Delete confirmation as the physical
    // hold (the popup is tap-operable), matching the file browser's long-press
    // delete flow. Does not open the bookmark.
    self->showDeleteConfirmation();
    return;
  }
  self->openSelectedBookmark();
}

void EpubReaderBookmarksActivity::loop() {
  // Delete confirmation popup
  if (confirmPopup.handleInput(mappedInput, [this] { requestUpdate(); })) {
    // The popup acts on button press; if that input closed it, the trailing
    // release must be swallowed below (Back would leave the activity, Confirm
    // would open the selected bookmark).
    popupClosing = !confirmPopup.isActive();
    return;
  }
  if (popupClosing) {
    if (mappedInput.isPressed(MappedInputManager::Button::Back) ||
        mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      return;  // closing press still held
    }
    popupClosing = false;
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      return;  // swallow the release that closed the popup
    }
  }
  if (confirmingDelete) {
    // Popup dismissed without a selection (Back button or tap outside): cancel delete
    confirmingDelete = false;
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  // Touch goes through the FreeInkApp: render() registered the row hit rects;
  // route the snapshot and let onRowEvent dispatch.
  if (uiReady) {
    const fui::InputSnapshot snap = longPressTouch.snapshot(mappedInput);
    if (snap.touchPressed || snap.touchReleased) {
      const auto event = app.route(snap);
      // No pressed-state repaint: the render it triggers would drop a slow
      // tap's release inside the uiReady window (tap-to-activate needed two
      // taps), and it costs a second e-ink refresh per tap.
      if (app.invalidated()) requestUpdate();
      if (event) return;  // dispatched to onRowEvent
    }
  }

  const int listSize = static_cast<int>(bookmarks.size());
  // Swipes scroll the viewport; the selection stays put and button navigation
  // pulls the view back to it.
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    const int delta = swipe == MappedInputManager::SwipeDir::Up ? visibleRows : -visibleRows;
    const int next = scrollListBy(topIndex, delta, visibleRows, listSize);
    if (next != topIndex) {
      topIndex = next;
      requestUpdate();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {  // Open
    openSelectedBookmark();
    return;
  }

  if (mappedInput.isPressed(MappedInputManager::Button::Confirm) && mappedInput.getHeldTime() > ENTER_DELETE_MODE_MS) {
    showDeleteConfirmation();
  }

  const auto moveSelection = [this, listSize](const int index) {
    selectorIndex = index;
    topIndex = followListSelection(selectorIndex, topIndex, visibleRows, listSize);
    requestUpdate();
  };

  buttonNavigator.onNextRelease(
      [this, listSize, &moveSelection] { moveSelection(ButtonNavigator::nextIndex(selectorIndex, listSize)); });

  buttonNavigator.onPreviousRelease(
      [this, listSize, &moveSelection] { moveSelection(ButtonNavigator::previousIndex(selectorIndex, listSize)); });

  buttonNavigator.onNextContinuous([this, listSize, &moveSelection] {
    moveSelection(ButtonNavigator::nextPageIndex(selectorIndex, listSize, visibleRows));
  });

  buttonNavigator.onPreviousContinuous([this, listSize, &moveSelection] {
    moveSelection(ButtonNavigator::previousPageIndex(selectorIndex, listSize, visibleRows));
  });
}

void EpubReaderBookmarksActivity::showDeleteConfirmation() {
  if (bookmarks.empty() || confirmPopup.isActive()) {
    return;
  }
  confirmingDelete = true;
  const char* options[] = {tr(STR_CANCEL), tr(STR_DELETE)};
  confirmPopup.show(tr(STR_CONFIRM_DELETE_BOOKMARK), options, 2, 0, [this](int idx) {
    confirmingDelete = false;
    if (idx == 1) {
      deleteSelectedBookmark();
    }
    requestUpdate();
  });
  requestUpdate();
}

void EpubReaderBookmarksActivity::deleteSelectedBookmark() {
  bookmarks.erase(bookmarks.begin() + selectorIndex);
  if (!BookmarkFile::save(epubPath, bookmarks)) {
    LOG_ERR("EPB", "Failed to save bookmarks after delete");
  }

  // Move selector up if we deleted the last item
  if (selectorIndex >= static_cast<int>(bookmarks.size()) && selectorIndex > 0) {
    selectorIndex--;
  }

  if (bookmarks.empty()) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  topIndex = followListSelection(selectorIndex, topIndex, visibleRows, static_cast<int>(bookmarks.size()));
  requestUpdate(true);
}

void EpubReaderBookmarksActivity::listScreen(UiApp::ScreenType& screen, void* user) {
  static_cast<EpubReaderBookmarksActivity*>(user)->buildListScreen(screen);
}

void EpubReaderBookmarksActivity::buildListScreen(UiApp::ScreenType& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the title band render() paints.
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                                      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                                      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)),
                                      static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (bookmarks.empty()) {
    screen.centeredText(tr(STR_NO_BOOKMARKS), screen.theme().bodyText);
    return;
  }

  // "Hold Open to Delete" names a physical button; on touch boards the row
  // long-press covers deletion, so the hint would be wrong there.
  if (!mappedInput.hasTouch()) {
    const int helpLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
    const fui::Rect band = screen.takeBottom(static_cast<int16_t>(helpLineHeight + metrics.verticalSpacing));
    GUI.drawHelpText(renderer, Rect{band.x, band.y + metrics.verticalSpacing, band.width, helpLineHeight},
                     tr(STR_HOLD_OPEN_TO_DELETE));
  }

  // Transient per-render: labels point into the bookmarks entries; subtitles
  // are composed strings that must outlive screen.list(), so they are owned by
  // a reserved vector (reserve keeps the c_str pointers stable).
  std::vector<std::string> subtitles;
  subtitles.reserve(bookmarks.size());
  std::vector<fui::ListItem> items;
  items.reserve(bookmarks.size());
  for (const auto& bookmark : bookmarks) {
    const auto tocIndex = epub->getTocIndexForSpineIndex(bookmark.computedSpineIndex);
    const auto tocTitle = (tocIndex >= 0) ? epub->getTocItem(tocIndex).title : tr(STR_UNNAMED);
    std::string subtitle = std::to_string((int)(std::clamp(bookmark.percentage, 0.0f, 1.0f) * 100.0f + 0.5f)) + "% - ";
    if (bookmark.computedChapterPageCount > 0) {
      subtitle += std::to_string(bookmark.computedChapterProgress + 1) + "/" +
                  std::to_string(bookmark.computedChapterPageCount) + " - ";
    }
    subtitle += tocTitle;
    subtitles.push_back(std::move(subtitle));

    fui::ListItem item;
    item.label = bookmark.summary.c_str();
    item.subtitle = subtitles.back().c_str();
    item.icon = listIconFor(UIIcon::Bookmark, 32);  // subtitle rows carry the larger icon
    item.actionValue = static_cast<int16_t>(items.size());
    items.push_back(item);
  }

  fui::ListProps props;
  props.items = items.data();
  props.count = static_cast<uint16_t>(items.size());
  props.selectedIndex = static_cast<int16_t>(selectorIndex);
  props.action = ACTION_ROW;
  // Tap opens; long-press deletes (physical buttons stay in loop()).
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  const auto rows = fui::listVisibleRows(screen.body(), screen.theme().rowHeight, screen.theme().listRowGap);
  visibleRows = rows > 0 ? rows : 1;
  topIndex = scrollListBy(topIndex, 0, visibleRows, static_cast<int>(bookmarks.size()));  // clamp to range
  props.topIndex = static_cast<uint16_t>(topIndex);
  screen.list(props);
}

void EpubReaderBookmarksActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto orientation = renderer.getOrientation();
  // Landscape orientation: reserve a horizontal gutter for button hints.
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  // Inverted portrait: reserve vertical space for hints at the top.
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? 40 : 0;
  // Landscape CW places hints on the left edge; CCW keeps them on the right.
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int contentWidth = pageWidth - hintGutterWidth;
  const int contentY = isPortraitInverted ? 50 : 0;

  // Manual centering to honor content gutters.
  const int titleX =
      contentX + (contentWidth - renderer.getTextWidth(UI_12_FONT_ID, tr(STR_BOOKMARKS), EpdFontFamily::BOLD)) / 2;
  renderer.drawText(UI_12_FONT_ID, titleX, 15 + contentY, tr(STR_BOOKMARKS), true, EpdFontFamily::BOLD);

  uiReady = false;
  app.render();
  uiReady = true;

  if (confirmPopup.processRender(renderer, mappedInput)) return;

  const auto confirmLabel = bookmarks.size() > 0 ? tr(STR_SELECT) : "";
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

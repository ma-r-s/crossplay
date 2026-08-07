#include "XtcReaderChapterSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <vector>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_ROW = 1;
}  // namespace

XtcReaderChapterSelectionActivity::XtcReaderChapterSelectionActivity(GfxRenderer& renderer,
                                                                     MappedInputManager& mappedInput,
                                                                     const std::shared_ptr<Xtc>& xtc,
                                                                     const uint32_t currentPage)
    : Activity("XtcReaderChapterSelection", renderer, mappedInput),
      xtc(xtc),
      currentPage(currentPage),
      uiTarget(makeUiTarget(renderer)),
      app(uiTarget, uiTarget.deviceContext()) {}

int XtcReaderChapterSelectionActivity::findChapterIndexForPage(const uint32_t page) const {
  if (!xtc) {
    return 0;
  }

  const auto& chapters = xtc->getChapters();
  for (size_t i = 0; i < chapters.size(); i++) {
    if (page >= chapters[i].startPage && page <= chapters[i].endPage) {
      return static_cast<int>(i);
    }
  }
  return 0;
}

void XtcReaderChapterSelectionActivity::onEnter() {
  Activity::onEnter();

  uiReady = false;
  visibleRows = 1;
  app.setTheme(uiThemeTokens(uiTarget));
  app.on(ACTION_ROW, &XtcReaderChapterSelectionActivity::onRowEvent, this);
  app.setScreen(&XtcReaderChapterSelectionActivity::chapterScreen, this);

  if (!xtc) {
    return;
  }

  selectorIndex = findChapterIndexForPage(currentPage);
  // Start with the current chapter at the top of the viewport (visibleRows is
  // unknown until the first screen build; the builder clamps into range).
  topIndex = followListSelection(selectorIndex, 0, visibleRows, static_cast<int>(xtc->getChapters().size()));

  requestUpdate();
}

void XtcReaderChapterSelectionActivity::onExit() { Activity::onExit(); }

void XtcReaderChapterSelectionActivity::activateSelected() {
  const auto& chapters = xtc->getChapters();
  if (!chapters.empty() && selectorIndex >= 0 && selectorIndex < static_cast<int>(chapters.size())) {
    setResult(PageResult{chapters[selectorIndex].startPage});
    finish();
  }
}

void XtcReaderChapterSelectionActivity::onRowEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<XtcReaderChapterSelectionActivity*>(user);
  if (event.value < 0 || event.value >= static_cast<int16_t>(self->xtc->getChapters().size())) return;
  self->selectorIndex = event.value;
  // The tapped row leaves this screen (finish); a lingering flash would gray
  // an unrelated element on the next render.
  self->app.clearTapFlash();
  self->activateSelected();
}

void XtcReaderChapterSelectionActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  if (!xtc) {
    return;
  }
  const int totalItems = static_cast<int>(xtc->getChapters().size());

  // Touch goes through the FreeInkApp: render() registered the row hit rects;
  // route the snapshot and let onRowEvent dispatch.
  if (uiReady) {
    const fui::InputSnapshot snap = touchSnapshotFrom(mappedInput);
    if (snap.touchPressed || snap.touchReleased) {
      const auto event = app.route(snap);
      if (app.invalidated()) requestUpdate();
      if (event) return;  // dispatched to onRowEvent
    }
  }

  // Swipes scroll the viewport; the selection stays put (it may scroll
  // off-screen) and button navigation pulls the view back to it.
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    const int delta = swipe == MappedInputManager::SwipeDir::Up ? visibleRows : -visibleRows;
    const int next = scrollListBy(topIndex, delta, visibleRows, totalItems);
    if (next != topIndex) {
      topIndex = next;
      requestUpdate();
    }
    return;
  }

  const auto moveSelection = [this, totalItems](const int index) {
    selectorIndex = index;
    topIndex = followListSelection(selectorIndex, topIndex, visibleRows, totalItems);
    requestUpdate();
  };

  buttonNavigator.onNextRelease(
      [this, totalItems, &moveSelection] { moveSelection(ButtonNavigator::nextIndex(selectorIndex, totalItems)); });

  buttonNavigator.onPreviousRelease(
      [this, totalItems, &moveSelection] { moveSelection(ButtonNavigator::previousIndex(selectorIndex, totalItems)); });

  buttonNavigator.onNextContinuous([this, totalItems, &moveSelection] {
    moveSelection(ButtonNavigator::nextPageIndex(selectorIndex, totalItems, visibleRows));
  });

  buttonNavigator.onPreviousContinuous([this, totalItems, &moveSelection] {
    moveSelection(ButtonNavigator::previousPageIndex(selectorIndex, totalItems, visibleRows));
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelected();
  }
}

void XtcReaderChapterSelectionActivity::chapterScreen(UiApp::ScreenType& screen, void* user) {
  static_cast<XtcReaderChapterSelectionActivity*>(user)->buildChapterScreen(screen);
}

void XtcReaderChapterSelectionActivity::buildChapterScreen(UiApp::ScreenType& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the header band render() paints the title in.
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                                      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                                      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)),
                                      static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (!xtc) {
    return;
  }
  const auto& chapters = xtc->getChapters();
  if (chapters.empty()) {
    screen.centeredText(tr(STR_NO_CHAPTERS), screen.theme().bodyText);
    return;
  }

  // Transient per-render: labels point at the chapter names owned by `xtc`
  // (alive for the activity's lifetime) or the static i18n fallback.
  std::vector<fui::ListItem> items;
  items.reserve(chapters.size());
  for (const auto& chapter : chapters) {
    fui::ListItem item;
    item.label = chapter.name.empty() ? tr(STR_UNNAMED) : chapter.name.c_str();
    item.actionValue = static_cast<int16_t>(items.size());
    items.push_back(item);
  }

  fui::ListProps props;
  props.items = items.data();
  props.count = static_cast<uint16_t>(items.size());
  props.selectedIndex = static_cast<int16_t>(selectorIndex);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  const auto rows = fui::listVisibleRows(screen.body(), screen.theme().rowHeight, screen.theme().listRowGap);
  visibleRows = rows > 0 ? rows : 1;
  topIndex = scrollListBy(topIndex, 0, visibleRows, static_cast<int>(chapters.size()));  // clamp to range
  props.topIndex = static_cast<uint16_t>(topIndex);
  screen.list(props);
}

void XtcReaderChapterSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  // Centered title in the header band the content margin reserves.
  const int titleWidth = renderer.getTextWidth(UI_12_FONT_ID, tr(STR_SELECT_CHAPTER), EpdFontFamily::BOLD);
  const int titleX = safe.x + (safe.width - titleWidth) / 2;
  const int titleY = safe.y + metrics.topPadding + (metrics.headerHeight - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
  renderer.drawText(UI_12_FONT_ID, titleX, titleY, tr(STR_SELECT_CHAPTER), true, EpdFontFamily::BOLD);

  uiReady = false;
  app.render();
  uiReady = true;

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

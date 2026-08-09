#include "EpubReaderFootnotesActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_ROW = 1;
}  // namespace

EpubReaderFootnotesActivity::EpubReaderFootnotesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                         const std::vector<FootnoteEntry>& footnotes)
    : Activity("EpubReaderFootnotes", renderer, mappedInput),
      footnotes(footnotes),
      uiTarget(makeUiTarget(renderer)),
      app(uiTarget, uiTarget.deviceContext()) {}

void EpubReaderFootnotesActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  uiReady = false;
  visibleRows = 1;
  topIndex = 0;
  app.setTheme(uiThemeTokens(uiTarget));
  app.on(ACTION_ROW, &EpubReaderFootnotesActivity::onRowEvent, this);
  app.setScreen(&EpubReaderFootnotesActivity::listScreen, this);
  requestUpdate();
}

void EpubReaderFootnotesActivity::onExit() { Activity::onExit(); }

void EpubReaderFootnotesActivity::activate(const int index) {
  if (index < 0 || index >= static_cast<int>(footnotes.size())) return;
  setResult(FootnoteResult{footnotes[index].href});
  finish();
}

void EpubReaderFootnotesActivity::onRowEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<EpubReaderFootnotesActivity*>(user);
  if (event.value < 0 || event.value >= static_cast<int16_t>(self->footnotes.size())) return;
  self->selectedIndex = event.value;
  // The tapped row leaves this screen; a lingering flash would gray an
  // unrelated element on the next render.
  self->app.clearTapFlash();
  self->activate(self->selectedIndex);
}

void EpubReaderFootnotesActivity::loop() {
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
    const fui::InputSnapshot snap = touchSnapshotFrom(mappedInput);
    if (snap.touchPressed || snap.touchReleased) {
      const auto event = app.route(snap);
      if (app.invalidated()) requestUpdate();
      if (event) return;  // dispatched to onRowEvent
    }
  }

  const int count = static_cast<int>(footnotes.size());
  if (count > 0) {
    // Swipes scroll the viewport; the selection stays put and button
    // navigation pulls the view back to it.
    const auto swipe = mappedInput.wasSwipe();
    if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
      const int delta = swipe == MappedInputManager::SwipeDir::Up ? visibleRows : -visibleRows;
      const int next = scrollListBy(topIndex, delta, visibleRows, count);
      if (next != topIndex) {
        topIndex = next;
        requestUpdate();
      }
      return;
    }

    const auto moveSelection = [this, count](const int index) {
      selectedIndex = index;
      topIndex = followListSelection(selectedIndex, topIndex, visibleRows, count);
      requestUpdate();
    };
    buttonNavigator.onNext(
        [this, count, &moveSelection] { moveSelection(ButtonNavigator::nextIndex(selectedIndex, count)); });
    buttonNavigator.onPrevious(
        [this, count, &moveSelection] { moveSelection(ButtonNavigator::previousIndex(selectedIndex, count)); });
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
      mappedInput.wasReleased(MappedInputManager::Button::Power)) {
    activate(selectedIndex);
    return;
  }
}

void EpubReaderFootnotesActivity::listScreen(UiApp::ScreenType& screen, void* user) {
  static_cast<EpubReaderFootnotesActivity*>(user)->buildListScreen(screen);
}

void EpubReaderFootnotesActivity::buildListScreen(UiApp::ScreenType& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the header band GUI.drawHeader paints.
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                                      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                                      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)),
                                      static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (footnotes.empty()) {
    screen.centeredText(tr(STR_NO_FOOTNOTES), screen.theme().bodyText);
    return;
  }

  // Labels point into the footnotes member vector (stable for the activity's
  // lifetime) or static i18n strings; no per-render copies needed.
  std::vector<fui::ListItem> items;
  items.reserve(footnotes.size());
  for (const auto& footnote : footnotes) {
    fui::ListItem item;
    item.label = footnote.number[0] ? footnote.number : tr(STR_LINK);
    item.actionValue = static_cast<int16_t>(items.size());
    items.push_back(item);
  }

  fui::ListProps props;
  props.items = items.data();
  props.count = static_cast<uint16_t>(items.size());
  props.selectedIndex = static_cast<int16_t>(selectedIndex);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  const auto rows = fui::listVisibleRows(screen.body(), screen.theme().rowHeight, screen.theme().listRowGap);
  visibleRows = rows > 0 ? rows : 1;
  topIndex = scrollListBy(topIndex, 0, visibleRows, static_cast<int>(footnotes.size()));  // clamp to range
  props.topIndex = static_cast<uint16_t>(topIndex);
  screen.list(props);
}

void EpubReaderFootnotesActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  // Header via GUI.drawHeader (already FreeInkUI-themed); the rest of the
  // screen renders through the app.
  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 tr(STR_FOOTNOTES));

  uiReady = false;
  app.render();
  uiReady = true;

  const auto labels = footnotes.empty() ? mappedInput.mapLabels(tr(STR_BACK), "", "", "")
                                        : mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

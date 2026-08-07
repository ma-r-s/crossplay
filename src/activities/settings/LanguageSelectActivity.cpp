#include "LanguageSelectActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <iterator>
#include <vector>

#include "CrossPointSettings.h"
#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_ROW = 1;
}  // namespace

LanguageSelectActivity::LanguageSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("LanguageSelect", renderer, mappedInput),
      uiTarget(makeUiTarget(renderer)),
      app(uiTarget, uiTarget.deviceContext()) {}

void LanguageSelectActivity::onEnter() {
  Activity::onEnter();

  // Set current selection based on current language
  const auto currentLang = static_cast<uint8_t>(I18N.getLanguage());
  const auto* begin = std::begin(SORTED_LANGUAGE_INDICES);
  const auto* end = std::end(SORTED_LANGUAGE_INDICES);
  const auto* it = std::find(begin, end, currentLang);
  selectedIndex = (it != end) ? std::distance(begin, it) : 0;

  uiReady = false;
  visibleRows = 1;
  topIndex = 0;
  followSelectionOnBuild = true;
  app.setTheme(uiThemeTokens(uiTarget));
  app.on(ACTION_ROW, &LanguageSelectActivity::onRowEvent, this);
  app.setScreen(&LanguageSelectActivity::languageScreen, this);
  requestUpdate();
}

void LanguageSelectActivity::onExit() { Activity::onExit(); }

void LanguageSelectActivity::onRowEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<LanguageSelectActivity*>(user);
  if (event.value < 0 || event.value >= static_cast<int16_t>(totalItems)) return;
  // The tapped row leaves this screen; a lingering flash would gray an
  // unrelated element on the next render.
  self->app.clearTapFlash();
  self->activate(event.value);
}

void LanguageSelectActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onBack();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    activate(selectedIndex);
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

  const auto moveSelection = [this](const int index) {
    selectedIndex = index;
    topIndex = followListSelection(selectedIndex, topIndex, visibleRows, totalItems);
    requestUpdate();
  };

  buttonNavigator.onNextRelease(
      [this, &moveSelection] { moveSelection(ButtonNavigator::nextIndex(selectedIndex, totalItems)); });

  buttonNavigator.onPreviousRelease(
      [this, &moveSelection] { moveSelection(ButtonNavigator::previousIndex(selectedIndex, totalItems)); });

  buttonNavigator.onNextContinuous([this, &moveSelection] {
    moveSelection(ButtonNavigator::nextPageIndex(selectedIndex, totalItems, visibleRows));
  });

  buttonNavigator.onPreviousContinuous([this, &moveSelection] {
    moveSelection(ButtonNavigator::previousPageIndex(selectedIndex, totalItems, visibleRows));
  });
}

void LanguageSelectActivity::activate(const int index) {
  selectedIndex = index;
  const uint8_t langIndex = SORTED_LANGUAGE_INDICES[selectedIndex];

  {
    RenderLock lock(*this);
    I18N.setLanguage(static_cast<Language>(langIndex));
  }

  SETTINGS.language = langIndex;
  SETTINGS.saveToFile();

  // Return to previous page
  onBack();
}

void LanguageSelectActivity::languageScreen(UiApp::ScreenType& screen, void* user) {
  static_cast<LanguageSelectActivity*>(user)->buildScreen(screen);
}

void LanguageSelectActivity::buildScreen(UiApp::ScreenType& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the header band GUI.drawHeader paints.
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                                      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                                      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)),
                                      static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // Transient per-render: sized once via reserve, points at static i18n
  // strings, freed on scope exit.
  const auto currentLang = static_cast<uint8_t>(I18N.getLanguage());
  std::vector<fui::ListItem> items;
  items.reserve(totalItems);
  for (int i = 0; i < totalItems; ++i) {
    fui::ListItem item;
    item.label = I18N.getLanguageName(static_cast<Language>(SORTED_LANGUAGE_INDICES[i]));
    if (SORTED_LANGUAGE_INDICES[i] == currentLang) {
      item.value = tr(STR_SELECTED);
    }
    item.actionValue = static_cast<int16_t>(i);
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
  if (followSelectionOnBuild) {
    followSelectionOnBuild = false;
    topIndex = followListSelection(selectedIndex, topIndex, visibleRows, totalItems);
  }
  topIndex = scrollListBy(topIndex, 0, visibleRows, totalItems);  // clamp to range
  props.topIndex = static_cast<uint16_t>(topIndex);
  screen.list(props);
}

void LanguageSelectActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  auto metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_LANGUAGE));

  uiReady = false;
  app.render();
  uiReady = true;

  // Button hints
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

#include "NetworkModeSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr int MENU_ITEM_COUNT = 3;
constexpr fui::ActionId ACTION_ROW = 1;
constexpr StrId menuItems[MENU_ITEM_COUNT] = {StrId::STR_JOIN_NETWORK, StrId::STR_CALIBRE_WIRELESS,
                                              StrId::STR_CREATE_HOTSPOT};
constexpr StrId menuDescs[MENU_ITEM_COUNT] = {StrId::STR_JOIN_DESC, StrId::STR_CALIBRE_DESC, StrId::STR_HOTSPOT_DESC};
constexpr UIIcon menuIcons[MENU_ITEM_COUNT] = {UIIcon::Wifi, UIIcon::Library, UIIcon::Hotspot};
}  // namespace

NetworkModeSelectionActivity::NetworkModeSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("NetworkModeSelection", renderer, mappedInput),
      uiTarget(makeUiTarget(renderer)),
      app(uiTarget, uiTarget.deviceContext()) {}

void NetworkModeSelectionActivity::selectCurrent() {
  NetworkMode mode = NetworkMode::JOIN_NETWORK;
  if (selectedIndex == 1) {
    mode = NetworkMode::CONNECT_CALIBRE;
  } else if (selectedIndex == 2) {
    mode = NetworkMode::CREATE_HOTSPOT;
  }
  onModeSelected(mode);
}

void NetworkModeSelectionActivity::onRowEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<NetworkModeSelectionActivity*>(user);
  if (event.value < 0 || event.value >= MENU_ITEM_COUNT) return;
  self->selectedIndex = event.value;
  // Selection leaves this screen; a lingering flash would gray an unrelated
  // element on the next render.
  self->app.clearTapFlash();
  self->selectCurrent();
}

void NetworkModeSelectionActivity::onEnter() {
  Activity::onEnter();

  selectedIndex = 0;
  uiReady = false;
  visibleRows = 1;
  topIndex = 0;
  app.setTheme(uiThemeTokens(uiTarget));
  app.on(ACTION_ROW, &NetworkModeSelectionActivity::onRowEvent, this);
  app.setScreen(&NetworkModeSelectionActivity::listScreen, this);
  requestUpdate();
}

void NetworkModeSelectionActivity::onExit() { Activity::onExit(); }

void NetworkModeSelectionActivity::loop() {
  // Handle back button - cancel
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onCancel();
    return;
  }

  // Handle confirm button - select current option
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    selectCurrent();
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

  // Handle navigation
  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, MENU_ITEM_COUNT);
    topIndex = followListSelection(selectedIndex, topIndex, visibleRows, MENU_ITEM_COUNT);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, MENU_ITEM_COUNT);
    topIndex = followListSelection(selectedIndex, topIndex, visibleRows, MENU_ITEM_COUNT);
    requestUpdate();
  });
}

void NetworkModeSelectionActivity::listScreen(UiApp::ScreenType& screen, void* user) {
  static_cast<NetworkModeSelectionActivity*>(user)->buildListScreen(screen);
}

void NetworkModeSelectionActivity::buildListScreen(UiApp::ScreenType& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  std::vector<fui::ListItem> items;
  items.reserve(MENU_ITEM_COUNT);
  for (int i = 0; i < MENU_ITEM_COUNT; i++) {
    fui::ListItem item;
    item.label = I18N.get(menuItems[i]);
    item.subtitle = I18N.get(menuDescs[i]);
    item.icon = listIconFor(menuIcons[i], 32);  // subtitle rows carry the larger icon
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
  topIndex = scrollListBy(topIndex, 0, visibleRows, MENU_ITEM_COUNT);  // clamp to range
  props.topIndex = static_cast<uint16_t>(topIndex);
  screen.list(props);
}

void NetworkModeSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();

  // Header via GUI.drawHeader (already FreeInkUI-themed) for the battery
  // indicator; the rest of the screen renders through the app.
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_FILE_TRANSFER));

  uiReady = false;
  app.render();
  uiReady = true;

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void NetworkModeSelectionActivity::onModeSelected(NetworkMode mode) {
  setResult(NetworkModeResult{mode});
  finish();
}

void NetworkModeSelectionActivity::onCancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

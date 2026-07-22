#include "FrontlightPanelActivity.h"

#include <GfxRenderer.h>
#include <HalFrontlight.h>
#include <I18n.h>

#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_BRIGHTNESS = 1;
constexpr fui::ActionId ACTION_WARMTH = 2;
constexpr fui::ActionId ACTION_TOGGLE = 3;
constexpr int BRIGHTNESS_STEP = 5;

uint8_t percentFromPermille(const int16_t permille) {
  int value = (static_cast<int>(permille) * 100 + 500) / 1000;
  if (value < 0) value = 0;
  if (value > 100) value = 100;
  return static_cast<uint8_t>(value);
}
}  // namespace

FrontlightPanelActivity::FrontlightPanelActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("FrontlightPanel", renderer, mappedInput),
      uiTarget(makeUiTarget(renderer)),
      app(uiTarget, uiTarget.deviceContext()) {}

void FrontlightPanelActivity::onEnter() {
  Activity::onEnter();

  // The HAL is the live source of truth; SETTINGS only mirrors it for boot.
  brightness = Frontlight.brightness();
  warmth = Frontlight.warmth();
  lightOn = Frontlight.isOn();

  uiReady = false;
  app.setTheme(uiThemeTokens(uiTarget));
  app.on(ACTION_BRIGHTNESS, &FrontlightPanelActivity::onBrightnessEvent, this);
  app.on(ACTION_WARMTH, &FrontlightPanelActivity::onWarmthEvent, this);
  app.on(ACTION_TOGGLE, &FrontlightPanelActivity::onToggleEvent, this);
  app.setScreen(&FrontlightPanelActivity::panelScreen, this);
  requestUpdate();
}

void FrontlightPanelActivity::onExit() {
  // Debounced persistence: one SPIFFS write on close, never per slider tick.
  if (SETTINGS.frontlightBrightness != brightness || SETTINGS.frontlightWarmth != warmth ||
      SETTINGS.frontlightOn != (lightOn ? 1 : 0)) {
    SETTINGS.frontlightBrightness = brightness;
    SETTINGS.frontlightWarmth = warmth;
    SETTINGS.frontlightOn = lightOn ? 1 : 0;
    SETTINGS.saveToFile();
  }
  Activity::onExit();
}

void FrontlightPanelActivity::onBrightnessEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<FrontlightPanelActivity*>(user);
  if (event.dragPermille < 0) return;
  self->brightness = percentFromPermille(event.dragPermille);
  Frontlight.setBrightness(self->brightness);
  // Adjusting brightness while off is an obvious "I want light" intent.
  if (!self->lightOn) {
    self->lightOn = true;
    Frontlight.setOn(true);
  }
}

void FrontlightPanelActivity::onWarmthEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<FrontlightPanelActivity*>(user);
  if (event.dragPermille < 0) return;
  self->warmth = percentFromPermille(event.dragPermille);
  Frontlight.setWarmth(self->warmth);
}

void FrontlightPanelActivity::onToggleEvent(const fui::ActionEvent&, void* user) {
  static_cast<FrontlightPanelActivity*>(user)->toggleLight();
}

void FrontlightPanelActivity::adjustBrightness(const int delta) {
  int next = static_cast<int>(brightness) + delta;
  if (next < 0) next = 0;
  if (next > 100) next = 100;
  if (next == brightness) return;
  brightness = static_cast<uint8_t>(next);
  Frontlight.setBrightness(brightness);
  if (!lightOn) {
    lightOn = true;
    Frontlight.setOn(true);
  }
  requestUpdate();
}

void FrontlightPanelActivity::toggleLight() {
  lightOn = !lightOn;
  Frontlight.setOn(lightOn);
  requestUpdate();
}

void FrontlightPanelActivity::close() { finish(); }

bool FrontlightPanelActivity::handleHomeGesture() {
  close();
  return true;
}

void FrontlightPanelActivity::loop() {
  fui::InputSnapshot snap{};
  if (uiReady) {
    snap = touchSnapshotFrom(mappedInput);
    if (snap.touchPressed || snap.touchHeld || snap.touchReleased) {
      const auto event = app.route(snap);
      if (app.invalidated()) requestUpdate();
      if (event) {
        if (event.dragPermille >= 0) draggingSlider = true;
        return;
      }
    }
    if (draggingSlider) {
      // Drag ended (possibly off the slider): swallow the release's swipe so
      // it can't double as the left-edge back gesture and close the panel.
      if (!snap.touchHeld) draggingSlider = false;
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    close();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    toggleLight();
    return;
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left},
                                       [this] { adjustBrightness(-BRIGHTNESS_STEP); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right},
                                       [this] { adjustBrightness(BRIGHTNESS_STEP); });
}

void FrontlightPanelActivity::panelScreen(UiApp::ScreenType& screen, void* user) {
  static_cast<FrontlightPanelActivity*>(user)->buildPanelScreen(screen);
}

void FrontlightPanelActivity::buildPanelScreen(UiApp::ScreenType& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& theme = screen.theme();
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});

  const int16_t lh = screen.target().lineHeight(theme.bodyText.font);
  const fui::Insets sideInset{0, static_cast<int16_t>(theme.spaceLg * 2), 0, static_cast<int16_t>(theme.spaceLg * 2)};
  char line[48];

  screen.spacer(theme.spaceLg);

  snprintf(line, sizeof(line), "%s  %u%%", tr(STR_BRIGHTNESS), static_cast<unsigned>(brightness));
  screen.target().text(screen.takeTop(lh, theme.spaceSm).inset(sideInset), line, theme.bodyText);
  fui::SliderProps brightnessSlider;
  brightnessSlider.value = brightness;
  brightnessSlider.max = 100;
  brightnessSlider.action = ACTION_BRIGHTNESS;
  brightnessSlider.inputMask = fui::InputTouch | fui::InputDrag;
  fui::slider(screen.frame(), screen.takeTop(theme.rowHeight, theme.spaceLg).inset(sideInset), brightnessSlider);

  if (Frontlight.hasColorTemperature()) {
    snprintf(line, sizeof(line), "%s  %u%%", tr(STR_WARMTH), static_cast<unsigned>(warmth));
    screen.target().text(screen.takeTop(lh, theme.spaceSm).inset(sideInset), line, theme.bodyText);
    fui::SliderProps warmthSlider;
    warmthSlider.value = warmth;
    warmthSlider.max = 100;
    warmthSlider.action = ACTION_WARMTH;
    warmthSlider.inputMask = fui::InputTouch | fui::InputDrag;
    fui::slider(screen.frame(), screen.takeTop(theme.rowHeight, theme.spaceLg).inset(sideInset), warmthSlider);
  }

  screen.spacer(theme.spaceLg);

  // On/off toggle: sun icon + explicit state label; the active (inverted)
  // style doubles as the unambiguous "light is on" indicator.
  const fui::Rect btnArea = screen.takeTop(theme.rowHeight);
  const int16_t btnW = static_cast<int16_t>(btnArea.width / 3);
  fui::ButtonProps toggle;
  toggle.label = lightOn ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  toggle.icon = fui::bitmapFromIcon(icon_sun_24);
  toggle.action = ACTION_TOGGLE;
  toggle.state = lightOn ? fui::StateActive : fui::StateNormal;
  screen.button(
      toggle, fui::Rect{static_cast<int16_t>(btnArea.x + (btnArea.width - btnW) / 2), btnArea.y, btnW, btnArea.height});
}

void FrontlightPanelActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 tr(STR_FRONTLIGHT));

  uiReady = false;
  app.render();
  uiReady = true;

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), lightOn ? tr(STR_STATE_OFF) : tr(STR_STATE_ON), "-", "+");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

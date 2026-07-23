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
      // No control took it. A tap released at/below the panel edge (in the
      // preserved content underneath) dismisses — the drop-down behaves like a
      // modal scrim. A drag-off release carries y = -1, so it never dismisses.
      if (snap.touchReleased && !draggingSlider && snap.touchY >= panelBottom) {
        close();
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

int FrontlightPanelActivity::computePanelBottom() const {
  // Mirror buildPanelScreen's takeTop/spacer sequence so the frame, content
  // margin, and dismiss threshold land on the same edge.
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto tokens = uiThemeTokens(uiTarget);
  const int16_t lh = uiTarget.lineHeight(tokens.bodyText.font);
  int y = metrics.topPadding + metrics.headerHeight;
  y += tokens.spaceLg;                     // leading spacer
  y += tokens.rowHeight + tokens.spaceSm;  // brightness label + sun toggle row
  y += tokens.rowHeight + tokens.spaceLg;  // brightness slider
  if (Frontlight.hasColorTemperature()) {
    y += lh + tokens.spaceSm + tokens.rowHeight + tokens.spaceLg;  // warmth label + slider
  }
  y += tokens.spaceLg;  // trailing padding
  return y;
}

void FrontlightPanelActivity::panelScreen(UiApp::ScreenType& screen, void* user) {
  static_cast<FrontlightPanelActivity*>(user)->buildPanelScreen(screen);
}

void FrontlightPanelActivity::buildPanelScreen(UiApp::ScreenType& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& theme = screen.theme();
  // Body spans from below the header down to the drop-down's bottom edge, so
  // the app never lays out over the preserved content underneath.
  const int16_t bottomInset = static_cast<int16_t>(renderer.getScreenHeight() - panelBottom);
  screen.setContentMargin(
      fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0, bottomInset, 0});

  const int16_t lh = screen.target().lineHeight(theme.bodyText.font);
  const int16_t rowH = theme.rowHeight;
  const fui::Insets sideInset{0, static_cast<int16_t>(theme.spaceLg * 2), 0, static_cast<int16_t>(theme.spaceLg * 2)};
  char line[48];

  screen.spacer(theme.spaceLg);

  // Header row: "Brightness NN%" on the left, a tappable sun icon on the right
  // that toggles the light — bright `sun` when on, `sun-dim` when off (the icon
  // itself is the state indicator). Sharing a row with the label frees the
  // whole bottom toggle row, shrinking the panel.
  const fui::Rect headerRow = screen.takeTop(rowH, theme.spaceSm).inset(sideInset);
  snprintf(line, sizeof(line), "%s  %u%%", tr(STR_BRIGHTNESS), static_cast<unsigned>(brightness));
  const fui::BitmapRef sunIcon = fui::bitmapFromIcon(lightOn ? icon_sun_32 : icon_sun_dim_32);
  const int16_t iconW = static_cast<int16_t>(sunIcon.width);
  const int16_t iconH = static_cast<int16_t>(sunIcon.height);
  const fui::Rect iconRect{static_cast<int16_t>(headerRow.x + headerRow.width - iconW),
                           static_cast<int16_t>(headerRow.y + (rowH - iconH) / 2), iconW, iconH};
  const fui::Rect labelRect{headerRow.x, static_cast<int16_t>(headerRow.y + (rowH - lh) / 2),
                            static_cast<int16_t>(headerRow.width - iconW - theme.spaceMd), lh};
  screen.target().text(labelRect, line, theme.bodyText);
  // Generous hit target: the full header-row height and a wide band on the right
  // (well beyond the 32px glyph) so the toggle is easy to hit. Stays within the
  // header row so it never steals taps from the brightness slider below.
  const int16_t hitW = static_cast<int16_t>(iconW + theme.spaceLg * 4);
  const fui::Rect hitRect{static_cast<int16_t>(headerRow.right() - hitW), headerRow.y, hitW, rowH};
  screen.frame().hit(hitRect, ACTION_TOGGLE);
  screen.target().bitmap(iconRect, sunIcon, fui::BitmapMode::Center);

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
}

void FrontlightPanelActivity::render(RenderLock&&) {
  // Overlay drop-down: keep the framebuffer content (the reader/menu we opened
  // over) intact below the panel; only the top third is repainted. No full
  // clearScreen — same as the theme popups draw over the current frame.
  panelBottom = computePanelBottom();
  const int pageWidth = renderer.getScreenWidth();
  renderer.fillRect(0, 0, pageWidth, panelBottom, false);  // white the panel area only

  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_FRONTLIGHT));

  uiReady = false;
  app.render();
  uiReady = true;

  // Bottom edge of the drop-down: a solid rule separating it from the content
  // (the scrim) underneath, reinforcing that a tap below dismisses.
  renderer.fillRect(0, panelBottom - 2, pageWidth, 2, true);

  renderer.displayBuffer();
}

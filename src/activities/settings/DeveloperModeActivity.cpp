#include "DeveloperModeActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "DevMode.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void DeveloperModeActivity::onEnter() {
  Activity::onEnter();
  nextPollAt = 0;
  requestUpdate();
}

void DeveloperModeActivity::toggle() {
  SETTINGS.devMode = SETTINGS.devMode ? 0 : 1;
  SETTINGS.saveToFile();
  LOG_INF("DEVMODE", "toggled %s from the device", SETTINGS.devMode ? "on" : "off");
  // devmode::update() reconciles on the next loop; the panel follows it rather
  // than predicting it, so what is drawn is what actually happened.
  requestUpdate();
}

void DeveloperModeActivity::loop() {
  int x = 0;
  int y = 0;
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(x, y)) {
    toggle();
    return;
  }

  // Poll the service so the code and address appear when the join completes.
  const unsigned long now = millis();
  if (nextPollAt != 0 && static_cast<long>(now - nextPollAt) < 0) return;
  nextPollAt = now + 500;

  const auto st = devmode::status();
  if (st.enabled != lastEnabled || st.connected != lastConnected || st.ip != lastIp || st.code != lastCode) {
    requestUpdate();
  }
}

void DeveloperModeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const auto st = devmode::status();
  lastEnabled = st.enabled;
  lastConnected = st.connected;
  lastIp = st.ip;
  lastCode = st.code;

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_DEV_MODE));

  const int midY = pageHeight / 2;

  if (!st.enabled) {
    renderer.drawCenteredText(UI_12_FONT_ID, midY - 20, tr(STR_DEV_MODE_OFF_HINT), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, midY + 10, tr(STR_DEV_MODE_ENABLE_HINT));
  } else if (st.ssid.empty()) {
    renderer.drawCenteredText(UI_12_FONT_ID, midY - 20, tr(STR_DEV_MODE_TITLE), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, midY + 10, tr(STR_DEV_MODE_NO_WIFI));
  } else if (!st.connected) {
    renderer.drawCenteredText(UI_12_FONT_ID, midY - 20, tr(STR_DEV_MODE_TITLE), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, midY + 10, tr(STR_DEV_MODE_WAITING));
  } else {
    renderer.drawCenteredText(UI_10_FONT_ID, midY - 70, tr(STR_DEV_MODE_TITLE));
    // The address first: a reader needs it before the code is any use.
    renderer.drawCenteredText(UI_12_FONT_ID, midY - 45, st.ip.c_str(), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, midY - 15, tr(STR_DEV_MODE_PAIRING_HINT));
    // The code is the whole point of the screen, so it gets the largest type
    // the theme has and stands alone on its line.
    renderer.drawCenteredText(UI_12_FONT_ID, midY + 20, st.code.c_str(), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, midY + 55, tr(STR_DEV_MODE_EXPOSED_HINT));
    renderer.drawCenteredText(UI_10_FONT_ID, midY + 78, tr(STR_DEV_MODE_NO_SLEEP_HINT));
  }

  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), "", "", st.enabled ? tr(STR_DEV_MODE_TURN_OFF) : tr(STR_DEV_MODE_TURN_ON));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

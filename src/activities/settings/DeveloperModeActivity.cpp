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
  sample();
  shown = latest;
  requestUpdate();
}

// Main task only.
void DeveloperModeActivity::sample() {
  const auto st = devmode::status();
  latest.enabled = st.enabled;
  latest.connected = st.connected;
  latest.hasNetwork = !st.ssid.empty();
  latest.ip = st.ip;
  latest.code = st.code;
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

  sample();
  if (latest.enabled != shown.enabled || latest.connected != shown.connected || latest.hasNetwork != shown.hasNetwork ||
      latest.ip != shown.ip || latest.code != shown.code) {
    shown = latest;  // still the main task; render() only ever reads `shown`
    requestUpdate();
  }
}

void DeveloperModeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  // Every hint on this screen is a sentence, and a sentence drawn with
  // drawCenteredText runs off both edges rather than wrapping -- which is
  // exactly what it did on the first device that showed it. Wrap against the
  // real content width, the way CrashActivity does with panic text.
  const auto contentWidth = pageWidth - 2 * metrics.contentSidePadding;
  const auto lineHeight = renderer.getLineHeight(UI_10_FONT_ID);

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_DEV_MODE));

  // Draws a wrapped, centred block and returns the y below it, so the layout
  // flows instead of every element carrying a hand-tuned offset that only
  // holds for one string length in one language.
  const auto block = [&](int y, int fontId, const char* text, bool bold) {
    for (const auto& line : renderer.wrappedText(fontId, text, contentWidth, 4)) {
      renderer.drawCenteredText(fontId, y, line.c_str(), true, bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
      y += renderer.getLineHeight(fontId);
    }
    return y;
  };

  int y = pageHeight / 3;

  if (!shown.enabled) {
    y = block(y, UI_12_FONT_ID, tr(STR_DEV_MODE_OFF_HINT), true);
    y += metrics.verticalSpacing;
    block(y, UI_10_FONT_ID, tr(STR_DEV_MODE_ENABLE_HINT), false);
  } else if (!shown.hasNetwork) {
    y = block(y, UI_12_FONT_ID, tr(STR_DEV_MODE_TITLE), true);
    y += metrics.verticalSpacing;
    block(y, UI_10_FONT_ID, tr(STR_DEV_MODE_NO_WIFI), false);
  } else if (!shown.connected) {
    y = block(y, UI_12_FONT_ID, tr(STR_DEV_MODE_TITLE), true);
    y += metrics.verticalSpacing;
    block(y, UI_10_FONT_ID, tr(STR_DEV_MODE_WAITING), false);
  } else {
    // Start higher: this branch has the most to say.
    y = metrics.topPadding + metrics.headerHeight + lineHeight * 2;
    y = block(y, UI_10_FONT_ID, tr(STR_DEV_MODE_TITLE), false);
    y += metrics.verticalSpacing;
    // The address, then the code. Both are short and fixed-width enough not to
    // need wrapping, but they go through the same helper so the flow is one
    // rule rather than two.
    y = block(y, UI_12_FONT_ID, shown.ip.c_str(), true);
    y += metrics.verticalSpacing * 2;
    y = block(y, UI_10_FONT_ID, tr(STR_DEV_MODE_PAIRING_HINT), false);
    y = block(y, UI_12_FONT_ID, shown.code.c_str(), true);
    y += metrics.verticalSpacing * 2;
    y = block(y, UI_10_FONT_ID, tr(STR_DEV_MODE_EXPOSED_HINT), false);
    y += metrics.verticalSpacing;
    block(y, UI_10_FONT_ID, tr(STR_DEV_MODE_NO_SLEEP_HINT), false);
  }

  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), "", "", shown.enabled ? tr(STR_DEV_MODE_TURN_OFF) : tr(STR_DEV_MODE_TURN_ON));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

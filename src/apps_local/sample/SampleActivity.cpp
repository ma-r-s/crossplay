#include "SampleActivity.h"

#include <Memory.h>

#include "../../components/UITheme.h"
#include "../Shelf.h"
#include "fontIds.h"

std::unique_ptr<Activity> SampleActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<SampleActivity>(renderer, mappedInput);
}

void SampleActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void SampleActivity::loop() {
  // An app never names where Back goes; the shelf puts it back in whichever
  // folder opened it. See docs/shelf.md. The global back-swipe gesture arrives
  // as Button::Back too.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    shelf::leave(renderer, mappedInput);
  }
}

void SampleActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, sw, metrics.headerHeight}, "Sample");

  const int bodyY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int bodyH = sh - bodyY - metrics.buttonHintsHeight - metrics.verticalSpacing;
  UITheme::drawCenteredWrappedText(renderer, Rect{0, bodyY, sw, bodyH}, UI_12_FONT_ID,
                                   "Local app seam works. Copy src/apps_local/sample/ to start a real one.", 4);

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

#include "TodoActivity.h"

#include <Memory.h>

#include "../../components/UITheme.h"
#include "../Shelf.h"
#include "fontIds.h"

std::unique_ptr<Activity> TodoActivity::create(
    GfxRenderer& renderer,
    MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<TodoActivity>(renderer, mappedInput);
}

void TodoActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void TodoActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    shelf::leave(renderer, mappedInput);
  }
}

void TodoActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();

  renderer.clearScreen();

  GUI.drawHeader(
      renderer,
      Rect{0, metrics.topPadding, sw, metrics.headerHeight},
      "TO DO");

  const int bodyY =
      metrics.topPadding +
      metrics.headerHeight +
      metrics.verticalSpacing;

  const int bodyH =
      sh -
      bodyY -
      metrics.buttonHintsHeight -
      metrics.verticalSpacing;

  UITheme::drawCenteredWrappedText(
      renderer,
      Rect{0, bodyY, sw, bodyH},
      UI_12_FONT_ID,
      "Todo app is working!",
      4);

  const auto labels =
      mappedInput.mapLabels("Back", "", "", "");

  GUI.drawButtonHints(
      renderer,
      labels.btn1,
      labels.btn2,
      labels.btn3,
      labels.btn4);

  renderer.displayBuffer();
}

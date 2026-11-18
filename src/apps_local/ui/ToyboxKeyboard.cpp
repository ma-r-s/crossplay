#include "ToyboxKeyboard.h"

#include <FreeInkUIGfxRenderer.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include "../../components/UITheme.h"
#include "../../fontIds.h"

namespace fui = freeink::ui;

namespace toybox {

fui::Rect searchKeyboardRect(const GfxRenderer& renderer, const fui::KeyboardLayout& layout) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int rows = layout.rowCount;
  const int gap = metrics.keyboardKeySpacing;
  const int height = rows * metrics.keyboardKeyHeight + (rows > 1 ? (rows - 1) * gap : 0);
  const int width = pageWidth * metrics.keyboardWidthPercent / 100;
  return fui::Rect{static_cast<int16_t>((pageWidth - width) / 2), static_cast<int16_t>(pageHeight - 8 - height),
                   static_cast<int16_t>(width), static_cast<int16_t>(height)};
}

void drawSearchKeyboard(const GfxRenderer& renderer, const fui::KeyboardLayout& layout, const bool symbols,
                        const fui::ActionId keyAction, SearchKeyboardTable& table) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  table.beginPublishCycle();
  fui::GfxRendererTarget target(renderer);
  target.setFont(fui::GfxRendererTarget::FONT_SMALL, SMALL_FONT_ID);
  target.setFont(fui::GfxRendererTarget::FONT_BODY, UI_12_FONT_ID);
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  fui::Frame<kSearchKeyboardKeys> frame(target, device, noInput, table);

  fui::KeyboardProps props;
  props.layout = &layout;
  props.keyAction = keyAction;
  props.okLabel = "GO";
  // No shiftLabel: see the header.
  props.modeLabel = symbols ? tr(STR_KEY_MODE_ABC) : tr(STR_KEY_MODE_SYMBOLS);
  props.inputMask = static_cast<uint16_t>(fui::InputTouch);
  props.selectedIndex = -1;
  props.labelText.font = fui::GfxRendererTarget::FONT_BODY;
  props.altText.font = fui::GfxRendererTarget::FONT_SMALL;
  props.gap = static_cast<int16_t>(metrics.keyboardKeySpacing);
  props.padding = fui::Insets{0, 0, 0, 0};
  const fui::Rect kb = searchKeyboardRect(renderer, layout);
  props.bottomHitOverflow = static_cast<int16_t>(renderer.getScreenHeight() - kb.bottom());
  fui::keyboard(frame, kb, props);
  table.publish();
}

}  // namespace toybox

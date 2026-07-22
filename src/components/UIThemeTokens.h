#pragma once
#include <FreeInkUIGfxRenderer.h>

#include "UITheme.h"

// Merges the active UITheme's shape (row gaps, radii, insets, selection
// style) with the uiScale-derived sizes into FreeInkUI theme tokens: the
// theme says what lists look like, the scale says how big they are.
// Everything read here is plain data from ThemeMetrics — the same values an
// SD-card theme file will eventually supply.
inline freeink::ui::ThemeTokens uiThemeTokens(const freeink::ui::GfxRendererTarget& target) {
  namespace fui = freeink::ui;
  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();

  fui::ThemeTokens tokens = fui::themeTokensForLineHeight(target.lineHeight(fui::GfxRendererTarget::FONT_BODY));
  tokens.listRowGap = static_cast<int16_t>(metrics.listRowGap);
  tokens.listRowRadius = static_cast<uint8_t>(metrics.listRowRadius);
  tokens.listInset = static_cast<int16_t>(metrics.listInset);
  tokens.listSidePadding = static_cast<int16_t>(metrics.listSidePadding);
  tokens.bodyText.bold = metrics.listTitleBold;
  if (metrics.listSelectionLight) {
    tokens.listRow.selected.background = fui::Paint::dither(fui::Color::LightGray);
    tokens.listRow.selected.foreground = fui::Paint::solid(fui::Color::Black);
    tokens.listRow.active = tokens.listRow.selected;
  }
  return tokens;
}

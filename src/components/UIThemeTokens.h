#pragma once
#include <BoardConfig.h>
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
  // Left unset, this falls through to defaultButtonStyles(), which paints a
  // white background, black text and NO border -- on a white screen that is a
  // label, not a control. Only the selected and active states carry any fill,
  // and a finger never selects anything, so a touch reader saw no button at
  // all. Toybox sets its own tokens.button for the same reason; this is that
  // fix at the same seam, outlined rather than inverted so it matches the
  // hairline list rows this theme already draws.
  tokens.button = fui::defaultButtonStyles();
  for (fui::BoxStyle* box : {&tokens.button.normal, &tokens.button.focused, &tokens.button.disabled}) {
    box->border = fui::Paint::solid(fui::Color::Black);
    box->borderWidth = 1;
  }
  tokens.button.disabled.border = fui::Paint::dither(fui::Color::LightGray);
  for (fui::BoxStyle* box : {&tokens.button.normal, &tokens.button.selected, &tokens.button.focused,
                             &tokens.button.active, &tokens.button.disabled}) {
    box->radius = 4;
  }
  tokens.listRowGap = static_cast<int16_t>(metrics.listRowGap);
  tokens.listRowRadius = static_cast<uint8_t>(metrics.listRowRadius);
  tokens.listInset = static_cast<int16_t>(metrics.listInset);
  tokens.listSidePadding = static_cast<int16_t>(metrics.listSidePadding);
  tokens.listSelectionStyle = static_cast<fui::SelectionStyle>(metrics.listSelectionStyle);
  tokens.listScrollWidth = static_cast<int16_t>(metrics.listScrollWidth);
  tokens.listScrollSide = static_cast<uint8_t>(metrics.listScrollSide);
  // The scroll track hugs the band edge; on boards whose panel sits recessed
  // behind the bezel the edge columns are covered, so push the indicator
  // inward past the covered side. Bezel truth is per-board data
  // (BoardConfig::ViewableInsets); lists render in the portrait UI frame, so
  // the panel-native portrait insets apply directly.
  const auto& vi = BoardConfig::ACTIVE.viewableInsets;
  tokens.listScrollInset = static_cast<int16_t>(metrics.listScrollSide == 1 ? vi.left : vi.right);
  // Screen::header()/status() band height. Without this the SDK's
  // line-height-derived default applies and fui-drawn headers (OPDS) come out
  // a different height than every GUI.drawHeader band.
  tokens.headerHeight = static_cast<int16_t>(metrics.headerHeight);
  tokens.headerSidePadding = static_cast<int16_t>(metrics.headerSidePadding);
  tokens.headerUnderline = static_cast<uint8_t>(metrics.headerUnderlineSize);
  tokens.headerTitleAlign = static_cast<fui::TextAlign>(metrics.headerTitleAlign);
  tokens.bodyText.bold = metrics.listTitleBold;
  return tokens;
}

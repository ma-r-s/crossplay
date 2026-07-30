#pragma once
#include "fontIds.h"

// FreeInkUI font slots. Row heights, header height, and touch sizes are not
// chosen here: FreeInkApp derives all metric tokens from the body font's line
// height (themeTokensForLineHeight). One fixed tier for every board — the
// user-facing UI-scale setting was removed.
struct UIScaleSpec {
  int smallFontId;
  int bodyFontId;
  int titleFontId;
};

inline UIScaleSpec uiScaleSpec() {
  UIScaleSpec spec{};
  spec.smallFontId = UI_10_FONT_ID;
  spec.bodyFontId = UI_12_FONT_ID;
#ifdef OMIT_FONTS
  // Slim builds only register the UI fonts.
  spec.titleFontId = UI_12_FONT_ID;
#else
  spec.titleFontId = NOTOSANS_14_FONT_ID;
#endif
  return spec;
}

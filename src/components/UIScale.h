#pragma once
#include "CrossPointSettings.h"
#include "fontIds.h"

// Maps the uiScale setting to the FreeInkUI font slots. Row heights, header
// height, and touch sizes are not chosen here: FreeInkApp derives all metric
// tokens from the body font's line height (themeTokensForLineHeight), so each
// scale step grows every element uniformly and layout stays within screen
// bounds through the components' own virtualization.
struct UIScaleSpec {
  int smallFontId;
  int bodyFontId;
  int titleFontId;
};

inline UIScaleSpec uiScaleSpec() {
  UIScaleSpec spec{};
  switch (SETTINGS.uiScale) {
    case CrossPointSettings::UI_SCALE_SMALL:
      spec.smallFontId = SMALL_FONT_ID;
      spec.bodyFontId = UI_10_FONT_ID;
      spec.titleFontId = UI_12_FONT_ID;
      break;
    case CrossPointSettings::UI_SCALE_LARGE:
#ifdef OMIT_FONTS
      // Slim builds only register the UI fonts; cap Large at the Medium tier.
      spec.smallFontId = UI_10_FONT_ID;
      spec.bodyFontId = UI_12_FONT_ID;
      spec.titleFontId = UI_12_FONT_ID;
#else
      spec.smallFontId = UI_12_FONT_ID;
      spec.bodyFontId = NOTOSANS_14_FONT_ID;
      spec.titleFontId = NOTOSANS_16_FONT_ID;
#endif
      break;
    case CrossPointSettings::UI_SCALE_MEDIUM:
    default:
      spec.smallFontId = UI_10_FONT_ID;
      spec.bodyFontId = UI_12_FONT_ID;
#ifdef OMIT_FONTS
      spec.titleFontId = UI_12_FONT_ID;
#else
      spec.titleFontId = NOTOSANS_14_FONT_ID;
#endif
      break;
  }
  return spec;
}

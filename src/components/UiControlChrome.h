#pragma once
#include <components/controls/header.h>

// The core theme's control shapes, kept here rather than inline in
// uiThemeTokens() so a host test can drive the REAL styles instead of a
// hand-copied fixture that agrees with whatever it was copied from.
// Freestanding: FreeInkUI only, no UITheme, no BoardConfig, no renderer.

// Left unset, a theme's button falls through to defaultButtonStyles(), which
// paints a white background, black text and NO border -- on a white screen
// that is a label, not a control. Only selected and active carry any fill, and
// a finger never selects anything, so a touch reader saw no button at all.
// Toybox sets its own tokens.button for the same reason; this is that fix at
// the core theme's seam, outlined rather than inverted so it matches the
// hairline list rows this theme already draws.
inline freeink::ui::StyleSet uiButtonStyles() {
  namespace fui = freeink::ui;
  fui::StyleSet button = fui::defaultButtonStyles();
  for (fui::BoxStyle* box : {&button.normal, &button.focused, &button.disabled}) {
    box->border = fui::Paint::solid(fui::Color::Black);
    box->borderWidth = 1;
  }
  button.disabled.border = fui::Paint::dither(fui::Color::LightGray);
  for (fui::BoxStyle* box : {&button.normal, &button.selected, &button.focused, &button.active, &button.disabled}) {
    box->radius = 4;
  }
  return button;
}

// A header's action icons are chrome, not buttons.
//
// Screen::header() hands the LEADING action `theme_.button` when the caller
// leaves leadingStyles unset, and plainStyles() to the trailing one. Those are
// the same thing on screen -- an icon sitting on the band -- so an outlined
// tokens.button boxes one end and not the other, and plainStyles() carries no
// fill in ANY state, so the other end acknowledges a tap with nothing at all.
// Both ends take defaultButtonStyles() here: no box, and the same tap flash.
//
// Call it on every HeaderProps before handing it to Screen::header(). Ends
// with no icon are unaffected -- header() only styles an action it draws.
inline void applyHeaderActionChrome(freeink::ui::HeaderProps& header) {
  header.leadingStyles = freeink::ui::defaultButtonStyles();
  header.trailingStyles = freeink::ui::defaultButtonStyles();
}

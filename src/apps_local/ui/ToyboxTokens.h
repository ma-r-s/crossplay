#pragma once

// Toybox expressed as FreeInkUI theme tokens.
//
// Freestanding on purpose: FreeInkUI plus plain constants, no GfxRenderer and
// no Arduino. Screen builders include this, so they compile in a host test as
// readily as on the device. The renderer-side half (binding real fonts to the
// three slots) lives in ToyboxTheme.h.
//
// Toybox.h holds the drawing vocabulary; this header is the binding that lets
// FreeInkUI's components draw in it. The split follows the SDK's own advice:
// the SDK owns the in-memory types, the app owns what goes in them, and new
// styling is expressed as tokens rather than as another hand-rolled helper.

#include <FreeInkUI.h>

#include <cstdint>

#include "ToyboxMetrics.h"

namespace toybox {

namespace fui = freeink::ui;

// Solid black, knocked-out label. The header never repaints, so by the ink
// budget rule its black is free.
inline fui::StyleSet invertedStyles() {
  fui::StyleSet styles;
  styles.explicitlySet = true;
  styles.normal.background = fui::Paint::solid(fui::Color::Black);
  styles.normal.foreground = fui::Paint::solid(fui::Color::White);
  styles.selected = styles.normal;
  styles.focused = styles.normal;
  styles.active = styles.normal;
  styles.disabled = styles.normal;
  return styles;
}

// A settings row: hairline outline at rest, inverted when selected. Same two
// states menuRow() drew by hand.
inline fui::StyleSet rowStyles() {
  fui::StyleSet styles;
  styles.explicitlySet = true;
  styles.normal.background = fui::Paint::solid(fui::Color::White);
  styles.normal.foreground = fui::Paint::solid(fui::Color::Black);
  styles.normal.border = fui::Paint::solid(fui::Color::Black);
  styles.normal.borderWidth = kHairline;
  styles.selected.background = fui::Paint::solid(fui::Color::Black);
  styles.selected.foreground = fui::Paint::solid(fui::Color::White);
  // Focus is what the buttons move and touch does not, so it has to look like
  // selection rather than like a third state: this screen only ever has one
  // current row however you got to it.
  styles.focused = styles.selected;
  styles.active = styles.selected;
  styles.disabled = styles.normal;
  styles.disabled.foreground = fui::Paint::dither(fui::Color::DarkGray);
  return styles;
}

// Font slots by name. Screens should not spell the slot numbers.
constexpr fui::FontId kSmallFont = fui::FONT_SLOT_SMALL;
constexpr fui::FontId kUiFont = fui::FONT_SLOT_BODY;
// Same slot, named for what a tile uses it as: the step down when a word will
// not fit at the full size.
constexpr fui::FontId kBodyFont = fui::FONT_SLOT_BODY;
constexpr fui::FontId kDisplayFont = fui::FONT_SLOT_TITLE;
// The tile cut lives in the SMALL slot: the adapter has exactly three, and
// nothing else in this fork needs a distinct small face. Note that the theme's
// smallText role is deliberately NOT this slot (see themeTokens) -- a list's
// right-hand value is a label, not tile art, and must stay at UI size.
constexpr fui::FontId kTileFont = fui::FONT_SLOT_SMALL;

// A button label, for an app whose body slot carries something other than
// Jersey. A button is the device speaking rather than the app, so it takes the
// small slot -- which readingChromeFaces() binds to the UI cut for exactly this.
//
// The alignment is named because it has to be: FONT_SLOT_SMALL is 0, and
// textStyleUnset() reads a style whose font is 0 and whose every other field is
// default as unset, so Screen would helpfully substitute the theme's body style
// and the button would come back in the wrong face.
inline fui::TextStyle buttonText(const fui::ThemeTokens& tokens) {
  fui::TextStyle text = tokens.bodyText;
  text.font = fui::FONT_SLOT_SMALL;
  text.align = fui::TextAlign::Center;
  return text;
}

// Vertical air between two stacked blocks that are not rows: half a gutter, so
// it reads as a join rather than as a new section.
constexpr int spaceBetween = kGutter / 2;

// An action that is present but cannot be used right now. Dithered rather than
// hidden: a control that vanishes moves everything next to it, and on e-ink
// that costs a repaint of the whole bar.
inline fui::StyleSet disabledButtonStyles() {
  fui::StyleSet styles = invertedStyles();
  styles.normal.background = fui::Paint::dither(fui::Color::DarkGray);
  styles.selected = styles.normal;
  styles.focused = styles.normal;
  styles.active = styles.normal;
  styles.disabled = styles.normal;
  return styles;
}

// A stepper arrow with nothing left to step to. Outline and glyph both dithered
// rather than solid, so it reads as a control that is spent rather than one that
// is broken -- and rather than vanishing, which on a 1-bit screen is hard to
// tell from a drawing bug.
inline fui::StyleSet disabledStepperStyles() {
  fui::StyleSet styles;
  styles.explicitlySet = true;
  // The DITHER HAS TO BE IN THE FILL. GfxRendererTarget::text() decides ink with
  // `style.color != Color::White`, so every non-white text colour draws solid
  // black -- there is no grey type on this renderer, and dimming a glyph is not
  // a thing that can work. A dithered background is how SUBMIT greys out, and it
  // is the only mechanism that actually reads as spent.
  styles.normal.background = fui::Paint::dither(fui::Color::LightGray);
  styles.normal.foreground = fui::Paint::solid(fui::Color::Black);
  styles.normal.border = fui::Paint::dither(fui::Color::DarkGray);
  styles.normal.borderWidth = kHairline;
  styles.selected = styles.normal;
  styles.focused = styles.normal;
  styles.active = styles.normal;
  styles.disabled = styles.normal;
  return styles;
}

inline fui::ThemeTokens themeTokens() {
  fui::ThemeTokens tokens = fui::defaultThemeTokens(fui::FONT_SLOT_SMALL, fui::FONT_SLOT_BODY, fui::FONT_SLOT_TITLE);

  tokens.spaceXs = 4;
  // Screen::takeRow() uses spaceSm as the gap between rows, so it has to be
  // the row gap, not a generic small space.
  tokens.spaceSm = 4;
  tokens.spaceMd = kGutter;
  tokens.spaceLg = kMargin;
  tokens.rowHeight = kRowHeight;
  tokens.headerHeight = kHeaderHeight;
  tokens.footerHeight = kPillHeight;
  tokens.minTouchSize = 44;

  tokens.listRowGap = 4;
  tokens.listSidePadding = kGutter;
  // Zero, not kMargin: the screen's content rect already carries the page
  // margin, and listInset is applied on top of it. Setting both indents the
  // rows twice.
  tokens.listInset = 0;
  tokens.listSelectionStyle = fui::SelectionStyle::InvertFill;

  tokens.headerSidePadding = kMargin;
  // Toybox draws its own rule under the header, offset from the band, so the
  // component must not draw one flush against it.
  tokens.headerUnderline = 0;
  tokens.headerTitleAlign = fui::TextAlign::Left;

  tokens.titleText.font = fui::FONT_SLOT_TITLE;
  // The display cut is only ever set on the header band, and that band is
  // always solid black, so the title colour belongs to the token rather than to
  // each caller. `header()` uses the title style as given instead of resolving
  // it against the band's foreground, so leaving this at its Black default
  // paints the title black on black and it simply disappears.
  tokens.titleText.color = fui::Color::White;
  tokens.bodyText.font = fui::FONT_SLOT_BODY;
  // UI cut, not the small slot: that slot holds the Connections tile face, and
  // list values would shrink to 13px if this followed it.
  tokens.smallText.font = kUiFont;

  tokens.listRow = rowStyles();
  tokens.button = invertedStyles();
  // Screen::header() takes the band's StyleSet from the popup slot, which is
  // the SDK's convention for "chrome surface" rather than an oversight.
  tokens.popup = invertedStyles();
  return tokens;
}

}  // namespace toybox

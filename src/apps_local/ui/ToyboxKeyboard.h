#pragma once

// The search keyboard, one implementation for every app that types into a
// field: Wikipedia's search and Weather's city. The fork's built-in layouts in
// the theme's key metrics, GO for Enter, and NO word on Shift: the letter pages
// draw the keyboard's own arrow in every theme and language, and the symbol
// pages their own "#+=" and "123", which say which page you are on. A "Shift"
// passed in fitted English at no key gap only, filling the key edge to edge in
// Classic, and gave way to the arrow wherever it did not, so one key read three
// ways (card #651's review; Weather's copy had the same, card #763's second).
//
// The keyboard registers more hit rects than a toybox screen holds, so it has a
// table of its own, which the activity routes before the screen's.

#include <FreeInkUI.h>

class GfxRenderer;

namespace toybox {

constexpr int kSearchKeyboardKeys = 56;
using SearchKeyboardTable = freeink::ui::InteractionBuffer<kSearchKeyboardKeys>;

// Where the keyboard sits for `layout`: the theme's width, centred, eight
// pixels above the panel's foot.
freeink::ui::Rect searchKeyboardRect(const GfxRenderer& renderer, const freeink::ui::KeyboardLayout& layout);

// Draws `layout` there and publishes `table`, every key reporting `keyAction`
// with the key as its value. `symbols` says which page the mode key returns to.
void drawSearchKeyboard(const GfxRenderer& renderer, const freeink::ui::KeyboardLayout& layout, bool symbols,
                        freeink::ui::ActionId keyAction, SearchKeyboardTable& table);

}  // namespace toybox

#pragma once

// The pad, and the one piece of geometry every layer asks.
//
// Freestanding on purpose. The rects here are what the activity DRAWS and what
// its taps are RESOLVED AGAINST -- one function, called twice, never two
// functions that agree today. Three separate bugs in this fork came from
// computing a hit rect a second time; see docs/building-apps.md.

#include <cstdint>

#include "CalcKeys.h"

namespace calc {

// What the display can ever be asked to draw.
//
// Not a limit imposed on the display: a limit imposed on the ENGINE, which is
// A limit imposed on the ENGINE, because that is the only place it can be
// guaranteed: a display cannot promise "nothing is drawn past its box" by being
// careful about what it draws. Sixteen characters is what the smallest of the
// four number cuts clears on this panel -- sixteen times Jersey 26's widest
// glyph is 432px of the 448 an app owns -- so a result that obeys this can never
// be drawn past its box, whatever it is.
//
// Sixteen rather than twelve because of the EXPONENT form: a sign, a digit, a
// point, nine more digits and e-99 is sixteen characters, and at twelve the
// mantissa was cut to seven digits -- 9999999999 x 9999999999 came out as
// "1e+20", arithmetically true and useless to read. Ten significant digits (the
// engine's, and a normal pocket calculator's) needs twelve characters in FIXED
// form and sixteen in exponent form.
constexpr int kMaxDisplayChars = 16;

struct Layout {
  uint8_t cols = 0;
  uint8_t rows = 0;
  const KeyDef* keys = nullptr;  // exactly cols * rows entries, row-major
};

struct Rect16 {
  int16_t x = 0, y = 0, w = 0, h = 0;
  int16_t right() const { return static_cast<int16_t>(x + w); }
  int16_t bottom() const { return static_cast<int16_t>(y + h); }
};

struct PadGeom {
  Rect16 display{};
  Rect16 grid{};
  int16_t cellW = 0;
  int16_t cellH = 0;
  int16_t gap = 0;
};

// `body` is the activity's content rect: below the chrome, inside the margins.
// `gap` and `displayH` come from CalcStyle.h, which derives the height from the
// cut metrics rather than picking it.
inline PadGeom padGeom(const Layout& layout, const Rect16& body, const int16_t gap, const int16_t displayH) {
  PadGeom g{};
  g.gap = gap;
  g.display = Rect16{body.x, body.y, body.w, displayH};
  // A lattice pad has no gutter under its display either: the rule that closes
  // the display IS the grid's top line.
  const int16_t below = static_cast<int16_t>(body.y + displayH + (gap > 0 ? gap : 0));
  g.grid = Rect16{body.x, below, body.w, static_cast<int16_t>(body.bottom() - below)};
  if (layout.cols == 0 || layout.rows == 0) return g;
  g.cellW = static_cast<int16_t>((g.grid.w - (layout.cols - 1) * gap) / layout.cols);
  g.cellH = static_cast<int16_t>((g.grid.h - (layout.rows - 1) * gap) / layout.rows);
  return g;
}

// The rect of the key at `index`. Every key is one cell: the pad is uniform so
// the grid is uniform, which is also why the plus-minus sits in the corner where
// the iPhone puts a double-width zero.
inline Rect16 keyRect(const Layout& layout, const PadGeom& g, const int index) {
  const int col = index % layout.cols;
  const int row = index / layout.cols;
  return Rect16{static_cast<int16_t>(g.grid.x + col * (g.cellW + g.gap)),
                static_cast<int16_t>(g.grid.y + row * (g.cellH + g.gap)), g.cellW, g.cellH};
}

// Which key a tap landed on, or -1. Derived from keyRect() and nothing else, so
// a layout change cannot move the pixels without moving the hit box with them.
// Where there IS a gap it refuses rather than rounding into a neighbour: on a panel
// this slow, a tap that did the wrong thing costs more than one that did
// nothing.
inline int keyAt(const Layout& layout, const PadGeom& g, const int x, const int y) {
  const int cells = layout.cols * layout.rows;
  for (int i = 0; i < cells; ++i) {
    if (layout.keys[i].key == Key::None) continue;
    const Rect16 r = keyRect(layout, g, i);
    if (x >= r.x && x < r.right() && y >= r.y && y < r.bottom()) return i;
  }
  return -1;
}

// ---------------------------------------------------------------------------
// The pad. The arrangement is the iPhone's, which is the pad the most hands
// already know, with two changes that every desk calculator also makes: DEL instead of a swipe to lose a digit (Casio
// spells it exactly that way), and a plus-minus key in the corner rather than a double-width zero, so the grid is
// uniform.
//
// Every label is ASCII or a codepoint the calculator's own cuts carry. NOTHING
// is drawn from primitives: a hand-drawn division sign is why these keys looked
// homemade, and Jersey 25 has had a real one all along -- it was the Toybox
// SUBSET, U+0020-007E, that dropped it. See tools_local/toybox/gen_calc_fonts.sh.
// ---------------------------------------------------------------------------

// The multiplication, division and minus signs, as UTF-8. Written as escapes
// rather than literal bytes because the fork's sources are ASCII by rule.
constexpr const char* kMul = "\xC3\x97";        // U+00D7
constexpr const char* kDiv = "\xC3\xB7";        // U+00F7
constexpr const char* kMinus = "\xE2\x88\x92";  // U+2212, not a hyphen
constexpr const char* kPlusMinus = "\xC2\xB1";  // U+00B1, absent from Jersey

constexpr KeyDef kPadKeys[] = {
    {Key::ClearAll, "AC"},  {Key::Backspace, "DEL"}, {Key::Percent, "%"}, {Key::Div, kDiv, true},
    {Key::D7, "7"},         {Key::D8, "8"},          {Key::D9, "9"},      {Key::Mul, kMul, true},
    {Key::D4, "4"},         {Key::D5, "5"},          {Key::D6, "6"},      {Key::Sub, kMinus, true},
    {Key::D1, "1"},         {Key::D2, "2"},          {Key::D3, "3"},      {Key::Add, "+", true},
    {Key::Negate, nullptr}, {Key::D0, "0"},          {Key::Dot, "."},     {Key::Equals, "=", true},
};
constexpr Layout kPad{4, 5, kPadKeys};

static_assert(sizeof(kPadKeys) / sizeof(kPadKeys[0]) == 4u * 5u, "the pad table must fill its grid exactly");

}  // namespace calc

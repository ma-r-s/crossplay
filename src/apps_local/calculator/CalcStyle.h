#pragma once

// TOYBOX: the look Mario chose on 2026-09-15, out of five rendered side by side.
//
// The other four (INSTRUMENT, NIGHT, SWISS, LEDGER) and the Skin struct that
// carried them are gone, along with the Ubuntu and Noto Serif cuts only they
// used -- a variant macro that survives a decision is a second design nobody
// maintains, and those faces were 450KB of flash for a look nothing draws.
//
// Two rules from that pass that ARE settled and stay:
//
//   NO ROUNDED CORNERS. Mario's call, twice.
//
//   NO UNEXPLAINED SPACE. Every vertical band is DERIVED from a cut metric, not
//   picked. The first pass guessed a display height, pinned the number to its
//   bottom, and the remainder came out as a band of empty panel over every
//   result. displayHeight() below is the pending line plus the number's band
//   plus the rule, and nothing is left over.

#include <cstdint>

#include "CalcCutMetrics.h"
#include "CalcLayout.h"

namespace calc {

// Chrome. The fork's black header band, which is what every other app on this
// shelf wears.
constexpr int16_t kChromeHeight = 76;
constexpr int16_t kMargin = 16;
constexpr int16_t kKeyGapPx = 12;

// Key borders. The weights have to differ enough to READ as different: at equal
// weight the operator column and the digits compete and neither wins.
constexpr int16_t kDigitStroke = 2;
constexpr int16_t kOperatorStroke = 6;

// The air above and below the number inside its own band.
constexpr int16_t kNumberAir = 10;
constexpr int16_t kRule = 3;

// Jersey 25 has no U+00B1, and a glyph the face lacks draws as a HOLE rather
// than a box -- so the sign key says what a keyboard-era calculator says.
constexpr const char* kPlusMinusLabel = "+/-";

// What the display is TALL, derived from what it holds and nothing else.
constexpr int16_t displayHeight() {
  return static_cast<int16_t>(kSmallCut.lineHeight + kNumberCut.capHeight + 2 * kNumberAir + kRule);
}

// Every layout question answers from here, so "where does the pad start" has one
// answer. The gutter under the chrome is the small cut's own line height rather
// than a round number.
inline Rect16 bodyRect(const int screenW, const int screenH) {
  const int16_t top = static_cast<int16_t>(kChromeHeight + kSmallCut.lineHeight / 2);
  return Rect16{kMargin, top, static_cast<int16_t>(screenW - 2 * kMargin),
                static_cast<int16_t>(screenH - kMargin - top)};
}

inline PadGeom padGeometry(const int screenW, const int screenH) {
  return padGeom(kPad, bodyRect(screenW, screenH), kKeyGapPx, displayHeight());
}

inline const char* labelFor(const KeyDef& key) { return key.label ? key.label : kPlusMinusLabel; }

// Which cut a label is drawn in. Structural rather than measured, so the host
// gate resolves the same face the panel will: one or two CHARACTERS is a digit,
// an operator or a two-letter word, and anything longer is a word key.
//
// CHARACTERS, counted by skipping UTF-8 continuation bytes -- not bytes. The
// first version counted bytes and claimed in its own comment that "the math
// signs are two bytes of UTF-8 each", which is true of U+00D7 and U+00F7 and
// false of U+2212, the minus sign, which is three. So the minus alone dropped to
// the small cut and was drawn 17x4 pixels of ink against the plus at 24x23 --
// visibly, in the same column, on a key the same size. label_fit.py could not
// see it: the label FITTED, it was just the wrong size.
inline int labelFontFor(const char* label) {
  int chars = 0;
  for (const char* p = label; *p && chars < 3; ++p) {
    if ((static_cast<unsigned char>(*p) & 0xC0) != 0x80) ++chars;
  }
  return chars > 2 ? kSmallFontId : kLabelFontId;
}

}  // namespace calc

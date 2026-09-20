#pragma once

// The calculator's six Jersey 25 cuts, and the two measurements drawing needs.
//
// New files rather than wider versions of the Toybox cuts, and that is not
// tidiness: tools_local/toybox/gen_toybox_fonts.sh spells out that regenerating
// toybox_20 or _30 today moves every glyph a pixel, because the current freetype
// is not the one that produced the committed headers, and ToyboxTokens.h's
// centring constants are solved against exactly those ink heights. A cut nothing
// else uses cannot shift anybody's text.
//
// What they carry that no Toybox cut can: U+00D7, U+00F7 and U+2212. The Toybox
// cuts are subset to U+0020-007E, so those three draw as NOTHING there -- a
// glyph the face lacks is a hole, not a box.

#include <GfxRenderer.h>

#include "CalcFontIds.h"

namespace calc {

struct Metrics {
  int ascender = 0;
  int capTop = 0;
  int capHeight = 0;
  int lineHeight = 0;
};

// Call from onEnter() before drawing. Idempotent.
void ensureCalcFonts(GfxRenderer& renderer);

// Understands the calculator's ids AND Toybox's, because the header band is
// drawn in a Toybox cut whatever the pad is set in.
Metrics metricsFor(int fontId);

// Capital ink centred in [boxY, boxY + boxH), the way a typesetter centres it.
// getTextHeight() reports the ASCENDER and drawText() takes the top of the
// ascender box, so centring on either puts the letters visibly low.
void drawCapsCentered(const GfxRenderer& renderer, int fontId, int x, int boxY, int boxH, const char* text, bool black);

// The same, centred horizontally in a box as well. Every key label wants this,
// and every one of them computing it again is how two labels come to disagree.
void drawCapsCenteredIn(const GfxRenderer& renderer, int fontId, int boxX, int boxW, int boxY, int boxH,
                        const char* text, bool black);

}  // namespace calc

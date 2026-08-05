#pragma once

// Toybox's own typefaces, registered from our own code so the design language
// costs zero upstream surface. See ToyboxFonts.cpp for why the bit depth
// matters more than the choice of face.

#include <GfxRenderer.h>

namespace toybox {

// Arbitrary ids that cannot collide with fontIds.h, which are FNV-ish hashes of
// the generated font names.
constexpr int kDisplayFontId = 0x70B0'0001;
constexpr int kUiFontId = 0x70B0'0002;
// A third, much smaller cut, for the one place a label cannot be resized to
// fit its box: a Connections tile, where the word is the content. 96% of the
// published archive's 18255 words fit one line at this size in a 105px tile,
// and the 1.4% that do not are broken across lines rather than shrunk again.
constexpr int kTileFontId = 0x70B0'0003;
// Instrument Serif: elegant and curvy, and condensed enough that a long word
// still fits a 111px tile at a 20px cap -- where the wide curvy faces (Lora,
// Fraunces, Young Serif) never fit at any size the tile can take.
constexpr int kSerifTileFontId = 0x70B0'0004;
constexpr int kSerifTitleFontId = 0x70B0'0005;
// The step down a tile takes when a word will not fit at the full size.
constexpr int kSerifSmallFontId = 0x70B0'0006;

// Call from an activity's onEnter() before drawing. Idempotent and cheap.
void ensureFonts(GfxRenderer& renderer);

// Vertical metrics of the actual ink, which is not what GfxRenderer reports.
// getTextHeight() returns the ASCENDER, and drawText() takes y as the top of the
// ascender box. Centring on that centres the box, not the letters: capital ink
// sits at the bottom of the ascender box, so the text visibly hangs low in every
// bar and capsule. Centring on cap height is what typesetters actually do.
struct FontMetrics {
  int ascender = 0;   // baseline = drawText y + ascender
  int capTop = 0;     // baseline up to the top of a capital
  int capHeight = 0;  // ink height of a capital
};

FontMetrics metricsFor(int fontId);

// Draws `text` with its capital ink vertically centred in [boxY, boxY + boxH).
// Returns the x it started at, so callers can chain.
int drawCapsCentered(const GfxRenderer& renderer, int fontId, int x, int boxY, int boxH, const char* text, bool black);

}  // namespace toybox

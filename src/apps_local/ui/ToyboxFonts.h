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

// A reading cut, for an app whose own surface is a page of prose rather than a
// board. The UI cut is 20px, which is right for a row you glance at and wrong
// for a screenful you actually read: at 20px a 480px panel holds about 28
// characters a line, so an article turns into forty page taps and a story
// headline cannot finish. 14px roughly halves both.
//
// Same face, one step down. Weight on this device comes from size and from
// inversion, so a second size is also the only new emphasis available.
constexpr int kReadingFontId = 0x70B0'0007;

// The same reading face, bold, for a header band that carries a *title* rather
// than a word. "ARTICLE" fits the display cut; "In Memory of My Wife, Elise
// Cawley, with Thanks for 36 Wonderful Years" does not, at any size that band
// can hold. Smaller and bold is what a headline looks like when it has to be
// read rather than shouted.
constexpr int kReadingBoldFontId = 0x70B0'0008;

// Jersey at button size. A button is the device speaking rather than the app,
// so it keeps Jersey even on a screen whose text does not -- but the UI cut is
// wide enough that "COMMENTS" clipped to "COMMEN" inside half a footer. Same
// face, small enough to fit a word.
constexpr int kButtonFontId = 0x70B0'0009;

// Smaller cuts of the same two reading faces, so a headline that will not fit
// can step down a size instead of being cut. The design language's rule, which
// this app was breaking everywhere: pick the largest cut it fits in, walking
// the available cuts down, and only break a word when the smallest still
// overflows.
constexpr int kReadingSmallFontId = 0x70B0'000A;
constexpr int kReadingBoldSmallFontId = 0x70B0'000B;

// Two cuts above the display cut, for a screen whose whole content is one word
// read from the other side of a room.
//
// The 30px display cut puts a 38px capital on the panel: 4.4mm at 220ppi, which
// is right for a header somebody is holding and useless at three metres. These
// are 57px and 82px of capital, or 6.6mm and 9.5mm -- the larger one is bigger
// than the word on a phone playing the same game, which is the benchmark that
// settled the sizes.
//
// Both, not one, because the ladder needs a rung: the longest single word in
// the word lists is fifteen characters, which does not fit the panel at the
// huge cut at any line count, and does fit at the large one.
constexpr int kHugeFontId = 0x70B0'000C;
constexpr int kLargeFontId = 0x70B0'000D;

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

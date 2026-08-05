#include "ToyboxFonts.h"

#include <EpdFont.h>
#include <EpdFontFamily.h>

#include "fonts/instrument_10.h"
#include "fonts/instrument_13.h"
#include "fonts/instrument_24.h"
#include "fonts/toybox_10.h"
#include "fonts/toybox_20.h"
#include "fonts/toybox_30.h"

namespace toybox {
namespace {

// Jersey 25 (SIL OFL, Google Fonts), subset to ASCII and converted in 1-BIT
// mode. The bit depth is the whole point. Our stock fonts are built with
// --2bit, and GfxRenderer's BW path paints a pixel for ANY coverage above zero
// (`bmpVal < 3`), so every antialiased edge floods to solid black: stems fatten,
// counters close, and the type turns to mush. A pixel font converted at 1 bit
// has no coverage to flood. Every glyph pixel is deliberate.
//
// Jersey is also condensed, which matters on a 480px-wide screen where a normal
// grotesque runs out of room.
//
// One face, no bold: at these sizes a synthesised bold would just be the same
// flooding by another route. Weight comes from size and from inversion instead.
EpdFont display30(&toybox_30);
EpdFont ui20(&toybox_20);
EpdFont tile10(&toybox_10);
EpdFont instrument10(&instrument_10);
EpdFont instrument13(&instrument_13);
EpdFont instrument24(&instrument_24);
EpdFontFamily displayFamily(&display30);
EpdFontFamily uiFamily(&ui20);
EpdFontFamily tileFamily(&tile10);
EpdFontFamily serifTileFamily(&instrument13);
EpdFontFamily serifSmallFamily(&instrument10);
EpdFontFamily serifTitleFamily(&instrument24);

bool registered = false;

}  // namespace

FontMetrics metricsFor(const int fontId) {
  const EpdFontFamily& family =
      fontId == kDisplayFontId ? displayFamily : (fontId == kTileFontId ? tileFamily : uiFamily);
  const EpdFontData* data = fontId == kDisplayFontId ? &toybox_30 : (fontId == kTileFontId ? &toybox_10 : &toybox_20);
  FontMetrics metrics;
  metrics.ascender = data->ascender;
  // 'H' stands in for cap height: a flat-topped capital with no overshoot, so it
  // measures the band the eye actually aligns to.
  if (const EpdGlyph* glyph = family.getGlyph('H'); glyph != nullptr) {
    metrics.capTop = glyph->top;
    metrics.capHeight = glyph->height;
  }
  return metrics;
}

int drawCapsCentered(const GfxRenderer& renderer, const int fontId, const int x, const int boxY, const int boxH,
                     const char* text, const bool black) {
  const FontMetrics metrics = metricsFor(fontId);
  // Ink top on screen is (y + ascender) - capTop. Solve for the y that puts ink
  // top at the box's centred position.
  const int y = boxY + (boxH - metrics.capHeight) / 2 - metrics.ascender + metrics.capTop;
  renderer.drawText(fontId, x, y, text, black);
  return x;
}

void ensureFonts(GfxRenderer& renderer) {
  if (registered) return;
  // Registered from here rather than main.cpp so the design language costs zero
  // upstream surface. insertFont is idempotent per id, but the guard keeps this
  // free to call from every activity's onEnter().
  renderer.insertFont(kDisplayFontId, displayFamily);
  renderer.insertFont(kUiFontId, uiFamily);
  renderer.insertFont(kTileFontId, tileFamily);
  renderer.insertFont(kSerifTileFontId, serifTileFamily);
  renderer.insertFont(kSerifSmallFontId, serifSmallFamily);
  renderer.insertFont(kSerifTitleFontId, serifTitleFamily);
  registered = true;
}

}  // namespace toybox

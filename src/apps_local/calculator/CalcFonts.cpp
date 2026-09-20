#include "CalcFonts.h"

#include "../ui/ToyboxFonts.h"
#include "fonts/calc_jersey_20.h"
#include "fonts/calc_jersey_26.h"
#include "fonts/calc_jersey_28.h"
#include "fonts/calc_jersey_34.h"
#include "fonts/calc_jersey_44.h"
#include "fonts/calc_jersey_56.h"

namespace calc {
namespace {

// All five converted at 1 BIT, like the Toybox cuts and for the same reason:
// GfxRenderer's BW path paints a pixel for ANY coverage above zero, so an
// antialiased cut floods -- stems fatten, counters close, and the type turns to
// mush. A pixel-shaped face converted at one bit has no coverage to flood.
EpdFont label28(&calc_jersey_28);
EpdFont small20(&calc_jersey_20);
EpdFont number56(&calc_jersey_56);
EpdFont mid44(&calc_jersey_44);
EpdFont tiny34(&calc_jersey_34);
EpdFont finest26(&calc_jersey_26);

EpdFontFamily labelFamily(&label28);
EpdFontFamily smallFamily(&small20);
EpdFontFamily numberFamily(&number56);
EpdFontFamily midFamily(&mid44);
EpdFontFamily tinyFamily(&tiny34);
EpdFontFamily finestFamily(&finest26);

bool registered = false;

const EpdFontFamily* familyFor(const int fontId) {
  switch (fontId) {
    case kLabelFontId:
      return &labelFamily;
    case kSmallFontId:
      return &smallFamily;
    case kNumberFontId:
      return &numberFamily;
    case kMidFontId:
      return &midFamily;
    case kTinyFontId:
      return &tinyFamily;
    case kFinestFontId:
      return &finestFamily;
    default:
      return nullptr;
  }
}

const EpdFontData* dataFor(const int fontId) {
  switch (fontId) {
    case kLabelFontId:
      return &calc_jersey_28;
    case kSmallFontId:
      return &calc_jersey_20;
    case kNumberFontId:
      return &calc_jersey_56;
    case kMidFontId:
      return &calc_jersey_44;
    case kTinyFontId:
      return &calc_jersey_34;
    case kFinestFontId:
      return &calc_jersey_26;
    default:
      return nullptr;
  }
}

}  // namespace

void ensureCalcFonts(GfxRenderer& renderer) {
  if (registered) return;
  renderer.insertFont(kLabelFontId, labelFamily);
  renderer.insertFont(kSmallFontId, smallFamily);
  renderer.insertFont(kNumberFontId, numberFamily);
  renderer.insertFont(kMidFontId, midFamily);
  renderer.insertFont(kTinyFontId, tinyFamily);
  renderer.insertFont(kFinestFontId, finestFamily);
  registered = true;
}

Metrics metricsFor(const int fontId) {
  const EpdFontData* data = dataFor(fontId);
  const EpdFontFamily* family = familyFor(fontId);
  if (!data || !family) {
    // A Toybox id: hand it back to the owner of those cuts rather than keeping a
    // second table that can drift from it.
    const toybox::FontMetrics m = toybox::metricsFor(fontId);
    return Metrics{m.ascender, m.capTop, m.capHeight, m.ascender * 2};
  }
  Metrics metrics;
  metrics.ascender = data->ascender;
  metrics.lineHeight = data->advanceY;
  // '8' rather than 'H': the number cuts carry no letters at all, and a digit is
  // the flat-topped shape a display actually aligns to.
  const EpdGlyph* glyph = family->getGlyph('8');
  if (glyph == nullptr) glyph = family->getGlyph('H');
  if (glyph != nullptr) {
    metrics.capTop = glyph->top;
    metrics.capHeight = glyph->height;
  }
  return metrics;
}

void drawCapsCentered(const GfxRenderer& renderer, const int fontId, const int x, const int boxY, const int boxH,
                      const char* text, const bool black) {
  const Metrics m = metricsFor(fontId);
  // Ink top on screen is (y + ascender) - capTop. Solve for the y that puts ink
  // top at the box's centred position.
  const int y = boxY + (boxH - m.capHeight) / 2 - m.ascender + m.capTop;
  renderer.drawText(fontId, x, y, text, black);
}

void drawCapsCenteredIn(const GfxRenderer& renderer, const int fontId, const int boxX, const int boxW, const int boxY,
                        const int boxH, const char* text, const bool black) {
  const int w = renderer.getTextWidth(fontId, text);
  drawCapsCentered(renderer, fontId, boxX + (boxW - w) / 2, boxY, boxH, text, black);
}

}  // namespace calc

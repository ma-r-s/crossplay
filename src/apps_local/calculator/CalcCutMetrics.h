#pragma once

// The vertical facts about each cut, freestanding.
//
// Two numbers per cut, and only two, because only two are read: the display's
// height is derived from the small cut's line height and the number cut's cap
// height. A third, a "widest glyph" for counting a character budget, was carried
// here and consumed by nothing -- the budget is measured through the renderer's
// real advance widths instead, because Jersey's digits are not tabular and a
// count would step every number down a size it did not need.
//
// They are HERE, as constants, rather than read from the font at layout time,
// because the display's height is DERIVED from them -- it is the pending line
// plus the number's band plus the rule, and nothing else -- and a layout derived
// from a number the host suite cannot see is a layout the host suite cannot
// check. Toybox solves the same problem the same way in ToyboxTokens.h.
//
// They are not a second opinion: host-tests/calculator/label_fit.py checks every
// one against the real generated header, so a regenerated cut cannot silently
// move a band and take the difference out of the key rows.

#include "CalcFontIds.h"

namespace calc {

struct CutMetrics {
  int lineHeight = 0;  // EpdFontData::advanceY
  int capHeight = 0;   // ink height of '8', which is what the eye measures
};

constexpr CutMetrics kSmallCut{42, 26};
constexpr CutMetrics kLabelCut{58, 36};
constexpr CutMetrics kFinestCut{54, 33};
constexpr CutMetrics kTinyCut{71, 43};
constexpr CutMetrics kMidCut{92, 57};
constexpr CutMetrics kNumberCut{117, 71};

constexpr CutMetrics cutFor(const int fontId) {
  return fontId == kSmallFontId    ? kSmallCut
         : fontId == kLabelFontId  ? kLabelCut
         : fontId == kFinestFontId ? kFinestCut
         : fontId == kTinyFontId   ? kTinyCut
         : fontId == kMidFontId    ? kMidCut
                                   : kNumberCut;
}

constexpr int numberFontFor(const int rung) {
  return rung <= 0 ? kNumberFontId : (rung == 1 ? kMidFontId : (rung == 2 ? kTinyFontId : kFinestFontId));
}

}  // namespace calc

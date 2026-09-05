#pragma once

// Toybox's numbers, and nothing else.
//
// Split out from Toybox.h so that the theme tokens and the screen builders can
// use them without dragging in GfxRenderer. That is what keeps screens
// freestanding, and freestanding is what makes them host-testable.

#include <cstdint>

namespace toybox {

// Chrome. Chunkier than the reader's, because these are apps, not pages.
constexpr int kHeaderHeight = 76;
constexpr int kMargin = 16;
constexpr int kGutter = 12;

// Stroke weights. Playdate's guidance is a 2px minimum so strokes do not look
// wispy; at our 220ppi (versus their 173) the equivalent is thicker, and going
// heavier is also what keeps outlines legible against a dithered ground.
constexpr int kHairline = 1;
constexpr int kRule = 3;
// The white between the band's black and the rule under it, and the reason the
// rule is NOT carved out of kHeaderHeight. Shortening the band to make room
// was tried and cannot work: the title cut's line box is about 64px, so a band
// shorter than that trips the vertical clamp in the text layout, the title
// stops being centred and pins to the top -- which on the X4 Pro, whose glass
// already hides the top ten rows, reads as a header sitting visibly low. The
// rule therefore keeps the position it has always had, just below the band,
// where 27 of the fork's 44 band sites were already drawing it by hand.
constexpr int kBandRuleGap = 4;
// What a screen must actually clear: the band, the gap AND the rule. Screens
// measured their first gutter from kHeaderHeight alone, which is why several
// sit their content five pixels under a line they never counted -- the rule
// was invisible to the arithmetic because it was drawn by a separate call.
// Measure the top gutter from this, not from kHeaderHeight.
constexpr int kChromeHeight = kHeaderHeight + kBandRuleGap + kRule;
constexpr int kFrame = 4;
// The board's own border is heavier than anything drawn inside it, so the
// playing surface reads as a single object rather than as a grid that happens
// to have a line round it.
constexpr int kBoardFrame = 9;

// Row height for a settings list, and capsule geometry. Chrome is drawn by
// FreeInkUI components now; these are the numbers ToyboxTheme.h feeds them.
constexpr int kRowHeight = 62;
constexpr int kPillRadius = 20;
constexpr int kPillHeight = 52;

}  // namespace toybox

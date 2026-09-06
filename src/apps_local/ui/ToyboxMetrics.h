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

// The first row a screen's own content may occupy. Read the next paragraph
// before adding a safe area to it.
//
// EVERY NUMBER IN THIS FILE IS AN ABSOLUTE PANEL ROW, measured from the
// panel's physical row 0 and NOT from the bezel's safe top. toybox::headerBand()
// calls absoluteChrome() before it takes the band, so the band paints rows
// 0..kHeaderHeight and its rule lands at kChromeHeight whatever the glass
// hides; host-tests/ui pins it with testTheBandIsAbsoluteWithoutBeingAsked,
// which asserts screen.body().y == kHeaderHeight on a BEZELLED frame. The X4
// Pro's ten covered rows are therefore already inside the band's paint
// (docs/bezel-insets.md: paint may bleed under the bezel, ink may not), and a
// screen that adds DeviceContext::safeArea.top to a value derived from these
// pushes its body ten pixels below every other app's while protecting nothing.
// xkcd and Wallpapers both did, for as long as either app existed, and the
// comment over each private copy of this constant claimed the opposite. Card
// 358.
//
// The value is kGutter * 3 under the band, which is exactly what the 41
// screens that call insetContent({kGutter * 3, kMargin, kMargin, kMargin})
// after their chrome already get from screen.body(). Screens that lay their
// body out by hand -- the shelf, hacker news, instapaper, xkcd, wallpapers --
// use this so the hand-rolled ones and the component-laid ones land on one
// row. host-tests/ui asserts that they do, behind the glass, in
// testEveryAppsBodyStartsOnTheSameRow.
constexpr int kBodyTop = kHeaderHeight + kGutter * 3;
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

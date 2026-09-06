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
// What a screen must actually clear below a band of `bandHeight`: the band, the
// gap AND the rule. Screens measured their first gutter from kHeaderHeight
// alone, which is why several sat their content a few pixels under a line they
// never counted -- the rule was invisible to the arithmetic because it was
// drawn by a separate call.
//
// A function as well as a constant because the band height is a THEME TOKEN and
// one app raises it: Solitaire runs a 56px band in landscape, so kChromeHeight
// is not its chrome and a screen that reached for the constant anyway would be
// wrong by exactly the amount that is invisible. Screens holding a Screen& do
// not need either -- headerBand() reserves the whole chrome, so screen.body().y
// already is this number. These are for the geometry functions an Activity
// shares with its builder, which have no Screen to ask.
constexpr int chromeBelow(const int bandHeight) { return bandHeight + kBandRuleGap + kRule; }
constexpr int kChromeHeight = chromeBelow(kHeaderHeight);

// The top gutter every toybox screen puts between its chrome and its first row:
// the `kGutter * 3` that 41 screens hand to insetContent() after their chrome.
constexpr int kBodyGutter = kGutter * 3;

// The first row a screen's own content may occupy, for the geometry functions
// that have no Screen to ask. Read the next two paragraphs before using it.
//
// DERIVED FROM chromeBelow(), NOT RESTATED BESIDE IT. headerBand() reserves
// exactly chromeBelow(band), so a screen holding a Screen& already has this
// number in screen.body().y after it insets by kBodyGutter -- and the two must
// not be able to disagree. Card 248 moved the reservation from the band alone
// to band + gap + rule; a kBodyTop written as its own sum of kHeaderHeight and
// three gutters would have kept the OLD number silently while every
// component-laid screen moved seven pixels down. host-tests/ui pins the two
// paths together in testTheHandRolledBodyTopMatchesTheReservedOne, which
// renders a screen through headerBand() + insetContent() and asserts
// screen.body().y == kBodyTop.
//
// EVERY NUMBER IN THIS FILE IS AN ABSOLUTE PANEL ROW, measured from the panel's
// physical row 0 and NOT from the bezel's safe top. headerBand() calls
// absoluteChrome() before it takes the band, so the band paints from row 0
// whatever the glass hides; testTheBandIsAbsoluteWithoutBeingAsked pins that on
// a BEZELLED frame. The X4 Pro's ten covered rows are therefore already inside
// the band's paint (docs/bezel-insets.md: paint may bleed under the bezel, ink
// may not), and a screen that adds DeviceContext::safeArea.top to any of these
// pushes its body ten pixels below every other app's while protecting nothing.
// xkcd and Wallpapers both did, for as long as either app existed, and the
// comment over each private copy of this constant claimed the opposite. Card
// 358.
constexpr int bodyTopBelow(const int bandHeight) { return chromeBelow(bandHeight) + kBodyGutter; }
constexpr int kBodyTop = bodyTopBelow(kHeaderHeight);
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

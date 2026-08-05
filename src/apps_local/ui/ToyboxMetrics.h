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

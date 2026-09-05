#pragma once

// The Wallpapers screens. The chrome (header, page label, hints, empty state)
// is a freestanding builder in the XkcdScreens mould, so host-tests/ui/ can
// assert it. The grid of wallpaper thumbnails is the app's own surface: it is
// drawn by the Activity, because a thumbnail is a BMP decoded and scaled down
// off the SD card, which a freestanding screen cannot touch. What lives here
// for the grid is only its GEOMETRY -- the cell rectangles -- shared between the
// Activity's drawing and its hit-test so the two cannot disagree (the rule that
// has caught more bugs in this fork than any other).
//
// Portrait, 480x800. Two columns of thumbnails (Mario's ask), the wallpaper's
// own 480x800 aspect preserved in each cell. The wallpaper currently set as the
// sleep screen wears a thick border; a tap on any cell makes that one the sleep
// screen.

#include "../ui/ToyboxScreen.h"

// Three grid stylings, chosen at build time, rendered side by side, the winner
// kept and this macro deleted in the same commit (docs/building-apps.md):
//   1  BIG      -- 2x2 big thumbnails, no caption, a heavy border
//   2  DENSE    -- 2x3 smaller thumbnails with a file-name caption, thinner border
//   3  CAPTIONED -- 2x2 big thumbnails WITH a caption, the heaviest border
#ifndef WALLPAPERS_VARIANT
#define WALLPAPERS_VARIANT 1
#endif

namespace wallpapersui {

namespace fui = freeink::ui;

// The grid's geometry for the current variant, derived from the panel rather
// than guessed, so the drawing, the captions and the hit-test all read one set
// of rectangles.
struct GridGeom {
  int cols = 2;     // two columns, always (the whole point of the redesign)
  int rows = 2;     // rows per page
  int perPage = 4;  // cols * rows
  int16_t cellW = 0;
  int16_t cellH = 0;     // the thumbnail area only (caption sits below it)
  int16_t captionH = 0;  // 0 in variants without a caption
  int16_t borderW = 6;   // the thick border on the set wallpaper
  int16_t originX = 0;   // top-left of slot 0
  int16_t originY = 0;
  int16_t gapX = 0;
  int16_t gapY = 0;
  int16_t pageDotsY = 0;  // where the page-dot strip sits (when more than one page)
};

GridGeom gridGeom(const fui::DeviceContext& device);

// slot is 0..perPage-1, row-major. thumbRect is the image; cellRect includes
// the caption row; captionRect is empty in variants without captions.
fui::Rect thumbRect(const GridGeom& g, int slot);
fui::Rect cellRect(const GridGeom& g, int slot);
fui::Rect captionRect(const GridGeom& g, int slot);

// Which slot a tap at (x, y) lands on, or -1. Reads the same rectangles the
// Activity draws into.
int cellAt(const GridGeom& g, int x, int y);

// The chrome above and around the grid. rightLabel carries the count or the
// page ("PAGE 2 / 3"); when nothing is set yet the hint says so, because a grid
// with no border and no words is indistinguishable from one whose selection
// simply is not drawing (a-silent-screen-reads-as-a-crash).
struct GridChromeModel {
  const char* title = "WALLPAPERS";
  const char* rightLabel = nullptr;
  const char* warning = nullptr;  // free-space advisory, null when there is room
  bool hasActive = false;         // false -> draw the "tap one to set it" hint
};

void buildGridChrome(toybox::Screen& screen, const GridChromeModel& model);

// The empty state: no wallpapers on the card at all. Names the gap and how to
// fill it, so a fresh device does not look broken.
struct EmptyModel {
  const char* title = "WALLPAPERS";
  const char* warning = nullptr;
};

void buildEmpty(toybox::Screen& screen, const EmptyModel& model);

}  // namespace wallpapersui

#pragma once

// Placing text by its INK, cap top to baseline, rather than by its line box.
//
// A screen that stacks text by adding line heights gets gaps it did not
// choose: the visible gap between two lines is the code's offset plus the
// leading of BOTH faces, and Jersey, the reading serif and the small cuts all
// carry different leading. Equal offsets in the code therefore come out as
// unequal gaps on the panel. The Weather screen measured 47.5, 25.5, 19.5,
// 17.5, 14.5, 17.5 and 20.5 px between its sections that way, no two alike.
//
// The cure is to decide every gap between inks and only then work out where
// each line box has to start. Three pieces:
//
//   inkBand()   where a capital's ink sits inside a face's line box, from the
//               cut table in ToyboxTokens.h (the same numbers inkCentred uses);
//   inkRect()   the line box to hand text() so the cap top lands on a given y;
//   InkColumn   a column of blocks (text, icons, rules) each a chosen gap after
//               the ink above it, laid out as a whole: centred in a span, so the
//               space a screen does not need is placed rather than left over.
//
// iconInk() answers the same question for a 1-bpp icon, whose drawn shape sits
// inside its square with padding that differs from icon to icon.
//
// Freestanding like the rest of the toybox screen layer: FreeInkUI, the Icons
// struct and the tokens, nothing renderer-shaped.

#include <FreeInkUI.h>
#include <Icon.h>

#include <cstdint>

#include "ToyboxTokens.h"

namespace toybox {

struct InkBand {
  int16_t lineHeight = 0;
  int16_t top = 0;     // line-box top to cap top
  int16_t height = 0;  // cap top to baseline
};

// A face not in the cut table (a host test's fake) is answered with a fixed
// proportion, so a layout built on this still stacks consistently there.
inline InkBand inkBand(const freeink::ui::DrawTarget& target, const freeink::ui::FontId font) {
  InkBand band;
  band.lineHeight = target.lineHeight(font);
  if (const CutMetrics* cut = cutForLineHeight(band.lineHeight)) {
    band.top = static_cast<int16_t>(cut->ascender - cut->inkHeight);
    band.height = cut->inkHeight;
  } else {
    band.top = static_cast<int16_t>(band.lineHeight / 5);
    band.height = static_cast<int16_t>(band.lineHeight * 3 / 5);
  }
  return band;
}

// Cap top of the first line to the baseline of the last.
inline int16_t inkHeight(const freeink::ui::DrawTarget& target, const freeink::ui::FontId font, const int lines = 1) {
  const InkBand band = inkBand(target, font);
  return static_cast<int16_t>((lines > 1 ? (lines - 1) * band.lineHeight : 0) + band.height);
}

// The rect to hand text() so the cap top of its first line lands on `inkTop`:
// exactly `lines` line boxes tall, so the target's own centring is a no-op.
inline freeink::ui::Rect inkRect(const freeink::ui::DrawTarget& target, const freeink::ui::FontId font, const int16_t x,
                                 const int16_t inkTop, const int16_t width, const int lines = 1) {
  const InkBand band = inkBand(target, font);
  return freeink::ui::makeRect(x, static_cast<int16_t>(inkTop - band.top), width,
                               static_cast<int16_t>((lines > 0 ? lines : 1) * band.lineHeight));
}

// The drawn part of a 1-bpp icon (bit 0 is ink), relative to its own square.
inline freeink::ui::Rect iconInk(const freeink::Icon& icon) {
  const int stride = (icon.w + 7) / 8;
  int x0 = icon.w, y0 = icon.h, x1 = -1, y1 = -1;
  for (int y = 0; y < icon.h; ++y) {
    for (int x = 0; x < icon.w; ++x) {
      if ((icon.bits[y * stride + x / 8] >> (7 - x % 8)) & 1) continue;
      if (x < x0) x0 = x;
      if (x > x1) x1 = x;
      if (y < y0) y0 = y;
      if (y > y1) y1 = y;
    }
  }
  if (x1 < 0) return freeink::ui::makeRect(0, 0, 0, 0);
  return freeink::ui::makeRect(static_cast<int16_t>(x0), static_cast<int16_t>(y0), static_cast<int16_t>(x1 - x0 + 1),
                               static_cast<int16_t>(y1 - y0 + 1));
}

// A column of blocks placed by their ink. add() each block with the gap that
// separates its ink from the ink above it (ignored for the first), place the
// column, then read where each block's ink starts.
template <int N>
class InkColumn {
 public:
  // Returns the block's index, or -1 when the column is full.
  int add(const int16_t gapBefore, const int16_t inkHeight) {
    if (count_ >= N) return -1;
    gap_[count_] = count_ == 0 ? 0 : gapBefore;
    height_[count_] = inkHeight;
    return count_++;
  }

  // From the first ink top to the last ink bottom.
  int16_t height() const {
    int total = 0;
    for (int i = 0; i < count_; ++i) total += gap_[i] + height_[i];
    return static_cast<int16_t>(total);
  }

  // Puts the column's ink in the middle of [top, bottom): the space it does not
  // need goes equally above and below it. Never closer than `minEdge` to
  // either end; a column taller than the span starts at top + minEdge.
  void centre(const int16_t top, const int16_t bottom, const int16_t minEdge = 0) {
    const int spare = bottom - top - height();
    int above = spare / 2;
    if (above < minEdge) above = minEdge;
    start(static_cast<int16_t>(top + above));
  }

  void start(const int16_t top) {
    int y = top;
    for (int i = 0; i < count_; ++i) {
      y += gap_[i];
      top_[i] = static_cast<int16_t>(y);
      y += height_[i];
    }
  }

  int16_t top(const int index) const { return top_[index]; }
  int16_t bottom(const int index) const { return static_cast<int16_t>(top_[index] + height_[index]); }
  int size() const { return count_; }

 private:
  int16_t gap_[N] = {};
  int16_t height_[N] = {};
  int16_t top_[N] = {};
  int count_ = 0;
};

}  // namespace toybox

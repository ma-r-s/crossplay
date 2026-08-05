#pragma once

// The sentence photographs, read from images.dat.
//
// 290 of the 301 cards in Mario's deck carry one. They are not drawn under the
// answer, because measuring the answer side found a third of cards leave under
// 160px and the tightest leaves nine -- so the answer offers the photo and a
// tap gives it the whole screen. See tools_local/study/make_images.py.
//
// Freestanding C++17 over the same ByteSource as StudyDeck, and **it never
// allocates**. A full image is up to 448x620 at one bit per pixel, which is
// 34KB -- a heap block that size, taken and released once per card, is exactly
// the churn that fragments a device with no room to spare. So the caller draws
// it a band at a time through a small stack buffer: one seek per band rather
// than one per row, and a fixed cost whatever the picture.

#include <cstdint>

#include "StudyDeck.h"

namespace study {

// Sized from the format's own ceiling: 448px wide is 56 bytes a row, and
// sixteen rows a time turns 620 SD reads into 39 without putting a kilobyte on
// the stack.
inline constexpr int kMaxImageWidth = 448;
inline constexpr int kMaxImageHeight = 620;
inline constexpr int kImageRowBytes = (kMaxImageWidth + 7) / 8;
inline constexpr int kImageBandRows = 16;
inline constexpr int kImageBandBytes = kImageRowBytes * kImageBandRows;

struct ImageRef {
  uint32_t offset = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  uint16_t stride = 0;  // bytes per row, rows are byte-padded

  // A width of zero is how the file says "this card has no photograph"; the
  // offset is meaningless then and must not be followed.
  bool valid() const { return width > 0 && height > 0 && stride > 0; }
};

class StudyImages {
 public:
  // Parse the header. Returns false for a missing or malformed file, which is
  // not an error worth failing a deck over -- a deck simply has no photographs.
  bool open(ByteSource& images);

  bool ready() const { return count_ > 0; }
  int count() const { return count_; }

  // The entry for one card. `index` is the note's index, which is also its
  // index in cards.dat: make_images.py reads the order out of the deck itself
  // rather than re-deriving it, so the two cannot drift apart.
  ImageRef at(ByteSource& images, int index) const;

  // Read up to kImageBandRows rows starting at `firstRow` into `out`, which
  // must hold kImageBandBytes. Returns how many rows were read, 0 at the end.
  int readBand(ByteSource& images, const ImageRef& image, int firstRow, uint8_t* out) const;

 private:
  int count_ = 0;
  uint32_t size_ = 0;
};

}  // namespace study

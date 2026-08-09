// The pack builder's image converter: grayscale in, device-native 1-bit out.
//
// This exists as a C++ tool rather than as twenty lines of Python for one
// reason. The device converts newly downloaded comics itself, through
// PngToBmpConverter and lib/GfxRenderer/BitmapHelpers.cpp. If the host pack
// used its own dither, the archive on the card and the comics fetched over
// wifi would be rendered by two different algorithms -- visibly different on
// the same screen, with nothing in the UI to explain it, and drifting further
// apart every time either side was tuned. So the pack builder links the
// firmware's own ditherer, and the panel-boundary scan comes from the
// freestanding XkcdCore that the device also uses. Neither can drift, because
// in both cases there is only one implementation.
//
// It speaks a stream protocol so the whole archive costs one process rather
// than 3281 of them. All integers are little-endian:
//
//   in :  uint32 width, uint32 height, width*height bytes of 8-bit gray
//   out:  uint32 stride, then stride*height bytes of packed rows
//         (MSB first, bit set = ink)
//
// It emits no panel boundaries. The reader finds gaps in the artwork itself
// when it steps, over a 200-row window it reads off the card, so there is
// nothing to precompute and nothing stored that could go stale.
//
// stdin closing ends the run.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "BitmapHelpers.h"
#include "XkcdCore.h"

namespace {

bool readExactly(void* dst, size_t n) {
  auto* p = static_cast<uint8_t*>(dst);
  size_t got = 0;
  while (got < n) {
    const size_t r = std::fread(p + got, 1, n - got, stdin);
    if (r == 0) return false;
    got += r;
  }
  return true;
}

bool readU32(uint32_t& out) {
  uint8_t b[4];
  if (!readExactly(b, 4)) return false;
  out = static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) | (static_cast<uint32_t>(b[2]) << 16) |
        (static_cast<uint32_t>(b[3]) << 24);
  return true;
}

void writeU32(uint32_t v) {
  const uint8_t b[4] = {static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>((v >> 8) & 0xFF),
                        static_cast<uint8_t>((v >> 16) & 0xFF), static_cast<uint8_t>((v >> 24) & 0xFF)};
  std::fwrite(b, 1, 4, stdout);
}

}  // namespace

int main() {
  uint32_t width = 0;
  uint32_t height = 0;

  while (readU32(width)) {
    if (!readU32(height)) {
      std::fprintf(stderr, "convert: truncated header\n");
      return 1;
    }
    if (width == 0 || height == 0 || width > static_cast<uint32_t>(xkcd::kMaxArtWidth) ||
        height > static_cast<uint32_t>(xkcd::kMaxComicHeight)) {
      std::fprintf(stderr, "convert: refusing %ux%u (limits are %dx%d)\n", width, height, xkcd::kMaxArtWidth,
                   xkcd::kMaxComicHeight);
      return 1;
    }

    const int w = static_cast<int>(width);
    const int h = static_cast<int>(height);
    const int stride = (w + 7) / 8;

    std::vector<uint8_t> gray(static_cast<size_t>(w) * h);
    if (!readExactly(gray.data(), gray.size())) {
      std::fprintf(stderr, "convert: truncated pixel data for %dx%d\n", w, h);
      return 1;
    }

    std::vector<uint8_t> bits(static_cast<size_t>(stride) * h, 0);

    // The firmware's own ditherer, carrying its error between rows exactly as
    // it does on the device. It quantises to 1 for white and 0 for black; our
    // format and toybox::blit1bpp both use a set bit to mean ink, so the sense
    // is inverted here on purpose.
    Atkinson1BitDitherer ditherer(w);
    for (int y = 0; y < h; ++y) {
      const uint8_t* src = gray.data() + static_cast<size_t>(y) * w;
      uint8_t* row = bits.data() + static_cast<size_t>(y) * stride;
      for (int x = 0; x < w; ++x) {
        if (ditherer.processPixel(src[x], x) == 0) row[x / 8] |= 0x80 >> (x % 8);
      }
      ditherer.nextRow();
    }

    writeU32(static_cast<uint32_t>(stride));
    std::fwrite(bits.data(), 1, bits.size(), stdout);
    std::fflush(stdout);
  }
  return 0;
}

#pragma once

// Toybox: the visual language for this fork's apps.
//
// Descended from Playdate, adapted for a screen that holds still. See
// docs/design-language.md for the reasoning; this header is the vocabulary.
//
// The one rule everything else follows:
//
//   THE BLACK YOU CAN AFFORD IS INVERSELY PROPORTIONAL TO HOW OFTEN IT CHANGES.
//
// A solid black header that never repaints costs nothing: e-ink holds it at
// zero power and it never ghosts. The same amount of black in a play area that
// repaints every move ghosts badly and slows the refresh. So: solid fills for
// static chrome, dither and heavy outlines for anything that moves. That is
// what lets this look loud without behaving badly.
//
// This is deliberately NOT a CrossPoint theme. Themes are upstream's frozen
// surface, and the reader should keep upstream's look. This applies to
// src/apps_local/ only.

#include <algorithm>
#include <cstdint>

#include "../../components/UITheme.h"
#include "ToyboxFonts.h"
#include "ToyboxMetrics.h"

namespace toybox {

// A full-width horizontal rule. Heavy by default: hairlines read as timid here.
inline void rule(const GfxRenderer& renderer, const int y, const int weight = kRule) {
  renderer.fillRect(kMargin, y, renderer.getScreenWidth() - 2 * kMargin, weight, true);
}

// A gear, drawn rather than stored: at this size it is a ring with eight teeth,
// and a procedural one scales with the chrome without another asset to keep in
// step with the piece set.
inline void gear(const GfxRenderer& renderer, const Rect& box, const bool ink) {
  const int cx = box.x + box.width / 2;
  const int cy = box.y + box.height / 2;
  const int outer = box.width / 2;
  const int tooth = std::max(3, outer / 3);
  const int body = outer - tooth / 2;

  // Teeth first, so the body's edge cleans up where they meet it.
  for (int i = 0; i < 8; ++i) {
    static constexpr int kDx[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    static constexpr int kDy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
    // Diagonal teeth sit closer in, so all eight land on the same circle.
    const int reach = (kDx[i] != 0 && kDy[i] != 0) ? (body * 7) / 10 : body;
    const int tx = cx + kDx[i] * reach - tooth / 2;
    const int ty = cy + kDy[i] * reach - tooth / 2;
    renderer.fillRoundedRect(tx, ty, tooth, tooth, 1, ink ? Black : White);
  }
  renderer.fillRoundedRect(cx - body, cy - body, body * 2, body * 2, body, ink ? Black : White);
  const int hole = std::max(2, body / 2);
  renderer.fillRoundedRect(cx - hole, cy - hole, hole * 2, hole * 2, hole, ink ? White : Black);
}

// Four corner marks inside a rect. Used to flag a square without covering what
// is standing on it, and it rhymes with the brackets around the board.
inline void cornerMarks(const GfxRenderer& renderer, const Rect& box, const int arm, const int weight) {
  const int x = box.x;
  const int y = box.y;
  const int w = box.width;
  const int h = box.height;
  renderer.fillRect(x, y, arm, weight, true);
  renderer.fillRect(x, y, weight, arm, true);
  renderer.fillRect(x + w - arm, y, arm, weight, true);
  renderer.fillRect(x + w - weight, y, weight, arm, true);
  renderer.fillRect(x, y + h - weight, arm, weight, true);
  renderer.fillRect(x, y + h - arm, weight, arm, true);
  renderer.fillRect(x + w - arm, y + h - weight, arm, weight, true);
  renderer.fillRect(x + w - weight, y + h - arm, weight, arm, true);
}

// Blits a 1bpp bitmap, MSB first, row-major, bit set = ink. Our own asset
// format (see tools_local/gen_chess_pieces.py). Deliberately not
// GfxRenderer::drawIcon, which bakes in a portrait rotation meant for the
// reader's themed lists and would turn a chess board on its side.
// `turned` draws the sprite rotated 180 degrees, for artwork that has to read
// right to somebody sitting on the other side of the device. A rotation rather
// than a second asset: at 1 bit there is nothing to resample, so reading the
// same bits from the far corner is exact.
inline void blit1bpp(const GfxRenderer& renderer, const uint8_t* bitmap, const int size, const int x, const int y,
                     const bool ink = true, const bool turned = false) {
  const int rowBytes = (size + 7) / 8;
  for (int row = 0; row < size; ++row) {
    for (int col = 0; col < size; ++col) {
      if ((bitmap[row * rowBytes + (col >> 3)] >> (7 - (col & 7))) & 1) {
        const int px = turned ? size - 1 - col : col;
        const int py = turned ? size - 1 - row : row;
        renderer.drawPixel(x + px, y + py, ink);
      }
    }
  }
}

}  // namespace toybox

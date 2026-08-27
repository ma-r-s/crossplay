#include "BezelActivity.h"

#include <Memory.h>

#include <cstdio>

#include "../Shelf.h"
#include "../ui/ToyboxFonts.h"

namespace {

// One tick per pixel, from the edge inward. Community measurements on the X4
// bezel reached 11 hidden rows; 20 leaves headroom for a worse unit.
constexpr int kTicks = 20;
constexpr int kDashLen = 13;

void drawNumber(const GfxRenderer& renderer, const int x, const int boxY, const char* text) {
  toybox::drawCapsCentered(renderer, toybox::kUiFontId, x, boxY, 16, text, true);
}

// Top and bottom edges: 1px-tall dashes at row k, hairline leaders down/up to
// staggered number labels (odd and even alternate rows so two-digit labels at
// a 24px pitch never touch).
void drawHorizontalRuler(const GfxRenderer& renderer, const bool top) {
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  const int pitch = sw / kTicks;
  char buf[4];
  for (int k = 0; k < kTicks; ++k) {
    const int cx = k * pitch + pitch / 2;
    const int leaderEnd = (k % 2 == 0) ? 42 : 60;
    const int labelBoxY = (k % 2 == 0) ? 46 : 64;
    snprintf(buf, sizeof(buf), "%d", k);
    const int w = renderer.getTextWidth(toybox::kUiFontId, buf);
    if (top) {
      renderer.fillRect(cx - kDashLen / 2, k, kDashLen, 1);
      renderer.drawLine(cx, k + 2, cx, leaderEnd);
      drawNumber(renderer, cx - w / 2, labelBoxY, buf);
    } else {
      renderer.fillRect(cx - kDashLen / 2, sh - 1 - k, kDashLen, 1);
      renderer.drawLine(cx, sh - 1 - leaderEnd, cx, sh - 3 - k);
      drawNumber(renderer, cx - w / 2, sh - 16 - labelBoxY, buf);
    }
  }
}

// Left and right edges: 1px-wide dashes at column k, leaders inward to labels.
// The vertical pitch is generous, so no stagger is needed.
void drawVerticalRuler(const GfxRenderer& renderer, const bool left) {
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  const int stripTop = 100;
  const int pitch = (sh - 2 * stripTop) / kTicks;
  char buf[4];
  for (int k = 0; k < kTicks; ++k) {
    const int cy = stripTop + k * pitch + pitch / 2;
    snprintf(buf, sizeof(buf), "%d", k);
    const int w = renderer.getTextWidth(toybox::kUiFontId, buf);
    if (left) {
      renderer.fillRect(k, cy - kDashLen / 2, 1, kDashLen);
      renderer.drawLine(k + 2, cy, 42, cy);
      drawNumber(renderer, 46, cy - 8, buf);
    } else {
      renderer.fillRect(sw - 1 - k, cy - kDashLen / 2, 1, kDashLen);
      renderer.drawLine(sw - 42, cy, sw - 3 - k, cy);
      drawNumber(renderer, sw - 46 - w, cy - 8, buf);
    }
  }
}

void drawCenteredLine(const GfxRenderer& renderer, const int fontId, const int boxY, const int boxH,
                      const char* text) {
  const int w = renderer.getTextWidth(fontId, text);
  toybox::drawCapsCentered(renderer, fontId, (renderer.getScreenWidth() - w) / 2, boxY, boxH, text, true);
}

}  // namespace

std::unique_ptr<Activity> BezelActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<BezelActivity>(renderer, mappedInput);
}

void BezelActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  requestUpdate();
}

void BezelActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    shelf::leave(renderer, mappedInput);
  }
}

void BezelActivity::render(RenderLock&&) {
  renderer.clearScreen();

  drawHorizontalRuler(renderer, true);
  drawHorizontalRuler(renderer, false);
  drawVerticalRuler(renderer, true);
  drawVerticalRuler(renderer, false);

  // The side rulers own everything outside x in [95, 385], so every line here
  // has to stay narrower than ~280px or it prints through their labels.
  const int sh = renderer.getScreenHeight();
  const int mid = sh / 2;
  drawCenteredLine(renderer, toybox::kDisplayFontId, mid - 140, 40, "BEZEL");
  drawCenteredLine(renderer, toybox::kUiFontId, mid - 80, 16, "LOOK STRAIGHT ON");
  drawCenteredLine(renderer, toybox::kUiFontId, mid - 40, 16, "TICK = 1 PX");
  drawCenteredLine(renderer, toybox::kUiFontId, mid - 10, 16, "SMALLEST NUMBER");
  drawCenteredLine(renderer, toybox::kUiFontId, mid + 20, 16, "VISIBLE = HIDDEN PX");

  int t, r, b, l;
  renderer.getOrientedViewableTRBL(&t, &r, &b, &l);
  char cfg[32];
  snprintf(cfg, sizeof(cfg), "SET T%d R%d B%d L%d", t, r, b, l);
  drawCenteredLine(renderer, toybox::kUiFontId, mid + 60, 16, cfg);

  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
}

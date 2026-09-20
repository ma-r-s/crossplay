#include "CalculatorActivity.h"

#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "CalcFonts.h"

namespace fui = freeink::ui;

namespace {

// The largest of the number cuts the string actually fits in, or -1 if none
// does.
//
// MEASURED, through the renderer's own advance widths, rather than counted:
// Jersey's digits are not tabular -- a '1' is 37px against a '0' at 57px in the
// 56 cut -- so counting characters would step the number down a size it did not
// need and every result would read smaller than it could.
//
// EVERY rung is measured, including the last. The first version measured three
// of four and returned the fourth unchecked, and the site that drew it clamped
// only the left edge -- so the one rung nothing verified was drawn by the one
// call that could not say no. It is meant to be unreachable, because the engine
// caps what it emits at calc::kMaxDisplayChars; it is checked anyway, because
// "meant to be" is what the percent key broke.
int rungFor(const GfxRenderer& renderer, const char* text, const int widthPx) {
  for (int rung = 0; rung < calc::kNumberRungs; ++rung) {
    if (renderer.getTextWidth(calc::numberFontFor(rung), text) <= widthPx) return rung;
  }
  return -1;
}

}  // namespace

std::unique_ptr<Activity> CalculatorActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<CalculatorActivity>(renderer, mappedInput);
}

void CalculatorActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  calc::ensureCalcFonts(renderer);
  requestUpdate();
}

// The band paints from the panel's physical row 0 whatever the glass hides --
// paint may bleed under the bezel, ink may not -- and its title centres between
// the bezel's safe top and the band's bottom. A covered row is not an invisible
// row: the eye sees past the bezel from below and reads a white strip above any
// header that started at the safe top.
void CalculatorActivity::drawChrome() {
  const int w = renderer.getScreenWidth();
  int safeTop = 0, safeRight = 0, safeBottom = 0, safeLeft = 0;
  renderer.getOrientedViewableTRBL(&safeTop, &safeRight, &safeBottom, &safeLeft);
  renderer.fillRect(0, 0, w, calc::kChromeHeight, true);
  calc::drawCapsCentered(renderer, calc::kLabelFontId, calc::kMargin, safeTop, calc::kChromeHeight - safeTop,
                         "CALCULATOR", false);
  renderer.fillRect(0, calc::kChromeHeight + 4, w, calc::kRule, true);
}

// Laid out TOP DOWN from the height calc::displayHeight() derived, band by band,
// so there is nothing left over at the end. The first pass pinned the number to
// the bottom of a guessed height and the remainder came out as a strip of empty
// panel over every result.
void CalculatorActivity::drawDisplay(const calc::PadGeom& g) {
  const calc::Rect16 d = g.display;
  int y = d.y;

  // The pending sum's band, reserved whether or not there is one: a display that
  // grew a line when you pressed an operator would shift the number under your
  // finger between one refresh and the next.
  const int pendingH = calc::kSmallCut.lineHeight;
  if (engine.pending()[0]) {
    const char* p = engine.pending();
    const int pw = renderer.getTextWidth(calc::kSmallFontId, p);
    // Guarded rather than trusted. The pending line is bounded by the engine and
    // the gate proves it fits, but a label drawn past its box is the one defect
    // that cannot be styled around, so the draw site refuses rather than the
    // reasoning holding.
    if (pw <= d.w) calc::drawCapsCentered(renderer, calc::kSmallFontId, d.right() - pw, y, pendingH, p, true);
  }
  y += pendingH;

  // The number keeps a hair of air off the panel's edge rather than sitting
  // flush against it: key labels are given ten per cent a side by the fit gate,
  // and the largest element on the screen was the only thing with none.
  const int numberWidth = d.w - calc::kNumberAir;
  const char* text = engine.display();
  // An error is words, and the number cuts have no letters in them at all -- a
  // message drawn in one is a blank display. The label cut is the smallest face
  // here that can spell one.
  int cut = calc::kLabelFontId;
  if (!engine.hasError()) {
    const int rung = rungFor(renderer, text, numberWidth);
    // Nothing fits. The engine's bound says this cannot happen and the suite
    // proves the bound, so if it ever does the honest thing is to say the
    // display cannot show the number -- not to draw it through the border.
    if (rung < 0) text = "TOO LONG";
    cut = rung < 0 ? calc::kLabelFontId : calc::numberFontFor(rung);
  }
  const int numberH = calc::kNumberCut.capHeight + 2 * calc::kNumberAir;
  const int tw = renderer.getTextWidth(cut, text);
  calc::drawCapsCentered(renderer, cut, d.right() - calc::kNumberAir / 2 - std::min(tw, numberWidth), y, numberH, text,
                         true);
  y += numberH;

  // A rule under the number, not a box around it: the number is the content, and
  // a box would give it a key's weight.
  renderer.fillRect(d.x, y, d.w, calc::kRule, true);
}

void CalculatorActivity::drawKey(const calc::KeyDef& key, const calc::Rect16& r) {
  // `=` is the only filled key. Filling the whole operator column was the other
  // candidate and it fails the ink-budget rule: black is for what changes, and a
  // pad's keys never change, so six permanently black slabs would be the largest
  // standing block of ink in the fork for no information at all.
  const bool filled = key.key == calc::Key::Equals;
  if (filled) {
    renderer.fillRect(r.x, r.y, r.w, r.h, true);
  } else {
    renderer.drawRect(r.x, r.y, r.w, r.h, key.emphasis ? calc::kOperatorStroke : calc::kDigitStroke, true);
  }
  const char* label = calc::labelFor(key);
  calc::drawCapsCenteredIn(renderer, calc::labelFontFor(label), r.x, r.w, r.y, r.h, label, !filled);
}

void CalculatorActivity::drawPad(const calc::PadGeom& g) {
  const int cells = calc::kPad.cols * calc::kPad.rows;
  for (int i = 0; i < cells; ++i) {
    if (calc::kPad.keys[i].key == calc::Key::None) continue;
    drawKey(calc::kPad.keys[i], calc::keyRect(calc::kPad, g, i));
  }
}

void CalculatorActivity::render(RenderLock&&) {
  renderer.clearScreen();
  // A Frame is still built so the interaction table has a generation the touch
  // router can read, even though this screen registers nothing in it: the pad is
  // hit-tested against geometry.
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, device, noInput, interactions);

  drawChrome();
  const calc::PadGeom g = calc::padGeometry(renderer.getScreenWidth(), renderer.getScreenHeight());
  drawDisplay(g);
  drawPad(g);

  interactionsReady = true;
  noteSurfaceBuilt();
  renderer.displayBuffer();
}

void CalculatorActivity::loop() {
  // Read on the per-frame path and ABOVE the "nothing to do unless a tap
  // arrived" return: a swipe is not a tap, and a Back read below that line is on
  // the frame path in name only. host-tests/backgesture enforces exactly this,
  // after Trivia shipped with no way out at all.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    shelf::leave(renderer, mappedInput);
    return;
  }

  int tapX = 0;
  int tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY) || !interactionsReady) return;

  const calc::PadGeom g = calc::padGeometry(renderer.getScreenWidth(), renderer.getScreenHeight());
  const int index = calc::keyAt(calc::kPad, g, tapX, tapY);
  if (index < 0) return;
  engine.press(calc::kPad.keys[index].key);
  requestUpdate();
}

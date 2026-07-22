#pragma once
#include <FreeInkUIGfxRenderer.h>

#include "MappedInputManager.h"
#include "components/UIScale.h"

// Shared glue for activities hosting a FreeInkApp: the font-bound render
// target and the touch snapshot FreeInkApp routing consumes.

// Bind the uiScale fonts before FreeInkApp's constructor derives its theme
// metrics from the body font's line height.
inline freeink::ui::GfxRendererTarget makeUiTarget(const GfxRenderer& renderer) {
  freeink::ui::GfxRendererTarget target(renderer);
  const auto spec = uiScaleSpec();
  target.setFont(freeink::ui::GfxRendererTarget::FONT_SMALL, spec.smallFontId);
  target.setFont(freeink::ui::GfxRendererTarget::FONT_BODY, spec.bodyFontId);
  // Title scales with the body font so headers stay consistent across the
  // app-rendered and GUI.drawHeader screens.
  target.setFont(freeink::ui::GfxRendererTarget::FONT_TITLE, spec.bodyFontId);
  return target;
}

// Tap release with coords, plus the raw release the tap classifier never
// reports (swipe end, drag-off) delivered off-target: nothing dispatches,
// but routing drops its pressed-element state instead of ghosting it onto
// the next render.
// Scroll semantics shared by every FreeInkUI list screen: swipes move the
// viewport (topIndex) without touching the selection; button navigation moves
// the selection and pulls the viewport along just enough to keep it visible.

inline int scrollListBy(const int topIndex, const int delta, const int visibleRows, const int count) {
  int maxTop = count - visibleRows;
  if (maxTop < 0) maxTop = 0;
  int next = topIndex + delta;
  if (next > maxTop) next = maxTop;
  if (next < 0) next = 0;
  return next;
}

inline int followListSelection(const int selectedIndex, const int topIndex, const int visibleRows, const int count) {
  return static_cast<int>(freeink::ui::listTopIndexFor(
      static_cast<int16_t>(selectedIndex), static_cast<uint16_t>(topIndex),
      static_cast<uint16_t>(visibleRows > 0 ? visibleRows : 1), static_cast<uint16_t>(count)));
}

inline freeink::ui::InputSnapshot touchSnapshotFrom(const MappedInputManager& mappedInput) {
  freeink::ui::InputSnapshot snap{};
  int tx = 0;
  int ty = 0;
  if (mappedInput.wasScreenTouchDown(tx, ty)) {
    snap.touchPressed = true;
    snap.touchX = static_cast<int16_t>(tx);
    snap.touchY = static_cast<int16_t>(ty);
  }
  if (mappedInput.wasScreenTapped(tx, ty)) {
    snap.touchReleased = true;
    snap.touchX = static_cast<int16_t>(tx);
    snap.touchY = static_cast<int16_t>(ty);
  } else if (mappedInput.wasScreenTouchReleased()) {
    snap.touchReleased = true;
    snap.touchX = -1;
    snap.touchY = -1;
  }
  return snap;
}

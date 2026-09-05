#include "ButtonReleaseGate.h"

void ButtonReleaseGate::settle(const uint8_t pressEdges, const uint8_t releaseEdges, const uint8_t heldMask) {
  // A fresh press starts a fresh cycle, so whatever we were waiting to swallow
  // is gone and THIS press's release belongs to the activity now on top.
  //
  // What guarantees a button cannot stay dead is that this clear is
  // UNCONDITIONAL -- it is subject to no state but the press edge itself, so
  // however the arm was set and whatever the caller then does, one press takes
  // it off. A Back that never works reads as a frozen device, which is worse
  // than the double-fire this gate exists to stop.
  //
  // Its POSITION is not part of that guarantee: both statements here are
  // AND-masks over the same byte, so swapping them lands on the same value and
  // the suite cannot tell the difference. The reason it reads first is that it
  // is the sentence to read first.
  armed = static_cast<uint8_t>(armed & ~pressEdges);

  // Keep the arm through the frame that CARRIES the release edge -- settle()
  // runs before the activity reads input, so this frame is the one where the
  // swallow has to happen -- and through every frame the button is still down.
  //
  // Anything else means nothing is pending: drop the arm so a later, honest
  // press/release pair passes through untouched. This is also the recovery
  // path for a release edge that was never delivered here at all (a frame in
  // which the activity stack did not run): the arm clears on the next settle
  // rather than waiting for a press.
  armed = static_cast<uint8_t>(armed & (releaseEdges | heldMask));
}

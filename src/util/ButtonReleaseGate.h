#pragma once

#include <cstdint>

// One physical press, one logical event -- even when the press crosses an
// activity boundary.
//
// THE SEAM. An activity that finishes on the PRESS edge hands control back
// while the button is still down. The RELEASE lands ~77ms later on whatever is
// underneath, which never saw the press and reads the release as its own
// input. WifiSelectionActivity is `wasPressed` throughout; fifty-five files in
// src/ act on `wasReleased`, and eleven of them launch that picker. On a device
// that has never joined Wi-Fi that made Hacker News unopenable: the app puts
// the picker in front of itself, backing out of the picker shut the app, and
// the saved-articles shelf -- the half that exists for having no network -- was
// unreachable without a network.
//
// It is not a stale latch handled twice. Measured in commit 168587fb with a
// probe on every loop pass, there are five clean passes between the two edges:
//
//   5607  WifiSelection cancels on the PRESS -> the caller is back on top
//   5621..5671  five caller passes, released=0
//   5684  the RELEASE lands and the caller acts on it
//
// THE RULE. A release belongs to whoever saw the press. When the activity on
// top changes while a button is down, that button's next release is swallowed
// once.
//
// THE FAILURE MODE TO FEAR is not the bug, it is the fix: a gate that swallows
// too much leaves Back permanently dead, which reads as a frozen device. So the
// arm is cleared by ANY of three things -- a fresh press edge, the release it
// was waiting for, or simply the button not being down -- and the press edge
// wins unconditionally. An arm therefore cannot outlive one press/release
// cycle no matter what the caller does or fails to do.
//
// Kept freestanding (nothing above <cstdint>) so the state machine can be
// driven frame by frame on the host. That matters more here than usual: the
// simulator CANNOT test this class of bug at all. It does not compile lib/hal,
// and its latch clears in beginFrame() rather than update(), so the two edges
// never land in different activities there. host-tests/pickerseam is the only
// place this behaviour is checked.
class ButtonReleaseGate {
 public:
  // Buttons whose release the activity now on top must not act on, as a mask
  // of (1 << HalGPIO::BTN_*) bits. Called when the activity on top changes,
  // with the buttons that are down at that moment.
  void arm(const uint8_t heldMask) { armed = static_cast<uint8_t>(armed | heldMask); }

  // Once per frame, BEFORE the activity reads any input, with this frame's
  // edges and levels. THAT order is the load-bearing one and the tests do
  // assert it: settle after the read and the frame carrying the release has
  // already been acted on. The order of the two lines inside settle() is not
  // load-bearing and no test can see it -- both are AND-masks over the same
  // byte, so they commute. Do not write an invariant there.
  void settle(uint8_t pressEdges, uint8_t releaseEdges, uint8_t heldMask);

  // True when this button's release edge is the other half of a press some
  // earlier activity already acted on.
  bool swallowsRelease(const uint8_t buttonIndex) const {
    return (armed & static_cast<uint8_t>(1u << buttonIndex)) != 0;
  }

  // For tests and logging; nothing in the firmware needs to read this.
  uint8_t armedMask() const { return armed; }

 private:
  uint8_t armed = 0;
};

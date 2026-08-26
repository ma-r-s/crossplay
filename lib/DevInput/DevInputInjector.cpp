#include "DevInputInjector.h"

#if CROSSPOINT_DEV_SERIAL_BRIDGE

#include <Arduino.h>

namespace devinput {
namespace {

// A synthetic long press fires its one-shot at this held time, then keeps the
// contact down until the scheduled hold ends so the consumer's
// suppressContact() has a remainder to suppress, like a real finger.
constexpr unsigned long LONG_PRESS_FIRE_MS = 600;

struct Contact {
  enum class Kind : uint8_t { None, Tap, Long, Swipe };
  Kind kind = Kind::None;
  float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  unsigned long startMs = 0;
  unsigned long holdMs = 0;
  bool suppressed = false;
  bool started = false;    // first update() after scheduling has run
  bool longFired = false;  // one-shot long-press already emitted
};

struct Btn {
  bool active = false;
  uint8_t index = 0;
  unsigned long startMs = 0;
  unsigned long holdMs = 0;
  bool started = false;
};

Contact contact;
Btn btn;
unsigned long lastHeldMs = 0;

// Frame snapshot, valid between two update() calls.
struct Frame {
  bool touchDownEdge = false;
  bool touchReleaseEdge = false;
  bool tapEdge = false;
  bool longEdge = false;
  bool swipeEdge = false;
  bool candidate = false;
  bool held = false;
  float heldX = 0, heldY = 0;
  float swipeX0 = 0, swipeY0 = 0, swipeX1 = 0, swipeY1 = 0;
  unsigned long heldMs = 0;
  bool btnPressEdge = false;
  bool btnReleaseEdge = false;
  bool btnDown = false;
  uint8_t btnIndex = 0;
  unsigned long btnHeldMs = 0;
  bool activity = false;
};

Frame frame;

}  // namespace

void update() {
  frame = Frame{};
  const unsigned long now = millis();

  if (contact.kind != Contact::Kind::None) {
    frame.activity = true;
    const unsigned long elapsed = contact.started ? now - contact.startMs : 0;
    if (!contact.started) {
      contact.started = true;
      contact.startMs = now;
      frame.touchDownEdge = true;
    }
    if (elapsed >= contact.holdMs && contact.started && elapsed > 0) {
      // Release frame. Tap reports the original touch-down position, like the
      // SDK classifier.
      frame.touchReleaseEdge = !contact.suppressed;
      frame.heldX = contact.x0;
      frame.heldY = contact.y0;
      if (contact.kind == Contact::Kind::Tap && !contact.suppressed) {
        frame.tapEdge = true;
      }
      if (contact.kind == Contact::Kind::Swipe && !contact.suppressed) {
        frame.swipeEdge = true;
        frame.swipeX0 = contact.x0;
        frame.swipeY0 = contact.y0;
        frame.swipeX1 = contact.x1;
        frame.swipeY1 = contact.y1;
      }
      lastHeldMs = elapsed;
      contact = Contact{};
    } else {
      // Held frame.
      frame.heldMs = elapsed;
      float t = contact.holdMs ? static_cast<float>(elapsed) / contact.holdMs : 0.0f;
      if (t > 1.0f) t = 1.0f;
      frame.heldX = contact.x0 + (contact.x1 - contact.x0) * t;
      frame.heldY = contact.y0 + (contact.y1 - contact.y0) * t;
      if (!contact.suppressed) {
        frame.held = true;
        frame.candidate = contact.kind != Contact::Kind::Swipe;
        if (contact.kind == Contact::Kind::Long && !contact.longFired && elapsed >= LONG_PRESS_FIRE_MS) {
          contact.longFired = true;
          frame.longEdge = true;
        }
      }
    }
  }

  if (btn.active) {
    frame.activity = true;
    if (!btn.started) {
      btn.started = true;
      btn.startMs = now;
      frame.btnPressEdge = true;
      frame.btnDown = true;
      frame.btnIndex = btn.index;
    } else if (now - btn.startMs >= btn.holdMs) {
      frame.btnReleaseEdge = true;
      frame.btnIndex = btn.index;
      frame.btnHeldMs = now - btn.startMs;
      btn = Btn{};
    } else {
      frame.btnDown = true;
      frame.btnIndex = btn.index;
      frame.btnHeldMs = now - btn.startMs;
    }
  }
}

bool tap(float nx, float ny, unsigned long holdMs) {
  if (contact.kind != Contact::Kind::None) return false;
  contact = Contact{};
  contact.kind = Contact::Kind::Tap;
  contact.x0 = contact.x1 = nx;
  contact.y0 = contact.y1 = ny;
  contact.holdMs = holdMs;
  return true;
}

bool longPress(float nx, float ny) {
  if (contact.kind != Contact::Kind::None) return false;
  contact = Contact{};
  contact.kind = Contact::Kind::Long;
  contact.x0 = contact.x1 = nx;
  contact.y0 = contact.y1 = ny;
  contact.holdMs = LONG_PRESS_FIRE_MS + 350;
  return true;
}

bool swipe(float nx0, float ny0, float nx1, float ny1, unsigned long ms) {
  if (contact.kind != Contact::Kind::None) return false;
  contact = Contact{};
  contact.kind = Contact::Kind::Swipe;
  contact.x0 = nx0;
  contact.y0 = ny0;
  contact.x1 = nx1;
  contact.y1 = ny1;
  contact.holdMs = ms;
  return true;
}

bool button(uint8_t buttonIndex, unsigned long holdMs) {
  if (btn.active) return false;
  btn = Btn{};
  btn.active = true;
  btn.index = buttonIndex;
  btn.holdMs = holdMs;
  return true;
}

bool busy() { return contact.kind != Contact::Kind::None || btn.active; }

bool isPressed(uint8_t buttonIndex) { return frame.btnDown && frame.btnIndex == buttonIndex; }
bool wasPressed(uint8_t buttonIndex) { return frame.btnPressEdge && frame.btnIndex == buttonIndex; }
bool wasReleased(uint8_t buttonIndex) { return frame.btnReleaseEdge && frame.btnIndex == buttonIndex; }
bool wasAnyPressed() { return frame.btnPressEdge; }
bool wasAnyReleased() { return frame.btnReleaseEdge; }
unsigned long heldTime() { return frame.btnHeldMs; }
unsigned long powerHeldTime() { return frame.btnDown ? frame.btnHeldMs : 0; }

bool wasTouchTap(float& nx, float& ny) {
  if (!frame.tapEdge) return false;
  nx = frame.heldX;
  ny = frame.heldY;
  return true;
}

bool wasTouchDown(float& nx, float& ny) {
  if (!frame.touchDownEdge) return false;
  nx = frame.heldX;
  ny = frame.heldY;
  return true;
}

bool wasTouchReleased() { return frame.touchReleaseEdge; }

bool isTouchTapCandidate(float& nx, float& ny, unsigned long& heldMs) {
  if (!frame.candidate) return false;
  nx = frame.heldX;
  ny = frame.heldY;
  heldMs = frame.heldMs;
  return true;
}

bool isTouchHeldAt(float& nx, float& ny) {
  if (!frame.held) return false;
  nx = frame.heldX;
  ny = frame.heldY;
  return true;
}

bool wasTouchLongPress(float& nx, float& ny) {
  if (!frame.longEdge) return false;
  nx = frame.heldX;
  ny = frame.heldY;
  return true;
}

void suppressContact() {
  if (contact.kind != Contact::Kind::None) contact.suppressed = true;
}

unsigned long lastTouchHeldMs() { return lastHeldMs; }

bool wasSwipe(float& nx0, float& ny0, float& nx1, float& ny1) {
  if (!frame.swipeEdge) return false;
  nx0 = frame.swipeX0;
  ny0 = frame.swipeY0;
  nx1 = frame.swipeX1;
  ny1 = frame.swipeY1;
  return true;
}

bool wasTouchActivity() { return frame.activity; }

}  // namespace devinput

#endif  // CROSSPOINT_DEV_SERIAL_BRIDGE

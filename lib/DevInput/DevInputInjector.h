#pragma once

// Synthetic input for driving a desk device from the host, dev builds only
// (-DCROSSPOINT_DEV_SERIAL_BRIDGE=1 in the dev envs; release envs never
// compile this). DevSerialBridge parses CMD lines from the board's serial
// transport and schedules events here; HalGPIO overlays the per-frame state
// onto the real hardware reads, so everything downstream (MappedInputManager,
// FreeInkUI, the apps) sees a synthetic finger or button exactly where it
// would see a real one.
//
// The overlay mirrors the SDK's edge semantics: an edge is true for the whole
// frame between two HalGPIO::update() calls, never consumed by reading, so
// multiple consumers polling in one frame all see it.

#if CROSSPOINT_DEV_SERIAL_BRIDGE

#include <cstdint>

namespace devinput {

// Advance the state machine one input frame. Called from HalGPIO::update().
void update();

// Schedule events (normalized panel-native coordinates, 0..1). Each returns
// false while a previous event of the same kind is still playing.
bool tap(float nx, float ny, unsigned long holdMs);
bool longPress(float nx, float ny);
bool swipe(float nx0, float ny0, float nx1, float ny1, unsigned long ms);
bool button(uint8_t buttonIndex, unsigned long holdMs);
bool busy();

// Frame-state queries, consulted by the HalGPIO overlay.
bool isPressed(uint8_t buttonIndex);
bool wasPressed(uint8_t buttonIndex);
bool wasReleased(uint8_t buttonIndex);
bool wasAnyPressed();
bool wasAnyReleased();
unsigned long heldTime();
unsigned long powerHeldTime();

bool wasTouchTap(float& nx, float& ny);
bool wasTouchDown(float& nx, float& ny);
bool wasTouchReleased();
bool isTouchTapCandidate(float& nx, float& ny, unsigned long& heldMs);
bool isTouchHeldAt(float& nx, float& ny);
bool wasTouchLongPress(float& nx, float& ny);
void suppressContact();
unsigned long lastTouchHeldMs();
bool wasSwipe(float& nx0, float& ny0, float& nx1, float& ny1);
bool wasTouchActivity();

}  // namespace devinput

#endif  // CROSSPOINT_DEV_SERIAL_BRIDGE

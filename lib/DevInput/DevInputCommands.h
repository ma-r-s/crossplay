#pragma once

// The one input vocabulary, shared by every transport that drives a desk
// device: TAP, LONG, SWIPE and BTN, in panel-native pixels.
//
// It lives beside the injector rather than inside DevSerialBridge because
// there are two transports now -- the serial bridge and Developer Mode's
// POST /api/dev/input. A device that answers TAP down a cable but not over
// Wi-Fi, or that takes the arguments in a different order on each, is worse
// than one that answers neither: the difference only shows up once somebody is
// already debugging something else.
//
// Dev builds only (-DCROSSPOINT_DEV_SERIAL_BRIDGE=1), same as the injector it
// schedules onto.

#if CROSSPOINT_DEV_SERIAL_BRIDGE

#include <cstddef>

namespace devinput {

// True when `cmd` opens with one of the input verbs, so a transport can hand
// it here and otherwise carry on parsing its own commands.
bool isCommand(const char* cmd);

// Parses `cmd` and schedules it. `reply` always receives one NUL-terminated
// line with no trailing newline: "OK ..." when scheduled, "ERR ..." when the
// arguments are wrong or an event of the same kind is still playing. Returns
// true only for OK.
bool runCommand(const char* cmd, char* reply, size_t replyLen);

}  // namespace devinput

#endif  // CROSSPOINT_DEV_SERIAL_BRIDGE

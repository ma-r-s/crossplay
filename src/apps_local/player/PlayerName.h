#pragma once

// The name this device shows to other devices.
//
// Device-wide on purpose, and this is the part worth getting right: a DS asked
// you for a nickname once, at first boot, in System Settings, and then every
// game it ever ran used it. No game asked, no game stored its own, and no game
// had a naming screen. Any app in this fork gets the same deal -- call name()
// and you are done.
//
// There is no keyboard, and that is a feature rather than a gap. Typing on a
// 1-bit panel with a 500ms refresh is a chore, and a name is not information the
// player is trying to communicate; it is a label they need to recognise. So it
// is rolled from two word lists, the way a console hands you one, and rerolled
// with a tap until you like it. That also means it can never be empty, never be
// rude, and never need validating.
//
// The composing half is freestanding so host-tests can exercise the lists; only
// the load/save half touches storage.

#include <cstddef>
#include <cstdint>

namespace player {

// Long enough for the widest pair the lists can produce, and it is what the
// wire format reserves.
constexpr size_t kMaxNameLength = 15;

// The stored name, generating and persisting one on first use so a device is
// never nameless. Stable for the life of the process.
const char* name();

// Rolls a different one and stores it. Never returns the name it replaced, so
// tapping twice cannot land back where it started.
const char* reroll();

// The freestanding half: writes an adjective and a noun into `out`. Same seed,
// same name, which is what makes it testable.
void compose(char* out, size_t capacity, uint32_t seed);

// Exposed for the tests, which check every pair fits.
size_t adjectiveCount();
size_t nounCount();

}  // namespace player

#include "ToyboxSeed.h"

#include <Arduino.h>

#ifdef SIMULATOR
#include <cstdlib>
#endif

namespace toybox {

namespace {

// Knuth's multiplicative constant. Two games started a millisecond apart
// otherwise begin with adjacent states, and an xorshift seeded with adjacent
// values takes a while to look different.
constexpr uint32_t kMix = 2654435761u;

}  // namespace

uint32_t seed() {
#ifdef SIMULATOR
  // CROSSPLAY_SEED pins it, and CROSSPLAY_SEED_STEP advances it by that much on
  // every call, so a recipe that plays two games in a row still gets two
  // different deals -- reproducibly. Unset, the simulator behaves like the
  // device.
  if (const char* pinned = std::getenv("CROSSPLAY_SEED")) {
    static uint32_t next = 0;
    static bool started = false;
    if (!started) {
      next = static_cast<uint32_t>(std::strtoul(pinned, nullptr, 10));
      started = true;
    }
    const uint32_t value = next;
    const char* step = std::getenv("CROSSPLAY_SEED_STEP");
    next += step != nullptr ? static_cast<uint32_t>(std::strtoul(step, nullptr, 10)) : 1u;
    return value == 0 ? kMix : value;
  }
#endif
  const uint32_t value = static_cast<uint32_t>(millis()) * kMix + 1u;
  // An xorshift seeded with zero is stuck at zero forever, and millis() really
  // is zero for the first millisecond after boot.
  return value == 0 ? kMix : value;
}

}  // namespace toybox

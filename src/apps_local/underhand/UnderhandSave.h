#pragma once

// What survives leaving Underhand: the profile, and the run in progress with
// the random generator it was using. Freestanding; the activity does the I/O.

#include <cstddef>
#include <cstdint>

#include "UnderhandEngine.h"

namespace underhand {

struct Save {
  Profile profile;
  bool inRun = false;
  Game game;
  uint64_t rng = 1;
};

// Magic, version and sizeof(Game), then the fields as they lie in memory. A
// changed Game layout changes the size, so an old save is refused rather than
// misread.
constexpr size_t kSaveBytes = 4 + 2 + 2 + sizeof(Profile) + 1 + sizeof(Game) + sizeof(uint64_t);

// Writes `save` into `out`, which holds kSaveBytes.
void encode(const Save& save, uint8_t* out);

// Reads a save back. A run these cards cannot continue is dropped and the
// profile kept; false only when nothing in the bytes can be trusted.
bool decode(const uint8_t* data, size_t len, const Cards& cards, Save& out);

}  // namespace underhand

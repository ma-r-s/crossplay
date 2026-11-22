#pragma once

// Puzzles New Game can always fall back on when generation runs out of time.
//
// Each entry is a (seed, attempt) pair whose attempt is known to succeed, so
// regenerating it costs exactly one attempt and the bank costs 36 bytes of
// flash instead of six stored grids per level. host-tests/traintracks proves
// every entry still regenerates; a generator change that breaks one fails
// there, not on a device.
//
// The seeds sit in their own range (0xF...) so a save can never mistake a
// fallback for a generated game with the same seed.

#include <cstdint>

#include "TrainTracksGenerator.h"

namespace traintracks {

struct FallbackEntry {
  uint32_t seed;
  uint16_t attempt;
};

constexpr int kFallbackPerLevel = 6;

constexpr FallbackEntry kFallback[kLevelCount][kFallbackPerLevel] = {
    {{0xF0000000u, 0}, {0xF0000001u, 0}, {0xF0000002u, 0}, {0xF0000003u, 0}, {0xF0000004u, 0}, {0xF0000005u, 0}},
    {{0xF0000064u, 2}, {0xF0000065u, 0}, {0xF0000066u, 0}, {0xF0000067u, 3}, {0xF0000068u, 1}, {0xF0000069u, 0}},
    {{0xF00000C8u, 2}, {0xF00000C9u, 0}, {0xF00000CAu, 0}, {0xF00000CBu, 1}, {0xF00000CCu, 2}, {0xF00000CDu, 5}},
};

inline bool loadFallback(const Level level, const int index, Generated& out, GenWork& work) {
  const FallbackEntry& e = kFallback[static_cast<int>(level)][index % kFallbackPerLevel];
  return generateAttempt(level, e.seed, e.attempt, out, work);
}

}  // namespace traintracks

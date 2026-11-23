#pragma once

// Puzzles New Game can always fall back on when generation runs out of time.
//
// Each entry is a (seed, attempt) pair whose attempt is known to succeed, so
// regenerating it costs exactly one attempt and the bank costs 144 bytes of
// flash instead of eighteen stored puzzles. host-tests/traintracks proves
// every entry still regenerates; a generator change that breaks one fails
// there, not on a device.
//
// A game is rebuilt from (level, seed, attempt) alone, so a fallback is saved
// and restored exactly as a generated game is, and nothing needs to tell the
// two apart.

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

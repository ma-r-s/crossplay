#pragma once

// What the player has achieved across all 125 levels, and its save format.
//
// Save line: "<current> <maxUnlocked> <best>\n", where <best> is two hex digits
// per level: 00 = not cleared, ff = cleared before bests were recorded (an old
// two-number save), anything else = fewest moves that cleared it.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "WappoLevels.h"

namespace wappo {

constexpr uint8_t kNoBest = 0;
constexpr uint8_t kBestUnknown = 0xFF;
constexpr int kMaxPoints = 100 * kLevelCount;
constexpr size_t kSaveBytes = 2 * kLevelCount + 32;

struct Progress {
  int current = 0;      // level PLAY starts
  int maxUnlocked = 0;  // highest level that may be played
  uint8_t best[kLevelCount] = {};
};

inline bool isCleared(const Progress& p, const int level) { return p.best[level] != kNoBest; }

inline bool clearedAtPar(const Progress& p, const int level) {
  const uint8_t b = p.best[level];
  return b != kNoBest && b != kBestUnknown && b <= kLevels[level].par;
}

inline bool allCleared(const Progress& p) {
  for (int i = 0; i < kLevelCount; ++i) {
    if (!isCleared(p, i)) return false;
  }
  return true;
}

inline int atParCount(const Progress& p) {
  int n = 0;
  for (int i = 0; i < kLevelCount; ++i) n += clearedAtPar(p, i) ? 1 : 0;
  return n;
}

// The original's Points: par * 100 / moves per level, summed. A level cleared
// before bests were recorded scores nothing until it is played again.
inline int totalPoints(const Progress& p) {
  int points = 0;
  for (int i = 0; i < kLevelCount; ++i) {
    const uint8_t b = p.best[i];
    if (b != kNoBest && b != kBestUnknown) points += kLevels[i].par * 100 / b;
  }
  return points;
}

inline void recordWin(Progress& p, const int level, const int moves) {
  const uint8_t m = static_cast<uint8_t>(moves < 1 ? 1 : (moves > 254 ? 254 : moves));
  const uint8_t b = p.best[level];
  if (b == kNoBest || b == kBestUnknown || m < b) p.best[level] = m;
  if (level + 1 < kLevelCount && level + 1 > p.maxUnlocked) p.maxUnlocked = level + 1;
}

inline int formatProgress(const Progress& p, char* out, const size_t size) {
  int n = std::snprintf(out, size, "%d %d ", p.current, p.maxUnlocked);
  for (int i = 0; i < kLevelCount && n > 0 && static_cast<size_t>(n) + 3 < size; ++i) {
    n += std::snprintf(out + n, size - n, "%02x", p.best[i]);
  }
  if (n > 0 && static_cast<size_t>(n) + 2 <= size) {
    out[n++] = '\n';
    out[n] = '\0';
  }
  return n;
}

inline int hexDigit(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Accepts both the current line and the old "<current> <maxUnlocked>" one.
inline bool parseProgress(const char* in, Progress& p) {
  char* end = nullptr;
  const long cur = std::strtol(in, &end, 10);
  if (end == in) return false;
  const char* rest = end;
  const long maxU = std::strtol(rest, &end, 10);
  const bool hasMax = end != rest;

  p = Progress{};
  if (cur >= 0 && cur < kLevelCount) p.current = static_cast<int>(cur);
  if (hasMax && maxU >= 0 && maxU < kLevelCount) p.maxUnlocked = static_cast<int>(maxU);
  if (p.maxUnlocked < p.current) p.maxUnlocked = p.current;

  const char* hex = end;
  while (*hex == ' ') ++hex;
  bool hasBests = true;
  for (int i = 0; i < 2 * kLevelCount; ++i) {
    if (hexDigit(hex[i]) < 0) {
      hasBests = false;
      break;
    }
  }
  if (hasBests) {
    for (int i = 0; i < kLevelCount; ++i) {
      p.best[i] = static_cast<uint8_t>(hexDigit(hex[2 * i]) * 16 + hexDigit(hex[2 * i + 1]));
    }
  } else {
    // Old save: every level below the unlocked one was cleared, moves unknown.
    for (int i = 0; i < p.maxUnlocked; ++i) p.best[i] = kBestUnknown;
  }
  return true;
}

}  // namespace wappo

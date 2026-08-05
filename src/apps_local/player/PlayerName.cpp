#include "PlayerName.h"

#include <cstdio>
#include <cstring>

#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#else
#define LOG_INF(...) ((void)0)
#define LOG_ERR(...) ((void)0)
#endif

namespace player {
namespace {

// Short on purpose: the pair has to fit kMaxNameLength, and a name that has to
// be truncated in a header is worse than a shorter one chosen deliberately.
constexpr const char* kAdjectives[] = {
    "BRAVE", "CALM",   "BOLD",   "KEEN",  "WILD",   "SWIFT",  "QUIET",  "SLY",   "GRAND",  "LUCKY", "NOBLE", "MERRY",
    "SHARP", "STEADY", "CLEVER", "PROUD", "GENTLE", "FIERCE", "SILENT", "SUNNY", "STORMY", "IRON",  "AMBER", "JOLLY",
};

constexpr const char* kNouns[] = {
    "FALCON", "OTTER",  "BADGER", "HERON", "FOX",   "WOLF",  "CRANE", "MOTH",  "PIKE",  "RAVEN", "STAG", "HARE",
    "LYNX",   "MAGPIE", "SWIFT",  "TROUT", "ADDER", "BISON", "EAGLE", "FINCH", "GOOSE", "MOLE",  "NEWT", "OWL",
};

constexpr size_t kAdjectiveCount = sizeof(kAdjectives) / sizeof(kAdjectives[0]);
constexpr size_t kNounCount = sizeof(kNouns) / sizeof(kNouns[0]);

char cached[kMaxNameLength + 1] = {};

uint32_t mix(uint32_t value) {
  // Two rounds of xorshift. The seed is a clock, which walks upward in small
  // steps, and an unmixed low bit would make consecutive rerolls pick adjacent
  // words -- visibly not random when a player taps twice.
  value ^= value << 13;
  value ^= value >> 17;
  value ^= value << 5;
  value *= 2654435761u;
  return value ^ (value >> 16);
}

uint32_t seedFromClock() {
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
  return static_cast<uint32_t>(millis());
#else
  return 1u;
#endif
}

// Next to the reader's own state, so clearing .crosspoint/ clears this too and
// there is one place to look. Inside the guard because the host build has no
// storage and an unused constant is a -Werror failure there.
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
constexpr char kPath[] = "/.crosspoint/player.cfg";
#endif

void store(const char* value) {
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
  char line[kMaxNameLength + 2];
  snprintf(line, sizeof(line), "%s\n", value);
  Storage.writeFile(kPath, String(line));
#else
  (void)value;
#endif
}

bool load(char* out, const size_t capacity) {
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
  if (!Storage.exists(kPath)) return false;
  char buffer[kMaxNameLength + 2] = {};
  if (Storage.readFileToBuffer(kPath, buffer, sizeof(buffer)) == 0) return false;
  size_t length = strnlen(buffer, sizeof(buffer));
  while (length > 0 && (buffer[length - 1] == '\n' || buffer[length - 1] == '\r')) length--;
  if (length == 0 || length >= capacity) return false;
  memcpy(out, buffer, length);
  out[length] = '\0';
  return true;
#else
  (void)out;
  (void)capacity;
  return false;
#endif
}

}  // namespace

size_t adjectiveCount() { return kAdjectiveCount; }
size_t nounCount() { return kNounCount; }

void compose(char* out, const size_t capacity, const uint32_t seed) {
  if (out == nullptr || capacity == 0) return;
  const uint32_t rolled = mix(seed);
  // Two independent draws from one roll: the halves of a mixed word are not
  // correlated, so this is a pair rather than a diagonal through the lists.
  const char* adjective = kAdjectives[rolled % kAdjectiveCount];
  const char* noun = kNouns[(rolled >> 16) % kNounCount];
  snprintf(out, capacity, "%s %s", adjective, noun);
}

const char* name() {
  if (cached[0] != '\0') return cached;
  if (load(cached, sizeof(cached))) return cached;
  // First run. Roll one and keep it, so a device is never nameless and the
  // player never meets a naming screen they did not ask for.
  compose(cached, sizeof(cached), seedFromClock());
  store(cached);
  LOG_INF("PLAYER", "named this device '%s'", cached);
  return cached;
}

const char* reroll() {
  char next[kMaxNameLength + 1] = {};
  // Tapping twice must not land back where it started, so keep rolling until it
  // actually changes. The lists are large enough that this is one extra draw at
  // worst.
  for (int attempt = 0; attempt < 8; ++attempt) {
    compose(next, sizeof(next), seedFromClock() + static_cast<uint32_t>(attempt) * 2654435761u);
    if (strcmp(next, cached) != 0) break;
  }
  memcpy(cached, next, sizeof(cached));
  store(cached);
  return cached;
}

}  // namespace player

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

// Six letters is the ceiling, and it is a drawing budget as much as a wire one:
// three of these plus two spaces is kMaxNameLength exactly. Each word also has
// to name something the artwork can actually show, so the lists and
// assets_local/avatar/ are edited together or not at all.
//
// THE ORDER IS USER-VISIBLE, because tapping a word steps to the next one.
// Each list is a walk rather than a bag: hair runs from none to most, eyes from
// moods to the things you wear on them, mouths from expressions into facial
// hair. Fourteen apiece is 2744 faces, which was the point -- eight per slot
// gave everybody the same handful of faces.
constexpr const char* kHair[] = {"BALD", "TUFTY", "SLICK",  "WAVY", "CURLY", "SPIKY", "PUNK",
                                 "MOP",  "BUN",   "BRAIDS", "PONY", "BOB",   "LONG",  "AFRO"};
constexpr const char* kEyes[] = {"GRIM", "SAD",   "SLY",  "SQUINT", "SLEEPY", "BLINK", "GLAD",
                                 "WINK", "BEADY", "WIDE", "CROSS",  "BUSHY",  "SPECS", "SHADES"};
constexpr const char* kMouth[] = {"GRIN",   "TEETH", "SMIRK",  "GLUM", "POUT",   "FROWN",  "GASP",
                                  "TONGUE", "FANGS", "SCRUFF", "TASH", "GOATEE", "MUTTON", "BEARD"};

struct List {
  const char* const* words;
  size_t count;
};

constexpr List kLists[kSlotCount] = {
    {kHair, sizeof(kHair) / sizeof(kHair[0])},
    {kEyes, sizeof(kEyes) / sizeof(kEyes[0])},
    {kMouth, sizeof(kMouth) / sizeof(kMouth[0])},
};

// kWordCount is what PlayerAvatar.cpp sizes its artwork tables against, so it
// is the one number the two files agree on. Pin it to the lists here rather
// than trusting anyone to update both.
static_assert(kLists[SlotHair].count == kWordCount[SlotHair], "hair list and kWordCount disagree");
static_assert(kLists[SlotEyes].count == kWordCount[SlotEyes], "eyes list and kWordCount disagree");
static_assert(kLists[SlotMouth].count == kWordCount[SlotMouth], "mouth list and kWordCount disagree");

bool validSlot(const int slot) { return slot >= 0 && slot < kSlotCount; }

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

bool Name::known() const {
  for (int slot = 0; slot < kSlotCount; ++slot) {
    if (word[slot] >= kLists[slot].count) return false;
  }
  return true;
}

size_t wordCount(const int slot) { return validSlot(slot) ? kLists[slot].count : 0; }

const char* word(const int slot, const uint8_t index) {
  if (!validSlot(slot) || index >= kLists[slot].count) return nullptr;
  return kLists[slot].words[index];
}

void compose(char* out, const size_t capacity, const Name& name) {
  if (out == nullptr || capacity == 0) return;
  size_t written = 0;
  out[0] = '\0';
  for (int slot = 0; slot < kSlotCount; ++slot) {
    const char* text = word(slot, name.word[slot]);
    if (text == nullptr) continue;
    // snprintf's return is what it *would* have written, so clamping here is
    // what keeps `written` an index into the buffer rather than past its end.
    const int n = snprintf(out + written, capacity - written, written == 0 ? "%s" : " %s", text);
    if (n <= 0) break;
    written += static_cast<size_t>(n);
    if (written >= capacity - 1) break;
  }
}

Name parse(const char* text) {
  Name name;
  if (text == nullptr) return name;
  const char* cursor = text;
  for (int slot = 0; slot < kSlotCount; ++slot) {
    while (*cursor == ' ') cursor++;
    const char* start = cursor;
    while (*cursor != '\0' && *cursor != ' ') cursor++;
    const size_t length = static_cast<size_t>(cursor - start);
    if (length == 0) break;
    for (size_t i = 0; i < kLists[slot].count; ++i) {
      const char* candidate = kLists[slot].words[i];
      if (strlen(candidate) == length && strncmp(candidate, start, length) == 0) {
        name.word[slot] = static_cast<uint8_t>(i);
        break;
      }
    }
  }
  return name;
}

void shortName(const char* name, char* out, const size_t capacity) {
  if (out == nullptr || capacity == 0) return;
  out[0] = '\0';
  if (name == nullptr) return;
  while (*name == ' ') name++;
  size_t written = 0;
  while (name[written] != '\0' && name[written] != ' ' && written + 1 < capacity) {
    out[written] = name[written];
    written++;
  }
  out[written] = '\0';
}

Name roll(const uint32_t seed) {
  const uint32_t rolled = mix(seed);
  Name name;
  // Three draws from one mixed word. The bytes of a mixed value are not
  // correlated, so this is three independent picks rather than a diagonal
  // through the lists -- the same reason the two-word version took its noun
  // from the high half.
  for (int slot = 0; slot < kSlotCount; ++slot) {
    const uint32_t slice = rolled >> (slot * 8);
    name.word[slot] = static_cast<uint8_t>(slice % kLists[slot].count);
  }
  return name;
}

Name nextWord(const Name& current, const int slot) {
  Name next = current;
  if (!validSlot(slot)) return next;
  const size_t count = kLists[slot].count;
  const uint8_t was = current.word[slot];
  // A word this build cannot read has no "next", so the walk starts over.
  next.word[slot] = was >= count ? 0 : static_cast<uint8_t>((was + 1) % count);
  return next;
}

const char* name() {
  if (cached[0] != '\0') return cached;
  char stored[kMaxNameLength + 1] = {};
  if (load(stored, sizeof(stored)) && parse(stored).known()) {
    memcpy(cached, stored, sizeof(cached));
    return cached;
  }
  // Either first run, or a saved name this build cannot read -- which is
  // exactly what a two-word name from before the face existed looks like.
  // Rolling a fresh one is the whole migration: everything downstream may then
  // assume the local name is three words it can draw.
  compose(cached, sizeof(cached), roll(seedFromClock()));
  store(cached);
  LOG_INF("PLAYER", "named this device '%s'", cached);
  return cached;
}

Name parts() { return parse(name()); }

void stepSlot(const int slot) {
  if (!validSlot(slot)) return;
  compose(cached, sizeof(cached), nextWord(parts(), slot));
  store(cached);
}

}  // namespace player

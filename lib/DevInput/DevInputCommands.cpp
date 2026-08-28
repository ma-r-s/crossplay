#include "DevInputCommands.h"

#if CROSSPOINT_DEV_SERIAL_BRIDGE

#include <BoardConfig.h>
#include <HalGPIO.h>

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "DevInputInjector.h"

namespace devinput {
namespace {

// A synthetic contact is released by elapsed >= holdMs, so a negative hold
// becomes an enormous unsigned one and the contact NEVER releases: busy() stays
// true and every later command answers "ERR busy" until someone power-cycles
// the device. Down a cable that was a person's own typo with the device in
// their hand; over Wi-Fi it is a wedged device with no CANCEL verb to unwedge
// it. Bound both durations, and keep coordinates on the panel so the
// float->int conversion downstream stays in range.
constexpr long kMaxHoldMs = 5000;
constexpr long kMaxSwipeMs = 10000;

bool onPanel(long x, long y) {
  return x >= 0 && y >= 0 && x < BoardConfig::ACTIVE.displayWidth && y < BoardConfig::ACTIVE.displayHeight;
}

// Panel-native pixels in, normalized out. The injector takes 0..1 precisely so
// it needs no board geometry; the conversion belongs to whoever speaks pixels.
float normX(long px) { return (static_cast<float>(px) + 0.5f) / BoardConfig::ACTIVE.displayWidth; }
float normY(long py) { return (static_cast<float>(py) + 0.5f) / BoardConfig::ACTIVE.displayHeight; }

// Parse up to n longs out of s; returns how many were found.
int parseLongs(const char* s, long* out, int n) {
  int found = 0;
  char* end = nullptr;
  while (found < n) {
    while (*s == ' ') s++;
    if (*s == '\0') break;
    const long v = strtol(s, &end, 10);
    if (end == s) break;
    out[found++] = v;
    s = end;
  }
  return found;
}

// True when anything follows the arguments already consumed. "Anything" means
// any non-space byte, not "a token that fails to start with a digit": the first
// version accepted 1.5, 1e3 and 100abc, took the integer prefix, and answered
// OK at a duration the caller never chose -- which is precisely the typo a
// remote driver makes and precisely what this exists to refuse. An extra
// argument past the optional one is refused for the same reason: silently
// dropping it is not an answer either.
bool trailingGarbage(const char* s, int consumed) {
  char* end = nullptr;
  for (int i = 0; i < consumed; ++i) {
    strtol(s, &end, 10);
    if (end == s) break;
    s = end;
  }
  // isspace, not ' ': a tab survives the serial path, and refusing it as garbage
  // was wrong -- trailing whitespace is an omission, not a typo.
  while (isspace(static_cast<unsigned char>(*s))) s++;
  return *s != '\0';
}

int buttonIndexByName(const char* name, size_t len) {
  struct Entry {
    const char* name;
    uint8_t index;
  };
  static constexpr Entry MAP[] = {
      {"BACK", HalGPIO::BTN_BACK},   {"CONFIRM", HalGPIO::BTN_CONFIRM}, {"LEFT", HalGPIO::BTN_LEFT},
      {"RIGHT", HalGPIO::BTN_RIGHT}, {"UP", HalGPIO::BTN_UP},           {"DOWN", HalGPIO::BTN_DOWN},
      {"POWER", HalGPIO::BTN_POWER},
  };
  for (const auto& e : MAP) {
    if (strlen(e.name) == len && strncmp(e.name, name, len) == 0) return e.index;
  }
  return -1;
}

// The printf attribute is not decoration: without it these call sites get no
// -Wformat checking at all, and a %d for a long would be silent.
//
// Two helpers rather than one that infers its answer from the text it just
// wrote -- one for each outcome. Re-reading the buffer for an "OK" prefix meant a reply too short to
// hold one reported failure for a command that had in fact been scheduled --
// latent at the 96 bytes both callers pass, and invisible to a test that also
// passes 96. The branch knows which it is; it should say so.
__attribute__((format(printf, 3, 4))) bool scheduled(char* reply, size_t replyLen, const char* fmt, ...);
bool scheduled(char* reply, size_t replyLen, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf(reply, replyLen, fmt, args);
  va_end(args);
  return true;
}

__attribute__((format(printf, 3, 4))) bool refused(char* reply, size_t replyLen, const char* fmt, ...);
bool refused(char* reply, size_t replyLen, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf(reply, replyLen, fmt, args);
  va_end(args);
  return false;
}

}  // namespace

// Leading whitespace is the transports' business only if they agree, and they
// did not: the HTTP handler trims its body while the serial bridge strips only
// \r and \n, so " TAP 400 240" worked over Wi-Fi and answered "unknown command"
// down the cable. That is a device answering TAP on one transport and not the
// other -- the exact sentence check 12 exists to prevent -- and neither check 12
// nor the devinput suite could see it, because both look at runCommand and not
// at what the transports hand it. So skip it HERE, once, for both.
const char* skipLeading(const char* cmd) {
  while (*cmd != '\0' && isspace(static_cast<unsigned char>(*cmd))) cmd++;
  return cmd;
}

bool isCommand(const char* raw) {
  if (raw == nullptr) return false;
  const char* cmd = skipLeading(raw);
  return strncmp(cmd, "TAP ", 4) == 0 || strncmp(cmd, "LONG ", 5) == 0 || strncmp(cmd, "SWIPE ", 6) == 0 ||
         strncmp(cmd, "BTN ", 4) == 0;
}

bool runCommand(const char* raw, char* reply, const size_t replyLen) {
  if (reply == nullptr || replyLen == 0) return false;
  if (raw == nullptr) return refused(reply, replyLen, "ERR no command");
  const char* cmd = skipLeading(raw);
  reply[0] = '\0';
  long v[5];

  if (strncmp(cmd, "TAP ", 4) == 0) {
    const int n = parseLongs(cmd + 4, v, 3);
    if (n < 2) return refused(reply, replyLen, "ERR TAP wants: x y [holdMs]");
    if (trailingGarbage(cmd + 4, n))
      return refused(reply, replyLen, "ERR TAP: holdMs must be a whole number, and nothing after it");
    if (!onPanel(v[0], v[1])) return refused(reply, replyLen, "ERR TAP off panel: %ld %ld", v[0], v[1]);
    if (n >= 3 && (v[2] < 0 || v[2] > kMaxHoldMs)) return refused(reply, replyLen, "ERR holdMs 0..%ld", kMaxHoldMs);
    const unsigned long hold = n >= 3 ? static_cast<unsigned long>(v[2]) : 140;
    if (!devinput::tap(normX(v[0]), normY(v[1]), hold)) return refused(reply, replyLen, "ERR busy");
    return scheduled(reply, replyLen, "OK TAP %ld %ld %lu", v[0], v[1], hold);
  }

  if (strncmp(cmd, "LONG ", 5) == 0) {
    if (parseLongs(cmd + 5, v, 2) < 2) return refused(reply, replyLen, "ERR LONG wants: x y");
    // LONG's duration is fixed in the injector, so a third argument is not
    // "ignored", it is a misunderstanding -- and the reply echoes no duration,
    // so the caller would get no signal at all that it was dropped.
    if (trailingGarbage(cmd + 5, 2)) return refused(reply, replyLen, "ERR LONG takes x y and nothing else");
    if (!onPanel(v[0], v[1])) return refused(reply, replyLen, "ERR LONG off panel: %ld %ld", v[0], v[1]);
    if (!devinput::longPress(normX(v[0]), normY(v[1]))) return refused(reply, replyLen, "ERR busy");
    return scheduled(reply, replyLen, "OK LONG %ld %ld", v[0], v[1]);
  }

  if (strncmp(cmd, "SWIPE ", 6) == 0) {
    const int n = parseLongs(cmd + 6, v, 5);
    if (n < 4) return refused(reply, replyLen, "ERR SWIPE wants: x0 y0 x1 y1 [ms]");
    if (trailingGarbage(cmd + 6, n))
      return refused(reply, replyLen, "ERR SWIPE: ms must be a whole number, and nothing after it");
    if (!onPanel(v[0], v[1]) || !onPanel(v[2], v[3])) {
      return refused(reply, replyLen, "ERR SWIPE off panel");
    }
    if (n >= 5 && (v[4] < 0 || v[4] > kMaxSwipeMs)) return refused(reply, replyLen, "ERR ms 0..%ld", kMaxSwipeMs);
    const unsigned long ms = n >= 5 ? static_cast<unsigned long>(v[4]) : 250;
    if (!devinput::swipe(normX(v[0]), normY(v[1]), normX(v[2]), normY(v[3]), ms)) {
      return refused(reply, replyLen, "ERR busy");
    }
    return scheduled(reply, replyLen, "OK SWIPE %ld %ld %ld %ld %lu", v[0], v[1], v[2], v[3], ms);
  }

  if (strncmp(cmd, "BTN ", 4) == 0) {
    const char* rest = cmd + 4;
    // Any whitespace, not just ' ', on BOTH sides of the name: a tab survives
    // the serial path (only \r and \n are stripped there), and splitting on a
    // space alone made "BTN UP<tab>" -- and "BTN  UP" -- fail as an unknown
    // BUTTON NAME, blaming the one part of the command that was right.
    while (*rest != '\0' && isspace(static_cast<unsigned char>(*rest))) rest++;
    const char* space = rest;
    while (*space != '\0' && !isspace(static_cast<unsigned char>(*space))) space++;
    const size_t nameLen = static_cast<size_t>(space - rest);
    if (*space == '\0') space = nullptr;
    const int index = buttonIndexByName(rest, nameLen);
    if (index < 0) return refused(reply, replyLen, "ERR BTN wants: UP|DOWN|CONFIRM|BACK|LEFT|RIGHT|POWER [holdMs]");
    unsigned long hold = 80;
    if (space != nullptr) {
      const int n = parseLongs(space, v, 1);
      if (trailingGarbage(space, n)) {
        return refused(reply, replyLen, "ERR BTN: holdMs must be a whole number, and nothing after it");
      }
      if (n == 1) {
        if (v[0] < 0 || v[0] > kMaxHoldMs) return refused(reply, replyLen, "ERR holdMs 0..%ld", kMaxHoldMs);
        hold = static_cast<unsigned long>(v[0]);
      }
    }
    if (!devinput::button(static_cast<uint8_t>(index), hold)) return refused(reply, replyLen, "ERR busy");
    return scheduled(reply, replyLen, "OK BTN %.*s %lu", static_cast<int>(nameLen), rest, hold);
  }

  return refused(reply, replyLen, "ERR not an input command");
}

}  // namespace devinput

#endif  // CROSSPOINT_DEV_SERIAL_BRIDGE

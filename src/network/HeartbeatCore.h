#pragma once

// The daily heartbeat and the crash report, as pure functions.
//
// Everything here is freestanding (no Arduino, no heap, no card, no radio) so
// host-tests/heartbeat can pin the parts that would otherwise be checkable
// only with a device on Wi-Fi: the device id is a hash and never the MAC, the
// body is the shape docs/workflow/events.md promises, the state file survives
// a truncated write, and once a day means once a day. The device glue (clock,
// radio, card, TLS) is Heartbeat.cpp.

#include <cstddef>
#include <cstdint>

namespace heartbeat {

constexpr size_t kIdLen = 64;  // sha256 as lowercase hex
constexpr int kMaxApps = 32;
constexpr size_t kMaxAppKey = 24;
constexpr size_t kMaxVersion = 32;
constexpr size_t kMaxOtaError = 32;
constexpr size_t kMaxCrashMessage = 160;
constexpr size_t kMaxCrashTrace = 240;
// The one request body, sized so the longest heartbeat and the longest
// realistic crash (a message made entirely of characters that escape to six
// bytes, and the hex trace) both fit; host-tests/heartbeat proves it.
constexpr size_t kBodySize = 2048;

void sha256(const uint8_t* data, size_t len, uint8_t out[32]);

// sha256(MAC || fixed salt) as 64 lowercase hex digits. The same device hashes
// to the same id on every post, and nothing about the MAC can be read back.
void deviceId(const uint8_t mac[6], char out[kIdLen + 1]);

// A shelf title as the word the board counts: "HACKER NEWS" -> "hackernews".
// Lowercase letters and digits only, cut at kMaxAppKey. False when nothing
// usable remains.
bool appKey(const char* title, char out[kMaxAppKey + 1]);

// What /.crosspoint/heartbeat.json holds between heartbeats.
struct State {
  long lastDay = -1;  // UTC day number of the last accepted heartbeat, -1 for never
  int appCount = 0;
  char apps[kMaxApps][kMaxAppKey + 1] = {};
  // Set when an OTA install starts, cleared by the next heartbeat. Success is
  // never written down: the install reboots the device, so it is inferred
  // from the version that boots afterwards differing from this one.
  char otaFrom[kMaxVersion] = {};
  char otaError[kMaxOtaError] = {};
  // A panic recorded at the boot after it, cleared once the board has it.
  char crashMessage[kMaxCrashMessage] = {};
  char crashTrace[kMaxCrashTrace] = {};
};

// True when `key` was added; false when it was already counted or the set is
// full, so the caller writes the card only for a first open.
bool addApp(State& s, const char* key);

// `out` is reset to defaults first. False when the file has no usable "day",
// which is what a truncated or foreign file looks like.
bool parseState(const char* json, State& out);

// Bytes written excluding the terminator, or 0 when it did not fit.
size_t formatState(const State& s, char* out, size_t outSize);

// UTC day number, or -1 while the clock has not been set (anything before
// 2025 is the RTC's factory default, not a date).
long dayFromEpoch(long long epochSeconds);

enum class Decision : uint8_t { Send, Off, NoClock, Backoff, AlreadyToday };

// millis() is 32 bits and wraps every 49 days; a host's unsigned long is not,
// so the comparison is spelled once, in 32 bits, and shared.
bool backingOff(unsigned long nowMs, unsigned long notBeforeMs);

Decision decide(bool enabled, long today, long lastDay, unsigned long nowMs, unsigned long notBeforeMs);
const char* decisionName(Decision d);

struct Sample {
  const char* version;
  const char* board;
  unsigned uptimeHours;
  unsigned batteryPct;
  unsigned heapMinKb;
};

struct OtaProps {
  bool attempted;
  bool ok;
  const char* error;  // "" when none
};
OtaProps otaProps(const State& s, const char* runningVersion);

// The heartbeat body, exactly as docs/workflow/events.md describes it.
size_t formatHeartbeat(const char* device, const Sample& sample, const State& s, char* out, size_t outSize);

// The level=error event for a recorded panic. 0 when there is none to send.
size_t formatCrash(const char* device, const char* version, const char* board, const State& s, char* out,
                   size_t outSize);

// The board accepted the heartbeat: the apps and the OTA record are its now.
void noteSent(State& s, long today);
void clearCrash(State& s);

// The site's /api/board-config answer: {"url":"https://...","anonKey":"..."}.
bool parseBoardConfig(const char* json, char* url, size_t urlSize, char* key, size_t keySize);

inline bool accepted(const int httpStatus) { return httpStatus >= 200 && httpStatus < 300; }

}  // namespace heartbeat

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
  // After a request fails: the epoch second before which nothing is tried
  // again, and how many failures in a row got it there. On the card, so a
  // device that boots often (deep sleep is a boot) does not pay the stall
  // again at every boot.
  long long retryAt = 0;
  int fails = 0;
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
// so the comparison is spelled once, in 32 bits, and shared. This is the
// RAM-only wait after a request that was never made (heap too low); the wait
// after a request that failed is `retryAt`, below.
bool backingOff(unsigned long nowMs, unsigned long notBeforeMs);

// A failed request costs the user a stall (TCP connect and TLS handshake each
// wait the timeout, and SecureClient tries twice), so the wait escalates: 15
// minutes after the first failure, the rest of the UTC day after the second,
// and one try a day until something is accepted.
constexpr long long kRetryS = 15 * 60;
// The longest wait the rules above can set. A retryAt further away than that
// is a clock that stepped back (or a foreign file), and is ignored rather
// than silencing the device until the clock catches up.
constexpr long long kMaxBackoffS = 86400 + kRetryS;
void noteFailed(State& s, long long epochNow);
void clearBackoff(State& s);
bool backingOffAt(long long epochNow, long long retryAt);

// The one decision per loop pass. A pending crash is sent whatever the day,
// but never without a clock and never inside the backoff: the stall it risks
// is the same stall, and the wait it earns needs a date to be written down.
Decision decide(bool enabled, long today, long long epochNow, const State& s, bool crashPending);
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

// The board accepted the heartbeat: the apps and the OTA record are its now,
// and the backoff is over.
void noteSent(State& s, long today);
void clearCrash(State& s);

// The site's /api/board-config answer: {"url":"https://...","anonKey":"..."}.
bool parseBoardConfig(const char* json, char* url, size_t urlSize, char* key, size_t keySize);

inline bool accepted(const int httpStatus) { return httpStatus >= 200 && httpStatus < 300; }

}  // namespace heartbeat

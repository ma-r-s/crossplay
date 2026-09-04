#pragma once

// Device reporting, as pure functions.
//
// The device never makes a request of its own to report. When the firmware
// talks to one of CrossPlay's own services for some other reason (a catalog
// page, a sync, a reading list), that request carries three headers: the
// pseudonymous device id, the board, and a small JSON report. The service
// posts the events; the device posts nothing.
//
// Everything here is freestanding (no Arduino, no heap, no card, no radio) so
// host-tests/devreport can pin what would otherwise need a device on Wi-Fi:
// the id is a hash and never the MAC, the report is the shape
// docs/workflow/events.md promises and never longer than a header should be,
// the headers go to our hosts and to nobody else, and a 2xx clears what it
// carried. The device glue (clock, card, settings) is DeviceReport.cpp.

#include <cstddef>
#include <cstdint>

namespace devreport {

constexpr size_t kIdLen = 64;  // sha256 as lowercase hex
constexpr size_t kMaxVersion = 32;
constexpr size_t kMaxOtaError = 32;
constexpr size_t kMaxOtaPath = 4;  // "ota" or "sd"
constexpr size_t kMaxCrashMessage = 160;
constexpr size_t kMaxCrashTrace = 240;
constexpr size_t kMaxHost = 128;
// The X-CrossPlay-Report value. A header, so small: the backtrace goes first
// when the crash does not fit, then the message is cut, then the crash is
// dropped; buildReportHeader() never writes more than this many bytes.
constexpr size_t kMaxReportBytes = 600;
// The state file, sized so the longest crash record (a message made entirely
// of characters that escape to six bytes, and the hex trace) fits.
constexpr size_t kStateSize = 2048;

// The header names and the zone, spelled once.
constexpr char kDeviceHeader[] = "X-CrossPlay-Device";
constexpr char kBoardHeader[] = "X-CrossPlay-Board";
constexpr char kReportHeader[] = "X-CrossPlay-Report";
constexpr char kOwnZone[] = "ma-r-s.com";

void sha256(const uint8_t* data, size_t len, uint8_t out[32]);

// sha256(MAC || secret) as 64 lowercase hex digits, where the secret is
// kSecretLen random bytes the device made once and keeps in NVS. The id is
// pseudonymous: the same device is the same id on every request, and without
// that device's own secret the id cannot be matched to a MAC (a vendor
// prefix leaves 2^24 MACs, seconds of work against a fixed salt). With no
// secret (secretLen 0: NVS unavailable) it is sha256(MAC || fixed salt),
// and the device logs that it fell back. A full flash erase makes a new
// secret, so the device comes back as a new id.
constexpr size_t kSecretLen = 16;
void deviceId(const uint8_t mac[6], const uint8_t* secret, size_t secretLen, char out[kIdLen + 1]);

// What /.crosspoint/devreport.json holds while something waits to be
// delivered.
struct State {
  // Set when an install starts, cleared by the first 2xx from one of our
  // hosts. Success is never written down: the install reboots the device, so
  // it is inferred from the version that boots afterwards differing from this
  // one.
  char otaFrom[kMaxVersion] = {};
  char otaError[kMaxOtaError] = {};
  // Which install: "ota" (the update screen, over Wi-Fi) or "sd" (a .bin
  // picked from the card). The same device can fail one and not the other.
  char otaPath[kMaxOtaPath] = {};
  // A panic recorded at the boot after it, cleared once a service has it.
  // The version is the one that crashed: the record outlives reboots and an
  // OTA in between, and the version running when it finally rides out may
  // not be the one to blame.
  char crashMessage[kMaxCrashMessage] = {};
  char crashTrace[kMaxCrashTrace] = {};
  char crashVersion[kMaxVersion] = {};
};

// The two things the device records between requests, each gated on the
// Settings toggle so that "off" records nothing at all, not merely sends
// nothing: a backlog gathered while off would go out the moment it came back
// on. Each returns true when the state changed and the card needs writing.
bool recordCrash(State& s, bool enabled, const char* message, const char* trace, const char* version);
bool recordOtaAttempt(State& s, bool enabled, const char* from, const char* path);
bool recordOtaFailure(State& s, bool enabled, const char* error);

// The toggle went from off to on: whatever the file still holds from before
// it went off (the OTA record, a crash) was never the services' to have, and
// is forgotten.
void noteSwitchedOn(State& s);

// Something is waiting to ride out on the next request to one of our hosts.
bool hasPending(const State& s);

// A request that carried the headers came back. A 2xx means the service has
// the report, so the pending crash and OTA record are cleared. Anything else
// (a refusal, a transport failure spelled as 0 or -1) changes nothing: the
// record waits for the next request. True when the state changed and the
// card needs writing.
bool noteDelivered(State& s, int httpStatus);
void clearCrash(State& s);
void clearOta(State& s);

// `out` is reset to defaults first. False when the text has neither record,
// which is what a truncated or foreign file looks like. The heartbeat-era
// file (with "day", "apps", "retry", "fails" beside the same two records)
// parses; those keys are ignored.
bool parseState(const char* json, State& out);

// Bytes written excluding the terminator, or 0 when it did not fit.
size_t formatState(const State& s, char* out, size_t outSize);

struct OtaProps {
  bool attempted;
  bool ok;
  const char* error;  // "" when none
  const char* path;   // "ota", "sd", or "" when nothing was attempted
};
OtaProps otaProps(const State& s, const char* runningVersion);

// The X-CrossPlay-Report value, exactly as docs/workflow/events.md describes
// it: always {"battery_pct":N,"heap_min_kb":N,"uptime_h":N}, plus "crash"
// and "ota" objects only while each is pending. `runningVersion` names the
// crashed version for a record from a build that did not write it down, and
// tells an OTA that "took" from one that did not. Never longer than
// kMaxReportBytes (see there); 0 only when `outSize` cannot hold even the
// three numbers.
size_t buildReportHeader(const State& s, const char* runningVersion, unsigned batteryPct, unsigned heapMinKb,
                         unsigned uptimeHours, char* out, size_t outSize);

// The host of a URL, lowercased, without scheme, userinfo, port, path or a
// trailing dot. False when there is none or it does not fit.
bool hostOf(const char* url, char* out, size_t outSize);

// True for kOwnZone itself and every name under it, and for nothing else:
// not a name that merely contains it, not a name that continues past it.
// Case-insensitive, like DNS.
bool isOwnHost(const char* host);

// The one question every transport asks before adding the headers: is the
// toggle on, and does this URL go to one of our hosts?
bool reportsTo(bool enabled, const char* url);

// The crash message the services fingerprint. On the ESP32-S3 boards only an
// assert or abort leaves a reason behind (HalSystem.cpp captures the message
// only in __wrap_panic_abort), so a CPU exception arrives with none; the
// reset reason and the subsystem that logged last before the reset are what
// is left to tell two of them apart. `reasonRecorded` is whether THIS crash
// wrote `reason` (the capture marker, read before checkPanic() clears it):
// the text in RTC memory outlives the crash it belongs to, and an exception
// after an assert with no clean boot between must not inherit the assert's
// words. `reason` may be empty, `reset` is the esp_reset_reason() name,
// `lastTag` may be empty.
size_t formatCrashMessage(bool reasonRecorded, const char* reason, const char* reset, const char* lastTag, char* out,
                          size_t outSize);

// The tag ("[ms] [LVL] [TAG] ...") of the last line the previous boot wrote
// to the RTC log ring, out of HalSystem::getPanicInfo(true). The ring is
// oldest to newest and this boot has already logged into it, so the previous
// boot ends where the millis() stamp drops. False when no such drop is in
// the text: the ring is all this boot's, or all the previous one's.
bool lastLogTagBeforeReset(const char* panicInfo, char* out, size_t outSize);

inline bool accepted(const int httpStatus) { return httpStatus >= 200 && httpStatus < 300; }

}  // namespace devreport

#include "DeviceReport.h"

#include <Arduino.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <Logging.h>

#include <cstring>
#include <string>

#if defined(ARDUINO_ARCH_ESP32) && !defined(SIMULATOR)
#include <Preferences.h>
#include <esp_random.h>
#include <esp_system.h>
#endif

#include "CrossPointSettings.h"
#include "DeviceReportCore.h"
#include "FirmwareFlasher.h"
#include "OtaUpdater.h"

#if defined(SIMULATOR)
// FirmwareBoardTag.h #errors without a FREEINK_DEVICE_* flag, which the
// simulator env deliberately does not set.
#define DEVREPORT_BOARD "sim"
#else
#include "FirmwareBoardTag.h"
#define DEVREPORT_BOARD CROSSPOINT_BOARD_NAME
#endif

namespace devreport {

namespace {

constexpr char kTag[] = "DEVREPORT";
constexpr char kStatePath[] = "/.crosspoint/devreport.json";
// What the heartbeat firmware (up to v1.12.13) left on the card: its state
// file, whose two records this reads once and moves, and its cached board
// address and public key, which nothing reads any more.
constexpr char kHeartbeatStatePath[] = "/.crosspoint/heartbeat.json";
constexpr char kHeartbeatBoardPath[] = "/.crosspoint/board.json";

State state;
bool loaded = false;
// The toggle as last seen, so the off-to-on edge can forget what the file
// still holds from before it went off.
bool lastEnabled = true;
char id[kIdLen + 1] = {};
// The report header value for the request in flight, and the scratch for the
// state file: nothing here runs concurrently with anything else here.
char report[kMaxReportBytes + 1];
char scratch[kStateSize];

bool enabled() { return SETTINGS.deviceReport != 0; }

void save() {
  const size_t n = formatState(state, scratch, sizeof(scratch));
  if (n == 0) {
    LOG_ERR(kTag, "state did not fit %u bytes; not saved", static_cast<unsigned>(sizeof(scratch)));
    return;
  }
  if (!Storage.writeFile(kStatePath, String(scratch))) LOG_ERR(kTag, "could not write %s", kStatePath);
}

bool readState(const char* path) {
  const size_t n = Storage.readFileToBuffer(path, scratch, sizeof(scratch));
  if (n == 0) return false;
  scratch[n < sizeof(scratch) ? n : sizeof(scratch) - 1] = '\0';
  if (parseState(scratch, state)) return true;
  LOG_ERR(kTag, "%s unreadable; starting over", path);
  return false;
}

void load() {
  loaded = true;
  if (Storage.exists(kStatePath)) {
    readState(kStatePath);
    return;
  }
  // First boot on this firmware: a crash the heartbeat firmware recorded and
  // never got to send rides out under the new name.
  if (Storage.exists(kHeartbeatStatePath)) {
    if (readState(kHeartbeatStatePath) && hasPending(state)) save();
    Storage.remove(kHeartbeatStatePath);
  }
  if (Storage.exists(kHeartbeatBoardPath)) Storage.remove(kHeartbeatBoardPath);
}

#if defined(ARDUINO_ARCH_ESP32) && !defined(SIMULATOR)
// The secret the id is hashed with: made once from the hardware RNG and
// kept in NVS, never on the card and never sent. False when NVS would not
// give one, in which case the id falls back to the fixed salt.
bool loadOrMakeSecret(uint8_t out[kSecretLen]) {
  Preferences prefs;
  if (!prefs.begin("crossplay", false)) return false;
  bool ok = prefs.getBytesLength("hbsecret") == kSecretLen && prefs.getBytes("hbsecret", out, kSecretLen) == kSecretLen;
  if (!ok) {
    esp_fill_random(out, kSecretLen);
    ok = prefs.putBytes("hbsecret", out, kSecretLen) == kSecretLen;
    if (ok) LOG_INF(kTag, "made the device secret");
  }
  prefs.end();
  return ok;
}
#endif

void computeId() {
  uint8_t mac[6] = {};
  const uint8_t* secret = nullptr;
  size_t secretLen = 0;
#if defined(ARDUINO_ARCH_ESP32) && !defined(SIMULATOR)
  // The factory MAC out of eFuse: needs no radio, and is what the station
  // MAC derives from. Hashed with the device's own secret before it goes
  // anywhere.
  const uint64_t raw = ESP.getEfuseMac();
  for (int i = 0; i < 6; ++i) mac[i] = static_cast<uint8_t>(raw >> (8 * i));
  static uint8_t nvsSecret[kSecretLen];
  if (loadOrMakeSecret(nvsSecret)) {
    secret = nvsSecret;
    secretLen = kSecretLen;
  } else {
    LOG_ERR(kTag, "no NVS secret; the id falls back to the fixed salt");
  }
#endif
  deviceId(mac, secret, secretLen, id);
}

// esp_reset_reason() as a word. It survives the reset when nothing else
// does: on the ESP32-S3 boards a CPU exception leaves no reason behind
// (HalSystem.cpp writes panicMessage only from __wrap_panic_abort) and no
// stack (its __wrap_panic_print_backtrace returns before capturing on
// anything but RISC-V), so this and the last logger are the fingerprint.
const char* resetReasonName() {
#if defined(ARDUINO_ARCH_ESP32) && !defined(SIMULATOR)
  switch (esp_reset_reason()) {
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
      return "int_wdt";
    case ESP_RST_TASK_WDT:
      return "task_wdt";
    case ESP_RST_WDT:
      return "wdt";
    case ESP_RST_CPU_LOCKUP:
      return "cpu_lockup";
    case ESP_RST_BROWNOUT:
      return "brownout";
    case ESP_RST_SW:
      return "sw";
    case ESP_RST_POWERON:
      return "poweron";
    case ESP_RST_EXT:
      return "ext";
    case ESP_RST_DEEPSLEEP:
      return "deepsleep";
    case ESP_RST_SDIO:
      return "sdio";
    default:
      return "unknown";
  }
#else
  return "sim";
#endif
}

// The panic HalSystem kept in RTC memory, cut to what a card needs: the
// reason (or, without one, the reset and the last logger before it), and
// the first two lines of the stack dump /api/dev/crash serves.
//
// The reason is used only when `reasonRecorded` says this crash wrote it.
// panicMessage outlives the crash it belongs to (checkPanic() clears the
// marker and keeps the text for CrashActivity; only a clean boot clears the
// text), and on Xtensa a CPU exception writes nothing there, so an exception
// after an assert with no clean boot between would otherwise be fingerprinted
// as that assert.
void capturePanic(const bool reasonRecorded) {
  if (!enabled()) {
    LOG_INF(kTag, "panic seen; device info is off, so nothing is recorded");
    return;
  }
  const std::string reason = HalSystem::getPanicInfo(false);
  if (!reasonRecorded && !reason.empty()) {
    LOG_INF(kTag, "reason in RTC memory is a previous crash's; not used: %.40s", reason.c_str());
  }
  const std::string full = HalSystem::getPanicInfo(true);
  char lastTag[16] = {};
  lastLogTagBeforeReset(full.c_str(), lastTag, sizeof(lastTag));
  char message[kMaxCrashMessage];
  formatCrashMessage(reasonRecorded, reason.c_str(), resetReasonName(), lastTag, message, sizeof(message));
  char trace[kMaxCrashTrace] = {};
  static constexpr char kStackHeader[] = "Stack memory:\n";
  const char* p = std::strstr(full.c_str(), kStackHeader);
  if (p != nullptr) {
    p += sizeof(kStackHeader) - 1;
    size_t n = 0;
    int lines = 0;
    for (; *p != '\0' && lines < 2 && n + 1 < sizeof(trace); ++p) {
      if (*p == '\n') {
        ++lines;
        if (lines < 2) trace[n++] = '|';
      } else {
        trace[n++] = *p;
      }
    }
    trace[n] = '\0';
  }
  // A panic reset boots the same partition, so the version running now is
  // the one that crashed; the record may ride out from a later one.
  if (!recordCrash(state, true, message, trace, CROSSPOINT_VERSION)) return;
  LOG_INF(kTag, "panic recorded for the next request to a CrossPlay service: %s", state.crashMessage);
  save();
}

// Does a request to `url` carry the headers? The toggle, the host, and never
// the simulator: it has no MAC, no board and no battery, and a run of the
// suite must not count as a user.
bool reporting(const char* url) {
#if defined(SIMULATOR)
  (void)url;
  return false;
#else
  return reportsTo(enabled(), url);
#endif
}

// The toggle is read at every entry point rather than polled from loop():
// nothing here runs between requests, so the edge is applied at the first
// thing that would otherwise act on the old value.
void syncToggle() {
  const bool on = enabled();
  if (on == lastEnabled) return;
  lastEnabled = on;
  if (!on) return;
  // Off recorded nothing, but the file may still hold what was gathered
  // before it went off; that was never the services' to have.
  if (hasPending(state)) {
    noteSwitchedOn(state);
    save();
  }
  LOG_INF(kTag, "switched on; starting from nothing");
}

}  // namespace

void begin(const bool rebootedFromPanic, const bool panicReasonRecorded) {
  computeId();
  load();
  lastEnabled = enabled();
  LOG_INF(kTag, "%s; device %.8s..; %s pending", lastEnabled ? "on" : "off", id,
          hasPending(state) ? (state.crashMessage[0] != '\0' ? "a crash" : "an install record") : "nothing");
  if (rebootedFromPanic) capturePanic(panicReasonRecorded);
}

int headersFor(const char* url, Header out[kHeaderCount]) {
  if (!loaded) return 0;
  syncToggle();
  if (!reporting(url)) return 0;
  const size_t n = buildReportHeader(state, CROSSPOINT_VERSION, powerManager.getBatteryPercentage(),
                                     static_cast<unsigned>(ESP.getMinFreeHeap() / 1024),
                                     static_cast<unsigned>(millis() / 3600000UL), report, sizeof(report));
  if (n == 0) {
    // Cannot happen: the three numbers fit any buffer this size. Said once
    // rather than on every request.
    static bool said = false;
    if (!said) LOG_ERR(kTag, "report did not fit %u bytes; sending no headers", static_cast<unsigned>(sizeof(report)));
    said = true;
    return 0;
  }
  if (hasPending(state)) {
    LOG_INF(kTag, "%s riding on this request: %s", state.crashMessage[0] != '\0' ? "crash" : "install record", report);
  } else {
    LOG_DBG(kTag, "report on this request: %s", report);
  }
  out[0] = {kDeviceHeader, id};
  out[1] = {kBoardHeader, DEVREPORT_BOARD};
  out[2] = {kReportHeader, report};
  return kHeaderCount;
}

void delivered(const char* url, const int httpStatus) {
  if (!loaded) return;
  syncToggle();
  if (!reporting(url)) return;
  if (!noteDelivered(state, httpStatus)) return;
  LOG_INF(kTag, "delivered (HTTP %d); nothing pending now", httpStatus);
  save();
}

void noteOtaAttempt(const char* path) {
  if (!loaded) return;
  syncToggle();
  if (!recordOtaAttempt(state, enabled(), CROSSPOINT_VERSION, path)) return;
  LOG_INF(kTag, "%s install attempt from %s recorded", state.otaPath, state.otaFrom);
  save();
}

void noteOtaFailed(const char* error) {
  if (!loaded) return;
  syncToggle();
  if (!recordOtaFailure(state, enabled(), error)) return;
  LOG_INF(kTag, "install failure recorded: %s", state.otaError);
  save();
}

const char* otaErrorName(const int otaUpdaterError) {
  switch (static_cast<OtaUpdater::OtaUpdaterError>(otaUpdaterError)) {
    case OtaUpdater::OK:
      return "";
    case OtaUpdater::NO_UPDATE:
      return "no_update";
    case OtaUpdater::HTTP_ERROR:
      return "http";
    case OtaUpdater::JSON_PARSE_ERROR:
      return "json";
    case OtaUpdater::UPDATE_OLDER_ERROR:
      return "older";
    case OtaUpdater::INTERNAL_UPDATE_ERROR:
      return "internal";
    case OtaUpdater::OOM_ERROR:
      return "oom";
    case OtaUpdater::WRONG_DEVICE_ERROR:
      return "wrong_device";
    case OtaUpdater::TOO_LARGE_ERROR:
      return "too_large";
  }
  return "unknown";
}

const char* flashErrorName(const int firmwareFlashResult) {
  using firmware_flash::Result;
  switch (static_cast<Result>(firmwareFlashResult)) {
    case Result::OK:
      return "";
    case Result::TOO_LARGE:
      return "too_large";
    case Result::TOO_SMALL:
      return "too_small";
    case Result::BAD_CHIP:
    case Result::WRONG_BOARD:
      return "wrong_device";
    case Result::OOM:
      return "oom";
    case Result::OPEN_FAIL:
    case Result::READ_FAIL:
      return "read";
    case Result::BAD_MAGIC:
    case Result::BAD_SEGMENTS:
    case Result::BAD_CHECKSUM:
    case Result::BAD_SHA:
    case Result::BAD_SIZE:
      return "invalid";
    case Result::NO_PARTITION:
      return "no_partition";
    case Result::ERASE_FAIL:
    case Result::WRITE_FAIL:
    case Result::OTADATA_FAIL:
      return "write";
  }
  return "unknown";
}

}  // namespace devreport

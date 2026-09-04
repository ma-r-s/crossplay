#include "Heartbeat.h"

#include <Arduino.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <Logging.h>
#include <WiFi.h>

#include <cstring>
#include <ctime>
#include <string>

#if defined(ARDUINO_ARCH_ESP32) && !defined(SIMULATOR)
#include <Preferences.h>
#include <esp_random.h>
#include <esp_system.h>
#endif

#include "CrossPointSettings.h"
#include "FirmwareFlasher.h"
#include "HeartbeatCore.h"
#include "OtaUpdater.h"

#if defined(SIMULATOR)
// FirmwareBoardTag.h #errors without a FREEINK_DEVICE_* flag, which the
// simulator env deliberately does not set.
#define HEARTBEAT_BOARD "sim"
#else
#include "FirmwareBoardTag.h"
#define HEARTBEAT_BOARD CROSSPOINT_BOARD_NAME
#endif

#if defined(FREEINK_NET_WOLFSSL)
#include <SecureHttpClient.h>

// The bridge's bundle covers both hosts: the site (Vercel, Let's Encrypt
// under ISRG Root X1) and the board (supabase.co, Google Trust Services under
// GTS Root R4). Both chains were read on 2026-09-03; a CA move shows up here
// as "not sent: HTTP 0" in the log, the same way it would for sync.
#include "../apps_local/study/StudySyncRoots.h"
#endif

#ifndef CROSSPLAY_BOARD_CONFIG_URL
#define CROSSPLAY_BOARD_CONFIG_URL "https://crossplay.ma-r-s.com/api/board-config"
#endif

namespace heartbeat {

namespace {

constexpr char kTag[] = "HEARTBEAT";
constexpr char kStatePath[] = "/.crosspoint/heartbeat.json";
// The site's /api/board-config answer, cached so the address and the public
// key are one fetch per card rather than one per day, and refetched when the
// board refuses the key (a rotation is a Vercel setting, not a release).
constexpr char kBoardPath[] = "/.crosspoint/board.json";
constexpr unsigned long kHeapRetryMs = 60UL * 1000UL;
// Per network wait, and SecureClient spends it up to four times on one
// request (TCP connect, then the handshake, then both again with a TLS
// 1.2-only hello), all of it inline in loop() with input and the power
// button waiting behind it. 5s bounds a blackholed :443 at 10s once, and the
// persisted backoff below makes once mean once.
constexpr uint32_t kNetTimeoutMs = 5000;
constexpr size_t kUrlSize = 160;
constexpr size_t kKeySize = 320;

State state;
bool loaded = false;
bool crashPending = false;
// The toggle as last seen, so the off-to-on edge can forget what the file
// still holds from before it went off.
bool lastEnabled = true;
unsigned long notBefore = 0;
Decision lastLogged = Decision::Send;
long lastLoggedDay = -2;
char id[kIdLen + 1] = {};
char boardUrl[kUrlSize] = {};
char boardKey[kKeySize] = {};
bool boardLoaded = false;
// One request body at a time, and the same bytes serve as the scratch for the
// two card files: nothing here runs concurrently with anything else here.
char body[kBodySize];

void save() {
  const size_t n = formatState(state, body, sizeof(body));
  if (n == 0) {
    LOG_ERR(kTag, "state did not fit %u bytes; not saved", static_cast<unsigned>(sizeof(body)));
    return;
  }
  if (!Storage.writeFile(kStatePath, String(body))) LOG_ERR(kTag, "could not write %s", kStatePath);
}

void load() {
  loaded = true;
  if (!Storage.exists(kStatePath)) return;
  const size_t n = Storage.readFileToBuffer(kStatePath, body, sizeof(body));
  if (n == 0) return;
  body[n < sizeof(body) ? n : sizeof(body) - 1] = '\0';
  if (!parseState(body, state)) LOG_ERR(kTag, "%s unreadable; starting over", kStatePath);
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
void capturePanic() {
  const bool on = SETTINGS.heartbeat != 0;
  if (!on) {
    LOG_INF(kTag, "panic seen; heartbeat is off, so nothing is recorded");
    return;
  }
  const std::string reason = HalSystem::getPanicInfo(false);
  const std::string full = HalSystem::getPanicInfo(true);
  char lastTag[16] = {};
  lastLogTagBeforeReset(full.c_str(), lastTag, sizeof(lastTag));
  char message[kMaxCrashMessage];
  formatCrashMessage(reason.c_str(), resetReasonName(), lastTag, message, sizeof(message));
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
  // the one that crashed; the record may be posted from a later one.
  if (!recordCrash(state, on, message, trace, CROSSPOINT_VERSION)) return;
  crashPending = true;
  LOG_INF(kTag, "panic recorded for the board: %s", state.crashMessage);
  save();
}

#if defined(FREEINK_NET_WOLFSSL)

// TLS wants ~35KB free with a 20KB block (the sync bridge's numbers). A
// heartbeat is never worth an OOM in whatever app brought the radio up.
bool heapTooLow() {
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t maxBlock = ESP.getMaxAllocHeap();
  if (freeHeap < 35000 || maxBlock < 20000) {
    LOG_INF(kTag, "heap too low for TLS (free=%u block=%u); later", freeHeap, maxBlock);
    return true;
  }
  return false;
}

// HTTP status, or 0 when no answer came back (DNS, TLS, timeout), or -1 when
// no request was made at all.
int postEvent(const char* payload, const size_t len) {
  if (heapTooLow()) return -1;
  freeink::SecureHttpClient http;
  http.setCACert(study::kBridgeCaRoots);
  http.setTimeout(kNetTimeoutMs);
  std::string url = boardUrl;
  url += "/rest/v1/events";
  if (!http.begin(url)) {
    LOG_ERR(kTag, "board address did not make sense: %s", boardUrl);
    return 0;
  }
  http.addHeader("apikey", boardKey);
  http.addHeader("Authorization", std::string("Bearer ") + boardKey);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");
  const int status = http.sendRequest("POST", reinterpret_cast<const uint8_t*>(payload), len);
  http.end();
  return status < 0 ? 0 : status;
}

// True when the site answered and the answer parsed. `made` says whether a
// request went out at all (false: heap too low), so the caller can tell a
// failure that cost a stall from one that did not.
bool fetchBoardConfig(bool& made) {
  made = false;
  if (heapTooLow()) return false;
  made = true;
  freeink::SecureHttpClient http;
  http.setCACert(study::kBridgeCaRoots);
  http.setTimeout(kNetTimeoutMs);
  if (!http.begin(std::string(CROSSPLAY_BOARD_CONFIG_URL))) {
    LOG_ERR(kTag, "config address did not make sense: %s", CROSSPLAY_BOARD_CONFIG_URL);
    return false;
  }
  const int status = http.GET();
  if (status != 200) {
    LOG_ERR(kTag, "board config: HTTP %d from %s", status, CROSSPLAY_BOARD_CONFIG_URL);
    http.end();
    return false;
  }
  const std::string& text = http.getString();
  if (!parseBoardConfig(text.c_str(), boardUrl, sizeof(boardUrl), boardKey, sizeof(boardKey))) {
    LOG_ERR(kTag, "board config unreadable (%u bytes)", static_cast<unsigned>(text.size()));
    http.end();
    return false;
  }
  if (!Storage.writeFile(kBoardPath, String(text.c_str()))) LOG_ERR(kTag, "could not cache %s", kBoardPath);
  http.end();
  LOG_INF(kTag, "board is %s (cached)", boardUrl);
  return true;
}

#else

// The simulator's HTTP stub carries no bodies and the browser build has no
// sockets. Every decision above this still runs and logs; only the request
// does not. Returning 0 puts the module on the same 15-minute retry as a
// device that cannot reach the board.
int postEvent(const char* payload, const size_t len) {
  (void)payload;
  LOG_INF(kTag, "simulator has no transport; would post %u bytes", static_cast<unsigned>(len));
  return 0;
}

bool fetchBoardConfig(bool& made) {
  made = true;
  LOG_INF(kTag, "simulator has no transport; no board config");
  return false;
}

#endif

// The cached config, without a request. False means the next pass's request
// is the fetch.
bool loadBoardConfig() {
  if (boardLoaded) return true;
  if (!Storage.exists(kBoardPath)) return false;
  const size_t n = Storage.readFileToBuffer(kBoardPath, body, sizeof(body));
  if (n == 0) return false;
  body[n < sizeof(body) ? n : sizeof(body) - 1] = '\0';
  if (parseBoardConfig(body, boardUrl, sizeof(boardUrl), boardKey, sizeof(boardKey))) {
    boardLoaded = true;
    LOG_DBG(kTag, "board is %s (from card)", boardUrl);
    return true;
  }
  LOG_ERR(kTag, "%s unreadable; fetching again", kBoardPath);
  return false;
}

void forgetBoardConfig(const char* why) {
  LOG_INF(kTag, "board refused the key (%s); will fetch the config again", why);
  boardLoaded = false;
  Storage.remove(kBoardPath);
}

// A request that went out and came back with nothing usable: the wait
// escalates and is written down, so a reboot does not pay the stall again.
void failed(const char* what, const long long epoch) {
  noteFailed(state, epoch);
  if (state.fails >= 2) {
    LOG_ERR(kTag, "%s failed (%d in a row); not before tomorrow", what, state.fails);
  } else {
    LOG_ERR(kTag, "%s failed; retry in %lld min", what, kRetryS / 60);
  }
  save();
}

// One request. On a 2xx the caller's `onAccepted` runs; anything else backs
// off. -1 (no request made) waits a minute in RAM, not fifteen on the card.
template <typename Accepted>
void send(const char* what, const size_t len, const unsigned long now, const long long epoch, Accepted onAccepted) {
  const int status = postEvent(body, len);
  if (status == -1) {
    notBefore = now + kHeapRetryMs;
    return;
  }
  if (accepted(status)) {
    LOG_INF(kTag, "%s sent (HTTP %d)", what, status);
    clearBackoff(state);
    onAccepted();
    return;
  }
  if (status == 401 || status == 403) forgetBoardConfig(what);
  LOG_ERR(kTag, "%s not sent: HTTP %d", what, status);
  failed(what, epoch);
}

void sendCrash(const unsigned long now, const long long epoch) {
  const size_t len = formatCrash(id, CROSSPOINT_VERSION, HEARTBEAT_BOARD, state, body, sizeof(body));
  if (len == 0) {
    // Nothing to send: either no message after all, or a record that cannot
    // be formatted. Either way it is dropped rather than kept pending forever.
    if (state.crashMessage[0] != '\0') LOG_ERR(kTag, "crash record did not fit %u bytes; dropped", static_cast<unsigned>(sizeof(body)));
    crashPending = false;
    clearCrash(state);
    save();
    return;
  }
  send("crash report", len, now, epoch, [] {
    crashPending = false;
    clearCrash(state);
    save();
  });
}

void sendHeartbeat(const unsigned long now, const long long epoch, const long today) {
  Sample sample;
  sample.version = CROSSPOINT_VERSION;
  sample.board = HEARTBEAT_BOARD;
  sample.uptimeHours = static_cast<unsigned>(millis() / 3600000UL);
  sample.batteryPct = powerManager.getBatteryPercentage();
  sample.heapMinKb = static_cast<unsigned>(ESP.getMinFreeHeap() / 1024);
  const size_t len = formatHeartbeat(id, sample, state, body, sizeof(body));
  if (len == 0) {
    // Cannot happen with the suite's fullest heartbeat fitting; if it does,
    // the day is written off rather than retried into the same wall.
    LOG_ERR(kTag, "body did not fit %u bytes; skipping day %ld", static_cast<unsigned>(sizeof(body)), today);
    noteSent(state, today);
    save();
    return;
  }
  const OtaProps ota = otaProps(state, CROSSPOINT_VERSION);
  LOG_INF(kTag, "posting day %ld: %d app(s), ota %s, battery %u%%, heap min %uKB", today, state.appCount,
          ota.attempted ? (ota.ok ? "ok" : (ota.error[0] ? ota.error : "did not take")) : "none", sample.batteryPct,
          sample.heapMinKb);
  send("heartbeat", len, now, epoch, [today] {
    noteSent(state, today);
    save();
  });
}

// One line per change of mind, not one per loop pass: "already today" is the
// answer several thousand times a day.
void logDecision(const Decision d, const long today) {
  if (d == lastLogged && today == lastLoggedDay) return;
  lastLogged = d;
  lastLoggedDay = today;
  LOG_INF(kTag, "%s (day %ld, last sent %ld)", decisionName(d), today, state.lastDay);
}

}  // namespace

void begin(const bool rebootedFromPanic) {
  computeId();
  load();
  lastEnabled = SETTINGS.heartbeat != 0;
  LOG_INF(kTag, "%s; device %.8s..; last sent day %ld; %d app(s) pending", SETTINGS.heartbeat ? "on" : "off", id,
          state.lastDay, state.appCount);
  if (rebootedFromPanic) {
    capturePanic();
  } else if (state.crashMessage[0] != '\0') {
    // Recorded at an earlier boot and never delivered.
    crashPending = true;
  }
}

void update() {
  if (!loaded) return;
  const bool on = SETTINGS.heartbeat != 0;
  if (on != lastEnabled) {
    lastEnabled = on;
    if (on) {
      // Off recorded nothing, but the file may still hold what was gathered
      // before it went off; that was never the board's to have.
      noteSwitchedOn(state);
      crashPending = false;
      save();
      LOG_INF(kTag, "switched on; starting from nothing");
    }
  }
  if (!on) {
    logDecision(Decision::Off, -1);
    return;
  }
  if (WiFi.status() != WL_CONNECTED) return;
  const unsigned long now = millis();
  if (backingOff(now, notBefore)) return;
  const long long epoch = static_cast<long long>(time(nullptr));
  const long today = dayFromEpoch(epoch);
  const Decision d = decide(true, today, epoch, state, crashPending);
  logDecision(d, today);
  if (d != Decision::Send) return;
  // Strictly one request per pass: the config fetch is a pass of its own,
  // and the post is the next one.
  if (!loadBoardConfig()) {
    bool made = false;
    boardLoaded = fetchBoardConfig(made);
    if (!boardLoaded) {
      if (made) {
        failed("board config", epoch);
      } else {
        notBefore = now + kHeapRetryMs;
      }
    }
    return;
  }
  if (crashPending) {
    sendCrash(now, epoch);
  } else {
    sendHeartbeat(now, epoch, today);
  }
}

void noteAppOpened(const char* title) {
  if (!loaded) return;
  if (!noteAppOpen(state, SETTINGS.heartbeat != 0, title)) return;
  LOG_DBG(kTag, "first open of %s since the last heartbeat", state.apps[state.appCount - 1]);
  save();
}

void noteOtaAttempt(const char* path) {
  if (!loaded) return;
  if (!recordOtaAttempt(state, SETTINGS.heartbeat != 0, CROSSPOINT_VERSION, path)) return;
  LOG_INF(kTag, "%s install attempt from %s recorded", state.otaPath, state.otaFrom);
  save();
}

void noteOtaFailed(const char* error) {
  if (!loaded) return;
  if (!recordOtaFailure(state, SETTINGS.heartbeat != 0, error)) return;
  LOG_INF(kTag, "ota failure recorded: %s", state.otaError);
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

}  // namespace heartbeat

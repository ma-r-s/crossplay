#include "DevMode.h"

#include <Arduino.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_random.h>

#include <cstdio>
#include <cstring>
#include <memory>

#include "CrossPointSettings.h"
#include "WifiCredentialStore.h"
#include "network/CrossPointWebServer.h"

namespace devmode {
namespace {

enum class State {
  Off,        // the setting is off; nothing running, nothing to do
  NoNetwork,  // on, but no saved network to join -- said once, then quiet
  Joining,    // on, WiFi.begin() issued, waiting on the AP
  Serving,    // on, joined, control server up
  Waiting,    // on, joined, but the server could not start; backing off
};

// Whether another owner (the web transfer screen) holds ports 80/81/8134.
//
// A LATCH, deliberately not a State. As a state it was bypassable: update()
// reconciles the setting before it checks for a yield, so toggling dev mode off
// and on again while the web screen was open walked straight back into
// startJoin() and bound the same ports a second time -- and dev mode being OFF
// made pause() a no-op, so enabling it from that very screen did the same. A
// latch is checked on every path and cannot be cleared by a state transition.
bool yielded = false;

// The pairing code, in RTC memory so it survives the reboot that every flash
// causes. Without this the token died with the reboot AND the code changed, so
// a flash-test loop needed someone to walk to the device and read a new code
// after every single flash -- which is most of "no cable" given back.
//
// It weakens nothing: the code is displayed continuously on the device panel
// for as long as Developer Mode is on, so persisting it across a reset exposes
// it to nobody who could not already read it. The TOKEN is still RAM-only and
// still dies with the reboot. RTC memory does not survive power loss, and
// tearDown() invalidates the magic, so switching Developer Mode off really does
// rotate the code.
constexpr uint32_t kPairMagic = 0x50414952;  // 'PAIR'
RTC_NOINIT_ATTR char rtcPairCode[8];
RTC_NOINIT_ATTR uint32_t rtcPairMagic;

State state = State::Off;
std::unique_ptr<CrossPointWebServer> server;
std::string ssid;
std::string password;
std::string pairingCode;
std::string activeToken;
bool paired = false;
// Whether WE brought the radio up. Dev mode must only ever put down a
// connection it made itself: the reader has a dozen other things that use
// Wi-Fi, and switching the radio off underneath one of them is indistinguishable
// from that feature being broken.
bool broughtRadioUp = false;
// Wrong-code attempts since the last rotation, and the earliest millis() at
// which another attempt will be considered.
int pairFailures = 0;
unsigned long pairNotBefore = 0;
unsigned long nextAttemptAt = 0;
unsigned long joinDeadline = 0;
int attempt = 0;

constexpr unsigned long kJoinTimeoutMs = 20000;
constexpr unsigned long kMinBackoffMs = 5000;
constexpr unsigned long kMaxBackoffMs = 60000;
// One attempt per second caps a grind at ~86k/day against a 10^6 space, and the
// rotation below means those attempts are not cumulative anyway.
constexpr unsigned long kPairRetryMs = 1000;
constexpr int kPairFailuresBeforeRotate = 5;

unsigned long backoff() {
  const unsigned long ms = kMinBackoffMs << (attempt < 4 ? attempt : 4);
  return ms > kMaxBackoffMs ? kMaxBackoffMs : ms;
}

// Six digits from the hardware RNG. Short enough to read off an e-ink panel and
// type, which is the whole job -- it guards a LAN endpoint against a stranger,
// not a firmware image against an attacker with the device in hand.
std::string makeCode() {
  char buf[7];
  snprintf(buf, sizeof(buf), "%06u", static_cast<unsigned>(esp_random() % 1000000u));
  return std::string(buf);
}

// 32 hex characters, also from the hardware RNG. Never shown to the user and
// never written to the card: a token dies with the reboot that ends the session
// it belongs to, so a stale one in a script cannot outlive dev mode being off.
std::string makeToken() {
  char buf[33];
  for (int i = 0; i < 4; ++i) snprintf(buf + i * 8, 9, "%08x", static_cast<unsigned>(esp_random()));
  return std::string(buf, 32);
}

void stopServer(const char* why) {
  if (server) {
    LOG_INF("DEVMODE", "control server down (%s)", why);
    server->stop();
    server.reset();
  }
}

// Everything that must stop when the toggle goes off, including the tokens.
void tearDown(const char* why) {
  stopServer(why);
  if (!activeToken.empty() || paired) {
    LOG_INF("DEVMODE", "revoking tokens");
  }
  activeToken.clear();
  pairingCode.clear();
  // Invalidate the RTC copy so the next enable mints a fresh code. Switching
  // Developer Mode off is the one action that must retire the old one.
  rtcPairMagic = 0;
  paired = false;
  attempt = 0;
  nextAttemptAt = 0;
  // Only put down what we picked up. A user who turns dev mode off did not ask
  // to stay online -- but nor did the one who is halfway through downloading a
  // book on a connection something else established.
  if (broughtRadioUp) {
    if (WiFi.status() == WL_CONNECTED) WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    broughtRadioUp = false;
  }
}

void startJoin() {
  attempt++;
  LOG_INF("DEVMODE", "joining '%s' (attempt %d)", ssid.c_str(), attempt);
  // Do not take a radio that is already in use. Something else owning it --
  // a link session, an OPDS download -- outranks a background convenience.
  if (WiFi.getMode() != WIFI_MODE_NULL && WiFi.status() == WL_CONNECTED && !broughtRadioUp) {
    LOG_INF("DEVMODE", "radio already in use by something else; not joining");
    state = State::Waiting;
    nextAttemptAt = millis() + kMaxBackoffMs;
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.empty() ? nullptr : password.c_str());
  joinDeadline = millis() + kJoinTimeoutMs;
  state = State::Joining;
}

// Read the saved network and mint a fresh code. Called every time dev mode goes
// off -> on, so the code a stranger might have seen last week is already dead.
void turnOn() {
  WIFI_STORE.loadFromFile();
  ssid = WIFI_STORE.getLastConnectedSsid();
  // Reuse the code across a reboot, mint a new one on a real off -> on.
  if (rtcPairMagic == kPairMagic && rtcPairCode[0] != '\0' && strnlen(rtcPairCode, sizeof(rtcPairCode)) == 6) {
    pairingCode.assign(rtcPairCode, 6);
  } else {
    pairingCode = makeCode();
  }
  std::snprintf(rtcPairCode, sizeof(rtcPairCode), "%s", pairingCode.c_str());
  rtcPairMagic = kPairMagic;
  activeToken.clear();
  paired = false;
  pairFailures = 0;
  pairNotBefore = 0;
  if (ssid.empty()) {
    LOG_INF("DEVMODE", "on, but no saved network; join one in Network settings first");
    state = State::NoNetwork;
    return;
  }
  const auto cred = WIFI_STORE.findCredential(ssid);
  password = cred ? cred->password : std::string();
  LOG_INF("DEVMODE", "on; pairing code %s", pairingCode.c_str());
  attempt = 0;
  startJoin();
}

}  // namespace

void begin() {
  // Nothing eager here. update() reconciles against the setting on its first
  // call, so boot and a mid-session toggle take exactly the same path and there
  // is no second code path to keep in step.
  state = State::Off;
}

void update() {
  // Before anything else. While another owner holds the ports, dev mode does
  // not touch the radio, the server, or its own state -- including when the
  // setting changes underneath it. resume() reconciles whatever it finds.
  if (yielded) return;

  const bool want = SETTINGS.devMode != 0;

  if (!want) {
    if (state != State::Off) {
      LOG_INF("DEVMODE", "off");
      tearDown("developer mode switched off");
      state = State::Off;
    }
    return;
  }

  if (state == State::Off) {
    turnOn();
    return;
  }

  if (state == State::NoNetwork) {
    // A network may have been joined since. Cheap to re-read: this only runs
    // while dev mode is on and no network was found.
    static unsigned long nextLook = 0;
    const unsigned long now = millis();
    if (nextLook == 0 || static_cast<long>(now - nextLook) >= 0) {
      nextLook = now + 5000;
      WIFI_STORE.loadFromFile();
      if (!WIFI_STORE.getLastConnectedSsid().empty()) {
        LOG_INF("DEVMODE", "a network was saved; joining");
        turnOn();
      }
    }
    return;
  }

  const unsigned long now = millis();

  if (state == State::Joining) {
    if (WiFi.status() == WL_CONNECTED) {
      attempt = 0;
      // Only now is the radio ours. Setting this at WiFi.begin() time made
      // holdsRadio() true while dev mode held nothing -- so with the AP out of
      // range every guarded activity skipped its cleanup forever, on a device
      // that also no longer deep-sleeps. Both heap-reclaim paths gone at once.
      broughtRadioUp = true;
      LOG_INF("DEVMODE", "joined '%s' as %s", ssid.c_str(), WiFi.localIP().toString().c_str());
      // makeUniqueNoThrow, not bare new: with -fno-exceptions a failed new
      // calls abort() rather than returning null, so the isRunning() check
      // below could never have run. This path fires on every reconnect, which
      // is exactly when the heap may be short.
      server = makeUniqueNoThrow<CrossPointWebServer>(/*devOnly=*/true);
      if (!server) {
        LOG_ERR("DEVMODE", "out of memory allocating the control server (free heap %u)", ESP.getFreeHeap());
        nextAttemptAt = now + backoff();
        state = State::Waiting;
        return;
      }
      server->begin();
      if (!server->isRunning()) {
        LOG_ERR("DEVMODE", "control server failed to start (free heap %u)", ESP.getFreeHeap());
        server.reset();
        nextAttemptAt = now + backoff();
        state = State::Waiting;
        return;
      }
      LOG_INF("DEVMODE", "ready at http://%s/  pair with code %s", WiFi.localIP().toString().c_str(),
              pairingCode.c_str());
      state = State::Serving;
      return;
    }
    if (static_cast<long>(now - joinDeadline) >= 0) {
      LOG_INF("DEVMODE", "join timed out; retrying in %lums", backoff());
      WiFi.disconnect();
      nextAttemptAt = now + backoff();
      joinDeadline = now + kJoinTimeoutMs + backoff();
    }
    if (nextAttemptAt != 0 && static_cast<long>(now - nextAttemptAt) >= 0) {
      nextAttemptAt = 0;
      startJoin();
    }
    return;
  }

  if (state == State::Waiting) {
    // The server could not be allocated or started. Wait out the backoff rather
    // than retrying every loop: the old code returned before its own timer was
    // ever read, so it constructed and destroyed a whole server on every
    // iteration, fragmenting the heap hardest precisely when it was shortest.
    if (WiFi.status() != WL_CONNECTED) {
      nextAttemptAt = now + kMinBackoffMs;
      joinDeadline = now + kJoinTimeoutMs + kMinBackoffMs;
      state = State::Joining;
      return;
    }
    if (static_cast<long>(now - nextAttemptAt) >= 0) {
      LOG_INF("DEVMODE", "retrying the control server");
      state = State::Joining;  // the CONNECTED branch above starts the server
    }
    return;
  }

  // Serving.
  if (WiFi.status() != WL_CONNECTED) {
    stopServer("wifi dropped");
    nextAttemptAt = now + kMinBackoffMs;
    joinDeadline = now + kJoinTimeoutMs + kMinBackoffMs;
    state = State::Joining;
    return;
  }
  if (server) server->handleClient();
}

void pause() {
  // Unconditional, including when dev mode is off: the point is to hold the
  // latch for as long as the other owner has the ports, so that dev mode being
  // enabled DURING that window cannot bind them underneath it.
  if (yielded) return;
  yielded = true;
  if (state != State::Off) stopServer("web server screen opened");
}

void resume() {
  if (!yielded) return;
  yielded = false;
  if (state == State::Off) return;  // never ran; nothing of ours to put down
  if (SETTINGS.devMode == 0) {
    tearDown("developer mode switched off while the web screen was open");
    state = State::Off;
    return;
  }
  LOG_INF("DEVMODE", "web server screen closed; taking the ports back");
  attempt = 0;
  nextAttemptAt = 0;
  joinDeadline = millis() + kJoinTimeoutMs;
  // Rejoin rather than assume: that screen may have switched to AP mode or
  // joined a different network entirely.
  state = ssid.empty() ? State::NoNetwork : State::Joining;
}

bool serving() { return state == State::Serving && server && server->isRunning(); }

bool holdsRadio() { return broughtRadioUp && SETTINGS.devMode != 0; }

bool inhibitsSleep() { return SETTINGS.devMode != 0; }

Status status() {
  Status s;
  s.enabled = SETTINGS.devMode != 0;
  s.connected = state == State::Serving;
  s.code = pairingCode;
  s.ssid = ssid;
  if (s.connected) s.ip = WiFi.localIP().toString().c_str();
  return s;
}

bool tokenValid(const std::string& token) {
  // Constant-time-ish: compare the whole string rather than returning early, so
  // a caller cannot learn the prefix from response timing. Cheap at 32 bytes.
  if (!paired || activeToken.empty() || token.size() != activeToken.size()) return false;
  unsigned diff = 0;
  for (size_t i = 0; i < activeToken.size(); ++i) {
    diff |= static_cast<unsigned>(activeToken[i] ^ token[i]);
  }
  return diff == 0;
}

std::string pair(const std::string& code) {
  if (SETTINGS.devMode == 0 || pairingCode.empty()) return std::string();

  // A minimum interval between attempts. Six digits is 10^6, which a LAN can
  // walk in hours unattended -- and until this branch disabled CORS on this
  // surface, so could a web page the owner merely visited.
  const unsigned long now = millis();
  if (pairNotBefore != 0 && static_cast<long>(now - pairNotBefore) < 0) {
    LOG_ERR("DEVMODE", "pairing attempt too soon; ignored");
    return std::string();
  }

  if (code != pairingCode) {
    pairFailures++;
    pairNotBefore = now + kPairRetryMs;
    // ROTATE rather than lock out. A lockout would hand anyone on the network a
    // way to stop the owner pairing; moving the target instead throws away
    // every guess made so far and costs the owner only a glance at the screen,
    // which is already showing the new code by the time they look.
    if (pairFailures >= kPairFailuresBeforeRotate) {
      pairingCode = makeCode();
      std::snprintf(rtcPairCode, sizeof(rtcPairCode), "%s", pairingCode.c_str());
      rtcPairMagic = kPairMagic;
      pairFailures = 0;
      LOG_ERR("DEVMODE", "too many wrong codes; rotated to %s", pairingCode.c_str());
    } else {
      LOG_ERR("DEVMODE", "wrong pairing code (%d of %d before rotation)", pairFailures, kPairFailuresBeforeRotate);
    }
    return std::string();
  }

  pairFailures = 0;
  pairNotBefore = 0;
  activeToken = makeToken();
  paired = true;
  LOG_INF("DEVMODE", "paired");
  return activeToken;
}

}  // namespace devmode

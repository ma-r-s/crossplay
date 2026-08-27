#include "DevMode.h"

#include <Arduino.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_random.h>

#include <memory>

#include "CrossPointSettings.h"
#include "WifiCredentialStore.h"
#include "network/CrossPointWebServer.h"

namespace devmode {
namespace {

enum class State {
  Off,         // the setting is off; nothing running, nothing to do
  NoNetwork,   // on, but no saved network to join -- said once, then quiet
  Joining,     // on, WiFi.begin() issued, waiting on the AP
  Serving,     // on, joined, control server up
  YieldedOut,  // on, but the web server activity owns the ports
};

State state = State::Off;
std::unique_ptr<CrossPointWebServer> server;
std::string ssid;
std::string password;
std::string pairingCode;
std::string activeToken;
bool paired = false;
unsigned long nextAttemptAt = 0;
unsigned long joinDeadline = 0;
int attempt = 0;

constexpr unsigned long kJoinTimeoutMs = 20000;
constexpr unsigned long kMinBackoffMs = 5000;
constexpr unsigned long kMaxBackoffMs = 60000;

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
  paired = false;
  attempt = 0;
  nextAttemptAt = 0;
  // Leave the radio as we found it. Disconnecting is the honest counterpart to
  // having connected: a user who turns dev mode off did not ask to stay online.
  if (WiFi.status() == WL_CONNECTED) WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

void startJoin() {
  attempt++;
  LOG_INF("DEVMODE", "joining '%s' (attempt %d)", ssid.c_str(), attempt);
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
  pairingCode = makeCode();
  activeToken.clear();
  paired = false;
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

  if (state == State::YieldedOut) return;

  const unsigned long now = millis();

  if (state == State::Joining) {
    if (WiFi.status() == WL_CONNECTED) {
      attempt = 0;
      LOG_INF("DEVMODE", "joined '%s' as %s", ssid.c_str(), WiFi.localIP().toString().c_str());
      server.reset(new CrossPointWebServer(/*devOnly=*/true));
      server->begin();
      if (!server->isRunning()) {
        LOG_ERR("DEVMODE", "control server failed to start (free heap %u)", ESP.getFreeHeap());
        server.reset();
        nextAttemptAt = now + backoff();
        joinDeadline = now + kJoinTimeoutMs + backoff();
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
  if (state == State::Off || state == State::YieldedOut) return;
  stopServer("web server screen opened");
  state = State::YieldedOut;
}

void resume() {
  if (state != State::YieldedOut) return;
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
  if (SETTINGS.devMode == 0 || pairingCode.empty() || code != pairingCode) {
    LOG_ERR("DEVMODE", "pairing refused");
    return std::string();
  }
  activeToken = makeToken();
  paired = true;
  LOG_INF("DEVMODE", "paired");
  return activeToken;
}

}  // namespace devmode

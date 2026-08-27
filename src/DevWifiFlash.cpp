#include "DevWifiFlash.h"

#if CROSSPOINT_DEV_WIFI_FLASH

#include <Arduino.h>
#include <Logging.h>
#include <WiFi.h>

#include <memory>

#include "WifiCredentialStore.h"
#include "network/CrossPointWebServer.h"

namespace devwifi {
namespace {

enum class State {
  Disabled,    // no saved network; nothing to do, and we say so once
  Joining,     // WiFi.begin() issued, waiting on the AP
  Serving,     // joined and the web server is up
  YieldedOut,  // the web server activity has the ports
};

State state = State::Disabled;
std::unique_ptr<CrossPointWebServer> server;
std::string ssid;
std::string password;
unsigned long nextAttemptAt = 0;
unsigned long joinDeadline = 0;
int attempt = 0;

// Long enough that a slow DHCP is not mistaken for a failure, short enough
// that a device left on the desk retries within a coffee break.
constexpr unsigned long kJoinTimeoutMs = 20000;
constexpr unsigned long kMinBackoffMs = 5000;
constexpr unsigned long kMaxBackoffMs = 60000;

unsigned long backoff() {
  unsigned long ms = kMinBackoffMs << (attempt < 4 ? attempt : 4);
  return ms > kMaxBackoffMs ? kMaxBackoffMs : ms;
}

void startJoin() {
  attempt++;
  LOG_INF("DEVWIFI", "joining '%s' (attempt %d)", ssid.c_str(), attempt);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.empty() ? nullptr : password.c_str());
  joinDeadline = millis() + kJoinTimeoutMs;
  state = State::Joining;
}

void stopServer(const char* why) {
  if (server) {
    LOG_INF("DEVWIFI", "web server down (%s)", why);
    server->stop();
    server.reset();
  }
}

}  // namespace

void begin() {
  WIFI_STORE.loadFromFile();
  ssid = WIFI_STORE.getLastConnectedSsid();
  if (ssid.empty()) {
    // Not a failure worth retrying: there is nothing to join until someone
    // picks a network on the device once. Say it plainly, once, because the
    // alternative is a dev wondering why wifi-flash.sh cannot find anything.
    LOG_INF("DEVWIFI",
            "no last-connected network saved; join skipped. "
            "Pick one once in Settings -> Network and it sticks.");
    state = State::Disabled;
    return;
  }
  const auto cred = WIFI_STORE.findCredential(ssid);
  password = cred ? cred->password : std::string();
  startJoin();
}

void update() {
  if (state == State::Disabled || state == State::YieldedOut) return;

  const unsigned long now = millis();

  if (state == State::Joining) {
    if (WiFi.status() == WL_CONNECTED) {
      attempt = 0;
      LOG_INF("DEVWIFI", "joined '%s' as %s", ssid.c_str(), WiFi.localIP().toString().c_str());
      server.reset(new CrossPointWebServer());
      server->begin();
      if (!server->isRunning()) {
        // Almost always heap. Drop it rather than hold a dead object, and let
        // the backoff retry when whatever was using the memory has let go.
        LOG_ERR("DEVWIFI", "web server failed to start (free heap %u)", ESP.getFreeHeap());
        server.reset();
        nextAttemptAt = now + backoff();
        state = State::Joining;
        joinDeadline = now + kJoinTimeoutMs;
        return;
      }
      LOG_INF("DEVWIFI", "ready: http://%s/  (POST /api/dev/flash)", WiFi.localIP().toString().c_str());
      state = State::Serving;
      return;
    }
    if (static_cast<long>(now - joinDeadline) >= 0) {
      LOG_INF("DEVWIFI", "join timed out; retrying in %lums", backoff());
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
    // The AP went away, or the device slept and came back. Tear the server
    // down before rejoining: its sockets are bound to an address we no longer
    // hold, and reusing it silently serves nobody.
    stopServer("wifi dropped");
    nextAttemptAt = now + kMinBackoffMs;
    joinDeadline = now + kJoinTimeoutMs + kMinBackoffMs;
    state = State::Joining;
    return;
  }
  if (server) server->handleClient();
}

void pause() {
  if (state == State::YieldedOut) return;
  stopServer("web server screen opened");
  state = State::YieldedOut;
}

void resume() {
  if (state != State::YieldedOut) return;
  LOG_INF("DEVWIFI", "web server screen closed; taking the ports back");
  // Rejoin rather than assume: the activity may have switched to AP mode or
  // joined a different network entirely, so the connection we had is not
  // necessarily the connection we have.
  attempt = 0;
  nextAttemptAt = 0;
  if (ssid.empty()) {
    state = State::Disabled;
    return;
  }
  if (WiFi.status() == WL_CONNECTED) {
    joinDeadline = millis() + kJoinTimeoutMs;
    state = State::Joining;  // update() sees CONNECTED and brings the server up
    return;
  }
  startJoin();
}

bool serving() { return state == State::Serving && server && server->isRunning(); }

}  // namespace devwifi

#endif  // CROSSPOINT_DEV_WIFI_FLASH

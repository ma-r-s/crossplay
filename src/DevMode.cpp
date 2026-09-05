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
#include "DevModePairing.h"
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

// Whether another owner holds the radio or ports 80/81/8134: the web transfer
// screen, the Wi-Fi picker, or a link match.
//
// A LATCH, deliberately not a State. As a state it was bypassable: update()
// reconciles the setting before it checks for a yield, so toggling dev mode off
// and on again while the web screen was open walked straight back into
// startJoin() and bound the same ports a second time -- and dev mode being OFF
// made pause() a no-op, so enabling it from that very screen did the same. A
// latch is checked on every path and cannot be cleared by a state transition.
//
// A COUNT rather than a bool, because the holders nest. The web server screen
// yields for the ports and then opens the Wi-Fi picker, which yields for the
// radio; as a bool the picker's resume() handed dev mode back while the screen
// underneath was still serving on 80/81/8134, and dev mode rebound them under
// it -- the exact collision pause() exists to prevent. Only the outermost
// release reconciles.
int yieldDepth = 0;

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
pairing::State pairState;

unsigned long nextAttemptAt = 0;
unsigned long joinDeadline = 0;
int attempt = 0;
// Set by resume(); consumed by startJoin(), which runs off the render path.
bool reloadCredentials = false;
// True once WE issued the WiFi.begin() for the join now in progress. A
// connection we did not ask for is somebody else's to put down.
bool joinIssued = false;

constexpr unsigned long kJoinTimeoutMs = 20000;
constexpr unsigned long kMinBackoffMs = 5000;
constexpr unsigned long kMaxBackoffMs = 60000;
// Wrong guesses back off exponentially from one second, and the code cannot
// rotate more often than once a minute.
//
// Both numbers exist because the first design was a denial of pairing wearing a
// comment that said it was not. One per second and rotate-every-five-failures
// meant anyone who could reach the device rotated the six digits EVERY FIVE
// SECONDS from a single loop -- less time than the owner needs to read the panel
// and type, so their attempt always arrived against a dead code, counted as
// another wrong guess, and brought the next rotation closer. The comment in
// pair() said rotation was chosen precisely so nobody could stop the owner
// pairing. Rotation was that lockout, by a different route.
//
// It is reachable from outside the LAN, too: /api/dev/pair takes the code as a
// query argument on a POST, which is a CORS-simple request, so a page the owner
// merely visits can fire it cross-origin. It cannot read the reply -- CORS is
// off on this surface -- but forcing rotation needs no reply.

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
  joinIssued = true;
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
  pairState = pairing::State{};
  // Switching dev mode on IS a rotation, and it starts the floor -- the minute
  // right after enabling is exactly when the owner is walking to their terminal
  // with the code, and the one an attacker would most want to churn.
  pairState.lastRotate = millis();
  joinIssued = false;
  if (ssid.empty()) {
    LOG_INF("DEVMODE", "on, but no saved network; join one in Network settings first");
    // Clear both, the same way update()'s NoNetwork path does. Fixing one of two
    // identical sites is exactly how the pair() bug happened.
    password.clear();
    nextAttemptAt = 0;
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
  if (yieldDepth > 0) return;

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
    // Before the CONNECTED check, not after, and not inside startJoin(): when
    // the holder handed the radio back still connected we short-circuit
    // straight to Serving below and startJoin() never runs, which left the
    // panel naming the network the user had just left. Here because update()
    // runs from loop() with no render lock -- see resume().
    if (reloadCredentials) {
      reloadCredentials = false;
      WIFI_STORE.loadFromFile();
      ssid = WIFI_STORE.getLastConnectedSsid();
      if (ssid.empty()) {
        // Assign first, THEN test. Keeping the old value on this path left the
        // panel deriving hasNetwork from a network that no longer exists, so it
        // showed "connecting" forever instead of "join a network first" -- which
        // is what the user sees after forgetting the current network in the
        // Wi-Fi picker, the one flow that reaches here.
        LOG_INF("DEVMODE", "no saved network to go back to; idle until one is joined");
        password.clear();
        // Left set and in the past, this made the later turnOn() -> startJoin()
        // fire again on the next pass: one redundant WiFi.begin(), and `attempt`
        // inflated to 2, so the first backoff was 5000<<2 = 20s instead of
        // 5000<<1 = 10s. (attempt++ is the first line of startJoin(), so a
        // backoff of 5s is unreachable.)
        nextAttemptAt = 0;
        state = State::NoNetwork;
        return;
      }
      const auto cred = WIFI_STORE.findCredential(ssid);
      password = cred ? cred->password : std::string();
    }
    if (WiFi.status() == WL_CONNECTED) {
      attempt = 0;
      // Only now is the radio ours -- and only if it was OUR join that produced
      // it. Adopting any connection found while Joining meant the Wi-Fi picker's
      // own successful join was claimed by dev mode, so ten activities skipped
      // the teardown of a radio they owned, and switching dev mode off later
      // disconnected a network it had never joined.
      //
      // (Setting it at WiFi.begin() time instead made holdsRadio() true while
      // dev mode held nothing, so with the AP out of range every guarded
      // activity skipped its cleanup forever, on a device that also no longer
      // deep-sleeps.)
      // joinIssued alone is not enough: it survives a join timeout and the whole
      // backoff window after a drop, so an activity that brought Wi-Fi up in the
      // meantime got claimed by dev mode -- which then skipped that activity's
      // teardown, re-issued WiFi.begin() over its download, and armed tearDown()
      // to disconnect a network dev mode never joined. Ask which network we are
      // actually on as well.
      const bool ours = joinIssued && WiFi.SSID() == String(ssid.c_str());
      if (joinIssued && !ours) {
        // The expensive direction, and it was silent: a false negative here
        // means tearDown() never puts the radio down, so switching Developer
        // Mode off leaves the device associated forever on a device that also
        // does not sleep -- with nothing in the log saying why.
        LOG_ERR("DEVMODE", "joined '%s' but the radio reports '%s'; not claiming it", ssid.c_str(),
                WiFi.SSID().c_str());
      }
      broughtRadioUp = ours;
      LOG_INF("DEVMODE", "joined '%s' as %s", ssid.c_str(), WiFi.localIP().toString().c_str());
      // makeUniqueNoThrow, not bare new: with -fno-exceptions a failed new
      // calls abort() rather than returning null, so the isRunning() check
      // below could never have run. This path fires on every reconnect, which
      // is exactly when the heap may be short.
      server = makeUniqueNoThrow<CrossPointWebServer>(CrossPointWebServer::Surface::DeveloperOnly);
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
  // latch for as long as the other owner has the radio or the ports, so that
  // dev mode being enabled DURING that window cannot take them underneath it.
  if (yieldDepth++ > 0) return;
  if (state != State::Off) stopServer("something else took the radio");
}

void resume() {
  // An unbalanced resume() is somebody else's bug, but swallowing it here is
  // still better than a negative count that never lets dev mode back.
  if (yieldDepth == 0) return;
  if (--yieldDepth > 0) return;
  if (state == State::Off) return;  // never ran; nothing of ours to put down
  if (SETTINGS.devMode == 0) {
    tearDown("developer mode switched off while something else held the radio");
    state = State::Off;
    return;
  }
  LOG_INF("DEVMODE", "radio handed back; taking the ports back");
  attempt = 0;
  nextAttemptAt = 0;

  // If the holder gave the radio back switched off -- which is exactly what a
  // link match does, WiFi.mode(WIFI_OFF) in Radio::end() -- then whatever dev
  // mode raised is gone, and saying otherwise is the lie that matters: ten
  // activities gate their radio teardown on holdsRadio(), and every one of them
  // would skip it. Worse, an AP that has
  // since gone out of range means the join below never completes, so nothing
  // would ever re-evaluate it. Only clear it here, never in pause(): during a
  // yield the association is still the one dev mode raised, and the web
  // transfer screen reads holdsRadio() precisely so it does not reboot to tear
  // down a connection that is not its own.
  //
  // getMode()/status() rather than WiFi.STA.connected(): the simulator's WiFi
  // shim is an external dependency with no STA member, and clearing is the safe
  // direction anyway -- a false clear costs an activity one teardown it did not
  // strictly need, a false keep costs ten activities the teardown they did.
  if (WiFi.getMode() == WIFI_MODE_NULL || WiFi.status() != WL_CONNECTED) broughtRadioUp = false;
  // Rejoin rather than assume: the holder may have switched to AP mode, joined
  // a different network entirely, or -- a link match -- handed the radio back
  // switched off.
  //
  // "A different network entirely" is not hypothetical and was not handled: the
  // Wi-Fi picker exists to change the network and is pushed by nine different
  // screens, so the saved SSID here is routinely stale by the time we get the
  // radio back. Re-read it, or dev mode spends the rest of the session
  // retrying a network the user has just left.
  // Reload and join on the NEXT update() pass, not here.
  //
  // resume() can run with the render mutex held -- ~LinkActivity reaches it
  // through ActivityManager::exitActivity(lock) -- and loadFromFile() takes
  // storeMutex, reads the card, parses JSON, and can write it back on a format
  // upgrade. PersistableStore.h warns in as many words against putting that on
  // the render path. update() runs from loop() with no lock, which is where it
  // belongs; startJoin() does the reload when it gets there.
  //
  // Firing on the next pass rather than immediately also keeps the join out of
  // onExit() entirely, so it cannot interleave with a holder still putting its
  // own radio down. It costs one loop iteration, against the 25s the original
  // joinDeadline-with-no-attempt cost.
  reloadCredentials = true;
  joinIssued = false;  // whatever is up now, we did not raise it
  nextAttemptAt = millis();
  if (nextAttemptAt == 0) nextAttemptAt = 1;  // 0 is the "no attempt pending" value
  joinDeadline = millis() + kJoinTimeoutMs;
  state = State::Joining;
}

bool holdsRadio() {
  // Self-checking, not a latch. The one caller that reads this DURING a yield
  // is the web transfer screen's onExit(), and it reads it eleven lines before
  // resume() gets a chance to correct broughtRadioUp -- so a screen that took
  // the radio into AP mode was still told dev mode held an association that
  // WIFI_AP had already destroyed, and skipped its own teardown on the strength
  // of it. Note what that teardown actually is on the shipping boards: both the
  // X4 Pro and the Sticky have touch, so silentRestart() takes
  // finishWifiSessionWithoutRestart() and RETURNS -- it stops SNTP and switches
  // the radio off in place. What gets skipped is a Wi-Fi shutdown, not a
  // reboot; the defrag reboot only happens on boards without touch.
  //
  // Asking the radio costs nothing and cannot go stale. In the case that
  // matters -- a screen reusing dev mode's own STA -- this still answers yes,
  // because that association really is dev mode's and really is up.
  return broughtRadioUp && SETTINGS.devMode != 0 && WiFi.getMode() != WIFI_MODE_NULL && WiFi.status() == WL_CONNECTED;
}

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

unsigned long pairRetryInMs() {
  if (pairState.notBefore == 0) return 0;
  const long remaining = static_cast<long>(pairState.notBefore - millis());
  return remaining > 0 ? static_cast<unsigned long>(remaining) : 0;
}

std::string pair(const std::string& code) {
  if (SETTINGS.devMode == 0 || pairingCode.empty()) return std::string();

  // The decision itself lives in DevModePairing.h as a pure function, and
  // host-tests/devpair exercises it. That split is not tidiness: three cold
  // review rounds found three different bugs in these twenty lines, none of
  // them visible to any suite, on the gate in front of replacing this device's
  // firmware. This function is now only the part that needs a device.
  // Qualified, not `using namespace pairing`: this file already has its own
  // State enum for the join state machine, and the two would collide.
  const pairing::Outcome out = pairing::decide(code == pairingCode, millis(), pairState);
  // ONE assignment. Copying the fields back individually is how round twelve
  // showed the shipped bug could return with every test green: drop the
  // notBefore line and the gate never closes again.
  pairState = out.next;

  if (out.rotate) {
    pairingCode = makeCode();
    std::snprintf(rtcPairCode, sizeof(rtcPairCode), "%s", pairingCode.c_str());
    rtcPairMagic = kPairMagic;
    LOG_ERR("DEVMODE", "too many wrong codes; rotated to %s", pairingCode.c_str());
  }

  if (out.verdict != pairing::Verdict::Accept) return std::string();

  activeToken = makeToken();
  paired = true;
  LOG_INF("DEVMODE", "paired");
  return activeToken;
}

}  // namespace devmode

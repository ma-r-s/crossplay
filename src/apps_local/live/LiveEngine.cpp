#include "LiveEngine.h"

#include <HalStorage.h>
#include <Logging.h>

#include <ctime>

#include "LiveBridge.h"
#include "LiveCore.h"

#if defined(FREEINK_NET_WOLFSSL)
#include <Arduino.h>
#include <WiFi.h>
#include <sys/time.h>

// Relative, both of them. The device envs carry no -Isrc (only the simulator
// does), so a bare "WifiCredentialStore.h" resolves from src/ and nowhere else
// -- which is every file that has ever included it, and none of them is here.
#include "../../DevMode.h"
#include "../../WifiCredentialStore.h"
#endif

namespace live {
namespace engine {

namespace {

int64_t nowEpoch() { return static_cast<int64_t>(std::time(nullptr)); }

// Set the clock from X-Server-Time.
//
// This is the only clock Live has. The ESP32 keeps system time across deep
// sleep on the RTC, so one successful pull makes every later "is a refresh
// due?" answerable without asking anybody -- which is what lets a device that
// was picked up and put down arm the REMAINDER of its interval instead of a
// fresh one.
void adoptServerTime(const int64_t serverEpoch) {
  if (!clockIsUsable(serverEpoch)) return;
#if defined(FREEINK_NET_WOLFSSL)
  // Only forward, and only by more than a minute. A settimeofday on every pull
  // would step the clock under anything else timing against it for the sake of
  // a second's drift, and Live is not the device's timekeeper -- it is a
  // consumer that would rather not be years wrong.
  const int64_t current = nowEpoch();
  if (current >= serverEpoch - 60) return;
  timeval tv{};
  tv.tv_sec = static_cast<time_t>(serverEpoch);
  settimeofday(&tv, nullptr);
  LOG_INF("LIVE", "clock set from the service");
#else
  (void)serverEpoch;
#endif
}

#if defined(FREEINK_NET_WOLFSSL)
bool broughtRadioUp = false;
bool yieldedDevMode = false;

// The headless join, from DevMode::startJoin's template including the part that
// matters most: a radio somebody else is using is not ours to take.
//
// Live is a BACKGROUND CONVENIENCE and it loses every argument about the radio.
// A link match, an OPDS download and Developer Mode all outrank it, and
// Developer Mode is the one that bites: it holds the radio for as long as the
// toggle is on, so a Live wake that brought the radio down afterwards would cut
// off the wireless flashing somebody is in the middle of -- and a Live wake
// that joined underneath dev mode's own retry would race it (radio-has-one-owner,
// and the five activities that once rebooted the device to tear down a
// connection they did not own).
bool joinWifi(std::string& message) {
  if (WiFi.status() == WL_CONNECTED) {
    // Already up, and not ours. Used as it stands and left exactly as found:
    // nothing below runs, so releaseWifi() has nothing to put down.
    LOG_INF("LIVE", "using the connection that is already up");
    return true;
  }
  if (devmode::holdsRadio()) {
    // Developer Mode owns the radio and is between attempts. Its retry is the
    // one that should win -- somebody is waiting on it to flash a build -- and
    // if it cannot reach the network right now then neither can this.
    LOG_INF("LIVE", "Developer Mode holds the radio; not joining");
    message = "Wi-Fi is busy. Live will try again later.";
    return false;
  }

  WIFI_STORE.loadFromFile();
  const std::string ssid = WIFI_STORE.getLastConnectedSsid();
  if (ssid.empty()) {
    message = "This reader has no Wi-Fi network saved. Connect it once from Settings.";
    return false;
  }
  const auto credential = WIFI_STORE.findCredential(ssid);
  if (!credential.has_value()) {
    message = "The saved Wi-Fi password for this network is gone. Connect it again from Settings.";
    return false;
  }

  // Stand Developer Mode down for the length of this request, the way the web
  // server and the Wi-Fi picker do. Without it dev mode's update() can issue
  // its own WiFi.begin() underneath this one.
  devmode::pause();
  yieldedDevMode = true;

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), credential->password.empty() ? nullptr : credential->password.c_str());
  broughtRadioUp = true;
  const unsigned long deadline = millis() + kJoinTimeoutMs;
  while (millis() < deadline) {
    if (WiFi.status() == WL_CONNECTED) {
      LOG_INF("LIVE", "joined '%s'", ssid.c_str());
      return true;
    }
    delay(100);
  }
  LOG_ERR("LIVE", "could not join '%s' in %ums", ssid.c_str(), static_cast<unsigned>(kJoinTimeoutMs));
  message = "Could not reach Wi-Fi. Live will try again later.";
  return false;
}

// Put down only what we picked up. A radio that was already up belongs to
// whoever brought it up.
void releaseWifi() {
  if (broughtRadioUp) {
    if (WiFi.status() == WL_CONNECTED) WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    broughtRadioUp = false;
  }
  // And hand Developer Mode back, whether or not the join worked. A pause with
  // no matching resume leaves dev mode off for the rest of the session with
  // nothing on any screen saying why -- the device simply stops accepting
  // firmware and the toggle still reads ON.
  if (yieldedDevMode) {
    devmode::resume();
    yieldedDevMode = false;
  }
}
#else
// The simulator has the laptop's network and no radio to own. Named rather than
// ifdef'd at each call site so the two builds read as one flow.
bool joinWifi(std::string&) { return true; }
void releaseWifi() {}
#endif

// Is the .part a whole picture?
//
// Asked of the FILE rather than of the byte count alone, because the count is
// only half the question: the BMP says how long it should be in its own first
// six bytes, and the two have to agree. Reading them back also proves the card
// took the write, which a return value from a buffered writer does not.
bool partIsWholePicture(const size_t received) {
  HalFile file;
  if (!Storage.openFileForRead("LIVE", kSleepImagePart, file)) return false;
  uint8_t head[6] = {};
  const int got = file.read(head, sizeof(head));
  file.close();
  if (got != static_cast<int>(sizeof(head))) return false;
  return bmpIsComplete(head, sizeof(head), received);
}

// Move a verified image over the one on the glass.
//
// Never opens /sleep.bmp for writing: the bytes are already on the card under
// a .part name and already checked, so the only thing left is a rename, which
// either happens or does not. That is what makes "never blank the panel on a
// failed wake" a property of the code rather than a hope.
bool commitImage() {
  if (!Storage.exists(kSleepImagePart)) return false;
  Storage.remove(kSleepImage);
  if (!Storage.rename(kSleepImagePart, kSleepImage)) {
    LOG_ERR("LIVE", "could not put the new image in place");
    Storage.remove(kSleepImagePart);
    return false;
  }
  return true;
}

}  // namespace

RadioLease::RadioLease(std::string& message) { held_ = joinWifi(message); }

// Puts the radio down only if this lease is what brought it up: joinWifi
// returns true without touching anything when somebody else already had it,
// and releaseWifi knows the difference.
RadioLease::~RadioLease() { releaseWifi(); }

bool checkNow(State& state, bool& imageArrived, std::string& message) {
  imageArrived = false;
  message.clear();
  if (!state.paired()) {
    message = "This reader is not connected to a phone yet.";
    return false;
  }

  if (!joinWifi(message)) {
    // releaseWifi() BEFORE the early return, not only on the success paths: a
    // join that failed after devmode::pause() still owes dev mode its resume,
    // and a half-raised radio still owes the modem its power domain back.
    releaseWifi();
    state.lastAttemptEpoch = nowEpoch();
    ++state.consecutiveFailures;
    save(state);
    return false;
  }

  PullResult result;
  const bool ok = pull(state.deviceToken, state.etag, state.on, kSleepImagePart, result);

  // The schedule headers are believed on every status that carried them,
  // including the failures that still answered. A 401 knows the cadence just as
  // well as a 200 does.
  if (result.serverEpoch > 0) adoptServerTime(result.serverEpoch);
  if (result.nextWakeSeconds > 0) state.intervalSeconds = result.nextWakeSeconds;
  // Adopted only when it was sent. A service that stops sending it leaves the
  // last cadence standing rather than reverting to the sleep, for the same
  // reason an absent X-Next-Wake keeps the interval we had: one quiet reply
  // must not rewrite a deliberate schedule.
  if (result.cadenceSeconds > 0) state.cadenceSeconds = result.cadenceSeconds;
  state.lastAttemptEpoch = nowEpoch();

  if (!ok) {
    if (result.status == 401) {
      // The phone let this reader go. Forget the token rather than spend a wake
      // a day proving it is still dead, and leave the toggle where the user put
      // it.
      forgetPairing(state);
      save(state);
      releaseWifi();
      message = result.message;
      return false;
    }
    ++state.consecutiveFailures;
    save(state);
    releaseWifi();
    message = result.message.empty() ? "Could not reach Live. It will try again later." : result.message;
    return false;
  }

  if (result.status == 200) {
    if (!partIsWholePicture(result.bytes)) {
      // A torn picture on a sleep screen is indistinguishable from a broken
      // device, so it never reaches the glass. The .part goes and yesterday's
      // message stays, which is the correct failure state; the ETag is NOT
      // stored, so the next wake asks for the same image again rather than
      // claiming to hold one it threw away.
      LOG_ERR("LIVE", "the image did not arrive whole (%u bytes); keeping what is on the glass",
              static_cast<unsigned>(result.bytes));
      Storage.remove(kSleepImagePart);
      // Counted as a failure, so the backoff applies and a service sending
      // something unusable is not asked again every six hours. lastSuccessEpoch
      // is LEFT ALONE: the last time this reader really got a message is still
      // the last time it really got one, and zeroing it here would erase that
      // from the screen because a later download was bad.
      ++state.consecutiveFailures;
      save(state);
      releaseWifi();
      message = "Live sent an image this reader could not use.";
      return false;
    }
    if (commitImage()) {
      // The ETag is stored ONLY after the image is in place. Stored first, a
      // rename that failed would leave the device claiming to hold a picture it
      // does not have, and every later wake would be answered 304 -- a fridge
      // stuck forever on the message before this one, with a log full of
      // successful checks.
      state.etag = result.etag;
      imageArrived = true;
    } else {
      ++state.consecutiveFailures;
      message = "Live sent a new message but it could not be saved to the card.";
      save(state);
      releaseWifi();
      return false;
    }
  } else {
    // 304 and 204: nothing written, nothing repainted, nothing to say.
    Storage.remove(kSleepImagePart);
  }

  // The success bookkeeping happens HERE, past every way this call can still
  // fail. Set before the checks -- which is where it was -- a torn image
  // counted as a success for one statement and then had to be un-counted, and
  // the un-counting is what wiped the real last-success time off the screen.
  state.consecutiveFailures = 0;
  state.lastSuccessEpoch = state.lastAttemptEpoch;

  save(state);
  releaseWifi();
  return true;
}

uint32_t onSleep(bool& repaintNeeded, const bool timerFired) {
  repaintNeeded = false;
  State state;
  if (!load(state)) return 0;

  const Decision decision = decide(state.schedule(), nowEpoch(), timerFired);
  if (!decision.fetchNow) return decision.timerSeconds;

  // The fetch happens HERE, with the sleep screen already on the glass: the
  // caller paints before it calls this. Pressing power therefore never makes
  // anybody wait on a radio -- the panel is finished before the first packet.
  bool arrived = false;
  std::string message;
  checkNow(state, arrived, message);
  repaintNeeded = arrived;

  // Re-decide from the state the check just wrote, rather than reusing the
  // number computed before it. The reply may have moved the cadence, and a
  // failure has certainly moved the backoff; arming the pre-check number would
  // mean a device that just learnt "come back in a week" waking in six hours,
  // and a device that just failed retrying on the old interval -- which is the
  // drain the backoff exists to prevent, defeated on the one path that uses it.
  // timerFired is deliberately NOT passed here: it described the sleep that
  // just ended, and this decides the next one. Passing it would make every
  // timer wake arm a fetch-on-wake regardless of what the reply just said.
  const Decision after = decide(state.schedule(), nowEpoch());
  return after.timerSeconds;
}

}  // namespace engine
}  // namespace live

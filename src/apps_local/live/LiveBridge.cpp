#include "LiveBridge.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdlib>

#include "../bridge/BridgeHttp.h"
#include "LiveCore.h"

namespace live {

namespace {

// The endpoint, in the shape Instapaper's and Study's take.
//
// CROSSPLAY_LIVE_URL is the simulator's override and exists for one reason:
// the public hostname does not resolve yet, and a laptop has to be able to
// point at the service on the LAN to prove any of this. On a device build
// bridge::base() does not read the environment at all, so a device can never be
// steered off the verified host by one.
constexpr bridge::Endpoint kEndpoint = {
    "fridge.ma-r-s.com",
    "LIVE",
    "CROSSPLAY_LIVE_URL",
    "/.crosspoint/live-roots.pem",
};

int64_t headerNumber(const bridge::Headers& headers, const char* name) {
  const std::string raw = headers.value(name);
  if (raw.empty()) return 0;
  return std::strtoll(raw.c_str(), nullptr, 10);
}

}  // namespace

bool pairStart(PairStart& out, std::string& message) {
  std::string response;
  const int status = bridge::request(kEndpoint, "POST", "/api/pair/start", "", nullptr, 0, response, message);
  if (status == 0) return false;
  if (status != 200) {
    if (!bridge::takeServerError(response, message)) message = "Live would not answer. Try again in a few minutes.";
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, response) != DeserializationError::Ok || !doc["code"].is<const char*>()) {
    message = "Live answered something unexpected.";
    return false;
  }
  out.code = doc["code"].as<const char*>();
  out.pollToken = doc["pollToken"] | "";
  out.expiresIn = doc["expiresIn"] | 0;
  // On serial deliberately, the way Study's is: "read me the code on the
  // screen" is the first support question of every pairing, and the code is
  // short-lived and single-use.
  LOG_INF("LIVE", "pairing code %s (expires in %ds)", out.code.c_str(), out.expiresIn);
  return true;
}

int pairPoll(const std::string& pollToken, std::string& deviceToken, std::string& fridgeId, std::string& message) {
  std::string response;
  const int status =
      bridge::request(kEndpoint, "GET", "/api/pair/poll?pollToken=" + pollToken, "", nullptr, 0, response, message);
  if (status == 0) return -1;
  if (status != 200) {
    if (!bridge::takeServerError(response, message)) message = "That code expired. Press Back and start again.";
    return -1;
  }
  JsonDocument doc;
  if (deserializeJson(doc, response) != DeserializationError::Ok) {
    message = "Live answered something unexpected.";
    return -1;
  }
  // `paired` false is the ordinary answer and sets NO message: a poll that is
  // still waiting is what every poll says until somebody picks up a phone, and
  // a sentence here would put an error on a screen that is working correctly.
  if (!(doc["paired"] | false)) return 0;
  if (!doc["deviceToken"].is<const char*>()) {
    message = "Live answered something unexpected.";
    return -1;
  }
  deviceToken = doc["deviceToken"].as<const char*>();
  fridgeId = doc["fridgeId"] | "";
  return 1;
}

bool pull(const std::string& deviceToken, const std::string& knownEtag, const char* destPath, PullResult& out) {
  out = PullResult{};

  bridge::Headers headers;
  // No If-None-Match on the first ever pull: there is nothing to claim to
  // have, and an empty one would be a header asserting a value it does not
  // hold.
  if (!knownEtag.empty()) headers.add("If-None-Match", "\"" + knownEtag + "\"");
  headers.collect("ETag");
  headers.collect("X-Next-Wake");
  headers.collect("X-Server-Time");

  out.status = bridge::getToFile(kEndpoint, "/api/pull", deviceToken, destPath, kImageBytes, out.message, &headers);

  // Read the schedule headers FIRST and on every status. They arrive on 200,
  // 304 and 204 alike, and the wake that finds nothing is exactly the wake that
  // most needs to know when to come back.
  const int64_t wake = headerNumber(headers, "X-Next-Wake");
  if (wake > 0) out.nextWakeSeconds = clampInterval(wake);
  out.serverEpoch = headerNumber(headers, "X-Server-Time");

  switch (out.status) {
    case 200:
      out.etag = unquoteEtag(headers.value("ETag"));
      LOG_INF("LIVE", "new image, etag %s, next wake %us", out.etag.c_str(),
              static_cast<unsigned>(out.nextWakeSeconds));
      return true;
    case 304:
      LOG_INF("LIVE", "nothing new; next wake %us", static_cast<unsigned>(out.nextWakeSeconds));
      return true;
    case 204:
      LOG_INF("LIVE", "nothing ever sent; next wake %us", static_cast<unsigned>(out.nextWakeSeconds));
      return true;
    case 401:
      // The phone let this reader go. Clearing the token is the CALLER's job,
      // the way Study does it, because only the caller knows whether a screen
      // is open that has to say so.
      out.message = "This reader was disconnected. Set Live up again.";
      LOG_ERR("LIVE", "401: the device token is no longer good");
      return false;
    case 0:
      LOG_ERR("LIVE", "could not reach Live");
      return false;
    default:
      if (out.message.empty()) out.message = "Live answered something unexpected.";
      LOG_ERR("LIVE", "unexpected status %d", out.status);
      return false;
  }
}

}  // namespace live

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

// The two pairing calls answer the same shape, so they read it in one place.
// `fallback` is what to say when the service refused without a sentence of its
// own; a refusal that HAS one always wins, because surfacing a service's own
// wording verbatim is the rule this transport exists to keep (BridgeHttp.h).
bool readMintedCode(const int status, const std::string& response, PairStart& out, std::string& message,
                    const char* fallback) {
  if (status == 0) return false;
  if (status != 200) {
    if (!bridge::takeServerError(response, message)) message = fallback;
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

}  // namespace

bool pairStart(PairStart& out, std::string& message) {
  std::string response;
  const int status = bridge::request(kEndpoint, "POST", "/api/pair/start", "", nullptr, 0, response, message);
  return readMintedCode(status, response, out, message, "Live would not answer. Try again in a few minutes.");
}

bool pairJoin(const std::string& deviceToken, PairStart& out, std::string& message) {
  std::string response;
  const int status = bridge::request(kEndpoint, "POST", "/api/pair/join", deviceToken, nullptr, 0, response, message);
  // The 409 at four phones lands here with the service's own sentence, which
  // names its own cap. Nothing in this file counts the senders to decide
  // whether to ask: the service is the one that decides, and a device that
  // pre-empted the refusal would be a second copy of the cap to keep in step.
  return readMintedCode(status, response, out, message, "Live would not answer. Try again in a few minutes.");
}

bool listSenders(const std::string& deviceToken, SenderList& out, std::string& message) {
  out = SenderList{};
  std::string response;
  const int status = bridge::request(kEndpoint, "GET", "/api/senders", deviceToken, nullptr, 0, response, message);
  if (status == 0) return false;
  if (status != 200) {
    if (!bridge::takeServerError(response, message)) message = "Live would not say who can send.";
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, response) != DeserializationError::Ok || !doc["senders"].is<JsonArray>()) {
    message = "Live answered something unexpected.";
    return false;
  }
  out.max = doc["max"] | kMaxSenders;
  for (JsonObject s : doc["senders"].as<JsonArray>()) {
    if (out.count >= kMaxSenders) break;
    Sender& sender = out.items[out.count];
    sender.name = s["name"] | "A phone";
    sender.id = s["id"] | "";
    sender.pairedAt = s["pairedAt"] | static_cast<int64_t>(0);
    // An entry with no id cannot be revoked, and a row that cannot be revoked
    // on the one screen that revokes them is worse than a row that is not
    // there: it is access you can see and not remove. Dropped and logged.
    if (sender.id.empty()) {
      LOG_ERR("LIVE", "a sender arrived with no id; it cannot be revoked and is not listed");
      sender = Sender{};
      continue;
    }
    ++out.count;
  }
  // Said out loud rather than silently clamped. The array is sized to the
  // service's cap; if the service ever raises its own, the senders past four
  // are real, can write to this fridge, and are invisible on the screen that
  // could take them off it.
  const int announced = static_cast<int>(doc["senders"].as<JsonArray>().size());
  if (announced > kMaxSenders || out.max > kMaxSenders) {
    LOG_ERR("LIVE", "Live allows %d senders and this reader can show %d; %d arrived", out.max, kMaxSenders, announced);
  }
  LOG_INF("LIVE", "%d sender(s), cap %d", out.count, out.max);
  return true;
}

bool revokeSender(const std::string& deviceToken, const std::string& id, int& remaining, std::string& message) {
  remaining = -1;
  const std::string body = std::string("{\"id\":\"") + id + "\"}";
  std::string response;
  const int status = bridge::request(kEndpoint, "POST", "/api/senders/revoke", deviceToken,
                                     reinterpret_cast<const uint8_t*>(body.data()), body.size(), response, message);
  if (status == 0) return false;
  if (status != 200) {
    if (!bridge::takeServerError(response, message)) message = "Live would not remove that phone.";
    LOG_ERR("LIVE", "revoke %s: %d", id.c_str(), status);
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, response) != DeserializationError::Ok || !(doc["ok"] | false)) {
    message = "Live answered something unexpected.";
    return false;
  }
  remaining = doc["remaining"] | -1;
  LOG_INF("LIVE", "revoked %s; %d left", id.c_str(), remaining);
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

bool reportOff(const std::string& deviceToken, std::string& message) {
  std::string response;
  const int status = bridge::request(kEndpoint, "POST", "/api/off", deviceToken, nullptr, 0, response, message);
  if (status == 200) {
    LOG_INF("LIVE", "told Live this reader is off");
    return true;
  }
  // Logged and returned, never escalated. The caller has already written the
  // toggle to the card; this call is a courtesy to the website and the page
  // has an answer for its absence.
  if (status != 0 && !bridge::takeServerError(response, message)) {
    message = "Live did not answer, so the website will not know for a while.";
  }
  LOG_ERR("LIVE", "could not tell Live this reader is off: %d", status);
  return false;
}

bool pull(const std::string& deviceToken, const std::string& knownEtag, const bool liveOn, const char* destPath,
          PullResult& out) {
  out = PullResult{};

  bridge::Headers headers;
  // No If-None-Match on the first ever pull: there is nothing to claim to
  // have, and an empty one would be a header asserting a value it does not
  // hold.
  if (!knownEtag.empty()) headers.add("If-None-Match", "\"" + knownEtag + "\"");
  // THE ONE FACT ONLY THE READER HAS, on the same request as the question: the
  // service cannot ask a sleeping device anything, and a call of its own would
  // double the round trips a wake costs.
  headers.add("X-Live-On", liveOn ? "1" : "0");
  headers.collect("ETag");
  headers.collect("X-Next-Wake");
  headers.collect("X-Cadence");
  headers.collect("X-Server-Time");

  size_t received = 0;
  out.status = bridge::getToFile(kEndpoint, "/api/pull", deviceToken, destPath, kMaxImageBytes, out.message, &headers,
                                 &received);
  out.bytes = received;

  // Read the schedule headers FIRST and on every status. They arrive on 200,
  // 304 and 204 alike, and the wake that finds nothing is exactly the wake that
  // most needs to know when to come back.
  const int64_t wake = headerNumber(headers, "X-Next-Wake");
  if (wake > 0) out.nextWakeSeconds = clampInterval(wake);
  // CLAMPED ONLY IF IT WAS SENT. clampInterval(0) is 15 minutes, not 0, so
  // clamping unconditionally would turn "the service said nothing" into "every
  // fifteen minutes" and print a cadence nobody chose.
  const int64_t cadence = headerNumber(headers, "X-Cadence");
  if (cadence > 0) out.cadenceSeconds = clampInterval(cadence);
  out.serverEpoch = headerNumber(headers, "X-Server-Time");

  switch (out.status) {
    case 200:
      out.etag = unquoteEtag(headers.value("ETag"));
      LOG_INF("LIVE", "new image, %u bytes, etag %s, next wake %us", static_cast<unsigned>(out.bytes), out.etag.c_str(),
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

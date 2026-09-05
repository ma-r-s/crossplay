#include "InstapaperSync.h"

#include <ArduinoJson.h>
#include <Logging.h>
#include <Utf8.h>

#include <cstdio>

#include "../bridge/BridgeHttp.h"

namespace instapaper {
namespace {

constexpr bridge::Endpoint kEndpoint = {
    kBridgeHost,
    "INSTASYNC",
    "READ_BRIDGE_URL",
    "/.crosspoint/instapaper/.bridge-roots.pem",
};

int request(const char* method, const std::string& path, const std::string& token, const std::string& body,
            std::string& response, std::string& message) {
  return bridge::request(kEndpoint, method, path, token,
                         body.empty() ? nullptr : reinterpret_cast<const uint8_t*>(body.data()), body.size(), response,
                         message);
}

// A 401 means this reader is no longer paired, and it is the one status worth
// handling identically everywhere: anything else is a bad day, this one is a
// state the device must leave.
bool refusedForToken(const int status, Sync& sync, std::string& message) {
  if (status != 401) return false;
  sync.unpaired = true;
  message = "This reader was unpaired. Pair it again to keep reading.";
  return true;
}

}  // namespace

std::string Sync::pairUrl(const std::string& code) { return std::string("https://") + kBridgeHost + "/pair#" + code; }

bool Sync::pairStart(PairStart& out, std::string& message) {
  std::string response;
  const int status = request("POST", "/api/pair/start", "", "", response, message);
  if (status == 0) return false;
  if (status != 200) {
    if (!bridge::takeServerError(response, message)) message = "The service refused. Try again in a few minutes.";
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, response) != DeserializationError::Ok || !doc["code"].is<const char*>()) {
    message = "The service answered something unexpected.";
    return false;
  }
  out.code = doc["code"].as<const char*>();
  out.pollToken = doc["pollToken"] | "";
  // On serial deliberately: the code is short-lived and single-use, and "read
  // me the code on the screen" is the first support question of every pairing.
  LOG_INF("INSTASYNC", "pairing code %s", out.code.c_str());
  return true;
}

int Sync::pairPoll(const std::string& pollToken, std::string& username, std::string& token, std::string& message) {
  std::string response;
  const int status = request("GET", "/api/pair/poll?pollToken=" + pollToken, "", "", response, message);
  if (status == 0) return -1;
  if (status == 410) {
    if (!bridge::takeServerError(response, message)) message = "That code expired. Start again for a fresh one.";
    return -1;
  }
  JsonDocument doc;
  if (deserializeJson(doc, response) != DeserializationError::Ok) {
    message = "The service answered something unexpected.";
    return -1;
  }
  if (doc["pending"] | false) return 0;
  if (!doc["deviceToken"].is<const char*>()) {
    message = "The service answered something unexpected.";
    return -1;
  }
  username = doc["username"] | "";
  token = doc["deviceToken"].as<const char*>();
  return 1;
}

void Sync::pairAbandon(const std::string& pollToken, const std::string& deviceToken) {
  JsonDocument doc;
  if (!pollToken.empty()) doc["pollToken"] = pollToken;
  if (!deviceToken.empty()) doc["deviceToken"] = deviceToken;
  std::string body;
  serializeJson(doc, body);
  std::string response;
  std::string message;
  request("POST", "/api/pair/abandon", "", body, response, message);
}

bool Sync::syncStart(const BridgeState& state, const std::vector<Article>& have, const std::vector<int64_t>& archive,
                     std::string& jobId, std::string& message) {
  unpaired = false;
  JsonDocument doc;
  JsonArray haveArray = doc["have"].to<JsonArray>();
  for (const Article& a : have) {
    JsonObject entry = haveArray.add<JsonObject>();
    entry["id"] = a.id;
    entry["hash"] = a.hash;
    // Progress rides here or not at all. A zero timestamp means "I have no
    // opinion", which is what the bridge needs to hear rather than a
    // confident 0.0 that would roll back what the phone read.
    if (a.progress > 0.0f && a.progressAt > 0) {
      entry["progress"] = a.progress;
      entry["progressAt"] = a.progressAt;
    }
  }
  JsonArray archiveArray = doc["archive"].to<JsonArray>();
  for (const int64_t id : archive) archiveArray.add(id);

  std::string body;
  serializeJson(doc, body);
  std::string response;
  const int status = request("POST", "/api/sync", state.token, body, response, message);
  if (status == 0) return false;
  if (refusedForToken(status, *this, message)) return false;
  if (status != 200) {
    if (!bridge::takeServerError(response, message)) message = "The service could not start a sync.";
    return false;
  }
  JsonDocument answer;
  if (deserializeJson(answer, response) != DeserializationError::Ok || !answer["job"].is<const char*>()) {
    message = "The service answered something unexpected.";
    return false;
  }
  jobId = answer["job"].as<const char*>();
  LOG_INF("INSTASYNC", "sync job %s (%d known, %d to archive)", jobId.c_str(), static_cast<int>(have.size()),
          static_cast<int>(archive.size()));
  return true;
}

std::string Sync::syncStatus(const BridgeState& state, const std::string& jobId, SyncSummary& out,
                             std::string& message) {
  std::string response;
  const int status = request("GET", "/api/sync/status?job=" + jobId, state.token, "", response, message);
  if (status == 0) return "";
  if (refusedForToken(status, *this, message)) return "error";
  if (status != 200) {
    if (!bridge::takeServerError(response, message)) message = "The service stopped answering about this sync.";
    return "error";
  }
  JsonDocument doc;
  if (deserializeJson(doc, response) != DeserializationError::Ok || !doc["status"].is<const char*>()) {
    message = "The service answered something unexpected.";
    return "error";
  }
  const std::string state_ = doc["status"].as<const char*>();
  if (state_ != "done") {
    message = doc["message"] | "";
    return state_;
  }

  JsonObject summary = doc["summary"].as<JsonObject>();
  out = SyncSummary{};
  for (JsonObject item : summary["articles"].as<JsonArray>()) {
    Delivery delivery;
    Article& a = delivery.article;
    a.id = item["id"] | static_cast<int64_t>(0);
    if (a.id == 0) continue;
    a.hash = item["hash"] | "";
    a.sha = item["sha"] | "";
    // Somebody else's headline, from somebody else's page: curly quotes, em
    // dashes and ellipses, none of which the reading cut can draw.
    a.title = utf8FoldTypography(item["title"] | "");
    a.domain = utf8FoldTypography(item["domain"] | "");
    a.savedAt = item["savedAt"] | 0u;
    a.words = item["words"] | 0u;
    a.minutes = static_cast<uint16_t>(item["minutes"] | 0u);
    a.progress = item["progress"] | 0.0f;
    a.progressAt = item["progressAt"] | 0u;
    a.renderable = item["renderable"] | true;
    // Neither flag is the server's to set: one records something this reader
    // did and has not sent, the other something it did and is waiting to have
    // confirmed. Taking them from a response would erase a pending archive.
    a.progressDirty = false;
    a.archivePending = false;
    delivery.bytes = item["bytes"] | 0u;
    out.articles.push_back(std::move(delivery));
  }
  for (JsonVariant id : summary["deleteIds"].as<JsonArray>()) out.deleteIds.push_back(id.as<int64_t>());
  for (JsonVariant id : summary["archived"].as<JsonArray>()) out.archived.push_back(id.as<int64_t>());
  for (JsonObject item : summary["failed"].as<JsonArray>()) {
    ++out.failed;
    if (out.firstFailure.empty()) out.firstFailure = item["why"] | "";
  }
  out.withheld = summary["withheld"] | 0;
  return "done";
}

std::vector<Article> deliveredArticles(const SyncSummary& summary) {
  std::vector<Article> out;
  out.reserve(summary.articles.size());
  for (const Delivery& d : summary.articles) out.push_back(d.article);
  return out;
}

uint32_t deliveredBytes(const SyncSummary& summary, const int64_t id) {
  for (const Delivery& d : summary.articles) {
    if (d.article.id == id) return d.bytes;
  }
  return 0;
}

bool Sync::downloadToPart(const BridgeState& state, const Article& article, const uint32_t expectedBytes,
                          const std::string& partPath, bool* cancel, std::string& message) {
  if (expectedBytes == 0) {
    // No size means no length check, and a length check is the only proof
    // this device has that a file arrived whole. Refusing is better than
    // writing an article nobody can tell is truncated.
    message = "The service did not say how big that article is.";
    return false;
  }
  char path[96];
  std::snprintf(path, sizeof(path), "/api/article/%lld/%s", static_cast<long long>(article.id), article.hash.c_str());
  return bridge::streamToFile(kEndpoint, path, state.token, partPath, expectedBytes,
                              "An article did not arrive whole. Try syncing again.", cancel, message);
}

}  // namespace instapaper

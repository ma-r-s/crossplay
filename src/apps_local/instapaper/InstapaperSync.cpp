#include "InstapaperSync.h"

#include <ArduinoJson.h>
#include <Logging.h>
#include <Utf8.h>

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
    // Normalised here as well as at every write, and for a reason the write
    // alone does not cover: the index on the card holds the sanitised form,
    // so an Article kept raw in RAM would compare unequal to its own saved
    // row and mergeSummary would queue a download for it on every single
    // sync, forever, with nothing on screen to say why. Same rule, one
    // definition -- InstapaperIndex.h.
    const std::string rawHash = item["hash"] | "";
    const std::string rawSha = item["sha"] | "";
    a.hash = sanitizeToken(rawHash);
    a.sha = sanitizeToken(rawSha);
    if (a.hash != rawHash || a.sha != rawSha) {
      // Not a crash and not a refusal: the article still syncs and still
      // reads. But the bridge and this reader now disagree about its key, so
      // it will be re-delivered every time, and that is the kind of slow
      // waste nobody finds without a line saying it happened.
      // ERR rather than INF because it is the only level that survives every
      // LOG_LEVEL, and this one has to be findable a month later.
      LOG_ERR("INSTASYNC", "article %lld sent a hash/sha this format cannot hold; cut to %u chars",
              static_cast<long long>(a.id), static_cast<unsigned>(kTokenLimit));
    }
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
  // The index writer's twin: this used to be a char[96] fed article.hash
  // straight, so the same unbounded wire string that could cut a row short
  // could also cut this path short and fetch a different article -- or spell
  // "/api/article/7/../../etc/passwd", which only the bridge's own resolve
  // check was stopping. Sanitised here rather than trusted from the Article,
  // and built as a string so no length can be got wrong again.
  const std::string path =
      "/api/article/" + std::to_string(static_cast<long long>(article.id)) + "/" + sanitizeToken(article.hash);
  return bridge::streamToFile(kEndpoint, path, state.token, partPath, expectedBytes,
                              "An article did not arrive whole. Try syncing again.", cancel, message);
}

}  // namespace instapaper

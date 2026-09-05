#include "StudySync.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

#if defined(FREEINK_NET_WOLFSSL)
#include <SecureHttpClient.h>

#include "StudySyncRoots.h"
#include "network/DeviceReport.h"
#else
#include <unistd.h>

#include <cstdlib>
#endif

namespace study {

namespace {

constexpr char kBridgeStatePath[] = "/study/.bridge";
constexpr char kRootsOverridePath[] = "/study/.bridge-roots.pem";

std::string bridgeBase() {
#if !defined(FREEINK_NET_WOLFSSL)
  // Simulator: point tests at a local bridge with a throwaway account. The
  // real account must never receive a simulator's reviews.
  if (const char* env = std::getenv("STUDY_BRIDGE_URL")) return env;
#endif
  return std::string("https://") + kBridgeHost;
}

#if defined(FREEINK_NET_WOLFSSL)

// The verification roots. An SD bundle wins so a Cloudflare CA change is a
// file copy, not a reflash; the baked bundle is the fallback. Held in a
// static because SecureClient borrows the pointer for every connect.
const char* caRoots() {
  static std::string sdRoots;
  static bool probed = false;
  if (!probed) {
    probed = true;
    HalFile file;
    if (Storage.openFileForRead("STUDYSYNC", kRootsOverridePath, file)) {
      const size_t size = file.size();
      // A sanity floor: a truncated bundle fails the handshake with a
      // generic error, so refuse obviously-broken files here where the log
      // can still say why.
      if (size > 512 && size < 65536) {
        sdRoots.resize(size);
        if (file.read(reinterpret_cast<uint8_t*>(sdRoots.data()), size) == static_cast<int>(size) &&
            sdRoots.find("-----BEGIN CERTIFICATE-----") != std::string::npos) {
          LOG_INF("STUDYSYNC", "using SD root bundle (%u bytes)", static_cast<unsigned>(size));
        } else {
          sdRoots.clear();
          LOG_ERR("STUDYSYNC", "SD root bundle unreadable; using baked roots");
        }
      } else {
        LOG_ERR("STUDYSYNC", "SD root bundle size %u rejected; using baked roots", static_cast<unsigned>(size));
      }
    }
  }
  return sdRoots.empty() ? kBridgeCaRoots : sdRoots.c_str();
}

// TLS wants ~35KB free with a 20KB block (the KOSync numbers, measured with
// the same wolfSSL build). Callers close the deck first; this is the last
// line of defense, not the plan.
bool insufficientHeap(std::string& message) {
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t maxBlock = ESP.getMaxAllocHeap();
  if (freeHeap < 35000 || maxBlock < 20000) {
    LOG_ERR("STUDYSYNC", "heap too low for TLS: free=%u block=%u", freeHeap, maxBlock);
    message = "Not enough memory free to sync right now. Leave and reopen Study, then try again.";
    return true;
  }
  return false;
}

// The firmware version, which every request to one of our hosts carries the
// way HttpDownloader's do; and the device report headers, when the toggle is
// on. Version alone names no device. (BridgeHttp.cpp has this twin; see
// docs/open-items.md for why Study still carries its own transport.)
void identify(freeink::SecureHttpClient& http, const std::string& url) {
  http.setUserAgent("CrossPlay-ESP32-" CROSSPOINT_VERSION);
  devreport::Header report[devreport::kHeaderCount];
  const int n = devreport::headersFor(url.c_str(), report);
  for (int i = 0; i < n; ++i) http.addHeader(report[i].name, report[i].value);
}

// One request, buffered response. Returns HTTP status, 0 on transport error.
int request(const char* method, const std::string& path, const std::string& token, const uint8_t* body, size_t bodyLen,
            std::string& response, std::string& message) {
  if (insufficientHeap(message)) return 0;
  freeink::SecureHttpClient http;
  http.setCACert(caRoots());
  http.setTimeout(30000);
  http.setFollowRedirects(2);
  const std::string url = bridgeBase() + path;
  if (!http.begin(url)) {
    message = "The sync service address did not make sense. Update the firmware.";
    return 0;
  }
  if (!token.empty()) http.addHeader("Authorization", std::string("Bearer ") + token);
  identify(http, url);
  LOG_INF("STUDYSYNC", "%s %s (verified TLS)", method, path.c_str());
  const int status = body ? http.sendRequest(method, body, bodyLen) : http.sendRequest(method, std::string());
  devreport::delivered(url.c_str(), status);
  if (status <= 0) {
    LOG_ERR("STUDYSYNC", "%s %s failed: %d", method, path.c_str(), status);
    message = "Could not reach the sync service. Check Wi-Fi and try again.";
    http.end();
#if defined(CROSSPOINT_DEV_SERIAL_BRIDGE)
    // Dev-build self-diagnosis, never a fallback: retry the same request
    // WITHOUT verification purely to bisect the failure, log the verdict,
    // and still fail. A release build never contains this branch.
    {
      freeink::SecureHttpClient probe;
      probe.setInsecure();
      probe.setTimeout(15000);
      int ps = -1;
      if (probe.begin(bridgeBase() + path)) ps = probe.sendRequest("GET", std::string());
      probe.end();
      if (ps > 0) {
        LOG_ERR("STUDYSYNC", "DIAGNOSIS: insecure probe got HTTP %d -- certificate VERIFICATION is the failure", ps);
        message = "The bridge answered but its certificate was refused. This build logged the details.";
      } else {
        LOG_ERR("STUDYSYNC", "DIAGNOSIS: insecure probe also failed (%d) -- network/DNS level, not certificates", ps);
      }
    }
#endif
    return 0;
  }
  response = http.getString();
  http.end();
  return status;
}

bool streamToFile(const std::string& path, const std::string& token, const std::string& destPart, size_t expectedSize,
                  bool* cancel, std::string& message) {
  if (insufficientHeap(message)) return false;
  HalFile out;
  if (!Storage.openFileForWrite("STUDYSYNC", destPart.c_str(), out)) {
    message = "Could not write to the card.";
    return false;
  }
  freeink::SecureHttpClient http;
  http.setCACert(caRoots());
  http.setTimeout(30000);
  http.setFollowRedirects(2);
  const std::string url = bridgeBase() + path;
  if (!http.begin(url)) {
    message = "The sync service address did not make sense.";
    return false;
  }
  http.addHeader("Authorization", std::string("Bearer ") + token);
  identify(http, url);
  size_t written = 0;
  const int status = http.GET(
      [&](const uint8_t* data, size_t len) {
        if (out.write(data, len) != static_cast<int>(len)) return false;
        written += len;
        return true;
      },
      [&]() { return cancel && *cancel; });
  http.end();
  out.close();
  devreport::delivered(url.c_str(), status);
  if (cancel && *cancel) {
    message = "Stopped.";
    return false;
  }
  if (status != 200 || written != expectedSize) {
    LOG_ERR("STUDYSYNC", "download %s: status=%d written=%u expected=%u", path.c_str(), status,
            static_cast<unsigned>(written), static_cast<unsigned>(expectedSize));
    message = "A deck file did not arrive whole. Try syncing again.";
    return false;
  }
  return true;
}

#else  // simulator: curl, because the HTTP stub cannot carry binary bodies.

int request(const char* method, const std::string& path, const std::string& token, const uint8_t* body, size_t bodyLen,
            std::string& response, std::string& message) {
  char bodyPath[] = "/tmp/studysync-body-XXXXXX";
  char outPath[] = "/tmp/studysync-out-XXXXXX";
  int fdBody = mkstemp(bodyPath);
  int fdOut = mkstemp(outPath);
  if (fdBody < 0 || fdOut < 0) {
    message = "sim: mkstemp failed";
    return 0;
  }
  if (body && bodyLen) {
    FILE* f = fdopen(fdBody, "wb");
    fwrite(body, 1, bodyLen, f);
    fclose(f);
  } else {
    close(fdBody);
  }
  close(fdOut);
  std::string cmd = "curl -sS -m 60 -o '" + std::string(outPath) + "' -w '%{http_code}' -X " + method;
  if (!token.empty()) cmd += " -H 'Authorization: Bearer " + token + "'";
  if (body) cmd += " --data-binary @'" + std::string(bodyPath) + "'";
  if (!body && std::strcmp(method, "POST") == 0) cmd += " --data ''";
  cmd += " '" + bridgeBase() + path + "'";
  FILE* pipe = popen(cmd.c_str(), "r");
  char statusBuf[8] = {};
  if (pipe) {
    fgets(statusBuf, sizeof(statusBuf), pipe);
    pclose(pipe);
  }
  const int status = atoi(statusBuf);
  response.clear();
  if (FILE* f = fopen(outPath, "rb")) {
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) response.append(buf, n);
    fclose(f);
  }
  remove(bodyPath);
  remove(outPath);
  if (status == 0) message = "Could not reach the sync service. Check Wi-Fi and try again.";
  return status;
}

bool streamToFile(const std::string& path, const std::string& token, const std::string& destPart, size_t expectedSize,
                  bool* cancel, std::string& message) {
  (void)cancel;
  std::string response;
  const int status = request("GET", path, token, nullptr, 0, response, message);
  if (status != 200 || response.size() != expectedSize) {
    message = "A deck file did not arrive whole. Try syncing again.";
    return false;
  }
  HalFile out;
  if (!Storage.openFileForWrite("STUDYSYNC", destPart.c_str(), out)) {
    message = "Could not write to the card.";
    return false;
  }
  out.write(reinterpret_cast<const uint8_t*>(response.data()), response.size());
  return true;
}

#endif

// The server's polite refusals arrive as {"error": "sentence"}; surface them
// verbatim so the device never invents its own wording for a server decision.
bool takeServerError(const std::string& response, std::string& message) {
  JsonDocument doc;
  if (deserializeJson(doc, response) == DeserializationError::Ok && doc["error"].is<const char*>()) {
    message = doc["error"].as<const char*>();
    return true;
  }
  return false;
}

}  // namespace

uint32_t BridgeState::ackFor(const char* dir) const {
  for (int i = 0; i < deckCount; ++i) {
    if (std::strcmp(deckDirs[i], dir) == 0) return ackOffsets[i];
  }
  return 0;
}

uint64_t BridgeState::ackHashFor(const char* dir) const {
  for (int i = 0; i < deckCount; ++i) {
    if (std::strcmp(deckDirs[i], dir) == 0) return ackHashes[i];
  }
  return 0;
}

void BridgeState::setAckHash(const char* dir, const uint64_t hash) {
  for (int i = 0; i < deckCount; ++i) {
    if (std::strcmp(deckDirs[i], dir) == 0) {
      ackHashes[i] = hash;
      return;
    }
  }
  if (deckCount >= kMaxSyncDecks) return;
  std::snprintf(deckDirs[deckCount], sizeof(deckDirs[0]), "%s", dir);
  ackHashes[deckCount] = hash;
  ++deckCount;
}

void BridgeState::setAck(const char* dir, uint32_t offset) {
  for (int i = 0; i < deckCount; ++i) {
    if (std::strcmp(deckDirs[i], dir) == 0) {
      ackOffsets[i] = offset;
      return;
    }
  }
  if (deckCount < kMaxSyncDecks) {
    std::snprintf(deckDirs[deckCount], sizeof(deckDirs[0]), "%s", dir);
    ackOffsets[deckCount] = offset;
    ++deckCount;
  }
}

const char* BridgeState::buildFor(const char* dir) const {
  for (int i = 0; i < deckCount; ++i) {
    if (std::strcmp(deckDirs[i], dir) == 0) return lastBuilds[i];
  }
  return "";
}

void BridgeState::setBuild(const char* dir, const char* buildId) {
  setAck(dir, ackFor(dir));  // ensures the dir has a slot
  for (int i = 0; i < deckCount; ++i) {
    if (std::strcmp(deckDirs[i], dir) == 0) {
      std::snprintf(lastBuilds[i], sizeof(lastBuilds[0]), "%s", buildId);
      return;
    }
  }
}

bool loadBridgeState(BridgeState& out) {
  out = BridgeState{};
  HalFile file;
  if (!Storage.openFileForRead("STUDYSYNC", kBridgeStatePath, file)) return false;
  JsonDocument doc;
  std::string raw;
  raw.resize(file.size());
  if (file.read(reinterpret_cast<uint8_t*>(raw.data()), raw.size()) != static_cast<int>(raw.size())) return false;
  if (deserializeJson(doc, raw) != DeserializationError::Ok) return false;
  out.token = doc["token"] | "";
  out.lastSyncAt = doc["lastSyncAt"] | static_cast<int64_t>(0);
  out.choseDecks = doc["choseDecks"] | false;
  out.paired = !out.token.empty();
  for (JsonPair kv : doc["ackhashes"].as<JsonObject>()) {
    out.setAckHash(kv.key().c_str(), kv.value().as<uint64_t>());
  }
  for (JsonPair kv : doc["acks"].as<JsonObject>()) {
    out.setAck(kv.key().c_str(), kv.value().as<uint32_t>());
  }
  for (JsonPair kv : doc["builds"].as<JsonObject>()) {
    out.setBuild(kv.key().c_str(), kv.value().as<const char*>());
  }
  return out.paired;
}

bool saveBridgeState(const BridgeState& state) {
  JsonDocument doc;
  doc["token"] = state.token;
  if (state.lastSyncAt > 0) doc["lastSyncAt"] = state.lastSyncAt;
  if (state.choseDecks) doc["choseDecks"] = true;
  JsonObject acks = doc["acks"].to<JsonObject>();
  for (int i = 0; i < state.deckCount; ++i) acks[state.deckDirs[i]] = state.ackOffsets[i];
  JsonObject hashes = doc["ackhashes"].to<JsonObject>();
  for (int i = 0; i < state.deckCount; ++i) {
    if (state.ackHashes[i] != 0) hashes[state.deckDirs[i]] = state.ackHashes[i];
  }
  JsonObject builds = doc["builds"].to<JsonObject>();
  for (int i = 0; i < state.deckCount; ++i) {
    if (state.lastBuilds[i][0] != '\0') builds[state.deckDirs[i]] = state.lastBuilds[i];
  }
  std::string raw;
  serializeJson(doc, raw);
  // Write beside it and rename. Opening the real path truncates first, so a
  // power cut mid-write left an unparseable .bridge, which reads exactly like
  // a device that was never paired: the next sync walked the user through
  // pairing again for no reason they could see.
  const std::string tempPath = std::string(kBridgeStatePath) + ".part";
  {
    HalFile file;
    if (!Storage.openFileForWrite("STUDYSYNC", tempPath.c_str(), file)) {
      LOG_ERR("STUDYSYNC", "cannot write %s", tempPath.c_str());
      return false;
    }
    if (file.write(reinterpret_cast<const uint8_t*>(raw.data()), raw.size()) != static_cast<int>(raw.size())) {
      LOG_ERR("STUDYSYNC", "short write to %s", tempPath.c_str());
      return false;
    }
  }
  Storage.remove(kBridgeStatePath);
  if (!Storage.rename(tempPath.c_str(), kBridgeStatePath)) {
    LOG_ERR("STUDYSYNC", "cannot rename %s into place", tempPath.c_str());
    return false;
  }
  return true;
}

std::string StudySync::pairUrl(const std::string& code) {
  return std::string("https://") + kBridgeHost + "/pair#" + code;
}

bool StudySync::pairStart(PairStart& out, std::string& message) {
  std::string response;
  const int status = request("POST", "/api/pair/start", "", nullptr, 0, response, message);
  if (status == 0) return false;
  if (status != 200) {
    if (!takeServerError(response, message)) message = "The sync service refused. Try again in a few minutes.";
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, response) != DeserializationError::Ok || !doc["code"].is<const char*>()) {
    message = "The sync service answered something unexpected.";
    return false;
  }
  out.code = doc["code"].as<const char*>();
  out.pollToken = doc["pollToken"].as<const char*>();
  // On serial deliberately: the code is short-lived and single-use, and "read
  // me the code on the screen" is the first support question of every pairing.
  LOG_INF("STUDYSYNC", "pairing code %s", out.code.c_str());
  return true;
}

int StudySync::pairPoll(const std::string& pollToken, std::string& username, std::string& token, std::string& message) {
  std::string response;
  const int status = request("GET", "/api/pair/poll?pollToken=" + pollToken, "", nullptr, 0, response, message);
  if (status == 0) return -1;
  if (status == 410) {
    takeServerError(response, message) || (message = "That code expired. Start again for a fresh one.", true);
    return -1;
  }
  JsonDocument doc;
  if (deserializeJson(doc, response) != DeserializationError::Ok) {
    message = "The sync service answered something unexpected.";
    return -1;
  }
  if (doc["pending"] | false) return 0;
  if (!doc["deviceToken"].is<const char*>()) {
    message = "The sync service answered something unexpected.";
    return -1;
  }
  username = doc["username"] | "";
  token = doc["deviceToken"].as<const char*>();
  return 1;
}

void StudySync::pairAbandon(const std::string& pollToken, const std::string& deviceToken) {
  JsonDocument doc;
  if (!pollToken.empty()) doc["pollToken"] = pollToken;
  if (!deviceToken.empty()) doc["deviceToken"] = deviceToken;
  std::string body;
  serializeJson(doc, body);
  std::string response;
  std::string message;
  request("POST", "/api/pair/abandon", "", reinterpret_cast<const uint8_t*>(body.data()), body.size(), response,
          message);
}

bool StudySync::listDecks(const BridgeState& state, std::vector<DeckChoice>& out, std::string& message) {
  decksWithheld = false;
  out.clear();
  std::string response;
  const int status = request("GET", "/api/decks", state.token, nullptr, 0, response, message);
  if (status == 0) return false;
  if (status == 401) {
    unpaired = true;
    message = "This reader was unpaired on the bridge.";
    return false;
  }
  if (status != 200) {
    if (!takeServerError(response, message)) message = "The sync service could not list your decks.";
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, response) != DeserializationError::Ok || !doc["decks"].is<JsonArray>()) {
    message = "The sync service answered something unexpected.";
    return false;
  }
  for (JsonVariant chosen : doc["chosen"].as<JsonArray>()) {
    (void)chosen;  // presence handled per deck below
  }
  for (JsonObject deck : doc["decks"].as<JsonArray>()) {
    DeckChoice choice;
    choice.name = deck["name"] | "";
    choice.cards = deck["cards"] | 0;
    if (choice.name.empty()) continue;
    for (JsonVariant chosen : doc["chosen"].as<JsonArray>()) {
      if (choice.name == (chosen.as<const char*>() ? chosen.as<const char*>() : "")) choice.chosen = true;
    }
    out.push_back(std::move(choice));
    if (out.size() >= kMaxOfferedDecks) {
      // The account can hold more decks than the picker can list. Saying so
      // is the difference between "my deck is not here" and "my deck is not
      // on this page", which the user cannot tell apart otherwise.
      decksWithheld = true;
      break;
    }
  }
  return true;
}

bool StudySync::chooseDecks(const BridgeState& state, const std::vector<std::string>& names, std::string& message) {
  JsonDocument doc;
  JsonArray arr = doc["decks"].to<JsonArray>();
  for (const auto& name : names) arr.add(name);
  std::string body;
  serializeJson(doc, body);
  std::string response;
  const int status = request("POST", "/api/decks/choose", state.token, reinterpret_cast<const uint8_t*>(body.data()),
                             body.size(), response, message);
  if (status == 0) return false;
  if (status == 401) {
    unpaired = true;
    message = "This reader was unpaired on the bridge.";
    return false;
  }
  if (status != 200) {
    if (!takeServerError(response, message)) message = "The sync service would not save that choice.";
    return false;
  }
  return true;
}

bool StudySync::syncStart(const BridgeState& state, const std::vector<DeckPayload>& decks, std::string& jobId,
                          std::vector<std::pair<std::string, uint32_t>>& acks, std::string& message) {
  unpaired = false;
  // Wire shape: [u32 LE header_len][JSON header][per-deck revlog tail then
  // cards.dat, in header order].
  JsonDocument doc;
  JsonArray arr = doc["decks"].to<JsonArray>();
  for (const auto& d : decks) {
    JsonObject o = arr.add<JsonObject>();
    o["slug"] = d.dirName;
    o["revlogOffset"] = d.revlogOffset;
    o["revlogLen"] = d.revlogTail.size();
    o["cardsLen"] = d.cards.size();
  }
  std::string header;
  serializeJson(doc, header);
  std::string body;
  size_t total = 4 + header.size();
  for (const auto& d : decks) total += d.revlogTail.size() + d.cards.size();
  body.reserve(total);
  const uint32_t hlen = header.size();
  body.append(reinterpret_cast<const char*>(&hlen), 4);
  body += header;
  for (const auto& d : decks) {
    body += d.revlogTail;
    body += d.cards;
  }

  std::string response;
  const int status = request("POST", "/api/sync", state.token, reinterpret_cast<const uint8_t*>(body.data()),
                             body.size(), response, message);
  if (status == 0) return false;
  if (status == 401) {
    unpaired = true;
    takeServerError(response, message) || (message = "This device is not paired anymore. Pair it again.", true);
    return false;
  }
  if (status != 200) {
    if (!takeServerError(response, message)) message = "The sync service refused. Try again in a few minutes.";
    return false;
  }
  JsonDocument reply;
  if (deserializeJson(reply, response) != DeserializationError::Ok || !reply["job"].is<const char*>()) {
    message = "The sync service answered something unexpected.";
    return false;
  }
  jobId = reply["job"].as<const char*>();
  for (JsonPair kv : reply["ackOffsets"].as<JsonObject>()) {
    acks.emplace_back(kv.key().c_str(), kv.value().as<uint32_t>());
  }
  return true;
}

std::string StudySync::syncStatus(const BridgeState& state, const std::string& jobId,
                                  std::vector<DeckManifest>& manifests, std::string& message) {
  std::string response;
  const int status = request("GET", "/api/sync/status?job=" + jobId, state.token, nullptr, 0, response, message);
  if (status == 0) return "";
  JsonDocument doc;
  if (deserializeJson(doc, response) != DeserializationError::Ok || !doc["status"].is<const char*>()) {
    message = "The sync service answered something unexpected.";
    return "";
  }
  const std::string jobStatus = doc["status"].as<const char*>();
  failedDecks.clear();
  reviewsMissing = doc["summary"]["missing"] | 0;
  if (jobStatus == "error" || jobStatus == "frozen") {
    message = doc["message"] | "Syncing hit a problem on the bridge. Try again in a while.";
  } else if (jobStatus == "done") {
    for (JsonVariant name : doc["summary"]["failedDecks"].as<JsonArray>()) {
      const char* text = name.as<const char*>();
      if (text && *text) failedDecks.push_back(text);
    }
    for (JsonObject m : doc["summary"]["manifests"].as<JsonArray>()) {
      DeckManifest deck;
      deck.slug = m["slug"] | "";
      deck.deckName = m["deck"] | "";
      deck.buildId = m["buildId"] | "";
      for (JsonPair kv : m["files"].as<JsonObject>()) {
        ManifestFile file;
        file.path = kv.key().c_str();
        file.size = kv.value()["size"] | 0;
        file.sha256 = kv.value()["sha256"] | "";
        deck.files.push_back(file);
      }
      manifests.push_back(std::move(deck));
    }
  }
  return jobStatus;
}

bool StudySync::downloadToPart(const BridgeState& state, const DeckManifest& deck, const ManifestFile& file,
                               const std::string& partPath, bool* cancel, std::string& message) {
  const std::string url = "/api/deck/" + deck.slug + "/" + deck.buildId + "/" + file.path;
  if (!streamToFile(url, state.token, partPath, file.size, cancel, message)) {
    Storage.remove(partPath.c_str());
    return false;
  }
  return true;
}

}  // namespace study

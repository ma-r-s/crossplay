#include "OpdsServerStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <ObfuscationUtils.h>

#include <algorithm>
#include <cstring>

// THE SHIPPED GET BOOKS CREDENTIALS ARE PUBLIC ON PURPOSE. They live in a
// public repository and in every release binary, so `strings` recovers them:
// they are a courtesy gate against casual crawlers, never a secret, and never
// Mario's own login. The server holds them as a second account so they can be
// rotated or revoked without touching his access or reflashing a device.
static constexpr OpdsServerStore::DefaultCatalog GET_BOOKS{"Get Books", "https://books.ma-r-s.com/opds", "crossplay",
                                                           "r4ulp-zm4cg-awjtf-z5zfj"};

// Gutenberg advertises search through an OpenSearch description document
// rather than an inline {searchTerms} href, which the reader follows -- see
// OpdsBookBrowserActivity. www, not m: m.gutenberg.org answers every request
// with a 301 to this URL, and following it costs the device a second TLS
// handshake on a catalog whose gateway already times out often enough.
static constexpr OpdsServerStore::DefaultCatalog GUTENBERG{"Project Gutenberg",
                                                           "https://www.gutenberg.org/ebooks.opds/", "", ""};

static constexpr OpdsServerStore::DefaultCatalog DEFAULTS[] = {GET_BOOKS, GUTENBERG};

void OpdsServerStore::toJson(JsonDocument& doc) const {
  doc["defaults_seeded"] = defaultsSeeded;
  doc["seed_version"] = seedVersion;
  JsonArray arr = doc["servers"].to<JsonArray>();
  for (const auto& server : servers) {
    JsonObject obj = arr.add<JsonObject>();
    obj["name"] = server.name;
    obj["url"] = server.url;
    obj["username"] = server.username;
    obj["password_obf"] = obfuscation::obfuscateToBase64(server.password);
  }
}

bool OpdsServerStore::fromJson(JsonVariantConst doc) {
  // Tolerate a missing/invalid 'servers' key (treat as empty list); only a
  // JSON parse error is fatal. A null JsonArray iterates zero times.
  servers.clear();
  defaultsSeeded = doc["defaults_seeded"] | false;
  seedVersion = doc["seed_version"] | 0;
  JsonArrayConst arr = doc["servers"].as<JsonArrayConst>();
  servers.reserve(std::min(arr.size(), MAX_SERVERS));
  bool needsResave = false;

  for (JsonObjectConst obj : arr) {
    if (servers.size() >= OpdsServerStore::MAX_SERVERS) break;
    OpdsServer server;
    server.name = obj["name"] | "";
    server.url = obj["url"] | "";
    server.username = obj["username"] | "";
    server.password = extractPassword(obj, needsResave);
    servers.push_back(std::move(server));
  }

  LOG_DBG("OPS", "Loaded %zu OPDS servers from file", servers.size());

  if (migrateSeeds()) needsResave = true;

  if (needsResave) {
    LOG_DBG("OPS", "Resaving JSON with obfuscated passwords");
    requestResave();
  }

  return true;
}

bool OpdsServerStore::importSeedFile() {
  const char* const path = getSeedFilePath();
  if (!Storage.exists(path)) return false;

  const String contents = Storage.readFile(path);
  if (contents.isEmpty()) {
    LOG_ERR("OPS", "Seed file %s is empty", path);
    return false;
  }

  JsonDocument doc;
  const auto parseError = deserializeJson(doc, contents);
  if (parseError) {
    // Left in place on a parse error: deleting it would destroy the only copy
    // of something the user meant to install, and they cannot see this log.
    LOG_ERR("OPS", "Seed file %s is not valid JSON: %s", path, parseError.c_str());
    return false;
  }

  size_t imported = 0;
  for (JsonObjectConst obj : doc["servers"].as<JsonArrayConst>()) {
    if (servers.size() >= MAX_SERVERS) {
      LOG_DBG("OPS", "Seed file truncated: server limit reached");
      break;
    }
    OpdsServer server;
    server.name = obj["name"] | "";
    server.url = obj["url"] | "";
    server.username = obj["username"] | "";
    server.password = obj["password"] | "";
    if (server.url.empty()) continue;

    const bool present =
        std::any_of(servers.begin(), servers.end(), [&](const OpdsServer& s) { return s.url == server.url; });
    if (present) continue;

    LOG_DBG("OPS", "Imported catalog from seed file: %s", server.name.c_str());
    servers.push_back(std::move(server));
    ++imported;
  }

  if (imported == 0) {
    LOG_DBG("OPS", "Seed file held nothing new; removing it anyway");
  }

  const bool saved = saveToFile();
  if (saved && !Storage.remove(path)) {
    // The catalogs are installed either way, so this is a warning and not a
    // failure -- but plaintext credentials are still on the card.
    LOG_ERR("OPS", "Could not remove seed file %s after import", path);
  }
  return saved && imported > 0;
}

// Seeding runs ONCE per device, which makes every change to the default list
// invisible to every device already in the world: retiring a catalog leaves it
// on them forever, and adding one never reaches them at all. This is the
// migration that does reach them, keyed by a version rather than a pile of
// one-shot booleans.
//
// The rule that does NOT change: a catalog the reader deleted stays deleted.
// Introducing a catalog is only safe for one nobody has had the chance to
// delete yet, which is why this runs once per version and not on every boot.
bool OpdsServerStore::migrateSeeds() {
  if (seedVersion >= SEED_VERSION) return false;
  const int from = seedVersion;
  seedVersion = SEED_VERSION;

  if (from < 1) {
    // Standard Ebooks retired: it answers 401 behind the Patrons Circle.
    // Only an UNTOUCHED entry goes -- same URL, no username. Someone who typed
    // a patron email into that row has a catalog that works, and deleting
    // their credentials to tidy up our own default would be the worse failure.
    static constexpr const char* RETIRED[] = {"https://standardebooks.org/feeds/opds"};
    const size_t before = servers.size();
    servers.erase(std::remove_if(servers.begin(), servers.end(),
                                 [](const OpdsServer& s) {
                                   if (!s.username.empty()) return false;
                                   return std::any_of(std::begin(RETIRED), std::end(RETIRED),
                                                      [&](const char* url) { return s.url == url; });
                                 }),
                  servers.end());
    if (servers.size() != before) LOG_DBG("OPS", "Retired %zu default catalog(s)", before - servers.size());

    // Get Books introduced. New in this version, so no existing reader can
    // have deleted it, and adding it is not overriding anybody's choice.
    addDefaultIfAbsent(GET_BOOKS);
  }

  return true;  // always resave: the version itself has to persist
}

// Shared by the migration and first-run seeding so the two cannot drift into
// disagreeing about what a default is.
void OpdsServerStore::addDefaultIfAbsent(const DefaultCatalog& entry) {
  if (servers.size() >= MAX_SERVERS) return;
  const bool present =
      std::any_of(servers.begin(), servers.end(), [&](const OpdsServer& s) { return s.url == entry.url; });
  if (present) return;
  servers.push_back(OpdsServer{entry.name, entry.url, entry.username, entry.password});
  LOG_DBG("OPS", "Seeded default catalog: %s", entry.name);
}

bool OpdsServerStore::seedDefaultCatalogs() {
  if (defaultsSeeded) return false;
  defaultsSeeded = true;

  for (const auto& entry : DEFAULTS) addDefaultIfAbsent(entry);

  return saveToFile();
}

bool OpdsServerStore::addServer(const OpdsServer& server) {
  if (servers.size() >= MAX_SERVERS) {
    LOG_DBG("OPS", "Cannot add more servers, limit of %zu reached", MAX_SERVERS);
    return false;
  }

  servers.push_back(server);
  LOG_DBG("OPS", "Added server: %s", server.name.c_str());
  return saveToFile();
}

bool OpdsServerStore::updateServer(size_t index, const OpdsServer& server) {
  if (index >= servers.size()) {
    return false;
  }

  servers[index] = server;
  LOG_DBG("OPS", "Updated server: %s", server.name.c_str());
  return saveToFile();
}

bool OpdsServerStore::removeServer(size_t index) {
  if (index >= servers.size()) {
    return false;
  }

  LOG_DBG("OPS", "Removed server: %s", servers[index].name.c_str());
  servers.erase(servers.begin() + static_cast<ptrdiff_t>(index));
  return saveToFile();
}

const OpdsServer* OpdsServerStore::getServer(size_t index) const {
  if (index >= servers.size()) {
    return nullptr;
  }
  return &servers[index];
}

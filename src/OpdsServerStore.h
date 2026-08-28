#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>
#include <vector>

struct OpdsServer {
  std::string name;
  std::string url;
  std::string username;
  std::string password;  // Plaintext in memory; obfuscated with hardware key on disk
};

/**
 * Singleton class for storing OPDS server configurations on the SD card.
 * Passwords are XOR-obfuscated with the device's unique hardware MAC address
 * and base64-encoded before writing to JSON.
 */
class OpdsServerStore : public PersistableStore<OpdsServerStore> {
 public:
  struct DefaultCatalog {
    const char* name;
    const char* url;
    const char* username;
    const char* password;
  };

 private:
  void addDefaultIfAbsent(const DefaultCatalog& entry);

  std::vector<OpdsServer> servers;
  // Persisted so a catalog the user deleted stays deleted; without it every
  // boot with an empty list would helpfully put the defaults back.
  bool defaultsSeeded = false;
  // Which generation of the default list this device has been through. Bump
  // SEED_VERSION and extend migrateSeeds() when the defaults change, or the
  // change reaches new devices only.
  static constexpr int SEED_VERSION = 1;
  int seedVersion = 0;
  bool migrateSeeds();

  static constexpr size_t MAX_SERVERS = 8;

  OpdsServerStore() = default;

  friend class PersistableStore<OpdsServerStore>;

 public:
  static const char* getFilePath() { return "/.crosspoint/opds.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  bool addServer(const OpdsServer& server);
  bool updateServer(size_t index, const OpdsServer& server);
  bool removeServer(size_t index);

  const std::vector<OpdsServer>& getServers() const { return servers; }
  const OpdsServer* getServer(size_t index) const;
  size_t getCount() const { return servers.size(); }
  bool hasServers() const { return !servers.empty(); }

  /**
   * Add the built-in public catalogs the first time this runs, so a fresh
   * install can search and download without typing a URL on an e-ink
   * keyboard. Both are free, legitimate libraries whose EPUBs need almost no
   * optimizing. Does nothing once it has run, whatever the user did after.
   */
  bool seedDefaultCatalogs();

  /**
   * Import catalogs from a provisioning file at the SD card root, if present.
   *
   * This exists so a private catalog can be put on a device without ever being
   * compiled into a public build: drop the file on the card, boot once, done.
   * Nothing about it is specific to any one server.
   *
   * Shape matches the stored config:
   *   {"servers":[{"name":"...","url":"...","username":"...","password":"..."}]}
   *
   * The file is DELETED after a successful import. It holds credentials in
   * plaintext, and leaving those sitting on a removable card -- readable by
   * anything that mounts it -- is worse than the small surprise of the file
   * disappearing. Re-drop it to re-import.
   */
  bool importSeedFile();

  static const char* getSeedFilePath() { return "/opds-seed.json"; }
};

#define OPDS_STORE OpdsServerStore::getInstance()

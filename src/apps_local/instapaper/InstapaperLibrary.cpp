#include "InstapaperLibrary.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

namespace instapaper {
namespace {

// Under /.crosspoint because this is a cache the app manages, not content a
// reader copies on by hand: everything here comes back from a sync. (Study's
// decks live at /study for the opposite reason.) The path itself is
// load-bearing -- see the fork-branding-boundary memory -- so the prefix is
// spelled once, here.
constexpr const char* kDir = "/.crosspoint/instapaper";
constexpr const char* kIndex = "/.crosspoint/instapaper/index.tsv";
constexpr const char* kBridge = "/.crosspoint/instapaper/.bridge";
constexpr const char* kTag = "INSTA";

bool readWholeFile(const char* path, std::string& out) {
  HalFile file;
  if (!Storage.openFileForRead(kTag, path, file)) return false;
  out.clear();
  char buffer[1024];
  int n = 0;
  while ((n = file.read(reinterpret_cast<uint8_t*>(buffer), sizeof(buffer))) > 0) {
    out.append(buffer, static_cast<size_t>(n));
  }
  file.close();
  return true;
}

bool writeWholeFile(const char* path, const std::string& text) {
  HalFile file;
  if (!Storage.openFileForWrite(kTag, path, file)) return false;
  const bool ok =
      file.write(reinterpret_cast<const uint8_t*>(text.data()), text.size()) == static_cast<int>(text.size());
  file.close();
  return ok;
}

// Write beside, then rename. Used for anything whose half-written form would
// be indistinguishable from a legitimate state.
bool writeAtomically(const char* path, const std::string& text) {
  const std::string temp = std::string(path) + ".part";
  if (!writeWholeFile(temp.c_str(), text)) {
    LOG_ERR(kTag, "cannot write %s", temp.c_str());
    return false;
  }
  Storage.remove(path);
  if (!Storage.rename(temp.c_str(), path)) {
    LOG_ERR(kTag, "cannot rename %s into place", temp.c_str());
    return false;
  }
  return true;
}

}  // namespace

const char* Library::directory() { return kDir; }

std::string Library::pathFor(const int64_t id) const { return std::string(kDir) + "/" + articleFileName(id); }

std::string Library::partPathFor(const int64_t id) const { return pathFor(id) + ".part"; }

void Library::load() {
  if (loaded_) return;
  loaded_ = true;
  std::string text;
  if (!readWholeFile(kIndex, text)) return;  // nothing synced yet is not an error
  if (!parseIndex(text, articles_)) {
    LOG_ERR(kTag, "the index is not in our format; leaving it alone");
    articles_.clear();
    return;
  }
  sortForQueue(articles_);
  LOG_INF(kTag, "queue: %d articles", static_cast<int>(articles_.size()));
}

Article* Library::find(const int64_t id) {
  for (Article& a : articles_) {
    if (a.id == id) return &a;
  }
  return nullptr;
}

bool Library::saveIndex() {
  Storage.ensureDirectoryExists(kDir);
  return writeAtomically(kIndex, serializeIndex(articles_));
}

bool Library::readArticle(const int64_t id, std::string& out) const { return readWholeFile(pathFor(id).c_str(), out); }

bool Library::hasArticle(const int64_t id) const {
  HalFile file;
  if (!Storage.openFileForRead(kTag, pathFor(id).c_str(), file)) return false;
  // An empty file is not an article. It is what a write that opened and then
  // failed leaves behind, and it would draw as a blank reader with no
  // explanation.
  const bool real = file.size() > 0;
  file.close();
  return real;
}

std::vector<int64_t> Library::presentIds() const {
  std::vector<int64_t> out;
  for (const Article& a : articles_) {
    if (hasArticle(a.id)) out.push_back(a.id);
  }
  return out;
}

bool Library::commitPart(const int64_t id) const {
  const std::string part = partPathFor(id);
  const std::string real = pathFor(id);
  Storage.remove(real.c_str());
  if (Storage.rename(part.c_str(), real.c_str())) return true;
  LOG_ERR(kTag, "cannot put %s into place", real.c_str());
  Storage.remove(part.c_str());
  return false;
}

void Library::discardPart(const int64_t id) const { Storage.remove(partPathFor(id).c_str()); }

void Library::removeArticle(const int64_t id) const {
  Storage.remove(pathFor(id).c_str());
  Storage.remove(partPathFor(id).c_str());
}

bool Library::loadBridgeState(BridgeState& out) const {
  out = BridgeState{};
  std::string raw;
  if (!readWholeFile(kBridge, raw)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, raw) != DeserializationError::Ok) return false;
  out.token = doc["token"] | "";
  out.lastSyncAt = doc["lastSyncAt"] | static_cast<int64_t>(0);
  out.user = doc["user"] | "";
  out.paired = !out.token.empty();
  return out.paired;
}

bool Library::saveBridgeState(const BridgeState& state) const {
  Storage.ensureDirectoryExists(kDir);
  JsonDocument doc;
  doc["token"] = state.token;
  if (state.lastSyncAt > 0) doc["lastSyncAt"] = state.lastSyncAt;
  if (!state.user.empty()) doc["user"] = state.user;
  std::string raw;
  serializeJson(doc, raw);
  return writeAtomically(kBridge, raw);
}

void Library::clearBridgeState() const { Storage.remove(kBridge); }

void Library::wipeAccount() {
  load();  // populate articles_ so every downloaded file can be named and removed
  for (const Article& a : articles_) {
    Storage.remove(pathFor(a.id).c_str());
    Storage.remove(partPathFor(a.id).c_str());
  }
  Storage.remove(kIndex);
  Storage.remove(kBridge);
  // Sweep the directory itself last, taking any orphaned .part a cancelled
  // download left behind, so the card carries no trace of the account.
  Storage.removeDir(kDir);
  articles_.clear();
  // Authoritative-empty: the files are gone, so a later load() must not try to
  // read them back. loaded_ stays true precisely so it does not.
  loaded_ = true;
}

}  // namespace instapaper

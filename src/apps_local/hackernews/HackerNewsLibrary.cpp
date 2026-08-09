#include "HackerNewsLibrary.h"

#include <HalStorage.h>
#include <Logging.h>

#include <ctime>

namespace hn {
namespace {

// Plain files in a folder of their own: an index anyone can read in a text
// editor, and one article per file. Chosen so that the day this app is deleted,
// every article is still there and still readable.
constexpr const char* kDir = "/.crosspoint/hn";
constexpr const char* kIndex = "/.crosspoint/hn/saved.tsv";

std::string articlePath(const std::string& id) { return std::string(kDir) + "/" + id + ".txt"; }

bool readWholeFile(const char* path, std::string& out) {
  HalFile file;
  if (!Storage.openFileForRead("HN", path, file)) return false;
  out.clear();
  char buffer[512];
  int n = 0;
  while ((n = file.read(reinterpret_cast<uint8_t*>(buffer), sizeof(buffer))) > 0) {
    out.append(buffer, static_cast<size_t>(n));
  }
  file.close();
  return true;
}

bool writeWholeFile(const char* path, const std::string& text) {
  HalFile file;
  if (!Storage.openFileForWrite("HN", path, file)) return false;
  const bool ok =
      file.write(reinterpret_cast<const uint8_t*>(text.data()), text.size()) == static_cast<int>(text.size());
  file.close();
  return ok;
}

}  // namespace

void Library::load() {
  if (loaded_) return;
  loaded_ = true;
  std::string text;
  if (!readWholeFile(kIndex, text)) return;  // nothing saved yet is not an error
  if (!parseSavedIndex(text, articles_)) {
    LOG_ERR("HN", "saved index is not in our format; leaving it alone");
    articles_.clear();
  }
  LOG_INF("HN", "library: %d saved", static_cast<int>(articles_.size()));
}

bool Library::writeIndex() {
  Storage.ensureDirectoryExists(kDir);
  if (writeWholeFile(kIndex, serializeSavedIndex(articles_))) return true;
  LOG_ERR("HN", "could not write the saved index");
  return false;
}

bool Library::contains(const std::string& url) const {
  if (url.empty()) return false;
  const std::string id = savedIdFor(url);
  for (const SavedArticle& article : articles_) {
    if (article.id == id) return true;
  }
  return false;
}

bool Library::save(const std::string& url, const std::string& title, const std::string& text) {
  if (url.empty()) return false;
  load();

  const std::string id = savedIdFor(url);
  Storage.ensureDirectoryExists(kDir);
  if (!writeWholeFile(articlePath(id).c_str(), text)) {
    LOG_ERR("HN", "could not write the article");
    return false;
  }

  // Replace rather than append, so saving the same page twice is one entry.
  for (SavedArticle& existing : articles_) {
    if (existing.id != id) continue;
    existing.title = title;
    return writeIndex();
  }

  SavedArticle article;
  article.id = id;
  article.title = title;
  article.url = url;
  article.savedAt = static_cast<uint32_t>(std::time(nullptr));
  articles_.push_back(std::move(article));
  LOG_INF("HN", "saved %s (%d bytes)", id.c_str(), static_cast<int>(text.size()));
  return writeIndex();
}

bool Library::remove(const std::string& url) {
  if (url.empty()) return false;
  load();

  const std::string id = savedIdFor(url);
  const size_t before = articles_.size();
  for (size_t i = 0; i < articles_.size(); ++i) {
    if (articles_[i].id != id) continue;
    articles_.erase(articles_.begin() + static_cast<long>(i));
    break;
  }
  if (articles_.size() == before) return false;

  writeIndex();
  Storage.remove(articlePath(id).c_str());
  LOG_INF("HN", "unsaved %s", id.c_str());
  return true;
}

bool Library::readArticle(const SavedArticle& article, std::string& out) const {
  return readWholeFile(articlePath(article.id).c_str(), out);
}

}  // namespace hn

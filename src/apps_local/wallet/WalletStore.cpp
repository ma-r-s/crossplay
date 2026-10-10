#include "WalletStore.h"

#include <HalStorage.h>
#include <Logging.h>

namespace wallet {
namespace store {

namespace {

std::string pathOf(const std::string& file) { return std::string(kDir) + "/" + file; }

std::vector<std::string> names() {
  std::vector<std::string> out;
  const std::vector<String> listed = Storage.listFiles(kDir, 500);
  out.reserve(listed.size());
  for (const String& name : listed) out.emplace_back(name.c_str());
  return cardFiles(out);
}

}  // namespace

bool begin() {
  if (!Storage.ensureDirectoryExists(kDir)) {
    LOG_ERR("CARDS", "could not make %s", kDir);
    return false;
  }
  return true;
}

size_t count() { return names().size(); }

bool load(const std::string& file, Card& out) {
  if (numberOf(file) < 0) return false;
  std::string text;
  if (!Storage.readFileToString("CARDS", pathOf(file), kMaxFileBytes, text)) return false;
  if (!parseCard(text, out)) return false;
  out.file = file;
  return true;
}

std::vector<Card> loadAll() {
  std::vector<Card> cards;
  const std::vector<std::string> files = names();
  cards.reserve(files.size() < static_cast<size_t>(kMaxCards) ? files.size() : kMaxCards);
  for (const std::string& file : files) {
    if (cards.size() >= static_cast<size_t>(kMaxCards)) break;
    std::string text;
    if (!Storage.readFileToString("CARDS", pathOf(file), kMaxFileBytes, text)) continue;
    Card card;
    if (!parseCard(text, card)) continue;
    card.file = file;
    cards.push_back(std::move(card));
  }
  return cards;
}

bool add(Card& card) {
  card.file = nextFileName(names());
  const std::string path = pathOf(card.file);
  const std::string part = path + ".part";
  const std::string text = formatCard(card);
  {
    HalFile file;
    if (!Storage.openFileForWrite("CARDS", part.c_str(), file)) return false;
    if (file.write(text.data(), text.size()) != text.size()) {
      file.close();
      Storage.remove(part.c_str());
      return false;
    }
  }
  // Written beside and renamed into place, so a power cut mid-write never
  // leaves a card file holding half a code.
  if (!Storage.replaceFile(part.c_str(), path.c_str())) {
    Storage.remove(part.c_str());
    LOG_ERR("CARDS", "could not save %s", path.c_str());
    return false;
  }
  return true;
}

bool remove(const std::string& file) {
  if (numberOf(file) < 0) return false;
  const std::string path = pathOf(file);
  if (!Storage.exists(path.c_str())) return true;
  if (!Storage.remove(path.c_str())) {
    LOG_ERR("CARDS", "could not remove %s", path.c_str());
    return false;
  }
  return true;
}

}  // namespace store
}  // namespace wallet

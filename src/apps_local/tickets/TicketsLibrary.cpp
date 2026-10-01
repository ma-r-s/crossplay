#include "TicketsLibrary.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cctype>

namespace tickets {
namespace {

constexpr const char* kDir = "/tickets";
constexpr const char* kExt = ".json";
constexpr const char* kPartExt = ".part";
constexpr size_t kNameMax = 96;   // a filename, capped well past anything useful
constexpr int kMaxTickets = 120;  // a wallet, not an archive

bool hasJsonExt(const char* name) {
  const size_t n = std::char_traits<char>::length(name);
  constexpr size_t e = 5;  // ".json"
  if (n <= e) return false;
  // FAT is case-insensitive; a computer that wrote .JSON still wrote a ticket.
  for (size_t i = 0; i < e; ++i) {
    const char a = name[n - e + i];
    const char b = kExt[i];
    if ((a | 0x20) != b) return false;  // both sides are ASCII lowercase after |0x20
  }
  return true;
}

}  // namespace

bool Library::begin() {
  if (!Storage.ensureDirectoryExists(kDir)) {
    LOG_ERR("TICKETS", "could not make %s", kDir);
    return false;
  }
  scan();
  return true;
}

void Library::scan() {
  entries_.clear();
  entries_.reserve(16);
  skipped_ = 0;

  auto dir = Storage.open(kDir);
  if (!dir || !dir.isDirectory()) return;
  auto name = makeUniqueNoThrow<char[]>(kNameMax);
  if (!name) {
    LOG_ERR("TICKETS", "OOM: name buffer");
    return;
  }

  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (entry.isDirectory()) continue;
    entry.getName(name.get(), kNameMax);
    if (!hasJsonExt(name.get())) continue;

    const std::string path = std::string(kDir) + "/" + name.get();
    HalFile file;
    if (!Storage.openFileForRead("TICKETS", path, file)) {
      ++skipped_;
      continue;
    }
    size_t size = static_cast<size_t>(file.size());
    if (size > kMaxTicketBytes) {
      LOG_ERR("TICKETS", "%s is %u bytes; past the %u a ticket can be", path.c_str(), static_cast<unsigned>(size),
              static_cast<unsigned>(kMaxTicketBytes));
      ++skipped_;
      continue;
    }
    std::string json(size, '\0');
    const int read = file.read(&json[0], size);
    if (read < 0) {
      ++skipped_;
      continue;
    }
    json.resize(static_cast<size_t>(read));

    Ticket ticket;
    const ParseError err = parseTicket(json, ticket);
    if (err != ParseError::None) {
      LOG_ERR("TICKETS", "%s: %s", path.c_str(), nameOf(err));
      ++skipped_;
      continue;
    }
    ticket.fileName = name.get();
    entries_.push_back(std::move(ticket));
    if (static_cast<int>(entries_.size()) >= kMaxTickets) break;
  }

  // Alphabetical, case-insensitively, because that is the order a person can
  // predict. Recency would move the row under the finger.
  std::sort(entries_.begin(), entries_.end(), [](const Ticket& a, const Ticket& b) {
    const size_t n = std::min(a.name.size(), b.name.size());
    for (size_t i = 0; i < n; i++) {
      const int ca = std::tolower(static_cast<unsigned char>(a.name[i]));
      const int cb = std::tolower(static_cast<unsigned char>(b.name[i]));
      if (ca != cb) return ca < cb;
    }
    return a.name.size() < b.name.size();
  });
}

bool saveUploaded(const std::string& json, std::string& savedName, std::string& refusal) {
  Ticket ticket;
  const ParseError err = parseTicket(json, ticket);
  if (err != ParseError::None) {
    refusal = std::string("Not a ticket: ") + nameOf(err) + ".";
    LOG_ERR("TICKETS", "upload refused: %s", nameOf(err));
    return false;
  }

  std::string stem = fileStemFor(ticket.name);
  if (stem.empty()) stem = "ticket";

  // The device names the file and dedupes the stem: a second "Ryanair FR1234"
  // lands beside the first, never over it.
  auto pathForStem = [](const std::string& s) { return std::string(kDir) + "/" + s + kExt; };
  std::string chosen = stem;
  for (int i = 2; Storage.exists(pathForStem(chosen).c_str()); ++i) {
    if (i > 99) {
      refusal = "Too many tickets with the same name.";
      return false;
    }
    chosen = stem + " " + std::to_string(i);
  }

  const std::string real = pathForStem(chosen);
  const std::string part = real + kPartExt;
  {
    HalFile file;
    if (!Storage.openFileForWrite("TICKETS", part, file)) {
      refusal = "The card would not take it.";
      return false;
    }
    const bool written = file.write(json.data(), json.size()) == json.size();
    const bool closed = file.close();
    if (!written || !closed) {
      Storage.remove(part.c_str());
      refusal = "The card would not take it.";
      return false;
    }
  }
  // The rename is the commit: until it lands the wallet is untouched, so a
  // dropped connection costs the upload and never a ticket.
  if (!Storage.rename(part.c_str(), real.c_str())) {
    Storage.remove(part.c_str());
    refusal = "The card would not take it.";
    return false;
  }
  savedName = ticket.name;
  LOG_INF("TICKETS", "uploaded -> %s", real.c_str());
  return true;
}

bool Library::remove(const int index) {
  if (index < 0 || index >= count()) return false;
  const std::string path = std::string(kDir) + "/" + entries_[static_cast<size_t>(index)].fileName;
  if (!Storage.remove(path.c_str())) {
    LOG_ERR("TICKETS", "could not delete %s", path.c_str());
    return false;
  }
  entries_.erase(entries_.begin() + index);
  LOG_INF("TICKETS", "deleted %s", path.c_str());
  return true;
}

}  // namespace tickets

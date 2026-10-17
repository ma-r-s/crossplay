#include "NotesLibrary.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>

#include "NotesCore.h"

namespace notes {
namespace {

constexpr const char* kDir = "/notes";
constexpr const char* kExt = ".md";
constexpr const char* kPartExt = ".part";
constexpr size_t kNameMax = 64;
constexpr size_t kPreviewMax = 80;  // more than the card's strip can hold at any cut
constexpr int kMaxNotes = 120;

// The floor is NOT sized to a note, which is a few hundred bytes. It is sized
// by who PAYS when the card fills: the reader's own caches and Study's review
// log, which loses answers silently rather than refusing. See
// preconditions-protect-another-app.
constexpr const char* kFullMessage = "The card is nearly full, so nothing was saved.";
constexpr const char* kRefusedMessage = "The card would not take the change.";
constexpr uint64_t kCardFloorBytes = 12ull * 1024 * 1024;

// A note is one screen of text that a person typed on a phone keyboard or a
// panel. Past this it is a document, and a document wants a computer. The cap
// exists so a file dropped on the card by mistake -- an export, a log -- cannot
// take the whole heap when it is opened.
constexpr size_t kNoteMax = 16 * 1024;

bool endsWith(const std::string& text, const char* suffix) {
  const size_t n = std::char_traits<char>::length(suffix);
  return text.size() >= n && text.compare(text.size() - n, n, suffix) == 0;
}

}  // namespace

std::string Library::sanitise(const std::string& name) {
  size_t start = 0;
  while (start < name.size() && (name[start] == ' ' || name[start] == '\t')) start++;
  size_t stop = name.size();
  while (stop > start && (name[stop - 1] == ' ' || name[stop - 1] == '\t')) stop--;

  std::string out;
  for (size_t i = start; i < stop && out.size() < kNameMax; i++) {
    const char c = name[i];
    // A separator would name a file in another directory, and the rest are
    // either illegal on FAT or match more than one file. Dropped rather than
    // substituted: a name silently turned into a different name is worse than
    // a name the user can see is missing a character.
    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' ||
        c == '\r' || c == '\n') {
      continue;
    }
    out.push_back(c);
  }
  // A name that is only dots names this directory or its parent.
  if (out == "." || out == "..") out.clear();
  return out;
}

bool Library::exists(const std::vector<Entry>& entries, const std::string& name) {
  const std::string wanted = sanitise(name);
  for (const Entry& entry : entries) {
    if (entry.name.size() != wanted.size()) continue;
    bool same = true;
    for (size_t i = 0; i < wanted.size(); i++) {
      // FAT is case-insensitive, so two notes whose names differ only in case
      // are ONE file. Comparing case-sensitively here would let "shopping"
      // silently overwrite "Shopping".
      if (std::tolower(static_cast<unsigned char>(entry.name[i])) !=
          std::tolower(static_cast<unsigned char>(wanted[i]))) {
        same = false;
        break;
      }
    }
    if (same) return true;
  }
  return false;
}

std::string Library::pathFor(const std::string& name) const { return std::string(kDir) + "/" + name + kExt; }

std::string Library::partPathFor(const std::string& name) const {
  return std::string(kDir) + "/" + name + kExt + kPartExt;
}

bool Library::begin() {
  if (!Storage.ensureDirectoryExists(kDir)) {
    LOG_ERR("NOTES", "could not make %s", kDir);
    return false;
  }
  scan();
  return true;
}

void Library::sweepPartFiles() const {
  auto dir = Storage.open(kDir);
  if (!dir || !dir.isDirectory()) return;
  auto name = makeUniqueNoThrow<char[]>(kNameMax + 16);
  if (!name) return;
  std::vector<std::string> doomed;
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (entry.isDirectory()) continue;
    entry.getName(name.get(), kNameMax + 16);
    if (endsWith(name.get(), kPartExt)) doomed.emplace_back(name.get());
  }
  for (const std::string& part : doomed) Storage.remove((std::string(kDir) + "/" + part).c_str());
}

void Library::scan() {
  entries_.clear();
  sweepPartFiles();

  auto dir = Storage.open(kDir);
  if (!dir || !dir.isDirectory()) return;
  auto name = makeUniqueNoThrow<char[]>(kNameMax + 16);
  if (!name) {
    LOG_ERR("NOTES", "OOM: name buffer");
    return;
  }

  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (entry.isDirectory()) continue;
    entry.getName(name.get(), kNameMax + 16);
    const std::string filename(name.get());
    if (!endsWith(filename, kExt)) continue;

    Entry row;
    row.name = filename.substr(0, filename.size() - std::char_traits<char>::length(kExt));
    if (row.name.empty()) continue;

    std::string doc;
    if (load(row.name, doc)) {
      const std::vector<Line> lines = parse(doc);
      const Counts c = counts(lines);
      row.done = c.done;
      row.total = c.marked;
      row.hasTasks = c.marked > 0;
      if (!row.hasTasks) {
        // A note's card shows its first words instead of a tally. Only the
        // first line with something on it: a file that opens with a blank line
        // would otherwise preview as nothing at all.
        for (const Line& line : lines) {
          std::string text = textOf(doc, line);
          size_t begin = text.find_first_not_of(" \t");
          if (begin == std::string::npos) continue;
          text = text.substr(begin);
          row.preview = text.size() > kPreviewMax ? text.substr(0, kPreviewMax) : text;
          break;
        }
      }
    }
    entries_.push_back(std::move(row));
    if (static_cast<int>(entries_.size()) >= kMaxNotes) break;
  }

  sortEntries();
}

// Alphabetical, case-insensitively, because that is the order a person can
// predict. Recency would put a note somewhere different every time it was
// ticked, on a screen where the row you are aiming at must not move.
void Library::sortEntries() {
  std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
    const size_t n = std::min(a.name.size(), b.name.size());
    for (size_t i = 0; i < n; i++) {
      const int ca = std::tolower(static_cast<unsigned char>(a.name[i]));
      const int cb = std::tolower(static_cast<unsigned char>(b.name[i]));
      if (ca != cb) return ca < cb;
    }
    return a.name.size() < b.name.size();
  });
}

bool Library::load(const std::string& name, std::string& doc) const {
  HalFile file;
  if (!Storage.openFileForRead("NOTES", pathFor(name), file)) return false;
  const size_t size = static_cast<size_t>(file.size());
  const size_t want = size > kNoteMax ? kNoteMax : size;
  doc.assign(want, '\0');
  if (want == 0) return true;
  const int read = file.read(&doc[0], want);
  if (read < 0) {
    doc.clear();
    return false;
  }
  doc.resize(static_cast<size_t>(read));
  return true;
}

// Asked ONLY after a write has failed. The walk is slow (5.3s on the desk
// device) and every millisecond of it used to land between OK and the next
// keystroke; here it buys an accurate sentence on a path that has already gone
// wrong, which is the only place it is worth paying for.
bool Library::cardIsFull() const {
  uint64_t free = 0;
  // "Could not answer" is not "no space".
  if (!Storage.freeBytes(free)) return false;
  return free < kCardFloorBytes;
}

bool Library::save(const std::string& name, const std::string& doc, std::string& message) {
  // No free-space probe on the way in. The write is the test: it either lands
  // or it does not, and the card only has to be interrogated in the second case.

  const std::string part = partPathFor(name);
  {
    HalFile file;
    if (!Storage.openFileForWrite("NOTES", part, file)) {
      message = cardIsFull() ? kFullMessage : kRefusedMessage;
      return false;
    }
    if (!doc.empty() && file.write(doc.data(), doc.size()) != static_cast<int>(doc.size())) {
      message = cardIsFull() ? kFullMessage : kRefusedMessage;
      Storage.remove(part.c_str());
      return false;
    }
  }
  // The rename is the commit. Until it lands the real file is untouched, so a
  // power cut costs the edit and never the note.
  const std::string real = pathFor(name);
  Storage.remove(real.c_str());
  if (!Storage.rename(part.c_str(), real.c_str())) {
    message = cardIsFull() ? kFullMessage : kRefusedMessage;
    Storage.remove(part.c_str());
    return false;
  }
  return true;
}

bool Library::create(const std::string& name, std::string& message) {
  const std::string clean = sanitise(name);
  if (clean.empty()) {
    message = "That name cannot be used.";
    return false;
  }
  if (exists(entries_, clean)) {
    message = "There is already a note called that.";
    return false;
  }
  if (!save(clean, std::string(), message)) return false;
  // Inserted in place rather than re-reading the directory: scan() opens and
  // parses EVERY note to count its lines, which is a read per note for a file
  // that was just written empty.
  Entry row;
  row.name = clean;
  entries_.push_back(std::move(row));
  sortEntries();
  return true;
}

bool Library::rename(const std::string& from, const std::string& to, std::string& message) {
  const std::string clean = sanitise(to);
  if (clean.empty()) {
    message = "That name cannot be used.";
    return false;
  }
  if (clean == from) return true;
  if (exists(entries_, clean)) {
    message = "There is already a note called that.";
    return false;
  }
  if (!Storage.rename(pathFor(from).c_str(), pathFor(clean).c_str())) {
    message = cardIsFull() ? kFullMessage : kRefusedMessage;
    return false;
  }
  for (Entry& entry : entries_) {
    if (entry.name != from) continue;
    entry.name = clean;
    break;
  }
  sortEntries();
  return true;
}

bool Library::remove(const std::string& name) {
  const bool gone = Storage.remove(pathFor(name).c_str());
  for (size_t i = 0; i < entries_.size(); i++) {
    if (entries_[i].name != name) continue;
    entries_.erase(entries_.begin() + static_cast<long>(i));
    break;
  }
  return gone;
}

}  // namespace notes

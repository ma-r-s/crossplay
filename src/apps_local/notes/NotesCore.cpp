#include "NotesCore.h"

#include <algorithm>
#include <cctype>

namespace notes {
namespace {

bool isBullet(char c) { return c == '-' || c == '*' || c == '+'; }

bool isSpace(char c) { return c == ' ' || c == '\t'; }

// Fills in the task fields when the line begins with a strict task marker.
// Anything else is left as prose, which is the safe direction: a line wrongly
// read as prose merely cannot be ticked, while a line wrongly read as a task
// would put a tickable box on the user's sentence.
void classify(const std::string& doc, Line& line) {
  size_t i = line.begin;
  while (i < line.end && isSpace(doc[i])) i++;
  if (i >= line.end || !isBullet(doc[i])) return;
  i++;
  if (i >= line.end || doc[i] != ' ') return;
  i++;
  if (i >= line.end || doc[i] != '[') return;
  const size_t mark = i + 1;
  if (mark + 1 >= line.end) return;
  const char m = doc[mark];
  if (m != ' ' && m != 'x' && m != 'X') return;
  if (doc[mark + 1] != ']') return;

  // The marker must be followed by a space or be the whole line. Without this
  // "- [x]tra credit" would parse as a ticked task called "tra credit".
  size_t after = mark + 2;
  if (after < line.end) {
    if (doc[after] != ' ') return;
    after++;
  }

  line.isTask = true;
  line.checked = (m == 'x' || m == 'X');
  line.markAt = mark;
  line.textBegin = after;
}

}  // namespace

std::vector<Line> parse(const std::string& doc) {
  std::vector<Line> lines;
  size_t i = 0;
  while (i <= doc.size()) {
    Line line;
    line.begin = i;
    size_t j = i;
    while (j < doc.size() && doc[j] != '\n') j++;
    line.end = j;
    // A '\r' belongs to the line ending, not to the text. Files arrive from
    // phones and desktop editors, so CRLF is not hypothetical.
    if (line.end > line.begin && doc[line.end - 1] == '\r') line.end--;
    line.textBegin = line.begin;
    classify(doc, line);
    lines.push_back(line);
    if (j >= doc.size()) break;
    i = j + 1;
  }
  // A document ending in a newline has produced a trailing empty line above.
  // Keep it: it is a real line to the user, and dropping it would make the
  // ranges disagree with the file on the next edit.
  return lines;
}

bool toggle(std::string& doc, Line& line) {
  if (!line.isTask || line.markAt >= doc.size()) return false;
  doc[line.markAt] = line.checked ? ' ' : 'x';
  line.checked = !line.checked;
  return true;
}

Counts counts(const std::vector<Line>& lines) {
  Counts c;
  for (const Line& line : lines) {
    if (!line.isTask) continue;
    c.total++;
    if (line.checked) c.done++;
  }
  return c;
}

std::string textOf(const std::string& doc, const Line& line) {
  if (line.textBegin >= line.end) return std::string();
  return doc.substr(line.textBegin, line.end - line.textBegin);
}

std::string title(const std::string& doc, const std::vector<Line>& lines) {
  for (const Line& line : lines) {
    std::string text = textOf(doc, line);
    // A task's own text is a fine title; textOf has already skipped its box.
    size_t start = 0;
    while (start < text.size() && isSpace(text[start])) start++;
    size_t stop = text.size();
    while (stop > start && isSpace(text[stop - 1])) stop--;
    if (start >= stop) continue;
    text = text.substr(start, stop - start);
    if (!line.isTask && text.size() > 1 && text[0] == '#') {
      size_t k = 0;
      while (k < text.size() && text[k] == '#') k++;
      while (k < text.size() && isSpace(text[k])) k++;
      if (k < text.size()) text = text.substr(k);
    }
    return text;
  }
  return std::string();
}

std::string fold(const std::string& text) {
  size_t start = 0;
  while (start < text.size() && isSpace(text[start])) start++;
  size_t stop = text.size();
  while (stop > start && isSpace(text[stop - 1])) stop--;
  std::string out;
  out.reserve(stop - start);
  for (size_t i = start; i < stop; i++) {
    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(text[i]))));
  }
  return out;
}

std::vector<std::string> taskTexts(const std::string& doc) {
  std::vector<std::string> out;
  for (const Line& line : parse(doc)) {
    if (line.isTask) out.push_back(textOf(doc, line));
  }
  return out;
}

std::vector<std::string> clearChecked(std::string& doc) {
  const std::vector<Line> lines = parse(doc);
  std::vector<std::string> removed;
  std::string kept;
  kept.reserve(doc.size());

  for (size_t i = 0; i < lines.size(); i++) {
    const Line& line = lines[i];
    const bool last = (i + 1 == lines.size());
    if (line.isTask && line.checked) {
      removed.push_back(textOf(doc, line));
      continue;
    }
    // Rebuild with the line ending the file actually used, rather than
    // normalising it: a note edited on a desktop and cleared on the device
    // should not come back as a one-line diff of every row.
    kept.append(doc, line.begin, line.end - line.begin);
    if (!last) {
      const size_t eol = line.end;
      if (eol < doc.size() && doc[eol] == '\r') kept.push_back('\r');
      kept.push_back('\n');
    }
  }
  doc = kept;
  return removed;
}

namespace often {

void record(std::vector<Entry>& history, const std::vector<std::string>& texts, long stamp) {
  for (const std::string& text : texts) {
    const std::string key = fold(text);
    if (key.empty()) continue;
    bool found = false;
    for (Entry& entry : history) {
      if (fold(entry.text) != key) continue;
      entry.count++;
      entry.lastSeen = stamp;
      // Keep the newest spelling: a person who starts writing "Oat milk"
      // instead of "oat milk" meant the change.
      entry.text = text;
      found = true;
      break;
    }
    if (!found) history.push_back(Entry{text, 1, stamp});
  }
}

std::vector<std::string> suggest(const std::vector<Entry>& history, const std::vector<std::string>& present,
                                 size_t max) {
  std::vector<std::string> presentKeys;
  presentKeys.reserve(present.size());
  for (const std::string& text : present) {
    std::string key = fold(text);
    if (!key.empty()) presentKeys.push_back(std::move(key));
  }

  std::vector<const Entry*> pool;
  pool.reserve(history.size());
  for (const Entry& entry : history) {
    const std::string key = fold(entry.text);
    if (key.empty()) continue;
    if (std::find(presentKeys.begin(), presentKeys.end(), key) != presentKeys.end()) continue;
    pool.push_back(&entry);
  }

  std::stable_sort(pool.begin(), pool.end(), [](const Entry* a, const Entry* b) {
    if (a->count != b->count) return a->count > b->count;
    return a->lastSeen > b->lastSeen;
  });

  std::vector<std::string> out;
  for (const Entry* entry : pool) {
    if (out.size() >= max) break;
    out.push_back(entry->text);
  }
  return out;
}

}  // namespace often

}  // namespace notes

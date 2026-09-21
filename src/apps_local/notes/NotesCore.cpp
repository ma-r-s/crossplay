#include "NotesCore.h"

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
  for (size_t i = 0; i < lines.size(); i++) {
    const Line& line = lines[i];
    // A blank line is not an item, and the trailing one every file ending in a
    // newline produces is not either.
    if (line.begin >= line.end) continue;
    c.total++;
    if (line.isTask) c.marked++;
    if (line.checked) c.done++;
  }
  return c;
}

std::string textOf(const std::string& doc, const Line& line) {
  if (line.textBegin >= line.end) return std::string();
  return doc.substr(line.textBegin, line.end - line.textBegin);
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

bool coerceToList(std::string& doc) {
  const std::vector<Line> lines = parse(doc);
  std::string out;
  out.reserve(doc.size() + lines.size() * 6);
  bool changed = false;

  for (size_t i = 0; i < lines.size(); i++) {
    const Line& line = lines[i];
    const bool last = (i + 1 == lines.size());
    const bool blank = line.begin >= line.end;
    // A blank line is left blank rather than turned into an empty tick box: a
    // person who pressed return twice meant a gap, not a thing to do.
    if (!blank && !line.isTask) {
      out += "- [ ] ";
      changed = true;
    }
    out.append(doc, line.begin, line.end - line.begin);
    if (!last) {
      const size_t eol = line.end;
      if (eol < doc.size() && doc[eol] == '\r') out.push_back('\r');
      out.push_back('\n');
    }
  }
  doc = out;
  return changed;
}

Kind kindOf(const std::vector<Line>& lines, const bool emptyIsList) {
  bool any = false;
  for (const Line& line : lines) {
    if (line.begin >= line.end) continue;
    any = true;
    if (line.isTask) return Kind::List;
  }
  return any ? Kind::Page : (emptyIsList ? Kind::List : Kind::Page);
}

bool stripMarkers(std::string& doc) {
  const std::vector<Line> lines = parse(doc);
  std::string out;
  out.reserve(doc.size());
  bool changed = false;

  for (size_t i = 0; i < lines.size(); i++) {
    const Line& line = lines[i];
    const bool last = (i + 1 == lines.size());
    if (line.isTask) {
      // From the text, not from the whole line: the marker and the indent in
      // front of it are what is being removed.
      out.append(doc, line.textBegin, line.end - line.textBegin);
      changed = true;
    } else {
      out.append(doc, line.begin, line.end - line.begin);
    }
    if (!last) {
      const size_t eol = line.end;
      if (eol < doc.size() && doc[eol] == '\r') out.push_back('\r');
      out.push_back('\n');
    }
  }
  doc = out;
  return changed;
}

}  // namespace notes
#include "ReadingStatsTitles.h"

#include <vector>

namespace rstatsui {

namespace {

// One codepoint of already-clean UTF-8 at `i`, and its length in bytes.
uint32_t codepointAt(const std::string& s, const size_t i, size_t& length) {
  const auto b = [&s](const size_t at) { return at < s.size() ? static_cast<uint8_t>(s[at]) : uint8_t{0x80}; };
  const uint8_t lead = b(i);
  if (lead < 0x80) {
    length = 1;
    return lead;
  }
  if ((lead & 0xE0) == 0xC0) {
    length = 2;
    return ((lead & 0x1Fu) << 6) | (b(i + 1) & 0x3Fu);
  }
  if ((lead & 0xF0) == 0xE0) {
    length = 3;
    return ((lead & 0x0Fu) << 12) | ((b(i + 1) & 0x3Fu) << 6) | (b(i + 2) & 0x3Fu);
  }
  length = 4;
  return ((lead & 0x07u) << 18) | ((b(i + 1) & 0x3Fu) << 12) | ((b(i + 2) & 0x3Fu) << 6) | (b(i + 3) & 0x3Fu);
}

// Han, kana, Hangul and the full-width forms: written without spaces, so a
// line may end between any two of them.
bool isCjk(const uint32_t cp) {
  return (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xAC00 && cp <= 0xD7AF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
         (cp >= 0xFF00 && cp <= 0xFFEF) || (cp >= 0x20000 && cp <= 0x2FFFF);
}

// Marks that close what came before them, which a line must not start with.
bool closesPrevious(const uint32_t cp) {
  switch (cp) {
    case 0x3001:  // ideographic comma
    case 0x3002:  // ideographic full stop
    case 0x3009:
    case 0x300B:
    case 0x300D:
    case 0x300F:
    case 0x3011:
    case 0x30FC:  // prolonged sound mark
    case 0xFF01:
    case 0xFF09:
    case 0xFF0C:
    case 0xFF0E:
    case 0xFF1F:
      return true;
    default:
      return false;
  }
}

// What a line may be broken into: a word, or one CJK character with any
// closing mark after it. `spaced`: a space separated it from the unit before.
struct Unit {
  size_t begin;
  size_t end;
  bool spaced;
  bool cjk;
};

std::vector<Unit> unitsOf(const std::string& text) {
  std::vector<Unit> units;
  units.reserve(text.size() / 2 + 1);
  bool spaced = false;
  size_t i = 0;
  while (i < text.size()) {
    size_t length = 1;
    const uint32_t cp = codepointAt(text, i, length);
    if (cp == ' ') {
      spaced = true;
    } else if (!units.empty() && !spaced && closesPrevious(cp)) {
      units.back().end = i + length;
    } else if (isCjk(cp)) {
      units.push_back(Unit{i, i + length, spaced && !units.empty(), true});
    } else if (!units.empty() && !spaced && !units.back().cjk) {
      units.back().end = i + length;
    } else {
      units.push_back(Unit{i, i + length, spaced && !units.empty(), false});
    }
    if (cp != ' ') spaced = false;
    i += length;
  }
  return units;
}

class Fitter {
 public:
  Fitter(const fui::DrawTarget& target, const fui::FontId font, const int16_t width)
      : target_(target), font_(font), width_(width) {
    style_.font = font;
  }
  bool fits(const std::string& run) const { return target_.measureText(font_, run.c_str(), style_).width <= width_; }

 private:
  const fui::DrawTarget& target_;
  fui::FontId font_;
  int16_t width_;
  fui::TextStyle style_;
};

void dropLastCodepoint(std::string& s) {
  while (!s.empty() && (static_cast<uint8_t>(s.back()) & 0xC0) == 0x80) s.pop_back();
  if (!s.empty()) s.pop_back();
}

// Lays `text` out at one cut. A cut that is not the last may refuse: a word
// wider than its line, or more lines than the box holds, means "try smaller".
// The last cut never refuses; it breaks inside the word and, past its lines,
// marks the cut.
bool layOutAt(const Fitter& fit, const std::string& text, const std::vector<Unit>& units, const int maxLines,
              const bool last, std::vector<std::string>& lines, bool& whole) {
  lines.clear();
  whole = true;
  std::string current;
  for (const Unit& unit : units) {
    const std::string piece = text.substr(unit.begin, unit.end - unit.begin);
    const std::string joined = current.empty() ? piece : current + (unit.spaced ? " " : "") + piece;
    if (fit.fits(joined)) {
      current = joined;
      continue;
    }
    if (!current.empty()) {
      lines.push_back(current);
      current.clear();
      if (!last && static_cast<int>(lines.size()) >= maxLines) return false;
    }
    if (fit.fits(piece)) {
      current = piece;
      continue;
    }
    if (!last) return false;
    // One word wider than the line, at the smallest cut: break it between
    // characters rather than lose it.
    size_t i = 0;
    while (i < piece.size()) {
      size_t length = 1;
      codepointAt(piece, i, length);
      const std::string next = current + piece.substr(i, length);
      if (fit.fits(next) || current.empty()) {
        current = next;
      } else {
        lines.push_back(current);
        current = piece.substr(i, length);
      }
      i += length;
    }
  }
  if (!current.empty()) lines.push_back(current);
  if (static_cast<int>(lines.size()) <= maxLines) return true;
  if (!last) return false;

  // Even the smallest cut cannot hold it: keep what fits and say the rest
  // exists. Only a title far longer than any real one reaches this.
  whole = false;
  lines.resize(static_cast<size_t>(maxLines));
  std::string& tail = lines.back();
  while (!tail.empty() && !fit.fits(tail + "...")) dropLastCodepoint(tail);
  while (!tail.empty() && tail.back() == ' ') tail.pop_back();
  tail += "...";
  return true;
}

}  // namespace

TitleLayout layOutTitle(const fui::DrawTarget& target, const char* title, const int16_t width, const int16_t height) {
  TitleLayout layout;
  if (title == nullptr || *title == '\0' || width <= 0 || height <= 0) return layout;
  const std::string text(title);
  const std::vector<Unit> units = unitsOf(text);

  // The three bound slots, largest line first: ordered by what the faces
  // measure, never by the slots' names.
  fui::FontId rungs[3] = {fui::FONT_SLOT_TITLE, fui::FONT_SLOT_BODY, fui::FONT_SLOT_SMALL};
  for (int i = 1; i < 3; ++i) {
    for (int j = i; j > 0 && target.lineHeight(rungs[j]) > target.lineHeight(rungs[j - 1]); --j) {
      const fui::FontId swap = rungs[j];
      rungs[j] = rungs[j - 1];
      rungs[j - 1] = swap;
    }
  }

  std::vector<std::string> lines;
  for (int r = 0; r < 3; ++r) {
    const int16_t lineHeight = target.lineHeight(rungs[r]);
    int maxLines = lineHeight > 0 ? height / lineHeight : 0;
    if (maxLines > kMaxTitleLines) maxLines = kMaxTitleLines;
    const bool last = r == 2;
    if (maxLines <= 0 && !last) continue;
    if (maxLines <= 0) maxLines = 1;
    bool whole = true;
    if (!layOutAt(Fitter(target, rungs[r], width), text, units, maxLines, last, lines, whole)) continue;
    layout.font = rungs[r];
    layout.lineHeight = lineHeight;
    layout.whole = whole;
    layout.lines = static_cast<int>(lines.size());
    for (int i = 0; i < layout.lines; ++i) layout.text[i] = lines[static_cast<size_t>(i)];
    return layout;
  }
  return layout;
}

void drawTitle(fui::DrawTarget& target, const fui::Rect& box, const TitleLayout& layout) {
  if (layout.lines <= 0) return;
  fui::TextStyle style;
  style.font = layout.font;
  style.align = fui::TextAlign::Left;
  style.color = fui::Color::Black;
  style.maxLines = 1;
  const int block = layout.lines * layout.lineHeight;
  int y = box.y + (box.height - block) / 2;
  if (y < box.y) y = box.y;
  for (int i = 0; i < layout.lines; ++i) {
    target.text(fui::makeRect(box.x, static_cast<int16_t>(y), box.width, layout.lineHeight), layout.text[i].c_str(),
                style);
    y += layout.lineHeight;
  }
}

}  // namespace rstatsui

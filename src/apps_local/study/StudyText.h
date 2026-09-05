#pragma once

// Line breaking for the card face, with the one thing the renderer cannot
// tell you: which codepoints ended up on which line.
//
// This used to live inside StudyActivity as a lambda over the renderer, which
// was fine while the only question was "where do the lines break". Cloze asks
// a second one -- "where on the screen is the run the converter marked" -- and
// answering it needs the codepoint index of every line. Written as one
// freestanding template rather than as two loops so the two can never
// disagree about where a break goes: an underline drawn from a second copy of
// the wrap is an underline under the wrong word.
//
// Freestanding C++17, like StudyDeck and StudyScheduler: no Arduino, no
// renderer, no heap. Measurement arrives as a callable, which is what lets
// host-tests/study drive the whole thing with a metric it chooses. The
// caller owns the line buffer for the same reason nothing else here
// allocates.

#include <cstdint>
#include <cstring>

namespace study {

// How many bytes this UTF-8 sequence occupies. Never zero, even for a stray
// continuation byte, or a caller stepping through a string would spin.
inline int utf8Length(const char* p) {
  const unsigned char c = static_cast<unsigned char>(*p);
  if (c < 0x80) return 1;
  if ((c & 0xE0) == 0xC0) return 2;
  if ((c & 0xF0) == 0xE0) return 3;
  if ((c & 0xF8) == 0xF0) return 4;
  return 1;
}

inline uint32_t nextCodepoint(const char*& p) {
  const unsigned char c = static_cast<unsigned char>(*p);
  const int length = utf8Length(p);
  uint32_t value = c;
  if (length == 2) {
    value = c & 0x1F;
  } else if (length == 3) {
    value = c & 0x0F;
  } else if (length == 4) {
    value = c & 0x07;
  }
  for (int i = 1; i < length; ++i) {
    value = (value << 6) | (static_cast<unsigned char>(p[i]) & 0x3F);
  }
  p += length;
  return value;
}

// May a line break happen either side of this character? True for spaces and
// for CJK, which is written without spaces and breaks almost anywhere, and
// for the newline, which is not a break the wrap may take but one it MUST --
// fitsAsDrawn measures runs with this same predicate, so a newline that was
// not breakable here made two paragraphs look like one unbreakable run and
// condemned the card to the fallback face.
inline bool isBreakable(const uint32_t codepoint) {
  if (codepoint == ' ' || codepoint == '\n') return true;
  return (codepoint >= 0x2E80 && codepoint <= 0x9FFF) || (codepoint >= 0xF900 && codepoint <= 0xFAFF) ||
         (codepoint >= 0xFF00 && codepoint <= 0xFFEF);
}

// Codepoints in a NUL-terminated string.
inline int codepointCount(const char* text) {
  int count = 0;
  for (const char* p = text; *p != '\0';) {
    if (nextCodepoint(p) == 0) break;
    ++count;
  }
  return count;
}

// Bytes occupied by the first `count` codepoints of `text`, clamped to its
// length. This is how a codepoint offset becomes something the renderer can
// measure.
inline int bytesForCodepoints(const char* text, const int count) {
  const char* p = text;
  for (int seen = 0; seen < count && *p != '\0'; ++seen) {
    if (nextCodepoint(p) == 0) break;
  }
  return static_cast<int>(p - text);
}

// One laid-out line, handed to the caller as it is produced.
struct WrappedLine {
  const char* text = "";  // NUL-terminated, in the caller's buffer
  int bytes = 0;
  int startCodepoint = 0;  // index of the first character, into the whole string
  int codepoints = 0;      // how many characters are on this line
};

// Break `text` into lines no wider than `maxWidth`.
//
// `line` is the caller's scratch buffer and `lineBytes` its size; every
// WrappedLine points into it and is only valid until the next call to `emit`.
// `measure(const char*)` returns the pixel width of a NUL-terminated string.
//
// A break unit is a whole word for Latin and a single character for CJK,
// because Chinese is written without spaces and a space-only rule finds no
// break at all -- every sentence then runs off both edges. A word longer than
// the line is not broken: it overhangs, and fitsAsDrawn() is what catches
// that before it is drawn.
template <typename Measure, typename Emit>
void wrapText(const char* text, const int maxWidth, char* line, const int lineBytes, Measure measure, Emit emit) {
  if (text == nullptr || *text == '\0' || lineBytes < 8) return;

  int lineLength = 0;
  int lineStartCp = 0;
  int cpIndex = 0;

  const auto flush = [&]() {
    if (lineLength == 0) return;
    line[lineLength] = '\0';
    WrappedLine out;
    out.text = line;
    out.bytes = lineLength;
    out.startCodepoint = lineStartCp;
    out.codepoints = cpIndex - lineStartCp;
    emit(out);
    lineLength = 0;
    lineStartCp = cpIndex;
  };

  const char* p = text;
  while (*p != '\0') {
    // One break unit. Measured, not counted: a CJK glyph is three bytes and
    // full width, a Latin one is one byte and narrow.
    const char* unitEnd = p;
    if (isBreakable(nextCodepoint(unitEnd))) {
      unitEnd = p + utf8Length(p);
    } else {
      while (*unitEnd != '\0') {
        const char* peek = unitEnd;
        if (isBreakable(nextCodepoint(peek))) break;
        unitEnd = peek;
      }
    }
    int unitBytes = static_cast<int>(unitEnd - p);
    if (unitBytes <= 0) break;

    // A hard break. Anki's fields carry real structure -- a <br>, a list, the
    // paragraphs of a Back Extra -- and flattening it to spaces ran a
    // four-item list into one grey line. The newline is consumed rather than
    // drawn, and still counts as a codepoint, because the span offsets the
    // converter recorded were measured over the string as it is.
    if (*p == '\n') {
      flush();
      ++cpIndex;
      ++lineStartCp;
      ++p;
      continue;
    }

    if (unitBytes >= lineBytes) {
      // A run with no break in it that is longer than the whole line buffer:
      // a URL, a chemical name, a paragraph of a script this build does not
      // know breaks in. It used to end the wrap, so the run AND everything
      // after it drew as nothing -- a blank card with no way to tell it from
      // a broken font. Break it by codepoint instead: an awkward line is a
      // card you can still read.
      flush();
      int taken = 0;
      for (const char* q = p; *q != '\0';) {
        const char* at = q;
        if (nextCodepoint(q) == 0) break;
        const int width = static_cast<int>(q - at);
        if (taken + width >= lineBytes) break;
        std::memcpy(line + taken, at, static_cast<size_t>(width));
        line[taken + width] = '\0';
        // At least one codepoint always goes on, however wide: a line that
        // takes nothing is a loop that never advances.
        if (taken > 0 && measure(line) > maxWidth) {
          line[taken] = '\0';
          break;
        }
        taken += width;
      }
      if (taken == 0) break;
      unitEnd = p + taken;
      unitBytes = taken;
    }

    if (lineLength > 0 && lineLength + unitBytes < lineBytes) {
      // Try it on the current line first. The candidate is assembled in place
      // and rolled back if it does not fit, which avoids a second buffer.
      std::memcpy(line + lineLength, p, static_cast<size_t>(unitBytes));
      line[lineLength + unitBytes] = '\0';
      if (measure(line) > maxWidth) {
        flush();
        // A leading space after a break IS the break, and it still counts as
        // a codepoint the span offsets were measured against.
        while (*p == ' ') {
          ++p;
          ++cpIndex;
          ++lineStartCp;
        }
        continue;
      }
    } else if (lineLength + unitBytes >= lineBytes) {
      flush();
      continue;
    } else {
      std::memcpy(line + lineLength, p, static_cast<size_t>(unitBytes));
    }

    int unitCp = 0;
    for (const char* q = p; q < unitEnd;) {
      if (nextCodepoint(q) == 0) break;
      ++unitCp;
    }
    lineLength += unitBytes;
    cpIndex += unitCp;
    p = unitEnd;
  }
  flush();
}

// The part of the codepoint run [spanStart, spanStart + spanLength) that falls
// on `line`, as codepoint offsets RELATIVE to the line. Returns false when
// none of it does. A span that wraps intersects more than one line and is
// marked on each, which is the only behaviour that does not lie about where
// the answer was.
inline bool spanOnLine(const WrappedLine& line, const int spanStart, const int spanLength, int& from, int& to) {
  if (spanLength <= 0) return false;
  const int spanEnd = spanStart + spanLength;
  const int lineEnd = line.startCodepoint + line.codepoints;
  from = spanStart > line.startCodepoint ? spanStart - line.startCodepoint : 0;
  to = (spanEnd < lineEnd ? spanEnd : lineEnd) - line.startCodepoint;
  return to > from;
}

// Draw `text` centred, wrapped, with the codepoint run
// [spanStart, spanStart + spanLength) underlined. Returns the y below the last
// line, so callers stack blocks by threading it.
//
// `target` is anything with the four calls below; the device passes the
// GfxRenderer, host-tests/study passes a recorder. Templated rather than
// virtual because this runs once per line per card on a device with no
// headroom for a vtable it does not need -- and because a header-only
// template is what lets the test compile the REAL geometry rather than a
// copy of it. The underline arithmetic is the part worth testing: it is
// three subtractions and a centring, all of which look right and one of
// which was not.
//
//   int  getScreenWidth() const
//   int  getTextWidth(int fontId, const char* text) const
//   int  getTextHeight(int fontId) const
//   void drawText(int fontId, int x, int y, const char* text, bool black) const
//   void fillRect(int x, int y, int w, int h, bool black) const
template <typename Target>
int drawWrappedMarked(const Target& target, const int fontId, int y, const int maxWidth, const char* text,
                      const int spanStart, const int spanLength, const bool measureOnly, char* line,
                      const int lineBytes, char* scratch) {
  if (text == nullptr || *text == '\0') return y;

  const int lineHeight = target.getTextHeight(fontId);
  const int screenWidth = target.getScreenWidth();

  wrapText(
      text, maxWidth, line, lineBytes, [&](const char* candidate) { return target.getTextWidth(fontId, candidate); },
      [&](const WrappedLine& laid) {
        if (!measureOnly) {
          const int textWidth = target.getTextWidth(fontId, laid.text);
          const int left = (screenWidth - textWidth) / 2;
          target.drawText(fontId, left, y, laid.text, true);

          int from = 0;
          int to = 0;
          if (spanOnLine(laid, spanStart, spanLength, from, to)) {
            // Measured through the same font that drew the line, so the mark
            // starts under the glyph rather than under the byte: one line can
            // hold a three-byte full-width hanzi beside a one-byte letter.
            const int fromBytes = bytesForCodepoints(laid.text, from);
            const int toBytes = bytesForCodepoints(laid.text, to);
            std::memcpy(scratch, laid.text, static_cast<size_t>(fromBytes));
            scratch[fromBytes] = '\0';
            const int x0 = left + target.getTextWidth(fontId, scratch);
            std::memcpy(scratch, laid.text, static_cast<size_t>(toBytes));
            scratch[toBytes] = '\0';
            const int x1 = left + target.getTextWidth(fontId, scratch);
            // Two pixels clear of the baseline so descenders are not struck
            // through, and one pixel thick: this is a mark, not a rule.
            if (x1 > x0) target.fillRect(x0, y + lineHeight - 2, x1 - x0, 1, true);
          }
        }
        y += lineHeight;
      });
  return y;
}

}  // namespace study

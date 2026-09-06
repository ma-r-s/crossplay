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

// Is this character drawn full width and written without spaces? Those two
// go together, and together they are what lets a line break beside it.
//
// Hangul is deliberately absent. It is full width, but Korean is written with
// spaces between words, so the space rule already breaks it correctly and
// breaking between syllables would split words the space rule keeps whole.
// This mirrors scripts.WIDE in tools_local/study/scripts.py, which is where
// the same question is answered for the converter.
inline bool isWideScript(const uint32_t codepoint) {
  return (codepoint >= 0x3000 && codepoint <= 0x303F) ||   // CJK punctuation
         (codepoint >= 0x3040 && codepoint <= 0x30FF) ||   // kana
         (codepoint >= 0x31F0 && codepoint <= 0x31FF) ||   // katakana extensions
         (codepoint >= 0x2E80 && codepoint <= 0x2FDF) ||   // radicals
         (codepoint >= 0x3400 && codepoint <= 0x4DBF) ||   // Extension A
         (codepoint >= 0x4E00 && codepoint <= 0x9FFF) ||   // Unified
         (codepoint >= 0xF900 && codepoint <= 0xFAFF) ||   // compatibility
         (codepoint >= 0xFF00 && codepoint <= 0xFFEF);     // fullwidth forms
}

// Kinsoku shori, the Japanese and Chinese rule about what may sit at the edge
// of a line. Two halves, and they are enforced from opposite ends.
//
// A character that may not BEGIN a line is handled by keeping it on the line
// that ends: the wrap is greedy, so it is already looking at the character
// when it decides, and one character of overhang is what a typesetter does
// here. A character that may not END a line is handled by pushing it DOWN --
// the wrap has already placed it, so it is taken back off and reprocessed on
// the next line, which is the only way a greedy wrap can implement a rule
// about a decision it has already made.
inline bool isProhibitedLineStart(const uint32_t codepoint) {
  switch (codepoint) {
    case 0x3001:  // 、
    case 0x3002:  // 。
    case 0xFF0C:  // ，
    case 0xFF0E:  // ．
    case 0xFF1A:  // ：
    case 0xFF1B:  // ；
    case 0xFF01:  // ！
    case 0xFF1F:  // ？
    case 0x300D:  // 」
    case 0x300F:  // 』
    case 0xFF09:  // ）
    case 0x3009:  // 〉
    case 0x300B:  // 》
    case 0x3011:  // 】
    case 0xFF3D:  // ］
    case 0xFF5D:  // ｝
    case 0x30FC:  // ー  the long vowel mark
    case 0x3005:  // 々  the repeat mark
    case 0x309D:  // ゝ
    case 0x309E:  // ゞ
      return true;
    default:
      return false;
  }
}

// Characters that may not END a line: the opening halves of the bracket and
// quote pairs, and the currency marks that bind to the number after them.
// Leaving 「 alone at the end of a line and its content on the next is the
// mirror of the mistake isProhibitedLineStart exists to stop, and it is just
// as visible.
inline bool isProhibitedLineEnd(const uint32_t codepoint) {
  switch (codepoint) {
    case 0x300C:  // 「
    case 0x300E:  // 『
    case 0xFF08:  // （
    case 0x3008:  // 〈
    case 0x300A:  // 《
    case 0x3010:  // 【
    case 0x3014:  // 〔
    case 0xFF3B:  // ［
    case 0xFF5B:  // ｛
    case 0x2018:  // '
    case 0x201C:  // "
    case 0xFF04:  // ＄
    case 0xFFE5:  // ￥
    case 0xFF03:  // ＃
      return true;
    default:
      return false;
  }
}

// The last codepoint of a NUL-terminated run of `length` bytes, or 0.
// Scanning back to the lead byte is cheaper than walking the line forward,
// and the line is walked forward often enough already.
inline uint32_t lastCodepoint(const char* text, const int length) {
  if (text == nullptr || length <= 0) return 0;
  int start = length - 1;
  while (start > 0 && (static_cast<unsigned char>(text[start]) & 0xC0) == 0x80) --start;
  const char* p = text + start;
  return nextCodepoint(p);
}

// May a line break happen either side of this character? True for spaces and
// for the wide scripts, and for the newline, which is not a break the wrap
// may take but one it MUST -- fitsAsDrawn measures runs with this same
// predicate, so a newline that was not breakable here made two paragraphs
// look like one unbreakable run and condemned the card to the fallback face.
inline bool isBreakable(const uint32_t codepoint) {
  if (codepoint == ' ' || codepoint == '\n') return true;
  return isWideScript(codepoint);
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

// --- Japanese ruby ----------------------------------------------------------
//
// A reading printed above the text it reads. Anki writes it into a field as
// " 漢字[かんじ]"; the converter turns that into
//
//     RUBY_START base RUBY_SEP reading RUBY_END
//
// using three control characters, which is what lets the wrap below see a
// segment coming without scanning ahead at every glyph -- and what keeps it
// from colliding with a cloze card's "[...]", where reading a hole as a
// reading would print the answer directly above the gap hiding it.
//
// See tools_local/study/scripts.py, which is where the markers are written.
inline constexpr char kRubyStart = '\x1e';
inline constexpr char kRubySep = '\x1f';
inline constexpr char kRubyEnd = '\x1d';

// One piece of a line: text, and the reading above it when it has one.
//
// A plain run of text is a segment with no reading, so everything downstream
// -- measuring, drawing, the emphasis underline -- has one shape rather than
// two. That is why the ruby path did not turn into a second renderer beside
// the plain one.
struct RubySegment {
  const char* base = "";
  int baseBytes = 0;
  const char* ruby = nullptr;  // null when this is plain text
  int rubyBytes = 0;
  int codepoints = 0;  // of the BASE only: markers and readings do not count,
                       // because the emphasis spans the converter records are
                       // measured over the text a reader sees.
};

// Walk the segments of an encoded string. `fn(const RubySegment&)`.
//
// Malformed input -- a start with no separator, a separator with no end --
// is treated as plain text rather than refused. This is drawn on a card, and
// a sentence with a stray control byte in it should still be readable.
template <typename Fn>
void forEachRubySegment(const char* text, Fn fn) {
  if (text == nullptr) return;
  const char* p = text;
  const char* plainStart = text;

  const auto flushPlain = [&](const char* end) {
    if (end <= plainStart) return;
    RubySegment segment;
    segment.base = plainStart;
    segment.baseBytes = static_cast<int>(end - plainStart);
    for (const char* q = plainStart; q < end;) {
      if (nextCodepoint(q) == 0) break;
      ++segment.codepoints;
    }
    fn(segment);
  };

  while (*p != '\0') {
    if (*p != kRubyStart) {
      ++p;
      continue;
    }
    const char* sep = p + 1;
    while (*sep != '\0' && *sep != kRubySep && *sep != kRubyStart) ++sep;
    if (*sep != kRubySep) {
      ++p;
      continue;
    }
    const char* end = sep + 1;
    while (*end != '\0' && *end != kRubyEnd && *end != kRubyStart) ++end;
    if (*end != kRubyEnd) {
      ++p;
      continue;
    }

    flushPlain(p);
    RubySegment segment;
    segment.base = p + 1;
    segment.baseBytes = static_cast<int>(sep - (p + 1));
    segment.ruby = sep + 1;
    segment.rubyBytes = static_cast<int>(end - (sep + 1));
    for (const char* q = segment.base; q < sep;) {
      if (nextCodepoint(q) == 0) break;
      ++segment.codepoints;
    }
    fn(segment);
    p = end + 1;
    plainStart = p;
  }
  flushPlain(p);
}

inline bool hasRuby(const char* text) {
  bool found = false;
  forEachRubySegment(text, [&](const RubySegment& segment) {
    if (segment.ruby != nullptr) found = true;
  });
  return found;
}

// Copy one segment's base or reading out as a NUL-terminated string, because
// the renderer measures and draws C strings. Returns the length written.
inline int copyRun(const char* text, const int bytes, char* out, const int outBytes) {
  const int length = bytes < outBytes - 1 ? bytes : outBytes - 1;
  if (length > 0) std::memcpy(out, text, static_cast<size_t>(length));
  out[length > 0 ? length : 0] = '\0';
  return length > 0 ? length : 0;
}

// How wide one segment is: the wider of its base and its reading, because the
// narrower of the two is centred under or over the other.
template <typename MeasureBase, typename MeasureRuby>
int rubySegmentWidth(const RubySegment& segment, MeasureBase measureBase, MeasureRuby measureRuby, char* scratch,
                     const int scratchBytes) {
  copyRun(segment.base, segment.baseBytes, scratch, scratchBytes);
  const int baseWidth = measureBase(scratch);
  if (segment.ruby == nullptr) return baseWidth;
  copyRun(segment.ruby, segment.rubyBytes, scratch, scratchBytes);
  const int rubyWidth = measureRuby(scratch);
  return baseWidth > rubyWidth ? baseWidth : rubyWidth;
}

// The advance of a whole encoded string. This is the measurement the wrap
// uses, so a line of ruby wraps by what it will actually occupy rather than
// by the width of its base text alone.
template <typename MeasureBase, typename MeasureRuby>
int measureRubyText(const char* text, MeasureBase measureBase, MeasureRuby measureRuby, char* scratch,
                    const int scratchBytes) {
  int total = 0;
  forEachRubySegment(text, [&](const RubySegment& segment) {
    total += rubySegmentWidth(segment, measureBase, measureRuby, scratch, scratchBytes);
  });
  return total;
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
  // Where the unit most recently placed on this line began -- in the buffer,
  // in the source, and in codepoints. Trailing kinsoku has to take that unit
  // back off again, and a greedy wrap can only do that by remembering where
  // it put it.
  int lastUnitOffset = 0;
  const char* lastUnitSrc = nullptr;
  int lastUnitCp = 0;

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
    lastUnitOffset = 0;
    lastUnitSrc = nullptr;
    lastUnitCp = 0;
  };

  const char* p = text;
  while (*p != '\0') {
    // One break unit. Measured, not counted: a CJK glyph is three bytes and
    // full width, a Latin one is one byte and narrow.
    //
    // A ruby segment is ONE unit, whole. Breaking inside it would put a
    // reading over half a word on one line and the other half bare on the
    // next, which is not a line break Japanese typesetting has.
    const char* unitEnd = p;
    int unitCodepoints = -1;  // -1: count it below, the ordinary way
    if (*p == kRubyStart) {
      const char* end = p;
      while (*end != '\0' && *end != kRubyEnd) ++end;
      if (*end == kRubyEnd) {
        unitEnd = end + 1;
        unitCodepoints = 0;
        forEachRubySegment(p, [&](const RubySegment& segment) {
          if (unitCodepoints == 0) unitCodepoints = segment.codepoints;
        });
      }
    }
    if (unitEnd == p) {
      if (isBreakable(nextCodepoint(unitEnd))) {
        unitEnd = p + utf8Length(p);
      } else {
        while (*unitEnd != '\0') {
          const char* peek = unitEnd;
          if (*peek == kRubyStart) break;  // a segment starts its own unit
          if (isBreakable(nextCodepoint(peek))) break;
          unitEnd = peek;
        }
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
      // Kinsoku: a full stop or a closing bracket may not open the next line,
      // so it stays on this one even though it does not fit. One character of
      // overhang is what every Japanese typesetter does here, and it is far
      // less wrong than a line beginning with 。
      const char* peek = p;
      if (*p != kRubyStart && measure(line) > maxWidth && isProhibitedLineStart(nextCodepoint(peek))) {
        lineLength += unitBytes;
        cpIndex += 1;
        p = unitEnd;
        continue;
      }
      if (measure(line) > maxWidth) {
        // Trailing kinsoku: this line is about to end, and it must not end on
        // an opening bracket. Take that unit back off and let the loop place
        // it at the head of the next line instead -- the only way a greedy
        // wrap can act on a decision it has already made.
        //
        // Only when something would be left behind. A line that is nothing
        // BUT the bracket has nowhere better to put it, and taking a unit off
        // an empty line would not terminate.
        if (lastUnitSrc != nullptr && lastUnitOffset > 0 &&
            isProhibitedLineEnd(lastCodepoint(line + lastUnitOffset, lineLength - lastUnitOffset))) {
          lineLength = lastUnitOffset;
          cpIndex -= lastUnitCp;
          p = lastUnitSrc;
          flush();
          continue;
        }
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

    int unitCp = unitCodepoints;
    if (unitCp < 0) {
      unitCp = 0;
      for (const char* q = p; q < unitEnd;) {
        if (nextCodepoint(q) == 0) break;
        ++unitCp;
      }
    }
    lastUnitOffset = lineLength;
    lastUnitSrc = p;
    lastUnitCp = unitCp;
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
                      const int lineBytes, char* scratch, const int rubyFontId = 0) {
  if (text == nullptr || *text == '\0') return y;

  const int lineHeight = target.getTextHeight(fontId);
  const int screenWidth = target.getScreenWidth();
  // Without a ruby face there is nowhere to put a reading, so readings are
  // measured as nothing and the base draws alone -- which is what a deck
  // whose fonts predate the ruby cut should do, rather than refusing to draw.
  const int rubyHeight = rubyFontId != 0 ? target.getTextHeight(rubyFontId) : 0;

  const auto measureBase = [&](const char* run) { return target.getTextWidth(fontId, run); };
  const auto measureRuby = [&](const char* run) {
    return rubyFontId != 0 ? target.getTextWidth(rubyFontId, run) : 0;
  };
  const auto measureLine = [&](const char* candidate) {
    return measureRubyText(candidate, measureBase, measureRuby, scratch, lineBytes);
  };

  wrapText(text, maxWidth, line, lineBytes, measureLine, [&](const WrappedLine& laid) {
    // A line carrying a reading is taller by the height of that reading, and
    // only that line: a card whose first sentence has furigana and whose
    // second does not should not be double-spaced throughout.
    const int above = (rubyFontId != 0 && hasRuby(laid.text)) ? rubyHeight : 0;
    if (!measureOnly) {
      const int totalWidth = measureLine(laid.text);
      int x = (screenWidth - totalWidth) / 2;
      int cp = laid.startCodepoint;

      forEachRubySegment(laid.text, [&](const RubySegment& segment) {
        const int width = rubySegmentWidth(segment, measureBase, measureRuby, scratch, lineBytes);

        // The base, centred under its reading when the reading is wider.
        copyRun(segment.base, segment.baseBytes, scratch, lineBytes);
        const int baseWidth = target.getTextWidth(fontId, scratch);
        const int baseX = x + (width - baseWidth) / 2;
        target.drawText(fontId, baseX, y + above, scratch, true);

        // The emphasis span, underlined. Measured inside THIS segment, which
        // is what makes one code path serve a plain line and a ruby one: a
        // plain line is a single segment and the arithmetic is the same.
        int from = 0;
        int to = 0;
        WrappedLine piece;
        piece.startCodepoint = cp;
        piece.codepoints = segment.codepoints;
        if (spanOnLine(piece, spanStart, spanLength, from, to)) {
          const int fromBytes = bytesForCodepoints(scratch, from);
          const int toBytes = bytesForCodepoints(scratch, to);
          // scratch still holds the base; truncate in place to measure the
          // prefix, longest first so the shorter cut does not destroy it.
          const char saved = scratch[toBytes];
          scratch[toBytes] = '\0';
          const int x1 = baseX + target.getTextWidth(fontId, scratch);
          scratch[toBytes] = saved;
          scratch[fromBytes] = '\0';
          const int x0 = baseX + target.getTextWidth(fontId, scratch);
          // Two pixels clear of the baseline so descenders are not struck
          // through, and one pixel thick: this is a mark, not a rule.
          if (x1 > x0) target.fillRect(x0, y + above + lineHeight - 2, x1 - x0, 1, true);
          copyRun(segment.base, segment.baseBytes, scratch, lineBytes);
        }
        // The reading, centred over its base.
        if (segment.ruby != nullptr && rubyFontId != 0) {
          copyRun(segment.ruby, segment.rubyBytes, scratch, lineBytes);
          const int rubyWidth = target.getTextWidth(rubyFontId, scratch);
          target.drawText(rubyFontId, x + (width - rubyWidth) / 2, y, scratch, true);
        }

        x += width;
        cp += segment.codepoints;
      });
    }
    y += lineHeight + above;
  });
  return y;
}

// Would this font draw `text` as the wrap will actually lay it out, with
// nothing missing and no run left overhanging the screen?
//
// Two ways a face fails a card, both ending in one you cannot read. Nothing
// painted at all -- stale or mis-built fonts, or a Latin-only face handed a
// CJK headword -- shows as a total measured width of zero. And a single
// unbreakable run wider than the screen: the wrap breaks on spaces and beside
// wide-script characters, so "capricious" at headword size has nowhere to
// break and hangs off both edges. Measured the same way the wrap breaks
// lines, so the two cannot disagree.
//
// `measure(fontId, text)` is the same pixel-width function drawWrappedMarked
// takes. `buffer`/`bufferBytes` is the caller's scratch, sized like the wrap's
// own line buffer since the runs being measured here are the same ones.
//
// A wide-script character -- kana, a hanzi, CJK punctuation -- is breakable on
// its own, which is what makes it a run of exactly one rather than part of a
// longer word. It still has to be MEASURED as that one-glyph run: an earlier
// version flushed the run built so far and moved on without ever copying the
// wide character into the buffer, so a string that is nothing but wide
// characters -- a bare kana or kanji headword, the ordinary case for every
// Japanese and Chinese deck -- left the buffer permanently empty, nothing was
// ever weighed, and a font that drew the card perfectly was rejected as
// drawing nothing. Every such headword then fell back to the built-in serif,
// which has none of them, and drew as a literal question mark.
template <typename Measure>
bool fitsAsDrawn(const int fontId, const char* text, const int maxWidth, Measure measure, char* buffer,
                 const int bufferBytes) {
  if (text == nullptr) return false;
  int runLength = 0;
  int painted = 0;
  bool fits = true;

  const auto runFits = [&]() {
    if (runLength == 0) return true;
    buffer[runLength] = '\0';
    runLength = 0;
    const int width = measure(fontId, buffer);
    painted += width;
    return width <= maxWidth;
  };

  forEachRubySegment(text, [&](const RubySegment& segment) {
    if (!fits) return;
    const char* const end = segment.base + segment.baseBytes;
    for (const char* p = segment.base; p < end;) {
      const char* at = p;
      const uint32_t codepoint = nextCodepoint(p);
      if (codepoint == 0) break;
      const int bytes = static_cast<int>(p - at);
      if (isBreakable(codepoint)) {
        if (!runFits()) {
          fits = false;
          return;
        }
        if (bytes < bufferBytes - 1) {
          for (int i = 0; i < bytes; ++i) buffer[runLength++] = at[i];
        }
        if (!runFits()) {
          fits = false;
          return;
        }
        continue;
      }
      if (runLength + bytes < bufferBytes) {
        for (int i = 0; i < bytes; ++i) buffer[runLength++] = at[i];
      }
    }
    // A ruby segment ends a run: the reading sits above this base, and the
    // next base starts its own run rather than joining this one.
    if (segment.ruby != nullptr && !runFits()) fits = false;
  });

  if (!fits) return false;
  if (!runFits()) return false;
  return painted > 0;
}

}  // namespace study

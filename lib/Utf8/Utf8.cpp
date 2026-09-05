#include "Utf8.h"

#include "Utf8ComposeTable.h"

namespace {
// Look up the canonical composition of (base + combining mark), or 0 if none.
uint32_t utf8ComposePair(const uint32_t base, const uint32_t mark) {
  if (base > 0xFFFF || mark > 0xFFFF) return 0;
  int lo = 0;
  int hi = kUtf8ComposeTableSize - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    const Utf8ComposeEntry& e = kUtf8ComposeTable[mid];
    if (e.base < base || (e.base == base && e.mark < mark)) {
      lo = mid + 1;
    } else if (e.base > base || (e.base == base && e.mark > mark)) {
      hi = mid - 1;
    } else {
      return e.composed;
    }
  }
  return 0;
}
}  // namespace

std::string utf8ComposeNfc(const std::string& in) {
  // Fast path: NFC composition can only change text that contains a combining
  // diacritical mark U+0300-036F (UTF-8 lead byte 0xCC or 0xCD). Plain ASCII and
  // already-precomposed (NFC) text -- the vast majority of words -- have none, so
  // return them untouched without walking codepoints or allocating. A 0xCD that is
  // actually a non-combining codepoint just falls through to the full pass below.
  bool maybeHasMarks = false;
  for (const unsigned char c : in) {
    if (c == 0xCC || c == 0xCD) {
      maybeHasMarks = true;
      break;
    }
  }
  if (!maybeHasMarks) return in;

  std::string out;
  out.reserve(in.size());
  const unsigned char* p = reinterpret_cast<const unsigned char*>(in.c_str());
  uint32_t base = 0;
  bool haveBase = false;
  while (*p) {
    const uint32_t cp = utf8NextCodepoint(&p);
    if (cp == 0) break;
    if (utf8IsCombiningMark(cp)) {
      const uint32_t composed = haveBase ? utf8ComposePair(base, cp) : 0;
      if (composed) {
        base = composed;  // keep accumulating further marks onto the composed char
        continue;
      }
      // No composition: flush the pending base, then emit the mark unchanged.
      if (haveBase) {
        utf8AppendCodepoint(base, out);
        haveBase = false;
      }
      utf8AppendCodepoint(cp, out);
    } else {
      if (haveBase) utf8AppendCodepoint(base, out);
      base = cp;
      haveBase = true;
    }
  }
  if (haveBase) utf8AppendCodepoint(base, out);
  return out;
}

int utf8CodepointLen(const unsigned char c) {
  if (c < 0x80) return 1;          // 0xxxxxxx
  if ((c >> 5) == 0x6) return 2;   // 110xxxxx
  if ((c >> 4) == 0xE) return 3;   // 1110xxxx
  if ((c >> 3) == 0x1E) return 4;  // 11110xxx
  return 1;                        // fallback for invalid
}

uint32_t utf8NextCodepoint(const unsigned char** string) {
  if (**string == 0) {
    return 0;
  }

  const unsigned char lead = **string;
  const int bytes = utf8CodepointLen(lead);
  const uint8_t* chr = *string;

  // Invalid lead byte (stray continuation byte 0x80-0xBF, or 0xFE/0xFF)
  if (bytes == 1 && lead >= 0x80) {
    (*string)++;
    return REPLACEMENT_GLYPH;
  }

  if (bytes == 1) {
    (*string)++;
    return chr[0];
  }

  // Validate continuation bytes before consuming them
  for (int i = 1; i < bytes; i++) {
    if ((chr[i] & 0xC0) != 0x80) {
      // Missing or invalid continuation byte — skip all bytes consumed so far
      *string += i;
      return REPLACEMENT_GLYPH;
    }
  }

  uint32_t cp = chr[0] & ((1 << (7 - bytes)) - 1);  // mask header bits

  for (int i = 1; i < bytes; i++) {
    cp = (cp << 6) | (chr[i] & 0x3F);
  }

  // Reject overlong encodings, surrogates, and out-of-range values
  const bool overlong = (bytes == 2 && cp < 0x80) || (bytes == 3 && cp < 0x800) || (bytes == 4 && cp < 0x10000);
  const bool surrogate = (cp >= 0xD800 && cp <= 0xDFFF);
  if (overlong || surrogate || cp > 0x10FFFF) {
    (*string)++;
    return REPLACEMENT_GLYPH;
  }

  *string += bytes;

  return cp;
}

void utf8AppendCodepoint(uint32_t cp, std::string& out) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

int utf8SafeTruncateBuffer(const char* buf, int len) {
  if (len <= 0) return 0;

  // Walk back past continuation bytes (10xxxxxx) to find the lead byte
  int leadPos = len - 1;
  while (leadPos > 0 && (static_cast<uint8_t>(buf[leadPos]) & 0xC0) == 0x80) {
    leadPos--;
  }

  // Determine expected length of the sequence starting at leadPos
  int expectedLen = utf8CodepointLen(static_cast<unsigned char>(buf[leadPos]));
  int actualLen = len - leadPos;

  if (actualLen < expectedLen && leadPos > 0) {
    // Incomplete UTF-8 sequence at the end — exclude it
    return leadPos;
  }
  return len;
}

size_t utf8RemoveLastChar(std::string& str) {
  if (str.empty()) return 0;
  size_t pos = str.size() - 1;
  while (pos > 0 && (static_cast<unsigned char>(str[pos]) & 0xC0) == 0x80) {
    --pos;
  }
  str.resize(pos);
  return pos;
}

// Truncate string by removing N UTF-8 characters from the end
void utf8TruncateChars(std::string& str, const size_t numChars) {
  for (size_t i = 0; i < numChars && !str.empty(); ++i) {
    utf8RemoveLastChar(str);
  }
}

namespace {
// Spelled out rather than std::isspace: isspace takes an int whose value must
// be representable as unsigned char, its answer depends on the locale, and
// neither property is worth inheriting in a function that runs over bytes from
// the network.
bool isAsciiSpace(const unsigned char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}
}  // namespace

std::string utf8CollapseWhitespace(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  // A run of whitespace becomes one space only once something follows it, which
  // is what trims both ends without a second pass: a leading run never sets
  // this (out is empty), and a trailing run is simply never flushed.
  bool pendingSpace = false;
  for (const char ch : in) {
    if (isAsciiSpace(static_cast<unsigned char>(ch))) {
      pendingSpace = !out.empty();
      continue;
    }
    if (pendingSpace) out.push_back(' ');
    pendingSpace = false;
    out.push_back(ch);
  }
  return out;
}

namespace {

// Sorted by codepoint; binary-searched. Every entry is at or above U+00A0, so
// ASCII cannot be touched by construction -- which is what makes the fast path
// below a shortcut rather than a special case.
//
// An entry earns its place by answering yes to both: does at least one cut that
// carries external text AND reaches past ASCII lack the glyph (measured in
// host-tests/typefold/), and does the ASCII spelling mean the same thing?
//
// The vulgar fractions were in this table and are not any more, which is the
// clearest statement of that first test. The reading cut draws U+00BD in 24px
// and "1/2" in 39px, so folding it made body text wider and worse to rescue the
// tile cut, which is the exact trade this change declines for accented letters
// one paragraph down. A row is for a character the cut that carries the PROSE
// cannot draw. Letters fail the second test and
// are absent on purpose. So are the bidi controls (U+200E, U+200F,
// U+202A..U+202E, U+2066..U+2069): they carry no ink either, but MiniBidi reads
// them during shaping and deleting them here would change the order glyphs come
// out in, which is a different bug from the one this fixes.
constexpr Utf8TypographyFold kFolds[] = {
    {0x00A0, " "},     // NO-BREAK SPACE
    {0x00AD, ""},      // SOFT HYPHEN. Removed rather than turned into a
                       // hyphen, and this is the one row that is not just
                       // closing a hole: the reading cuts DO carry a glyph for
                       // it, 8 pixels of ink at 14px, so a soft hyphen inside
                       // a word currently draws a hyphen there. Nothing in
                       // this firmware breaks lines on one, so it is noise
                       // either way -- measured in host-tests/typefold/.
    {0x02BC, "'"},     // MODIFIER LETTER APOSTROPHE
    {0x2000, " "},     // EN QUAD
    {0x2001, " "},     // EM QUAD
    {0x2002, " "},     // EN SPACE
    {0x2003, " "},     // EM SPACE
    {0x2004, " "},     // THREE-PER-EM SPACE
    {0x2005, " "},     // FOUR-PER-EM SPACE
    {0x2006, " "},     // SIX-PER-EM SPACE
    {0x2007, " "},     // FIGURE SPACE
    {0x2008, " "},     // PUNCTUATION SPACE
    {0x2009, " "},     // THIN SPACE
    {0x200A, " "},     // HAIR SPACE
    {0x200B, ""},      // ZERO WIDTH SPACE
    {0x200C, ""},      // ZERO WIDTH NON-JOINER
    {0x200D, ""},      // ZERO WIDTH JOINER
    {0x2010, "-"},     // HYPHEN
    {0x2011, "-"},     // NON-BREAKING HYPHEN
    {0x2012, "-"},     // FIGURE DASH
    {0x2013, "-"},     // EN DASH
    {0x2014, "--"},    // EM DASH
    {0x2015, "--"},    // HORIZONTAL BAR
    {0x2018, "'"},     // LEFT SINGLE QUOTATION MARK
    {0x2019, "'"},     // RIGHT SINGLE QUOTATION MARK -- the apostrophe in prose
    {0x201A, "'"},     // SINGLE LOW-9 QUOTATION MARK
    {0x201B, "'"},     // SINGLE HIGH-REVERSED-9 QUOTATION MARK
    {0x201C, "\""},    // LEFT DOUBLE QUOTATION MARK
    {0x201D, "\""},    // RIGHT DOUBLE QUOTATION MARK
    {0x201E, "\""},    // DOUBLE LOW-9 QUOTATION MARK
    {0x201F, "\""},    // DOUBLE HIGH-REVERSED-9 QUOTATION MARK
    {0x2022, "*"},     // BULLET
    {0x2023, "*"},     // TRIANGULAR BULLET
    {0x2026, "..."},   // HORIZONTAL ELLIPSIS
    {0x2028, " "},     // LINE SEPARATOR
    {0x2029, " "},     // PARAGRAPH SEPARATOR
    {0x202F, " "},     // NARROW NO-BREAK SPACE
    {0x2032, "'"},     // PRIME
    {0x2033, "\""},    // DOUBLE PRIME
    {0x2039, "<"},     // SINGLE LEFT-POINTING ANGLE QUOTATION MARK
    {0x203A, ">"},     // SINGLE RIGHT-POINTING ANGLE QUOTATION MARK
    {0x2043, "-"},     // HYPHEN BULLET
    {0x2044, "/"},     // FRACTION SLASH
    {0x205F, " "},     // MEDIUM MATHEMATICAL SPACE
    {0x2060, ""},      // WORD JOINER
    {0x20AC, "EUR"},   // EURO SIGN: present in the tile cut, absent from the
                       // reading cut, and the only hole a census of 3665 live
                       // Hacker News strings found that the rest of this table
                       // did not already close
    {0x2122, "(TM)"},  // TRADE MARK SIGN
    {0x2190, "<-"},    // LEFTWARDS ARROW
    {0x2192, "->"},    // RIGHTWARDS ARROW
    {0x2194, "<->"},   // LEFT RIGHT ARROW
    {0x21D2, "=>"},    // RIGHTWARDS DOUBLE ARROW
    {0x2212, "-"},     // MINUS SIGN
    {0x2215, "/"},     // DIVISION SLASH
    {0x2248, "~="},    // ALMOST EQUAL TO
    {0x2260, "!="},    // NOT EQUAL TO
    {0x2264, "<="},    // LESS-THAN OR EQUAL TO
    {0x2265, ">="},    // GREATER-THAN OR EQUAL TO
    {0x25AA, "*"},     // BLACK SMALL SQUARE
    {0x25CF, "*"},     // BLACK CIRCLE
    {0x25E6, "*"},     // WHITE BULLET
    {0x3000, " "},     // IDEOGRAPHIC SPACE
    {0xFB00, "ff"},    // the f-ligatures a PDF extractor emits in article text
    {0xFB01, "fi"},
    {0xFB02, "fl"},
    {0xFB03, "ffi"},
    {0xFB04, "ffl"},
    // U+FE00..U+FE0F, the variation selectors, are a contiguous range handled in
    // foldFor() rather than sixteen rows saying the same thing. They are not in
    // this table at all, and that matters: the table is what the tests walk.
    {0xFEFF, ""},  // ZERO WIDTH NO-BREAK SPACE / byte order mark
};

const char* foldFor(const uint32_t cp) {
  // Stated here as well as enforced by the table, because this is where the
  // next special case will be written. ASCII never folds: the caller does not
  // even decode it (a byte below 0x80 is copied straight through) and a whole
  // string of it returns without allocating, so a row or a branch added below
  // for an ASCII codepoint would be dead code that reads like a feature.
  if (cp < 0xA0) return nullptr;
  // The variation selectors are a contiguous run of sixteen; spelling all of
  // them into the table would be sixteen lines saying the same thing.
  if (cp >= 0xFE00 && cp <= 0xFE0F) return "";
  int lo = 0;
  int hi = static_cast<int>(sizeof(kFolds) / sizeof(kFolds[0])) - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    if (kFolds[mid].cp < cp) {
      lo = mid + 1;
    } else if (kFolds[mid].cp > cp) {
      hi = mid - 1;
    } else {
      return kFolds[mid].ascii;
    }
  }
  return nullptr;
}

}  // namespace

const Utf8TypographyFold* utf8TypographyFolds(size_t* count) {
  if (count != nullptr) *count = sizeof(kFolds) / sizeof(kFolds[0]);
  return kFolds;
}

std::string utf8FoldTypography(const std::string& in) {
  // Nothing in the table is below U+00A0, so a string of pure ASCII cannot
  // change. Returning it untouched is the proof as well as the shortcut.
  bool hasHighByte = false;
  for (const unsigned char c : in) {
    if (c >= 0x80) {
      hasHighByte = true;
      break;
    }
  }
  if (!hasHighByte) return in;

  std::string out;
  out.reserve(in.size());
  // Bounded by size(), not by the terminator. A NUL inside the string is a byte
  // like any other and everything after it has to survive: an article read off
  // the card comes back from readWholeFile() as raw bytes, and a `while (*p)`
  // walk returned only what preceded the first NUL -- on the path that does
  // work, too, since the pure-ASCII shortcut above returns the whole string.
  // utf8CollapseWhitespace beside this is a range-for for the same reason.
  const unsigned char* const begin = reinterpret_cast<const unsigned char*>(in.c_str());
  const unsigned char* const end = begin + in.size();
  const unsigned char* p = begin;
  while (p < end) {
    if (*p < 0x80) {
      out.push_back(static_cast<char>(*p++));
      continue;
    }
    const unsigned char* const start = p;
    // The decoder treats a NUL exactly as it treats any other invalid
    // continuation byte, advancing only by what it consumed, so it cannot walk
    // past `end`: the byte at `end` is c_str()'s terminator.
    const uint32_t cp = utf8NextCodepoint(&p);
    if (p == start) {  // unreachable today; a decoder that stalled would spin here
      out.push_back(static_cast<char>(*p++));
      continue;
    }
    const char* const ascii = foldFor(cp);
    if (ascii != nullptr) {
      out.append(ascii);
      continue;
    }
    // Not folded: copy the ORIGINAL bytes rather than re-encoding the decoded
    // codepoint. utf8NextCodepoint answers REPLACEMENT_GLYPH for a malformed
    // sequence, and re-encoding would turn text we merely could not read into
    // text we changed.
    out.append(reinterpret_cast<const char*>(start), static_cast<size_t>(p - start));
  }
  return out;
}

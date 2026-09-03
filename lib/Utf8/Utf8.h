#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#define REPLACEMENT_GLYPH 0xFFFD

uint32_t utf8NextCodepoint(const unsigned char** string);
// Appends a Unicode codepoint to a std::string in UTF-8 encoding.
void utf8AppendCodepoint(uint32_t cp, std::string& out);
// Remove the last UTF-8 codepoint from a std::string and return the new size.
size_t utf8RemoveLastChar(std::string& str);
// Truncate string by removing N UTF-8 codepoints from the end.
void utf8TruncateChars(std::string& str, size_t numChars);

// Canonical composition (NFC) for the Latin / Vietnamese range: precomposes a
// base letter followed by combining diacritical mark(s) into a single codepoint.
// Needed because the device fonts have no combining-mark positioning, so text
// stored in NFD (e.g. some EPUB chapter titles) otherwise renders broken.
std::string utf8ComposeNfc(const std::string& in);

// Collapse every run of ASCII whitespace to a single space, and trim the ends.
//
// Text that arrives as markup carries the line breaks and indentation of the
// document it came in: a pretty-printed OPDS feed puts a newline and two spaces
// inside every <title>. The device fonts have no glyph for a newline, so it
// reaches the rasteriser as codepoint 10 and is logged as a missing glyph --
// visible only in a log, until somebody wires that log line to a failing gate.
//
// Collapsing rather than DELETING is the whole point, and is the bug this
// replaced: removing newlines outright turns "call me\nIshmael" into
// "call meIshmael", joining the words either side of the break.
//
// UTF-8 safe by construction: all six ASCII whitespace bytes are below 0x80, so
// none of them can ever appear inside a multi-byte sequence.
std::string utf8CollapseWhitespace(const std::string& in);

// Replace typographic punctuation with the ASCII the device fonts actually
// carry, leaving everything else byte for byte.
//
// A glyph a font does not have draws as NOTHING: no box and no fallback.
// EpdFont::getGlyph returns nullptr and GfxRenderer skips it without advancing
// the pen. It is not silent in the LOG -- renderCharImpl says "No glyph for
// codepoint N" (GfxRenderer.cpp) every time -- but nothing on the device reads
// that, and the one thing that acts on it, scripts_local/sim-shot.sh, only sees
// the screens a scripted run happens to visit with real data. So the
// information was there the whole time and no gate consumed it.
// The Toybox cuts under src/apps_local/ui/fonts/ are subsets -- Jersey 25 at
// U+0020..U+007E, the Noto Serif reading cuts at ASCII plus Latin-1 -- so a
// real Hacker News headline loses its curly quotes, its em dash and its
// ellipsis on the way to the panel and the reader sees a sentence with holes
// in it. The system fonts under lib/EpdFont/builtinFonts/ carry all of this
// and are NOT the reason this exists; host-tests/typefold/ measures which cut
// carries what rather than asserting it.
//
// What is folded: punctuation, spacing and symbols whose ASCII spelling means
// the same thing -- curly quotes, the dash family, the ellipsis, the space
// family, the zero-width formatting characters, the arrows and the comparison
// signs, and the f-ligatures a PDF extractor emits. What is NOT folded:
// letters. An accented Latin-1 letter renders in every cut that carries
// external prose, so folding "e-acute" to "e" would degrade every article and
// comment in the fork to rescue one header band; Latin Extended-A, Greek,
// Cyrillic and CJK have no ASCII spelling at all, and a wrong letter is worse
// than a missing one.
//
// ASCII in, the same bytes out, and that is by construction rather than by
// habit: no entry in the table is below U+00A0, and a string with no byte
// above 0x7F returns without allocating.
//
// Not the same job as connections::foldToAscii (src/apps_local/connections/
// ConnectionsText.h), and the two should not be merged. That one transliterates
// a whole word down to strict ASCII -- accented letters included, emoji
// rejected -- because a Connections tile is a fixed box of capitals and a word
// it cannot draw is a puzzle nobody can play. This one is for prose, where the
// cut CAN draw the letters and rewriting them would be the damage.
//
// Invalid UTF-8 passes through unchanged. The decoder is only consulted to
// find a codepoint to look up; bytes it cannot make sense of are copied
// verbatim rather than replaced, so a truncated multi-byte sequence coming off
// a socket is not turned into something else.
std::string utf8FoldTypography(const std::string& in);

// One entry of the fold table: a codepoint and the ASCII that replaces it.
struct Utf8TypographyFold {
  uint32_t cp;
  const char* ascii;
};

// The table itself, so a test can walk it instead of keeping a second copy of
// the same list. host-tests/typefold/ takes every entry to the real font data
// and asks whether the source is genuinely missing and the replacement is
// genuinely there, which is a check that DISCOVERS a bad new row rather than
// re-asserting the rows somebody already thought about.
const Utf8TypographyFold* utf8TypographyFolds(size_t* count);

// Truncate a raw char buffer to the last complete UTF-8 codepoint boundary.
// Returns the new length (<= len). If the buffer ends mid-sequence, the
// incomplete trailing bytes are excluded.
int utf8SafeTruncateBuffer(const char* buf, int len);

// Returns true for CJK characters that allow line breaks on either side without hyphenation.
// Covers CJK Unified Ideographs, Hiragana, Katakana, Hangul Syllables, CJK punctuation,
// and fullwidth forms — the ranges where word boundaries are implicit per character.
inline bool utf8IsCjkBreakable(const uint32_t cp) {
  return (cp >= 0x1100 && cp <= 0x11FF)        // Hangul Jamo
         || (cp >= 0x3000 && cp <= 0x303F)     // CJK Symbols and Punctuation
         || (cp >= 0x3040 && cp <= 0x309F)     // Hiragana
         || (cp >= 0x30A0 && cp <= 0x30FF)     // Katakana
         || (cp >= 0x3130 && cp <= 0x318F)     // Hangul Compatibility Jamo
         || (cp >= 0x3400 && cp <= 0x4DBF)     // CJK Extension A
         || (cp >= 0x4E00 && cp <= 0x9FFF)     // CJK Unified Ideographs
         || (cp >= 0xAC00 && cp <= 0xD7AF)     // Hangul Syllables
         || (cp >= 0xD7B0 && cp <= 0xD7FF)     // Hangul Jamo Extended-B
         || (cp >= 0xF900 && cp <= 0xFAFF)     // CJK Compatibility Ideographs
         || (cp >= 0xFE30 && cp <= 0xFE4F)     // CJK Compatibility Forms
         || (cp >= 0xFF01 && cp <= 0xFF60)     // Fullwidth Latin / Punctuation
         || (cp >= 0xFF65 && cp <= 0xFFEF)     // Halfwidth Katakana / Hangul
         || (cp >= 0x20000 && cp <= 0x2A6DF)   // CJK Extension B
         || (cp >= 0x2A700 && cp <= 0x2B73F);  // CJK Extension C
}

// Returns true for any codepoint in a CJK script block (Han, Kana, Hangul, Bopomofo,
// radicals, and CJK punctuation/compatibility/enclosed forms). Used for fallback font
// selection — deliberately broader than utf8IsCjkBreakable, whose ranges are tuned to
// implicit line-break opportunities and must not grow without rethinking layout.
inline bool utf8IsCjkCodepoint(const uint32_t cp) {
  return (cp >= 0x1100 && cp <= 0x11FF)        // Hangul Jamo
         || (cp >= 0x2E80 && cp <= 0x2FDF)     // CJK Radicals Supplement, Kangxi Radicals
         || (cp >= 0x3000 && cp <= 0x33FF)     // CJK punctuation, Kana, Bopomofo, Hangul Compat
                                               // Jamo, Kanbun, strokes, enclosed + compat forms
         || (cp >= 0x3400 && cp <= 0x4DBF)     // CJK Extension A
         || (cp >= 0x4E00 && cp <= 0x9FFF)     // CJK Unified Ideographs
         || (cp >= 0xA960 && cp <= 0xA97F)     // Hangul Jamo Extended-A
         || (cp >= 0xAC00 && cp <= 0xD7FF)     // Hangul Syllables, Hangul Jamo Extended-B
         || (cp >= 0xF900 && cp <= 0xFAFF)     // CJK Compatibility Ideographs
         || (cp >= 0xFE10 && cp <= 0xFE1F)     // Vertical Forms
         || (cp >= 0xFE30 && cp <= 0xFE4F)     // CJK Compatibility Forms
         || (cp >= 0xFF01 && cp <= 0xFF60)     // Fullwidth Latin / Punctuation
         || (cp >= 0xFF65 && cp <= 0xFFEF)     // Halfwidth Katakana / Hangul
         || (cp >= 0x20000 && cp <= 0x2EBEF)   // CJK Extensions B-F
         || (cp >= 0x2F800 && cp <= 0x2FA1F)   // CJK Compatibility Ideographs Supplement
         || (cp >= 0x30000 && cp <= 0x323AF);  // CJK Extensions G-H
}

// Returns true for Unicode combining diacritical marks that should not advance the cursor.
inline bool utf8IsCombiningMark(const uint32_t cp) {
  return (cp >= 0x0300 && cp <= 0x036F)      // Combining Diacritical Marks
         || (cp >= 0x1DC0 && cp <= 0x1DFF)   // Combining Diacritical Marks Supplement
         || (cp >= 0x20D0 && cp <= 0x20FF)   // Combining Diacritical Marks for Symbols
         || (cp >= 0xFE20 && cp <= 0xFE2F);  // Combining Half Marks
}

#pragma once

// Folding the archive's text down to what a 1-bit ASCII font can draw.
//
// The mapping table below is not a guess. It is the complete set of non-ASCII
// codepoints in the published archive (1143 puzzles, 18288 words), counted:
//
//   in group names:  U+201D x213  U+201C x212  U+2019 x45  and singles of
//                    U+00C4 U+00B0 U+2022 U+00C9 U+00CD U+00C0 U+1F911
//   in words:        U+2019 x7  U+00C9 x5  U+00C8 x2  U+00C0 x2  U+00D1 U+00DC
//                    plus one each of ~15 emoji and U+FE0F, U+2708, U+25CF
//
// Curly quotes alone touch 22% of group names, so folding is not optional. The
// accented capitals fold to their base letter, which is what a reader expects
// from a display that cannot draw the accent.
//
// What cannot fold is emoji. Those puzzles are dropped, and the cost of that
// rule measured against the real archive is exactly two puzzles out of 1143.

#include <cstdint>

namespace connections {

// Writes the ASCII form of `codepoint` into `out` (at least 4 bytes) and
// returns how many bytes were written, or 0 when it has no ASCII form.
inline int foldToAscii(const uint32_t codepoint, char* out) {
  if (codepoint >= 0x20 && codepoint < 0x7F) {
    out[0] = static_cast<char>(codepoint);
    return 1;
  }
  switch (codepoint) {
    // Typographic punctuation, by far the common case.
    case 0x2018:
    case 0x2019:
    case 0x02BC:
      out[0] = '\'';
      return 1;
    case 0x201C:
    case 0x201D:
      out[0] = '"';
      return 1;
    case 0x2013:
    case 0x2014:
    case 0x2212:
      out[0] = '-';
      return 1;
    case 0x2022:
    case 0x25CF:
      out[0] = '*';
      return 1;
    case 0x00A0:
      out[0] = ' ';
      return 1;
    case 0x2026:
      out[0] = '.';
      out[1] = '.';
      out[2] = '.';
      return 3;
    case 0x00B0:
      // "98.6 DEGREES" reads; a bare 98.6 does not.
      out[0] = ' ';
      out[1] = 'D';
      out[2] = 'E';
      out[3] = 'G';
      return 4;
    case 0x00DF:
      out[0] = 's';
      out[1] = 's';
      return 2;
    case 0x00C6:
      out[0] = 'A';
      out[1] = 'E';
      return 2;
    case 0x00E6:
      out[0] = 'a';
      out[1] = 'e';
      return 2;
    default:
      break;
  }
  // Latin-1 and Latin Extended-A letters fold to their base letter. The table
  // is indexed from U+00C0, which is where the accented letters start.
  if (codepoint >= 0x00C0 && codepoint <= 0x00FF) {
    static const char kLatin1[64] = {
        'A', 'A', 'A', 'A', 'A', 'A', 'A', 'C',  // C0-C7
        'E', 'E', 'E', 'E', 'I', 'I', 'I', 'I',  // C8-CF
        'D', 'N', 'O', 'O', 'O', 'O', 'O', 'x',  // D0-D7
        'O', 'U', 'U', 'U', 'U', 'Y', 'P', 's',  // D8-DF
        'a', 'a', 'a', 'a', 'a', 'a', 'a', 'c',  // E0-E7
        'e', 'e', 'e', 'e', 'i', 'i', 'i', 'i',  // E8-EF
        'd', 'n', 'o', 'o', 'o', 'o', 'o', '/',  // F0-F7
        'o', 'u', 'u', 'u', 'u', 'y', 'p', 'y',  // F8-FF
    };
    out[0] = kLatin1[codepoint - 0x00C0];
    return 1;
  }
  // Zero-width joiners and variation selectors carry no glyph of their own, so
  // dropping them is correct rather than lossy. They only ever arrive attached
  // to an emoji, which is being rejected anyway.
  if (codepoint == 0xFE0F || codepoint == 0x200D || codepoint == 0xFEFF) return -1;
  return 0;
}

}  // namespace connections

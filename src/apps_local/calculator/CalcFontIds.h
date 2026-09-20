#pragma once

// The calculator's font ids, and nothing else.
//
// Split from CalcFonts.h because the layout constants name their faces and the
// layout has to stay freestanding: CalcFonts.h includes GfxRenderer.h, and one
// include of that would put the whole renderer on the host suite's include path.
//
// Their own 0x70B1 block so they cannot collide with fontIds.h (FNV hashes of
// generated font names) or with Toybox's 0x70B0 block.
//
// All six are Jersey 25, the fork's own face, cut by
// tools_local/toybox/gen_calc_fonts.sh WITH the math block -- the Toybox cuts
// are subset to U+0020-007E, so the division, multiplication and minus signs
// draw as NOTHING there. Jersey has had all three all along.

namespace calc {

// The pad. Digits and operators at full size; the word keys (AC, DEL, +/-) one
// step down, because a calculator sets its word keys smaller than its digits.
constexpr int kLabelFontId = 0x70B1'0001;  // Jersey 25 @28, ASCII + the math block
constexpr int kSmallFontId = 0x70B1'0007;  // Jersey 25 @20

// The display's four rungs, largest first. Four, because the number has a
// bounded worst case and these are sized so it always lands on one of them: a
// RESULT is never drawn in a label cut, which matters because the label cut is
// the only one with letters in it and these have none.
constexpr int kNumberFontId = 0x70B1'0002;  // Jersey 25 @56, digits and signs only
constexpr int kMidFontId = 0x70B1'000A;     // Jersey 25 @44
constexpr int kTinyFontId = 0x70B1'000B;    // Jersey 25 @34
// The last rung, and the only one an everyday result never reaches: it exists
// so a sixteen-character exponent form is shown in full rather than rounded
// away. At twelve characters, 9999999999 x 9999999999 came out as "1e+20" --
// arithmetically true and useless to read.
constexpr int kFinestFontId = 0x70B1'000C;  // Jersey 25 @26
constexpr int kNumberRungs = 4;

}  // namespace calc

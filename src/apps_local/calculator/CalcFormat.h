#pragma once

// Laying a decimal value out as a display string, inside a hard character
// budget.
//
// Separate from the arithmetic and freestanding on purpose: this is the half no
// library does. Every decimal library hands back a value; none of them knows
// that this panel has room for twelve characters and that a thirteenth would be
// drawn through the display's own rule. It takes a value already in decimal form
// -- sign, coefficient digits most significant first, and a power of ten -- so
// it can be tested exhaustively without a decimal library present at all, and so
// swapping the arithmetic underneath cannot change what reaches the panel.
//
// The rules, which are a pocket calculator's rather than printf's:
//
//   * FIXED whenever fixed fits. A calculator shows 1000, not 1E+3.
//   * Trailing fractional zeros go. Decimal arithmetic preserves scale, so
//     4.35 x 100 is exactly 435.00 and a calculator shows 435.
//   * SCIENTIFIC only when fixed cannot fit, with the mantissa cut to whatever
//     room the exponent leaves -- which is what a real calculator does when its
//     exponent field eats into its mantissa field.
//   * Never longer than the budget. Not "almost never": the caller draws the
//     result without checking, so a string over budget is a number drawn through
//     a border.

#include <cstdint>
#include <cstring>

namespace calc {

// The value is: (negative ? -1 : 1) * coeff[0..n) * 10^exponent, where coeff
// holds one decimal digit per byte, most significant first.
struct Decimal {
  const uint8_t* coeff = nullptr;
  int digits = 0;
  int exponent = 0;
  bool negative = false;
};

namespace detail {

// The coefficient rounded to `keep` digits, half away from zero, carrying.
// Returns the new digit count; `exponent` is raised by what was dropped, and by
// one more if the carry ran off the top (999 -> 100 with one more power of ten).
//
// Shared by both layouts on purpose. Rounding written twice is rounding that
// disagrees with itself at exactly one value, and the value it disagrees at is
// the one somebody reports.
inline int roundTo(uint8_t* out, const uint8_t* coeff, const int digits, int& exponent, const int keep) {
  if (keep >= digits) {
    for (int i = 0; i < digits; ++i) out[i] = coeff[i];
    return digits;
  }
  for (int i = 0; i < keep; ++i) out[i] = coeff[i];
  exponent += digits - keep;
  if (coeff[keep] < 5) return keep;
  int i = keep - 1;
  for (; i >= 0; --i) {
    if (out[i] < 9) {
      ++out[i];
      return keep;
    }
    out[i] = 0;
  }
  // Every kept digit was a nine. The value becomes one followed by zeros, one
  // power of ten bigger -- which is SHORTER once trailing zeros go, never
  // longer, so no layout that fitted before can stop fitting here.
  out[0] = 1;
  ++exponent;
  return keep;
}

// Digits with the trailing zeros of the FRACTION dropped, by raising the
// exponent. 435.00 becomes 435; 1500 stays 1500, because those zeros are to the
// left of the point and carry magnitude.
inline void trimFraction(int& digits, int& exponent, const uint8_t* coeff) {
  while (exponent < 0 && digits > 1 && coeff[digits - 1] == 0) {
    --digits;
    ++exponent;
  }
  // A coefficient that trimmed to a single zero is zero, whatever the exponent.
  if (digits == 1 && coeff[0] == 0) exponent = 0;
}

// How many characters the fixed form would take, sign included.
inline int fixedLength(const int digits, const int exponent, const bool negative) {
  const int sign = negative ? 1 : 0;
  if (exponent >= 0) return sign + digits + exponent;   // 1234, 1234000
  if (digits + exponent > 0) return sign + digits + 1;  // 12.34
  return sign + 2 + (-exponent - digits) + digits;      // 0.00034
}

}  // namespace detail

// Returns the length written, or 0 when even the shortest scientific form does
// not fit -- which the caller shows as an overflow rather than as a clipped
// number. `out` must hold budget + 1 bytes.
inline int formatDecimal(const Decimal& value, const int budget, char* out, const int outCap) {
  if (!out || outCap < 1) return 0;
  out[0] = '\0';
  if (outCap < budget + 1 || !value.coeff || value.digits <= 0) return 0;

  int digits = value.digits;
  int exponent = value.exponent;
  const uint8_t* coeff = value.coeff;
  detail::trimFraction(digits, exponent, coeff);
  const bool negative = value.negative && !(digits == 1 && coeff[0] == 0);

  int at = 0;
  const auto put = [&](const char c) {
    if (at < outCap - 1) out[at++] = c;
  };

  // A fixed form that is too long is ROUNDED to fit before scientific is
  // considered. A calculator with room for eight characters shows 12345.68, not
  // 1.235e+4 -- dropping to an exponent for a number you can read plainly is the
  // thing that makes a display feel broken.
  uint8_t fixedDigits[40];
  if (detail::fixedLength(digits, exponent, negative) > budget && exponent < 0) {
    const int whole = digits + exponent > 0 ? digits + exponent : 1;
    const int room = budget - (negative ? 1 : 0) - whole - 1;
    if (room >= 1 && whole + room < digits) {
      int rounded = exponent;
      const int n = detail::roundTo(fixedDigits, coeff, digits, rounded, whole + room);
      int trimmed = n;
      detail::trimFraction(trimmed, rounded, fixedDigits);
      if (detail::fixedLength(trimmed, rounded, negative) <= budget) {
        coeff = fixedDigits;
        digits = trimmed;
        exponent = rounded;
      }
    }
  }

  if (detail::fixedLength(digits, exponent, negative) <= budget) {
    if (negative) put('-');
    if (exponent >= 0) {
      for (int i = 0; i < digits; ++i) put(static_cast<char>('0' + coeff[i]));
      for (int i = 0; i < exponent; ++i) put('0');
    } else if (digits + exponent > 0) {
      const int whole = digits + exponent;
      for (int i = 0; i < whole; ++i) put(static_cast<char>('0' + coeff[i]));
      put('.');
      for (int i = whole; i < digits; ++i) put(static_cast<char>('0' + coeff[i]));
    } else {
      put('0');
      put('.');
      for (int i = 0; i < -exponent - digits; ++i) put('0');
      for (int i = 0; i < digits; ++i) put(static_cast<char>('0' + coeff[i]));
    }
    out[at] = '\0';
    return at;
  }

  // Scientific. The exponent is the power of ten of the LEADING digit, which is
  // what a calculator's exponent field shows.
  int adjusted = exponent + digits - 1;
  int expDigits = 1;
  for (int e = adjusted < 0 ? -adjusted : adjusted; e >= 10; e /= 10) ++expDigits;
  // sign, leading digit, 'e', exponent sign, exponent digits
  const int fixedCost = (negative ? 1 : 0) + 1 + 1 + 1 + expDigits;
  // What is left pays for the point and the fraction. One spare character buys
  // nothing: a point with no digit after it.
  int fraction = budget - fixedCost - 1;
  if (fraction < 0) fraction = 0;
  if (fraction > digits - 1) fraction = digits - 1;
  if (budget - fixedCost < 2) fraction = 0;
  if (fixedCost > budget) {
    // Not even a single digit and its exponent fit. The caller shows an
    // overflow; it must not draw the buffer, which is why it is terminated.
    out[0] = '\0';
    return 0;
  }

  // Round the mantissa at the length that fits, half away from zero, and carry.
  // Doing it here rather than asking the arithmetic to re-round keeps the value
  // the user has and the string on the panel from disagreeing about the last
  // digit.
  uint8_t shown[40];
  const int keep = fraction + 1;
  int scratch = exponent;
  const int before = scratch;
  detail::roundTo(shown, coeff, digits, scratch, keep);
  // roundTo raises the exponent by what it dropped, plus one if it carried off
  // the top. The mantissa's own power of ten moves only by that carry, and after
  // one the mantissa is a single 1 -- shorter, never longer, so no second pass.
  if (scratch - before > digits - keep) {
    ++adjusted;
    expDigits = 1;
    for (int e = adjusted < 0 ? -adjusted : adjusted; e >= 10; e /= 10) ++expDigits;
    fraction = 0;
    // "Shorter, never longer" is only true while there is a fraction to give
    // back. At a budget that already had none, 9.9e+99 carrying to 1e+100 is one
    // character WIDER, and the fuzz found it: twenty-two of a quarter million
    // cases. Nothing can be shown at that budget, so say so rather than draw
    // seven characters into six.
    if ((negative ? 1 : 0) + 1 + 1 + 1 + expDigits > budget) {
      out[0] = '\0';
      return 0;
    }
  }

  if (negative) put('-');
  put(static_cast<char>('0' + shown[0]));
  if (fraction > 0) {
    // Trailing zeros are noise in an exponent form too: 1.500e9 is 1.5e9.
    int last = fraction;
    while (last > 0 && shown[last] == 0) --last;
    if (last > 0) {
      put('.');
      for (int i = 1; i <= last; ++i) put(static_cast<char>('0' + shown[i]));
    }
  }
  put('e');
  put(adjusted < 0 ? '-' : '+');
  const int mag = adjusted < 0 ? -adjusted : adjusted;
  if (expDigits >= 3) put(static_cast<char>('0' + (mag / 100) % 10));
  if (expDigits >= 2) put(static_cast<char>('0' + (mag / 10) % 10));
  put(static_cast<char>('0' + mag % 10));
  out[at] = '\0';
  return at;
}

}  // namespace calc

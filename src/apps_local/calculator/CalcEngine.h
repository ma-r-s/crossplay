#pragma once

// The calculator itself: what a key press does to the numbers.
//
// Immediate execution, the way every pocket calculator since the 1970s works: an
// operator key completes the sum so far and shows it, rather than waiting for a
// closing bracket.
//
// THE ARITHMETIC IS DECIMAL, NOT BINARY. IBM decNumber, vendored at
// lib/decNumber, ICU licence. That is the whole reason this file is not fifty
// lines of `double`:
//
//   0.1 + 0.2        binary double, rounded to 12 digits:  0.3
//   0.1 + 0.2 - 0.3  binary double, rounded to 12 digits:  5.55111512313e-17
//
// Rounding the display hides the first and CANNOT hide the second, because there
// the error is the whole answer. Casio and TI show 0 because their arithmetic is
// decimal (BCD), not because their displays are cleverer. Ours is decimal for
// the same reason, and it costs about 25KB of flash.
//
// Everything else here is somewhere calculators are commonly wrong, which is why
// it is one file with one suite rather than sprinkled through an activity:
//
//   * `200 + 10 %` is 220 and `200 x 10 %` is 20. Percent is not one operation;
//     it reads the pending operator. Getting this wrong is the single most
//     common calculator bug.
//   * `2 + 3 = = =` is 5, 8, 11. Equals repeats the last operator and operand.
//   * Two operators in a row replace, they do not stack.
//   * Divide by zero says so and then refuses every key but clear.
//   * Nothing the display can be handed is longer than kMaxDisplayChars.

#include <DecNumber.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "CalcFormat.h"
#include "CalcKeys.h"
#include "CalcLayout.h"

namespace calc {

// TWO precisions, and the difference between them is what makes this feel like a
// calculator rather than like arithmetic.
//
// Ten is what the display SHOWS -- a normal pocket calculator; TI's is ten -- and
// it is what kMaxDisplayChars allows in fixed form: a sign, ten digits and a
// point. Sixteen is what the arithmetic WORKS in. The six extra are guard digits,
// and they are the reason (1 / 3) x 3 reads 1:
//
//   at ten working digits   0.3333333333 x 3 = 0.9999999999   shown as 0.9999999999
//   at sixteen, shown at 10 0.3333333333333333 x 3 = 0.9999999999999999 -> 1
//
// Casio and TI both carry hidden guard digits for exactly this. Without them a
// decimal calculator is still wrong, just wrong in a different place than a
// binary one.
constexpr int kSignificantDigits = 10;
constexpr int kWorkingDigits = 16;
// Never let an operand carry more digits than the working precision. decNumber
// 3.68 has a published erratum where add and subtract can be off by one in the
// last digit when an operand is LONGER than the context precision; capping the
// keypad is what keeps that unreachable rather than unlikely.
constexpr int kMaxEntryDigits = kSignificantDigits;
static_assert(kMaxEntryDigits <= kWorkingDigits, "an operand longer than the working precision hits an erratum");
// Bounded so an exponent is at most three characters. A pocket calculator errors
// past its exponent range rather than showing a number nobody asked for, and a
// bounded range is also what keeps the display's worst case provable.
constexpr int kMaxExponent = 99;
constexpr size_t kTextMax = 32;

class Engine {
 public:
  Engine() {
    decContextDefault(&ctx_, DEC_INIT_BASE);
    ctx_.digits = kWorkingDigits;
    ctx_.emax = kMaxExponent;
    ctx_.emin = -kMaxExponent;
    // Half UP, not half EVEN. Bankers' rounding is right for accounting and
    // wrong for a calculator: a person who types 0.5 and rounds expects 1.
    ctx_.round = DEC_ROUND_HALF_UP;
    // Never trap, and this one is not tidiness: DEC_INIT_BASE leaves
    // traps = DEC_Errors, and decContextSetStatus then calls raise(SIGFPE). Left
    // alone, the first division by zero aborts the firmware. Status bits are
    // also the whole reason decNumber suits a build with exceptions off.
    ctx_.traps = 0;
    clearAll();
  }

  // --- what the screen asks -------------------------------------------------

  const char* display() const { return display_; }
  const char* pending() const { return pending_; }
  bool hasError() const { return error_; }

  // --- what the pad does ----------------------------------------------------

  void press(const Key k) {
    // An error is a wall: only clear gets through. Letting a digit land on top of
    // "DIVIDE BY 0" is how a calculator starts lying quietly.
    if (error_ && k != Key::ClearAll && k != Key::ClearEntry) return;

    if (isDigit(k)) return pressDigit(digitValue(k));
    switch (k) {
      case Key::Dot:
        return pressDot();
      case Key::Add:
      case Key::Sub:
      case Key::Mul:
      case Key::Div:
        return pressOperator(k);
      case Key::Equals:
        return pressEquals();
      case Key::Percent:
        return pressPercent();
      case Key::Negate:
        return pressNegate();
      case Key::ClearAll:
        return clearAll();
      case Key::ClearEntry:
        return clearEntry();
      case Key::Backspace:
        return pressBackspace();
      default:
        return;
    }
  }

  void clearAll() {
    resetEntry();
    decNumberZero(&acc_);
    decNumberZero(&operand_);
    operandLive_ = false;
    decNumberZero(&repeatOperand_);
    pendingOp_ = Key::None;
    repeatOp_ = Key::None;
    error_ = false;
    pending_[0] = '\0';
    ctx_.status = 0;
    refresh();
  }

 private:
  void resetEntry() {
    entry_[0] = '0';
    entry_[1] = '\0';
    entryDigits_ = 0;
    entryLive_ = false;
    entryHasDot_ = false;
    entryNegative_ = false;
  }

  // CE clears the number being typed and nothing else -- EXCEPT after an error,
  // where there is no "else" worth keeping. The first version cleared the
  // message and left the pending operator standing: `5 / 0 =` then CE then `3 =`
  // quietly answered 1.666666667, computing 5 / 3 from an operation the user had
  // watched fail. An error means the sum is gone.
  //
  // And CE leaves a live ZERO rather than falling back to the accumulator. The
  // first version showed `5` under a `5 +` pending line, which reads as 5 + 5,
  // and `5 + 3 CE =` came to 10 where every calculator gives 5.
  void clearEntry() {
    if (error_) {
      clearAll();
      return;
    }
    ctx_.status = 0;
    resetEntry();
    entryLive_ = true;
    operandLive_ = false;
    refresh();
  }

  // The number on screen: what is being typed, a computed operand waiting for an
  // operator, or the accumulator. One question, one answer, so no branch can
  // form a second opinion about which the user is looking at.
  void currentValue(decNumber* out) const {
    if (!entryLive_) {
      decNumberCopy(out, operandLive_ ? &operand_ : &acc_);
      return;
    }
    char text[kTextMax + 2];
    std::snprintf(text, sizeof(text), "%s%s", entryNegative_ ? "-" : "", entry_);
    decContext scratch = ctx_;
    decNumberFromString(out, text, &scratch);
  }

  void takeResult(const decNumber& value) {
    decNumberCopy(&acc_, &value);
    entryLive_ = false;
    operandLive_ = false;
    refresh();
  }

  // Infinity and NaN are real decNumber values and they format as "0": their
  // coefficient is a single zero, so decNumberGetBCD hands the formatter a zero
  // and the panel says the sum came to nothing. Every path that produces a value
  // goes through here, because a quiet NaN also propagates through an add
  // WITHOUT setting Invalid_operation -- so the status bits alone cannot catch
  // it downstream.
  bool guardSpecial(const decNumber& value) {
    if (decNumberIsNaN(&value)) return fail("BAD INPUT");
    if (decNumberIsInfinite(&value)) return fail("OVERFLOW");
    return true;
  }

  // decNumber never throws; it sets bits. Reading and clearing them in one place
  // is what keeps a stale bit from a sum three presses ago failing the next one.
  bool checkStatus() {
    const uint32_t status = ctx_.status;
    ctx_.status = 0;
    if (status & DEC_Division_by_zero) return fail("DIVIDE BY 0");
    if (status & (DEC_Overflow | DEC_Underflow)) return fail("OVERFLOW");
    // Zero over zero is DIVISION_UNDEFINED, a different bit from a division by a
    // zero divisor, and an invalid operation rather than an infinity.
    if (status & (DEC_Invalid_operation | DEC_Conversion_syntax | DEC_Division_undefined)) return fail("BAD INPUT");
    return true;
  }

  void pressDigit(const int d) {
    operandLive_ = false;
    if (!entryLive_) {
      entry_[0] = '\0';
      entryDigits_ = 0;
      entryHasDot_ = false;
      entryNegative_ = false;
      entryLive_ = true;
    }
    // Refusing the eleventh digit rather than accepting and rounding it: a key
    // that silently does nothing is better than a number that silently changes.
    if (entryDigits_ >= kMaxEntryDigits) return;
    if (entryDigits_ == 0 && d == 0 && !entryHasDot_) {
      std::snprintf(entry_, sizeof(entry_), "0");
      refresh();
      return;
    }
    if (std::strcmp(entry_, "0") == 0 && !entryHasDot_) entry_[0] = '\0';
    const size_t len = std::strlen(entry_);
    if (len + 2 >= sizeof(entry_)) return;
    entry_[len] = static_cast<char>('0' + d);
    entry_[len + 1] = '\0';
    ++entryDigits_;
    refresh();
  }

  void pressDot() {
    operandLive_ = false;
    if (!entryLive_) {
      std::snprintf(entry_, sizeof(entry_), "0");
      entryDigits_ = 0;
      entryHasDot_ = false;
      entryNegative_ = false;
      entryLive_ = true;
    }
    if (entryHasDot_) return;
    const size_t len = std::strlen(entry_);
    if (len + 2 >= sizeof(entry_)) return;
    entry_[len] = '.';
    entry_[len + 1] = '\0';
    entryHasDot_ = true;
    refresh();
  }

  void pressBackspace() {
    // Only the number being typed. Backspacing a RESULT would have to undo the
    // sum that produced it, and there is no sensible answer to what `5` means
    // after you back a digit off the 25 that arrived from 5 x 5.
    if (!entryLive_) return;
    const size_t len = std::strlen(entry_);
    if (len == 0) return;
    if (entry_[len - 1] == '.') entryHasDot_ = false;
    if (entry_[len - 1] >= '0' && entry_[len - 1] <= '9' && entryDigits_ > 0) --entryDigits_;
    entry_[len - 1] = '\0';
    if (entry_[0] == '\0') {
      entry_[0] = '0';
      entry_[1] = '\0';
      entryDigits_ = 0;
      entryNegative_ = false;
    }
    refresh();
  }

  void pressNegate() {
    if (entryLive_) {
      entryNegative_ = !entryNegative_;
    } else if (operandLive_) {
      decNumberCopyNegate(&operand_, &operand_);
    } else {
      // CopyNegate, not Minus: Minus rounds under the context, and flipping a
      // sign is not an operation that should be able to change a digit.
      decNumberCopyNegate(&acc_, &acc_);
    }
    refresh();
  }

  bool apply(const Key op, const decNumber& lhs, const decNumber& rhs, decNumber* out) {
    ctx_.status = 0;
    switch (op) {
      case Key::Add:
        decNumberAdd(out, &lhs, &rhs, &ctx_);
        break;
      case Key::Sub:
        decNumberSubtract(out, &lhs, &rhs, &ctx_);
        break;
      case Key::Mul:
        decNumberMultiply(out, &lhs, &rhs, &ctx_);
        break;
      case Key::Div:
        decNumberDivide(out, &lhs, &rhs, &ctx_);
        break;
      default:
        decNumberCopy(out, &rhs);
        break;
    }
    return checkStatus() && guardSpecial(*out);
  }

  void pressOperator(const Key op) {
    // Two operators in a row replace rather than stack: the second one is what
    // you meant, and every calculator on every desk behaves this way.
    if (!entryLive_ && pendingOp_ != Key::None) {
      pendingOp_ = op;
      writePending();
      refresh();
      return;
    }
    decNumber rhs;
    currentValue(&rhs);
    if (pendingOp_ == Key::None) {
      decNumberCopy(&acc_, &rhs);
    } else {
      decNumber out;
      if (!apply(pendingOp_, acc_, rhs, &out)) return;
      decNumberCopy(&acc_, &out);
    }
    pendingOp_ = op;
    entryLive_ = false;
    operandLive_ = false;
    repeatOp_ = Key::None;
    writePending();
    refresh();
  }

  void pressEquals() {
    decNumber rhs;
    Key op;
    if (pendingOp_ != Key::None) {
      currentValue(&rhs);
      op = pendingOp_;
    } else if (repeatOp_ != Key::None) {
      // `2 + 3 =` then `=` again: repeat the operator AND the operand. If a NEW
      // number has been typed since, it becomes the left-hand side rather than
      // being thrown away -- `5 + 3 = 7 =` is 10, the way a TI or a Casio does
      // it. The first version ignored the 7 and recomputed 8 + 3, which is 11,
      // an answer no calculator anywhere gives.
      if (entryLive_ || operandLive_) {
        decNumber typed;
        currentValue(&typed);
        decNumberCopy(&acc_, &typed);
      }
      decNumberCopy(&rhs, &repeatOperand_);
      op = repeatOp_;
    } else {
      decNumber value;
      currentValue(&value);
      pending_[0] = '\0';
      takeResult(value);
      return;
    }
    decNumber out;
    if (!apply(op, acc_, rhs, &out)) return;
    decNumberCopy(&repeatOperand_, &rhs);
    repeatOp_ = op;
    pendingOp_ = Key::None;
    pending_[0] = '\0';
    takeResult(out);
  }

  // `200 + 10 %` is 220 and `200 x 10 %` is 20, because percent reads the
  // pending operator: additive operators want a percentage OF the running total,
  // multiplicative ones want a plain hundredth. This is the rule iOS and Casio
  // share. Windows differs on the multiplicative case (500 x 5 % is 12500
  // there); if that is wanted, this is the one branch that changes.
  void pressPercent() {
    decNumber x;
    currentValue(&x);
    decNumber hundred;
    decNumberFromString(&hundred, "100", &ctx_);
    decNumber out;
    ctx_.status = 0;
    decNumberDivide(&out, &x, &hundred, &ctx_);
    if (!checkStatus()) return;
    if (pendingOp_ == Key::Add || pendingOp_ == Key::Sub) {
      decNumber scaled;
      ctx_.status = 0;
      decNumberMultiply(&scaled, &acc_, &out, &ctx_);
      if (!checkStatus()) return;
      decNumberCopy(&out, &scaled);
    }
    // The percentage becomes a computed OPERAND, waiting for the pending
    // operator to consume it. It is deliberately NOT written back into the typed
    // entry, which is what the first version did and is what broke the display's
    // whole guarantee: writeNumber fills the full sixteen-character budget, then
    // refresh() prepends a sign and Dot appends a point, neither of them counted.
    // `9999999999 x 9999999999 = % % . +/-` put EIGHTEEN characters on a display
    // that promises at most sixteen, and it only failed to clip because the
    // widest reachable string came to 445px in a 448px box. A result is a result;
    // it is not text somebody can carry on typing into.
    decNumberCopy(&operand_, &out);
    operandLive_ = true;
    resetEntry();
    refresh();
  }

  bool fail(const char* why) {
    error_ = true;
    std::snprintf(display_, sizeof(display_), "%s", why);
    pending_[0] = '\0';
    return false;
  }

  static const char* opGlyph(const Key op) {
    // The real signs, as UTF-8. Safe because every line the engine produces is
    // drawn in a calculator cut, and all of them carry U+00D7, U+00F7 and
    // U+2212 -- which is the entire reason those cuts exist. A Toybox cut would
    // draw them as nothing at all.
    switch (op) {
      case Key::Add:
        return "+";
      case Key::Sub:
        return "\xE2\x88\x92";
      case Key::Mul:
        return "\xC3\x97";
      case Key::Div:
        return "\xC3\xB7";
      default:
        return "?";
    }
  }

  // A decimal value as a display string, inside the budget. The ONE place a
  // number becomes text, so nothing on the panel can be longer than the display
  // can hold.
  void writeNumber(const decNumber& value, char* out, const size_t n) const {
    // Rounded to the SHOWN precision first. The guard digits exist so the
    // arithmetic is right; showing them is what would put 0.9999999999 on the
    // panel where a calculator says 1.
    decContext shown = ctx_;
    shown.digits = kSignificantDigits;
    shown.status = 0;
    decNumber rounded;
    decNumberPlus(&rounded, &value, &shown);

    uint8_t bcd[DECNUMDIGITS + 1];
    decNumberGetBCD(&rounded, bcd);
    Decimal shape;
    shape.coeff = bcd;
    shape.digits = rounded.digits;
    shape.exponent = rounded.exponent;
    shape.negative = decNumberIsNegative(&rounded) != 0;
    const int budget = static_cast<int>(n) - 1 < kMaxDisplayChars ? static_cast<int>(n) - 1 : kMaxDisplayChars;
    if (formatDecimal(shape, budget, out, static_cast<int>(n)) == 0) std::snprintf(out, n, "OVERFLOW");
  }

  void writePending() {
    char lhs[kTextMax];
    writeNumber(acc_, lhs, sizeof(lhs));
    std::snprintf(pending_, sizeof(pending_), "%s %s", lhs, opGlyph(pendingOp_));
  }

  void refresh() {
    if (error_) return;
    if (entryLive_) {
      // What is being TYPED is shown exactly as typed: a trailing point stays
      // while you are still in the middle of putting one there, and "0.50" does
      // not collapse to "0.5" under your finger.
      //
      // The one thing suppressed is a minus in front of a zero. Pressing 0 and
      // then +/- would otherwise read "-0", which is a wart on any display and
      // the formatter already refuses it for RESULTS. The sign is still held --
      // type a digit after it and the number is negative -- it just is not shown
      // while there is nothing for it to be the sign of.
      const bool anyDigit = std::strpbrk(entry_, "123456789") != nullptr;
      std::snprintf(display_, sizeof(display_), "%s%s", entryNegative_ && anyDigit ? "-" : "", entry_);
      return;
    }
    writeNumber(operandLive_ ? operand_ : acc_, display_, sizeof(display_));
  }

  decContext ctx_{};
  decNumber acc_{};
  // A computed right-hand operand -- what percent produces -- waiting for the
  // pending operator. Separate from both the typed entry and the accumulator
  // because it is neither: it is not editable text, and committing it to acc_
  // would throw away the sum it is a percentage OF.
  decNumber operand_{};
  bool operandLive_ = false;
  decNumber repeatOperand_{};
  char entry_[kTextMax] = {};
  char display_[kTextMax * 2] = {};
  char pending_[kTextMax * 2] = {};
  int entryDigits_ = 0;
  bool entryLive_ = false;
  bool entryHasDot_ = false;
  bool entryNegative_ = false;
  bool error_ = false;
  Key pendingOp_ = Key::None;
  Key repeatOp_ = Key::None;
};

}  // namespace calc

// utf8HasCjkBreakOpportunity: where every CJK line in this firmware is allowed
// to end. Both the reader's layout and every UI label ask it, and it arrived
// with no test of its own.
//
// The cases here are the ones a wrong answer makes visible on the panel: a
// closing bracket orphaned onto the next line, an opening bracket left alone
// at the end of one, a Latin word split down the middle, and a combining mark
// separated from the letter it sits on.

#include <cstdio>

#include "Utf8.h"

static int failures = 0;
static int checks = 0;

static void expect(const bool got, const bool want, const char* what) {
  ++checks;
  if (got == want) return;
  ++failures;
  std::printf("  FAIL %s: got %s, want %s\n", what, got ? "break" : "no break", want ? "break" : "no break");
}

int main() {
  // Han, kana and Hangul break between any two characters: that is the whole
  // point, since none of the three is written with spaces at every boundary.
  expect(utf8HasCjkBreakOpportunity(0x4E00, 0x4E8C), true, "between two hanzi");
  expect(utf8HasCjkBreakOpportunity(0x3042, 0x3044), true, "between two hiragana");
  expect(utf8HasCjkBreakOpportunity(0x30AB, 0x30BF), true, "between two katakana");
  expect(utf8HasCjkBreakOpportunity(0xAC00, 0xAC01), true, "between two hangul syllables");

  // Latin is space-delimited and must never break mid-word, which is what
  // makes the "either side is CJK" guard load-bearing.
  expect(utf8HasCjkBreakOpportunity('a', 'b'), false, "inside a Latin word");
  expect(utf8HasCjkBreakOpportunity('t', 'h'), false, "inside a Latin digraph");

  // The boundary between scripts is a real opportunity in both directions.
  expect(utf8HasCjkBreakOpportunity('a', 0x4E00), true, "Latin then hanzi");
  expect(utf8HasCjkBreakOpportunity(0x4E00, 'a'), true, "hanzi then Latin");

  // Kinsoku, first half: a line may not BEGIN with closing punctuation, so
  // there is no opportunity immediately before one.
  expect(utf8HasCjkBreakOpportunity(0x4E00, 0x3002), false, "before a full stop");
  expect(utf8HasCjkBreakOpportunity(0x4E00, 0x3001), false, "before an ideographic comma");
  expect(utf8HasCjkBreakOpportunity(0x4E00, 0x300D), false, "before a closing bracket");
  expect(utf8HasCjkBreakOpportunity(0x4E00, 0xFF09), false, "before a fullwidth paren");

  // Kinsoku, second half: a line may not END on an opening bracket.
  expect(utf8HasCjkBreakOpportunity(0x300C, 0x4E00), false, "after an opening bracket");
  expect(utf8HasCjkBreakOpportunity(0xFF08, 0x4E00), false, "after a fullwidth open paren");

  // A closing bracket is itself breakable, so the opportunity is AFTER it.
  expect(utf8HasCjkBreakOpportunity(0x3002, 0x4E00), true, "after a full stop");

  // A combining mark belongs to the character before it, whichever script.
  expect(utf8HasCjkBreakOpportunity(0x4E00, 0x0301), false, "before a combining acute");
  expect(utf8HasCjkBreakOpportunity(0x4E00, 0xFE20), false, "before a combining half mark");

  std::printf("cjkbreak: %d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}

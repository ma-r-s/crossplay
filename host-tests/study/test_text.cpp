// The card face's line breaking, and the span arithmetic the cloze underline
// rides on.
//
// This exists because StudyActivity itself cannot be built on a host -- it
// needs Arduino, the renderer and the HAL -- so for as long as the wrap lived
// inside it, nothing could check where a line broke or which characters
// landed on it. What is under test here is exactly what the underline
// depends on: a mark drawn from a second copy of the wrap would sit under the
// wrong word, and on an e-ink card that is indistinguishable from the
// converter having marked the wrong word.
//
// Widths come from a fake metric rather than from a font, on purpose. The
// question is not "how wide is this glyph" -- that is the renderer's job and
// it has its own tests -- but "given widths, where do the breaks and the span
// go", and a fake metric is the only way to ask it in a way that cannot
// change when a font is rebuilt.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../src/apps_local/study/StudyText.h"

namespace {

int checks = 0;
int failures = 0;

void check(const bool condition, const char* label) {
  ++checks;
  if (!condition) {
    ++failures;
    std::printf("  FAIL: %s\n", label);
  }
}

// Ten pixels per Latin codepoint, thirty per CJK one: the ratio a real face
// has, in numbers a test can do in its head.
int fakeWidth(const char* text) {
  int width = 0;
  for (const char* p = text; *p != '\0';) {
    const char* at = p;
    if (study::nextCodepoint(p) == 0) break;
    width += (p - at) >= 3 ? 30 : 10;
  }
  return width;
}

struct Laid {
  std::vector<std::string> lines;
  std::vector<int> startCp;
  std::vector<int> counts;
};

Laid lay(const char* text, const int maxWidth) {
  Laid out;
  char buffer[256];
  study::wrapText(text, maxWidth, buffer, sizeof(buffer), fakeWidth, [&](const study::WrappedLine& line) {
    out.lines.emplace_back(line.text);
    out.startCp.push_back(line.startCodepoint);
    out.counts.push_back(line.codepoints);
  });
  return out;
}

// Where an underline would go on each line, as codepoint ranges relative to
// that line -- which is what drawWrappedUnderlined turns into pixels.
std::vector<std::string> marks(const char* text, const int maxWidth, const int start, const int length) {
  std::vector<std::string> out;
  char buffer[256];
  study::wrapText(text, maxWidth, buffer, sizeof(buffer), fakeWidth, [&](const study::WrappedLine& line) {
    int from = 0;
    int to = 0;
    if (!study::spanOnLine(line, start, length, from, to)) return;
    const int fromByte = study::bytesForCodepoints(line.text, from);
    const int toByte = study::bytesForCodepoints(line.text, to);
    out.emplace_back(line.text + fromByte, line.text + toByte);
  });
  return out;
}

// A draw target that records instead of drawing. Ten pixels per Latin
// codepoint, thirty per CJK one, a 20px line and an 800px screen -- the real
// panel's width, because the centring arithmetic is half of what is under
// test and it is the half that is easy to get right for the wrong screen.
struct Recorder {
  struct Text {
    int x = 0;
    int y = 0;
    std::string text;
  };
  struct Rule {
    int x = 0;
    int y = 0;
    int width = 0;
  };
  mutable std::vector<Text> texts;
  mutable std::vector<Rule> rules;

  int getScreenWidth() const { return 800; }
  int getTextHeight(int) const { return 20; }
  int getTextWidth(int, const char* text) const { return fakeWidth(text); }
  void drawText(int, int x, int y, const char* text, bool) const { texts.push_back({x, y, text}); }
  void fillRect(int x, int y, int width, int, bool) const { rules.push_back({x, y, width}); }
};

Recorder draw(const char* text, int maxWidth, int spanStart, int spanLength) {
  Recorder target;
  char line[256];
  char scratch[256];
  study::drawWrappedMarked(target, 1, 100, maxWidth, text, spanStart, spanLength, false, line, sizeof(line), scratch);
  return target;
}

void run() {
  // --- codepoints, not bytes ------------------------------------------------
  check(study::codepointCount("abc") == 3, "ASCII counts one per byte");
  check(study::codepointCount("中文") == 2, "CJK counts one per character");
  check(study::codepointCount("a中b") == 3, "a mixed string counts characters");
  check(study::bytesForCodepoints("a中b", 2) == 4, "two codepoints of 'a中b' are four bytes");
  check(study::bytesForCodepoints("abc", 99) == 3, "asking past the end clamps");
  check(study::bytesForCodepoints("", 3) == 0, "an empty string is zero bytes");

  // --- Latin wraps on spaces ------------------------------------------------
  {
    // "aaa bbb ccc" at 70px: "aaa bbb" is 7 chars = 70, "ccc" follows.
    const Laid out = lay("aaa bbb ccc", 70);
    check(out.lines.size() == 2, "Latin wraps into two lines");
    check(out.lines[0] == "aaa bbb", "the first line takes what fits");
    check(out.lines[1] == "ccc", "and the leading space is eaten by the break");
    check(out.startCp[1] == 8, "the second line starts at codepoint 8, past the space");
    check(out.counts[0] == 7, "the first line is seven codepoints");
  }

  // A word wider than the line is not chopped: it overhangs, and fitsAsDrawn
  // is what refuses the font before this is ever drawn.
  {
    const Laid out = lay("incontrovertible", 50);
    check(out.lines.size() == 1, "an unbreakable word stays whole");
    check(out.lines[0] == "incontrovertible", "and is handed over intact");
  }

  // --- CJK breaks between characters ---------------------------------------
  {
    // Four hanzi at 30px each, in a 90px line: three, then one.
    const Laid out = lay("中文句子", 90);
    check(out.lines.size() == 2, "CJK wraps without spaces");
    check(out.lines[0] == "中文句", "three characters fit at 90px");
    check(out.counts[0] == 3, "counted in characters, not bytes");
    check(out.startCp[1] == 3, "the second line starts at the fourth character");
  }

  // --- hard breaks ----------------------------------------------------------
  //
  // Anki fields carry structure: a <br>, a list, the paragraphs of a Back
  // Extra. The converter turns those into newlines, and a newline the wrap
  // ignored ran a four-item list into one grey line.
  {
    const Laid out = lay("one\ntwo", 1000);
    check(out.lines.size() == 2, "a newline breaks the line even when it would fit");
    check(out.lines.size() == 2 && out.lines[0] == "one", "the first line stops at the newline");
    check(out.lines.size() == 2 && out.lines[1] == "two", "and the newline itself is not drawn");
    check(out.startCp.size() == 2 && out.startCp[1] == 4, "the newline still counts as a codepoint");
  }
  {
    // Span offsets are measured over the string as it is, newline included,
    // so a mark after a break has to land on the right characters.
    const std::vector<std::string> marked = marks("one\ntwo", 1000, 4, 3);
    check(marked.size() == 1 && marked[0] == "two", "a span after a hard break marks the right word");
  }
  {
    const Laid out = lay("a\nb\nc", 1000);
    check(out.lines.size() == 3, "several newlines make several lines");
  }
  {
    // The wrap has to keep working around the break, not just at it: each
    // paragraph wraps on its own, so two that each need two lines make four.
    const Laid out = lay("aaa bbb ccc\nddd eee fff", 70);
    check(out.lines.size() == 4, "each paragraph wraps on its own");
    check(out.lines.size() == 4 && out.lines[1] == "ccc", "the first paragraph's tail is its own line");
    check(out.lines.size() == 4 && out.lines[2] == "ddd eee", "and the second starts a new one");
  }

  // --- the span, which is what cloze rides on -------------------------------
  {
    // "The capital is Paris." -- underline "Paris", codepoints 15..19.
    const char* answer = "The capital is Paris.";
    check(std::strlen(answer) == 21, "fixture string is the length the offsets assume");
    const std::vector<std::string> one = marks(answer, 1000, 15, 5);
    check(one.size() == 1 && one[0] == "Paris", "a span on one line marks exactly the word");
  }
  {
    // The same span, on a line narrow enough that it wraps mid-word is not
    // possible (words are not broken), so wrap BETWEEN words and check the
    // span lands on the right line rather than the first.
    const std::vector<std::string> two = marks("The capital is Paris.", 100, 15, 6);
    check(two.size() == 1 && two[0] == "Paris.", "a span still marks its word after a wrap");
  }
  {
    // A span that crosses a line break is marked on both lines. "aaa bbb"
    // at 40px gives "aaa" / "bbb"; the span covers "aa bb".
    // The space fits on the first line, so it stays there and is a codepoint
    // of it -- only a space that CAUSES the break is eaten. The span is
    // therefore "aa " on line one and "bb" on line two.
    const std::vector<std::string> split = marks("aaa bbb", 40, 1, 5);
    check(split.size() == 2, "a span across a break marks two lines");
    check(split.size() == 2 && split[0] == "aa ", "the tail of the first line");
    check(split.size() == 2 && split[1] == "bb", "and the head of the second");
  }
  {
    check(marks("hello", 1000, 0, 0).empty(), "a zero-length span marks nothing");
    check(marks("hello", 1000, 99, 3).empty(), "a span past the end marks nothing");
    const std::vector<std::string> clipped = marks("hello", 1000, 3, 99);
    check(clipped.size() == 1 && clipped[0] == "lo", "a span running past the end is clipped, not dropped");
  }

  // --- the drawn geometry ---------------------------------------------------
  //
  // Everything above is arithmetic on indices. This is the part that puts
  // pixels somewhere, and the part no other test in this repository can
  // reach: StudyActivity needs Arduino, the HAL and a panel to build at all.
  {
    // "The capital is Paris." is 21 Latin codepoints = 210px, centred on an
    // 800px screen, so it starts at (800 - 210) / 2 = 295.
    const Recorder out = draw("The capital is Paris.", 800, 15, 5);
    check(out.texts.size() == 1, "a line that fits is drawn once");
    check(out.texts.size() == 1 && out.texts[0].x == 295, "and is centred on the screen");
    check(out.texts.size() == 1 && out.texts[0].y == 100, "at the y it was given");
    check(out.rules.size() == 1, "the marked run gets one underline");
    // "The capital is " is 15 codepoints = 150px, so the mark starts at
    // 295 + 150 = 445 and is "Paris" wide, which is 50.
    check(out.rules.size() == 1 && out.rules[0].x == 445, "the underline starts under the first marked glyph");
    check(out.rules.size() == 1 && out.rules[0].width == 50, "and is exactly as wide as the run");
    // Baseline plus the line height, less the two-pixel clearance.
    check(out.rules.size() == 1 && out.rules[0].y == 118, "and sits below the baseline, not through it");
  }
  {
    // Full width: the mark spans the whole line, so it starts where the text
    // starts and is as wide as the text.
    const Recorder out = draw("Paris", 800, 0, 5);
    check(out.rules.size() == 1 && out.rules[0].x == out.texts[0].x, "a whole-line mark starts with the text");
    check(out.rules.size() == 1 && out.rules[0].width == 50, "and is as wide as it");
  }
  {
    // No span: nothing is underlined, which is the case every vocabulary card
    // on the question face takes.
    const Recorder out = draw("The capital is Paris.", 800, 0, 0);
    check(out.rules.empty(), "no span means no underline");
    check(out.texts.size() == 1, "and the text is still drawn");
  }
  {
    // Wrapped: each line is centred on its own width, and the second line
    // sits one line height lower.
    const Recorder out = draw("aaa bbb ccc", 70, 0, 0);
    check(out.texts.size() == 2, "a long string is drawn line by line");
    check(out.texts.size() == 2 && out.texts[1].y == 120, "the second line is one line height down");
    check(out.texts.size() == 2 && out.texts[0].x != out.texts[1].x, "each line is centred on its own width");
  }
  {
    // A span crossing a break is underlined on both lines.
    const Recorder out = draw("aaa bbb", 40, 1, 5);
    check(out.rules.size() == 2, "a span across a break is underlined twice");
  }
  {
    // A CJK line: full-width glyphs are three bytes and thirty pixels, so a
    // mark measured in bytes would land at a third of the right offset. Two
    // characters in, the mark starts 60px along.
    const Recorder out = draw("中文句子", 800, 2, 2);
    check(out.texts.size() == 1, "four hanzi fit on one line at 800px");
    check(out.rules.size() == 1 && out.rules[0].x - out.texts[0].x == 60,
          "the mark is offset by glyph width, not by byte count");
    check(out.rules.size() == 1 && out.rules[0].width == 60, "and covers two full-width glyphs");
  }

  // --- degenerate input must not spin --------------------------------------
  check(lay("", 100).lines.empty(), "an empty string produces no lines");
  check(lay(nullptr, 100).lines.empty(), "a null string produces no lines");
  {
    // A stray UTF-8 continuation byte. utf8Length never returns zero, so this
    // has to terminate; before that rule it was an infinite loop.
    const Laid out = lay("a\x80\x80z", 1000);
    check(out.lines.size() == 1, "a malformed sequence still lays out");
  }
  {
    // More text than the line buffer can hold. The buffer is the caller's and
    // wrapText must respect it rather than writing past the end.
    std::string huge(2000, 'x');
    char small[16];
    int emitted = 0;
    int longest = 0;
    study::wrapText(huge.c_str(), 100000, small, sizeof(small), fakeWidth, [&](const study::WrappedLine& line) {
      ++emitted;
      if (line.bytes > longest) longest = line.bytes;
    });
    check(longest < static_cast<int>(sizeof(small)), "no line overruns the caller's buffer");
    check(emitted > 0, "an over-long run still produces lines");
    check(emitted >= 2000 / static_cast<int>(sizeof(small)), "and produces all of them");
  }
}

}  // namespace

int main() {
  std::printf("StudyText\n");
  run();
  std::printf("%s %d checks, %d failed\n", failures ? "FAIL" : "PASS", checks, failures);
  return failures ? 1 : 0;
}

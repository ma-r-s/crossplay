// A card longer than its screen, read a page at a time (card #761, report box
// #669).
//
// Two halves. The page arithmetic in StudyText.h -- where a line lands, that it
// lands whole, that a reading stays with its base, that every field of a face
// shares one run of pages -- driven through the same drawWrappedMarked the
// device calls, against a recorder. And the input rule in StudyPaging.h --
// what a side key, a swipe and a tap do on each face -- walked over every
// state, because the rule that matters most is a negative one: nothing that
// turns a page may ever grade a card.
//
// Fake metrics, like test_text.cpp: ten pixels per Latin codepoint, a 20px
// line, a 10px reading. The questions are about where lines go, not how wide
// a glyph is.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../src/apps_local/study/StudyPaging.h"
#include "../../src/apps_local/study/StudyText.h"

namespace {

int checks = 0;
int failures = 0;

void check(const bool condition, const char* label, const int line) {
  ++checks;
  if (!condition) {
    ++failures;
    std::printf("  FAIL (line %d): %s\n", line, label);
  }
}
#define CHECK(cond, label) check((cond), (label), __LINE__)

int fakeWidth(const char* text) {
  int width = 0;
  for (const char* p = text; *p != '\0';) {
    const char* at = p;
    if (study::nextCodepoint(p) == 0) break;
    width += (p - at) >= 3 ? 30 : 10;
  }
  return width;
}

struct Recorder {
  struct Text {
    int x = 0;
    int y = 0;
    std::string text;
    int font = 0;
  };
  struct Fill {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
  };
  mutable std::vector<Text> texts;
  mutable std::vector<Fill> fills;

  static constexpr int kRubyFont = 2;

  int getScreenWidth() const { return 480; }
  int getTextHeight(int fontId) const { return fontId == kRubyFont ? 10 : 20; }
  int getTextWidth(int fontId, const char* text) const {
    return fontId == kRubyFont ? fakeWidth(text) / 2 : fakeWidth(text);
  }
  void drawText(int fontId, int x, int y, const char* text, bool) const { texts.push_back({x, y, text, fontId}); }
  void fillRect(int x, int y, int width, int height, bool) const { fills.push_back({x, y, width, height}); }
};

std::string ruby(const char* base, const char* reading) {
  std::string out;
  out += study::kRubyStart;
  out += base;
  out += study::kRubySep;
  out += reading;
  out += study::kRubyEnd;
  return out;
}

// One field, drawn into a recorder through a flow. Returns the y the field
// ends at, as the card's layout threads it into the next field.
int field(Recorder& target, study::TextFlow& flow, const int y, const int maxWidth, const char* text,
          const bool measureOnly = false, const int rubyFont = 0, const int bottom = 1 << 30, bool* clipped = nullptr) {
  char line[256];
  char scratch[256];
  return study::drawWrappedMarked(target, 1, y, maxWidth, text, 0, 0, measureOnly, line, sizeof(line), scratch,
                                  rubyFont, bottom, clipped, &flow);
}

study::TextFlow pagedFlow(const int top, const int height, const int selected) {
  study::TextFlow flow;
  flow.pages.top = top;
  flow.pages.height = height;
  flow.pages.selected = selected;
  return flow;
}

// The text of every base-font line drawn, in order.
std::vector<std::string> lines(const Recorder& target) {
  std::vector<std::string> out;
  for (const auto& text : target.texts) {
    if (text.font != Recorder::kRubyFont) out.push_back(text.text);
  }
  return out;
}

void testOnePageFaceIsUnchanged() {
  // A face that fits draws exactly what it drew before pages existed: the
  // same strings at the same places, whether the flow is absent, unpaged, or
  // paged with room to spare.
  const char* text = "aaa bbb ccc ddd eee";
  Recorder plain;
  char line[256];
  char scratch[256];
  const int plainEnd =
      study::drawWrappedMarked(plain, 1, 100, 70, text, 0, 0, false, line, sizeof(line), scratch, 0, 1 << 30, nullptr);

  Recorder unpaged;
  study::TextFlow none;
  const int unpagedEnd = field(unpaged, none, 100, 70, text);

  Recorder roomy;
  study::TextFlow flow = pagedFlow(100, 500, 0);
  const int roomyEnd = field(roomy, flow, 100, 70, text);

  bool same = plain.texts.size() == unpaged.texts.size() && plain.texts.size() == roomy.texts.size();
  for (size_t i = 0; same && i < plain.texts.size(); ++i) {
    same = plain.texts[i].text == unpaged.texts[i].text && plain.texts[i].y == unpaged.texts[i].y &&
           plain.texts[i].x == unpaged.texts[i].x && plain.texts[i].text == roomy.texts[i].text &&
           plain.texts[i].y == roomy.texts[i].y && plain.texts[i].x == roomy.texts[i].x;
  }
  CHECK(plain.texts.size() == 3, "the face wraps to three lines");
  CHECK(same, "a face that fits draws the same lines at the same places through a flow");
  CHECK(plainEnd == unpagedEnd && plainEnd == roomyEnd, "and ends at the same y");
  CHECK(flow.pages.count() == 1, "a face that fits is one page");
}

void testLineEndIsTheLastLinesFoot() {
  // Whether a face needs pages is decided by where its last LINE ends, not by
  // the y its layout returns: the card adds a gap after a field whether or
  // not the next field has anything in it, so a face whose lines fit could
  // otherwise be paged by its own trailing gaps (cold review, card #761).
  Recorder target;
  study::TextFlow flow;
  int y = field(target, flow, 100, 30, "aaa bbb ccc");  // three lines, 100-160
  y += 6;
  y = field(target, flow, y, 30, "");  // an empty field: no line, only its gap
  y += 2;
  y = field(target, flow, y, 30, "");
  CHECK(y == 168, "the layout's y carries the gaps after the empty fields");
  CHECK(flow.lineEnd == 160, "lineEnd is the foot of the last line drawn, not the gaps after it");
}

void testPagesBreakOnWholeLines() {
  // A 50px page holds two 20px lines; the third would cross the foot, so it
  // starts the next page rather than be cut. Five one-word lines: 2 + 2 + 1.
  const char* text = "aaa bbb ccc ddd eee";
  study::TextFlow count = pagedFlow(100, 50, 0);
  Recorder measure;
  field(measure, count, 100, 30, text, true);
  CHECK(count.pages.count() == 3, "five lines at two a page are three pages");
  CHECK(measure.texts.empty(), "a measuring pass draws nothing");

  std::vector<std::string> seen;
  bool insidePage = true;
  bool sameRows = true;
  const std::vector<std::vector<std::string>> expected = {{"aaa", "bbb"}, {"ccc", "ddd"}, {"eee"}};
  for (int page = 0; page < 3; ++page) {
    Recorder target;
    study::TextFlow flow = pagedFlow(100, 50, page);
    field(target, flow, 100, 30, text);
    CHECK(lines(target) == expected[static_cast<size_t>(page)], "each page draws its own whole lines");
    for (size_t i = 0; i < target.texts.size(); ++i) {
      const auto& drawn = target.texts[i];
      // Every page starts at the same row, and no line reaches past its foot.
      if (drawn.y < 100 || drawn.y + 20 > 150) insidePage = false;
      if (drawn.y != 100 + static_cast<int>(i) * 20) sameRows = false;
      seen.push_back(drawn.text);
    }
  }
  CHECK(insidePage, "no line is drawn across the foot of its page");
  CHECK(sameRows, "every page starts at the top and steps a line at a time");
  CHECK(seen == (std::vector<std::string>{"aaa", "bbb", "ccc", "ddd", "eee"}),
        "across the pages every line is drawn exactly once, in order");
}

void testFieldsShareOnePageSequence() {
  // The card stacks fields by threading y: a headword, a 20px gap, a rule,
  // another 20px gap, then the sentence. Paged, the sentence must carry on
  // from where the headword stopped -- on its page, not on a page of its own
  // -- and the rule must land on the page of the line under it.
  //
  // Page: top 100, 130px tall, so page 0 is rows 100-229. Headword: three
  // lines (100-160). Gap to 180, rule at 180, gap to 200. Sentence: "xxx" at
  // 200 fits (it ends at 220); "yyy" at 220 would end at 240, past 230, so it
  // starts page 1.
  const auto drawFace = [](const int page, Recorder& target) {
    study::TextFlow flow = pagedFlow(100, 130, page);
    int y = field(target, flow, 100, 30, "aaa bbb ccc");
    y += 20;
    flow.pages.rule = y;
    y += 20;
    field(target, flow, y, 30, "xxx yyy");
    return flow.pages.count();
  };
  Recorder first;
  const int pages = drawFace(0, first);
  Recorder second;
  drawFace(1, second);
  CHECK(pages == 2, "two fields that overrun one page share two pages");
  CHECK(lines(first) == (std::vector<std::string>{"aaa", "bbb", "ccc", "xxx"}),
        "the second field starts on the page the first one ended on");
  CHECK(lines(second) == (std::vector<std::string>{"yyy"}), "and runs onto the next");
  CHECK(first.fills.size() == 1 && first.fills[0].y == 180 && first.fills[0].width == 30 &&
            first.fills[0].x == (480 - 30) / 2,
        "the rule is drawn once, on the page holding the line under it, a line's width");
  CHECK(second.fills.empty(), "and not again on the next page");
  bool yyyAtTop = false;
  for (const auto& text : second.texts) yyyAtTop = yyyAtTop || (text.text == "yyy" && text.y == 100);
  CHECK(yyyAtTop, "the line that moved starts the next page at its top");

  // A rule whose line moved to the next page is dropped, not left alone at a
  // page foot dividing nothing from nothing.
  const auto drawOrphan = [](const int page, Recorder& target) {
    study::TextFlow flow = pagedFlow(100, 130, page);
    field(target, flow, 100, 30, "aaa bbb ccc");  // ends at 160
    flow.pages.rule = 170;                        // page 0
    field(target, flow, 220, 30, "zzz");          // would end at 240: moves to page 1
    return flow.pages.count();
  };
  Recorder orphanFirst;
  const int orphanPages = drawOrphan(0, orphanFirst);
  Recorder orphanSecond;
  drawOrphan(1, orphanSecond);
  CHECK(orphanPages == 2 && lines(orphanSecond) == (std::vector<std::string>{"zzz"}),
        "the line after the rule moved to the next page");
  CHECK(orphanFirst.fills.empty() && orphanSecond.fills.empty(),
        "a rule separated from its line by a page break is not drawn on either page");

  // A gap between two fields that carries y over a page foot must not leave
  // that page blank: 50px pages, two lines end at 140, a 45px gap lands at
  // 185, 35px into page 1, where a 20px line would cross its foot. It starts
  // page 1 instead of skipping to page 2.
  study::TextFlow gap = pagedFlow(100, 50, 1);
  Recorder gapped;
  field(gapped, gap, 100, 30, "aaa bbb");
  field(gapped, gap, 185, 30, "ccc");
  CHECK(gap.pages.count() == 2, "a gap over a page foot does not add a blank page");
  CHECK(lines(gapped) == (std::vector<std::string>{"ccc"}) && gapped.texts.size() == 1 && gapped.texts[0].y == 100,
        "the line after the gap starts the page the gap ran onto");
}

void testFuriganaStaysWithItsBase() {
  // Two plain lines take 40px of a 65px page. A third plain line would fit
  // (60px); the third line here carries a reading, which makes it 30px tall
  // (70px), so it cannot fit, and it moves to the next page WHOLE -- the
  // reading and the base it reads, together. Placed at its plain height it
  // would stay, with its base hanging 5px past the foot.
  const std::string text = std::string("aa\nbb\n") + ruby("\xe6\xbc\xa2", "\xe3\x81\x8b\xe3\x82\x93");
  study::TextFlow count = pagedFlow(100, 65, 0);
  Recorder measure;
  field(measure, count, 100, 300, text.c_str(), true, Recorder::kRubyFont);
  CHECK(count.pages.count() == 2, "a ruby line that does not fit makes a second page");

  Recorder first;
  study::TextFlow flow0 = pagedFlow(100, 65, 0);
  field(first, flow0, 100, 300, text.c_str(), false, Recorder::kRubyFont);
  Recorder second;
  study::TextFlow flow1 = pagedFlow(100, 65, 1);
  field(second, flow1, 100, 300, text.c_str(), false, Recorder::kRubyFont);

  bool rubyOnFirst = false;
  bool firstInside = true;
  for (const auto& drawn : first.texts) {
    rubyOnFirst = rubyOnFirst || drawn.font == Recorder::kRubyFont;
    if (drawn.y + (drawn.font == Recorder::kRubyFont ? 10 : 20) > 165) firstInside = false;
  }
  CHECK(lines(first) == (std::vector<std::string>{"aa", "bb"}), "the first page holds only the plain lines");
  CHECK(!rubyOnFirst, "no reading is left at the foot of the first page");
  CHECK(firstInside, "nothing on the first page hangs past its foot");

  int rubyY = -1;
  int baseY = -1;
  for (const auto& drawn : second.texts) {
    if (drawn.font == Recorder::kRubyFont) rubyY = drawn.y;
    if (drawn.font == 1 && drawn.text == "\xe6\xbc\xa2") baseY = drawn.y;
  }
  CHECK(rubyY == 100, "the reading starts the second page");
  CHECK(baseY == rubyY + 10, "with its base directly under it, on the same page");
  CHECK(baseY + 20 <= 165, "and the pair fits inside that page");
}

void testPagesPastAnyCap() {
  // Nothing about a page is stored: the arithmetic places it, so there is no
  // table to run out. 300 one-word lines at two a page are 150 pages, past
  // the 128 starts CrossNext's plain-text cache keeps, and the last one is
  // as reachable as the first.
  std::string text;
  for (int i = 0; i < 300; ++i) {
    char word[8];
    std::snprintf(word, sizeof(word), "w%03d", i);
    if (i > 0) text += '\n';
    text += word;
  }
  study::TextFlow count = pagedFlow(100, 50, 0);
  Recorder measure;
  field(measure, count, 100, 100, text.c_str(), true);
  CHECK(count.pages.count() == 150, "300 lines at two a page are 150 pages");

  Recorder last;
  study::TextFlow flow = pagedFlow(100, 50, 149);
  field(last, flow, 100, 100, text.c_str());
  CHECK(lines(last) == (std::vector<std::string>{"w298", "w299"}), "page 150 draws the last two lines");

  int drawn = 0;
  for (int page = 0; page < 150; ++page) {
    Recorder target;
    study::TextFlow each = pagedFlow(100, 50, page);
    field(target, each, 100, 100, text.c_str());
    drawn += static_cast<int>(target.texts.size());
  }
  CHECK(drawn == 300, "every line is drawn on exactly one of the 150 pages");
  // The whole of the paging state: a few ints, never a copy of the text.
  CHECK(sizeof(study::TextFlow) <= 32, "paging keeps a handful of ints, not text");
}

void testStressLongAnswerAndLongWord() {
  // A very long answer: 150 words, far past one page, every word exactly once
  // across the pages, in order, and no line wider than the line or past a foot.
  std::string answer;
  std::vector<std::string> words;
  for (int i = 0; i < 150; ++i) {
    char word[12];
    std::snprintf(word, sizeof(word), "word%d", i);
    words.emplace_back(word);
    if (i > 0) answer += ' ';
    answer += word;
  }
  study::TextFlow count = pagedFlow(100, 200, 0);
  Recorder measure;
  field(measure, count, 100, 200, answer.c_str(), true);
  const int pages = count.pages.count();
  CHECK(pages > 3, "a 150-word answer at 200px a line runs to several pages");
  std::vector<std::string> seen;
  bool narrow = true;
  bool inside = true;
  for (int page = 0; page < pages; ++page) {
    Recorder target;
    study::TextFlow flow = pagedFlow(100, 200, page);
    field(target, flow, 100, 200, answer.c_str());
    for (const auto& drawn : target.texts) {
      if (fakeWidth(drawn.text.c_str()) > 200) narrow = false;
      if (drawn.y < 100 || drawn.y + 20 > 300) inside = false;
      size_t at = 0;
      while (at < drawn.text.size()) {
        const size_t space = drawn.text.find(' ', at);
        const size_t end = space == std::string::npos ? drawn.text.size() : space;
        if (end > at) seen.push_back(drawn.text.substr(at, end - at));
        at = end + 1;
      }
    }
  }
  CHECK(seen == words, "every word of a long answer is drawn once, in order, across its pages");
  CHECK(narrow, "no line of it is wider than the line");
  CHECK(inside, "no line of it crosses the foot of a page");

  // A very long unbroken word. Without breakLong it overhangs, as it always
  // did, and says so; with it, it is cut into lines that each fit and that
  // put the whole word back together.
  const std::string word(100, 'x');  // 1000px against a 200px line
  {
    Recorder target;
    study::TextFlow flow;
    field(target, flow, 100, 200, word.c_str());
    CHECK(flow.overlong, "a word wider than the line is reported");
    CHECK(target.texts.size() == 1 && target.texts[0].text == word,
          "and, unbroken, is drawn whole on one line as before");
  }
  {
    Recorder target;
    study::TextFlow flow;
    flow.breakLong = true;
    field(target, flow, 100, 200, (word + " tail").c_str());
    std::string joined;
    bool fits = true;
    for (const auto& drawn : target.texts) {
      if (fakeWidth(drawn.text.c_str()) > 200) fits = false;
      if (drawn.text != "tail") joined += drawn.text;
    }
    CHECK(flow.overlong, "a broken word is still reported");
    CHECK(target.texts.size() == 6, "broken at the width it is five full lines, then the rest of the text");
    CHECK(fits, "every piece of it fits the line");
    CHECK(joined == word, "and the pieces are the whole word, nothing lost or doubled");
  }
  {
    // Longer than the 256-byte line buffer as well, and paged.
    const std::string huge(600, 'y');
    study::TextFlow count600 = pagedFlow(100, 100, 0);
    count600.breakLong = true;
    Recorder measure600;
    field(measure600, count600, 100, 200, huge.c_str(), true);
    const int hugePages = count600.pages.count();
    std::string joined;
    for (int page = 0; page < hugePages; ++page) {
      Recorder target;
      study::TextFlow flow = pagedFlow(100, 100, page);
      flow.breakLong = true;
      field(target, flow, 100, 200, huge.c_str());
      for (const auto& drawn : target.texts) joined += drawn.text;
    }
    CHECK(hugePages == 6, "a 600-character word is 30 lines, six pages of five");
    CHECK(joined == huge, "and paged, it is all there exactly once");
  }
  {
    // A sentence of ordinary words reports nothing: the smaller sizes are for
    // the cards that need them.
    study::TextFlow flow;
    Recorder target;
    field(target, flow, 100, 200, answer.c_str(), true);
    CHECK(!flow.overlong, "ordinary words are not overlong");
  }
}

using study::CardAction;
using study::CardInput;

study::CardView view(const bool answer, const int page, const int pages) {
  study::CardView out;
  out.answer = answer;
  out.page = page;
  out.pages = pages;
  return out;
}

void testButtonsPageThenReveal() {
  // A three-page question: Down, Down, Down is page 2, page 3, the answer.
  study::CardView state = view(false, 0, 3);
  study::CardOutcome out = study::routeCardInput(state, CardInput::KeyNext);
  CHECK(out.action == CardAction::Turn && out.page == 1, "the first press turns to page 2");
  state.page = out.page;
  out = study::routeCardInput(state, CardInput::KeyNext);
  CHECK(out.action == CardAction::Turn && out.page == 2, "the second turns to page 3");
  state.page = out.page;
  out = study::routeCardInput(state, CardInput::KeyNext);
  CHECK(out.action == CardAction::Reveal, "the press past the last question page reveals");
  CHECK(out.page == 0, "and the answer opens on its first page");

  // Up walks back, and on the first page of a long question does nothing.
  out = study::routeCardInput(view(false, 2, 3), CardInput::KeyPrevious);
  CHECK(out.action == CardAction::Turn && out.page == 1, "Up turns back a page");
  out = study::routeCardInput(view(false, 0, 3), CardInput::KeyPrevious);
  CHECK(out.action == CardAction::None, "Up on a long question's first page does nothing");

  // A one-page question reveals on either key, as a tap always did.
  CHECK(study::routeCardInput(view(false, 0, 1), CardInput::KeyNext).action == CardAction::Reveal,
        "a one-page question reveals on Down");
  CHECK(study::routeCardInput(view(false, 0, 1), CardInput::KeyPrevious).action == CardAction::Reveal, "and on Up");

  // On the answer the keys only page, and stop at either end.
  out = study::routeCardInput(view(true, 0, 2), CardInput::KeyNext);
  CHECK(out.action == CardAction::Turn && out.page == 1, "Down pages the answer");
  out = study::routeCardInput(view(true, 1, 2), CardInput::KeyNext);
  CHECK(out.action == CardAction::None && out.page == 1, "Down on the answer's last page does nothing");
  CHECK(study::routeCardInput(view(true, 0, 1), CardInput::KeyNext).action == CardAction::None,
        "Down on a one-page answer does nothing");
  CHECK(study::routeCardInput(view(true, 0, 2), CardInput::KeyPrevious).action == CardAction::None,
        "Up on the answer's first page does nothing");

  // Swipes and the pager page; they never reveal, even off the last page.
  CHECK(study::routeCardInput(view(false, 0, 2), CardInput::SwipeNext).page == 1, "a swipe up turns forward");
  CHECK(study::routeCardInput(view(false, 1, 2), CardInput::SwipePrevious).page == 0, "a swipe down turns back");
  CHECK(study::routeCardInput(view(false, 1, 2), CardInput::SwipeNext).action == CardAction::None,
        "a swipe on the question's last page does not reveal");
  CHECK(study::routeCardInput(view(false, 1, 2), CardInput::PagerNext).action == CardAction::None,
        "nor does the pager's arrow");
  CHECK(study::routeCardInput(view(false, 0, 1), CardInput::SwipeNext).action == CardAction::None,
        "a swipe on a one-page question does nothing");
}

void testNoPageTurnEverGrades() {
  // Walk every state the card view can be in and every input that turns
  // pages. None of them may grade, undo or open the photograph; only a side
  // key on the question may reveal; a turn moves exactly one page and stays
  // inside the face.
  const CardInput turning[] = {CardInput::KeyNext,       CardInput::KeyPrevious, CardInput::SwipeNext,
                               CardInput::SwipePrevious, CardInput::PagerNext,   CardInput::PagerPrevious,
                               CardInput::Pager};
  int walked = 0;
  bool neverGrades = true;
  bool revealsOnlyByKey = true;
  bool turnsStayInside = true;
  for (int answer = 0; answer < 2; ++answer) {
    for (int pages = 1; pages <= 4; ++pages) {
      for (int page = 0; page < pages; ++page) {
        for (int flags = 0; flags < 4; ++flags) {
          study::CardView state = view(answer == 1, page, pages);
          state.canUndo = (flags & 1) != 0;
          state.hasPhoto = (flags & 2) != 0;
          for (const CardInput input : turning) {
            for (int slot = 0; slot < 4; ++slot) {
              const study::CardOutcome out = study::routeCardInput(state, input, slot);
              ++walked;
              if (out.action == CardAction::Grade || out.action == CardAction::Undo ||
                  out.action == CardAction::Photo || out.rating != 0) {
                neverGrades = false;
              }
              const bool key = input == CardInput::KeyNext || input == CardInput::KeyPrevious;
              if (out.action == CardAction::Reveal && (answer == 1 || !key)) revealsOnlyByKey = false;
              if (out.action == CardAction::Turn &&
                  (out.page < 0 || out.page >= pages || (out.page - page != 1 && page - out.page != 1))) {
                turnsStayInside = false;
              }
            }
          }
        }
      }
    }
  }
  CHECK(walked == 2 * 10 * 4 * 7 * 4, "every state was walked");
  CHECK(neverGrades, "no key, swipe or pager input ever grades, undoes or opens the photo");
  CHECK(revealsOnlyByKey, "only a side key on the question reveals");
  CHECK(turnsStayInside, "a turn moves one page and stays inside the face");
}

void testTapsKeepTheirMeaning() {
  // The footer grades on the answer, a quarter each, Again to Easy.
  for (int slot = 0; slot < 4; ++slot) {
    const study::CardOutcome out = study::routeCardInput(view(true, 1, 3), CardInput::Footer, slot);
    CHECK(out.action == CardAction::Grade && out.rating == slot + 1, "a footer quarter grades its rating");
  }
  // On the question: UNDO's quarter undoes, the rest reveals.
  study::CardView question = view(false, 1, 2);
  question.canUndo = true;
  CHECK(study::routeCardInput(question, CardInput::Footer, 0).action == CardAction::Undo,
        "UNDO's quarter undoes when there is something to undo");
  CHECK(study::routeCardInput(question, CardInput::Footer, 2).action == CardAction::Reveal,
        "the rest of the question footer reveals");
  question.canUndo = false;
  CHECK(study::routeCardInput(question, CardInput::Footer, 0).action == CardAction::Reveal,
        "with nothing to undo, the first quarter reveals too");
  // The body: reveals the question, opens the answer's photograph.
  CHECK(study::routeCardInput(view(false, 1, 2), CardInput::Body).action == CardAction::Reveal,
        "a tap on a question reveals, from any page");
  study::CardView photo = view(true, 0, 2);
  photo.hasPhoto = true;
  CHECK(study::routeCardInput(photo, CardInput::Body).action == CardAction::Photo,
        "a tap on an answer with a photograph opens it");
  CHECK(study::routeCardInput(view(true, 0, 2), CardInput::Body).action == CardAction::None,
        "a tap on an answer without one does nothing");
  CHECK(study::routeCardInput(photo, CardInput::Pager).action == CardAction::None,
        "the pager between its arrows takes the tap and does nothing");
}

}  // namespace

int main() {
  std::printf("StudyPaging\n");
  testOnePageFaceIsUnchanged();
  testLineEndIsTheLastLinesFoot();
  testPagesBreakOnWholeLines();
  testFieldsShareOnePageSequence();
  testFuriganaStaysWithItsBase();
  testPagesPastAnyCap();
  testStressLongAnswerAndLongWord();
  testButtonsPageThenReveal();
  testNoPageTurnEverGrades();
  testTapsKeepTheirMeaning();
  std::printf("%s %d checks, %d failed\n", failures ? "FAIL" : "PASS", checks, failures);
  return failures ? 1 : 0;
}

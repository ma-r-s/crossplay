// The note format, checked without a panel.
//
// The claim under most scrutiny is that a tick changes ONE BYTE and leaves the
// rest of the file alone. That is not a property to sample; it is the reason
// the format is strict, so the test holds it to account directly: it ticks
// every task in a document full of awkward prose and asserts the whole buffer
// is byte-identical apart from the single mark.

#include <cstdio>
#include <string>
#include <vector>

#include "NotesCore.h"

using namespace notes;

static int checks = 0;
static int failures = 0;

#define CHECK(cond)                                               \
  do {                                                            \
    ++checks;                                                     \
    if (!(cond)) {                                                \
      ++failures;                                                 \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                             \
  } while (0)

namespace {

size_t differingBytes(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return static_cast<size_t>(-1);
  size_t n = 0;
  for (size_t i = 0; i < a.size(); i++) {
    if (a[i] != b[i]) n++;
  }
  return n;
}

void testWhatIsATask() {
  const std::string doc =
      "Shopping\n"
      "- [ ] Milk\n"
      "- [x] Eggs\n"
      "- [X] Bread\n"
      "  - [ ] Indented is still a task\n"
      "* [ ] Star bullet\n"
      "+ [ ] Plus bullet\n"
      "- [] No space is prose\n"
      "-[ ] No bullet space is prose\n"
      "- [ ]\n"
      "- [x]tra credit is prose\n"
      "Just a sentence with - [ ] inside it\n";

  const std::vector<Line> lines = parse(doc);
  CHECK(!lines[0].isTask);
  CHECK(lines[1].isTask && !lines[1].checked);
  CHECK(lines[2].isTask && lines[2].checked);
  CHECK(lines[3].isTask && lines[3].checked);  // capital X
  CHECK(lines[4].isTask && !lines[4].checked);
  CHECK(lines[5].isTask);
  CHECK(lines[6].isTask);
  CHECK(!lines[7].isTask);  // "- [] "
  CHECK(!lines[8].isTask);  // "-[ ] "
  CHECK(lines[9].isTask);   // a box and nothing else is an empty task
  CHECK(textOf(doc, lines[9]).empty());
  CHECK(!lines[10].isTask);  // "- [x]tra"
  CHECK(!lines[11].isTask);  // a box mid-sentence is not a task

  CHECK(textOf(doc, lines[1]) == "Milk");
  CHECK(textOf(doc, lines[4]) == "Indented is still a task");

  // Every non-empty line counts, marker or not: the deck's tally is "how much
  // of this list is done", and a line somebody typed on their phone is part of
  // the list whether or not it arrived with a marker on it.
  const Counts c = counts(lines);
  CHECK(c.total == 12);
  CHECK(c.done == 2);
  // Blank lines are not items, and neither is the trailing empty line a file
  // ending in a newline produces.
  CHECK(counts(parse("a\n\nb\n")).total == 2);
  CHECK(counts(parse("")).total == 0);
}

void testATickIsOneByte() {
  const std::string original =
      "# Packing\n"
      "\n"
      "Ferry leaves at 07:40 -- do not forget the tickets [in the drawer].\n"
      "- [ ] Passport\n"
      "- [x] Charger\n"
      "\tTabbed prose with trailing spaces   \n"
      "- [ ] Toothbrush\n";

  std::string doc = original;
  std::vector<Line> lines = parse(doc);

  for (Line& line : lines) {
    if (!line.isTask) continue;
    const std::string before = doc;
    const bool wasChecked = line.checked;
    CHECK(toggle(doc, line));
    CHECK(line.checked != wasChecked);
    CHECK(differingBytes(before, doc) == 1);
  }

  // Ticking everything twice must return the file to exactly what it was,
  // including the tab, the double spaces and the square brackets in the prose.
  for (Line& line : lines) {
    if (line.isTask) toggle(doc, line);
  }
  CHECK(doc == original);

  // Prose cannot be ticked, and a failed toggle changes nothing.
  std::string untouched = doc;
  Line prose = parse(doc)[2];
  CHECK(!toggle(doc, prose));
  CHECK(doc == untouched);
}

void testCoercing() {
  // What a person types on their phone, with no syntax at all.
  std::string plain = "Milk\nEggs\nBread flour\n";
  CHECK(coerceToList(plain));
  CHECK(plain == "- [ ] Milk\n- [ ] Eggs\n- [ ] Bread flour\n");
  // And every one of them is now tickable, which is the whole point.
  CHECK(counts(parse(plain)).total == 3);

  // A file that is already a list is left BYTE FOR BYTE alone, ticks included:
  // a save from the phone must not disturb what was ticked on the device.
  std::string already = "- [x] Milk\n- [ ] Eggs\n";
  CHECK(!coerceToList(already));
  CHECK(already == "- [x] Milk\n- [ ] Eggs\n");

  // Mixed, which is what an edit of an existing list looks like.
  std::string mixed = "- [x] Milk\nLemons\n";
  CHECK(coerceToList(mixed));
  CHECK(mixed == "- [x] Milk\n- [ ] Lemons\n");

  // A blank line stays blank. Somebody who pressed return twice meant a gap,
  // not a thing to do, and an empty tick box is a hole in the list.
  std::string gap = "Milk\n\nEggs\n";
  CHECK(coerceToList(gap));
  CHECK(gap == "- [ ] Milk\n\n- [ ] Eggs\n");

  // CRLF survives: the phone is not the only thing that writes this file.
  std::string crlf = "Milk\r\nEggs\r\n";
  CHECK(coerceToList(crlf));
  CHECK(crlf == "- [ ] Milk\r\n- [ ] Eggs\r\n");

  // The near-misses the parser rejects are coerced rather than left as a second
  // kind of line. "- [] milk" is not a marker, so it becomes one.
  std::string nearly = "- [] milk\n- [x]tra\n";
  CHECK(coerceToList(nearly));
  CHECK(counts(parse(nearly)).total == 2);

  std::string empty;
  CHECK(!coerceToList(empty));
  CHECK(empty.empty());
}

void testKind() {
  // One tick box anywhere makes the whole file a list; the kind is never a
  // property of the line.
  CHECK(kindOf(parse("- [ ] Milk\nEggs\n")) == Kind::List);
  CHECK(kindOf(parse("Flour 500g\nWater 375g\n")) == Kind::Page);
  CHECK(kindOf(parse("Notes about the flat\n- [x] Ask about the boiler\n")) == Kind::List);

  // An empty note has no evidence, so the caller's choice at creation decides,
  // and the first line written settles it for good.
  CHECK(kindOf(parse(""), true) == Kind::List);
  CHECK(kindOf(parse(""), false) == Kind::Page);
  CHECK(kindOf(parse("\n\n"), false) == Kind::Page);

  // The tally only belongs on a list, so the count of MARKED lines is what the
  // deck asks for -- not the count of lines, which a page also has.
  const Counts page = counts(parse("Flour\nWater\n"));
  CHECK(page.total == 2);
  CHECK(page.marked == 0);
  const Counts list = counts(parse("- [x] Milk\n- [ ] Eggs\n"));
  CHECK(list.marked == 2);
  CHECK(list.done == 1);
}

void testSwitchingKind() {
  // A note becomes a list and back, and the round trip keeps the words. This is
  // the escape hatch for a file whose kind was inferred wrong -- a shopping
  // list typed as plain lines on a computer opens as a note, and one tap fixes
  // it without anybody editing syntax.
  std::string doc = "Milk\nEggs\n";
  CHECK(coerceToList(doc));
  CHECK(kindOf(parse(doc)) == Kind::List);
  CHECK(stripMarkers(doc));
  CHECK(doc == "Milk\nEggs\n");
  CHECK(kindOf(parse(doc)) == Kind::Page);

  // Ticks are lost on the way to a note, which is the honest outcome: a note
  // has nothing to be done with.
  std::string ticked = "- [x] Milk\n- [ ] Eggs\n";
  CHECK(stripMarkers(ticked));
  CHECK(ticked == "Milk\nEggs\n");

  // Nothing to strip is not a change, and must not rewrite the file.
  std::string plain = "Milk\r\nEggs\r\n";
  CHECK(!stripMarkers(plain));
  CHECK(plain == "Milk\r\nEggs\r\n");
}

void testClearing() {
  std::string doc =
      "Shopping\n"
      "- [x] Milk\n"
      "- [ ] Bread flour\n"
      "- [x] Eggs\n"
      "Remember the deposit\n";

  const std::vector<std::string> removed = clearChecked(doc);
  CHECK(removed.size() == 2);
  CHECK(removed[0] == "Milk");
  CHECK(removed[1] == "Eggs");
  CHECK(doc ==
        "Shopping\n"
        "- [ ] Bread flour\n"
        "Remember the deposit\n");

  // Prose is never removed, whatever it says.
  std::string prose = "- [x] done\nnot a task\n";
  clearChecked(prose);
  CHECK(prose == "not a task\n");

  // Clearing a note with nothing ticked is a no-op, not a reformat.
  std::string same = "- [ ] a\r\nplain\r\n";
  const std::vector<std::string> none = clearChecked(same);
  CHECK(none.empty());
  CHECK(same == "- [ ] a\r\nplain\r\n");

  // CRLF survives a clear. Files arrive from desktop editors.
  std::string crlf = "- [x] gone\r\n- [ ] stays\r\n";
  clearChecked(crlf);
  CHECK(crlf == "- [ ] stays\r\n");
}

}  // namespace

int main() {
  testWhatIsATask();
  testATickIsOneByte();
  testKind();
  testCoercing();
  testSwitchingKind();
  testClearing();

  std::printf("%s  notes: %d checks, %d failed\n", failures ? "FAIL" : "ok  ", checks, failures);
  return failures == 0 ? 0 : 1;
}

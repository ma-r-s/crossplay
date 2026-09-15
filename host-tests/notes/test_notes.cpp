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

  const Counts c = counts(lines);
  CHECK(c.total == 7);
  CHECK(c.done == 2);
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

void testStripHeading() {
  CHECK(stripHeading("# Bread recipe") == "Bread recipe");
  CHECK(stripHeading("### Deep heading") == "Deep heading");
  CHECK(stripHeading("  ## Indented") == "Indented");
  CHECK(stripHeading("no hashes here") == "no hashes here");
  // Hashes with nothing after them ARE the content; stripping would empty the line.
  CHECK(stripHeading("###") == "###");
  CHECK(stripHeading("#  ") == "#  ");
  CHECK(stripHeading("") == "");
  // Only a LEADING run counts. A hash mid-line is a word.
  CHECK(stripHeading("aisle #4") == "aisle #4");
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
  testStripHeading();
  testClearing();

  std::printf("notes: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}

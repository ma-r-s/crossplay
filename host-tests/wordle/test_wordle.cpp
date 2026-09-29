// Wordle's rules and data formats, checked without a panel.
//
// Scoring is the part people argue about: a repeated letter is marked at most
// as many times as the answer holds it, exact matches first. The cases below
// are the ones that separate a correct scorer from a plausible one.

#include <cstdio>
#include <cstring>
#include <string>

#include "WordleCore.h"

using namespace wordle;

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

// "AP.C." style: C correct, P present, . absent.
std::string marks(const char* guess, const char* answer) {
  Mark out[kLetters];
  score(guess, answer, out);
  std::string s;
  for (const Mark m : out) s += m == Mark::Correct ? 'C' : m == Mark::Present ? 'P' : '.';
  return s;
}

void testScoring() {
  CHECK(marks("CIGAR", "CIGAR") == "CCCCC");
  CHECK(marks("CRANE", "PLANT") == "..CC.");
  CHECK(marks("TULIP", "PLANT") == "P.P.P");
  // A repeated guess letter against one copy in the answer: the exact match
  // takes it, and the other copies are absent, not present.
  CHECK(marks("GEESE", "THOSE") == "...CC");
  // Two copies in the answer, one matched exactly: the other copy can still
  // mark one more.
  CHECK(marks("KEBAB", "ABBEY") == ".PCPP");
  // Two copies in the answer, none matched exactly: two presents, not three.
  CHECK(marks("ERASE", "SPEED") == "P..PP");
  CHECK(marks("EERIE", "SPEED") == "PP...");
  // Nothing in common.
  CHECK(marks("QUICK", "PLANT") == ".....");
}

void testKeys() {
  Mark key = Mark::Empty;
  mergeKey(key, Mark::Absent);
  CHECK(key == Mark::Absent);
  mergeKey(key, Mark::Present);
  CHECK(key == Mark::Present);
  mergeKey(key, Mark::Correct);
  CHECK(key == Mark::Correct);
  // Never climbs down: a later guess placing the letter wrong does not undo
  // that an earlier one placed it right.
  mergeKey(key, Mark::Present);
  mergeKey(key, Mark::Absent);
  CHECK(key == Mark::Correct);
}

void testDates() {
  CHECK(dayIndex(2021, 6, 19) == 0);
  CHECK(dayIndex(2021, 6, 20) == 1);
  // NYT's own days_since_launch for these two print dates.
  CHECK(dayIndex(2026, 9, 27) == 1926);
  CHECK(dayIndex(2026, 9, 28) == 1927);
  CHECK(dayIndex(2021, 6, 18) == -1);
  CHECK(dayIndex(2024, 3, 1) - dayIndex(2024, 2, 28) == 2);  // leap year
  CHECK(dayIndex(2025, 3, 1) - dayIndex(2025, 2, 28) == 1);

  bool roundTrips = true;
  for (int i = -10; i < 5000; ++i) {
    int y = 0;
    int m = 0;
    int d = 0;
    dateOfDay(i, y, m, d);
    if (dayIndex(y, m, d) != i) roundTrips = false;
  }
  CHECK(roundTrips);
  int y = 0;
  int m = 0;
  int d = 0;
  dateOfDay(0, y, m, d);
  CHECK(y == 2021 && m == 6 && d == 19);
}

void testArchiveLines() {
  int day = -1;
  char w[kLetters];
  CHECK(parseAnswerLine("2021-06-19 CIGAR", 16, day, w) && day == 0 && std::memcmp(w, "CIGAR", 5) == 0);
  const char* crlf = "2021-06-20 rebut\r\n";
  CHECK(parseAnswerLine(crlf, std::strlen(crlf), day, w) && day == 1 && std::memcmp(w, "REBUT", 5) == 0);
  CHECK(!parseAnswerLine("2021-06-19 CIGARS", 17, day, w));
  CHECK(!parseAnswerLine("2021-06-19 CIG4R", 16, day, w));
  CHECK(!parseAnswerLine("2021-6-19 CIGAR", 15, day, w));
  CHECK(!parseAnswerLine("2021-13-19 CIGAR", 16, day, w));
  CHECK(!parseAnswerLine("", 0, day, w));
}

void testDailyJson() {
  int day = -1;
  char w[kLetters];
  const std::string body =
      "{\"id\":1848,\"solution\":\"sloop\",\"print_date\":\"2026-09-27\",\"days_since_launch\":1926,"
      "\"editor\":\"Tracy Bennett\"}";
  CHECK(parseDailyJson(body, day, w) && day == 1926 && std::memcmp(w, "SLOOP", 5) == 0);
  CHECK(!parseDailyJson("{\"status\":\"ERROR\"}", day, w));
  CHECK(!parseDailyJson("{\"solution\":\"sloops\",\"print_date\":\"2026-09-27\"}", day, w));
  CHECK(!parseDailyJson("", day, w));
}

void testAnswerFile() {
  std::string file;
  CHECK(lastKnownDay(file) == -1);
  putAnswer(file, 0, "CIGAR");
  putAnswer(file, 3, "HUMPH");
  CHECK(file == "CIGAR..........HUMPH");
  char w[kLetters];
  CHECK(answerFor(file, 0, w) && std::memcmp(w, "CIGAR", 5) == 0);
  CHECK(!answerFor(file, 1, w));  // a gap is not an answer
  CHECK(!answerFor(file, 4, w));  // past the end
  CHECK(!answerFor(file, -1, w));
  CHECK(lastKnownDay(file) == 3);
  putAnswer(file, 1, "REBUT");
  CHECK(file == "CIGARREBUT.....HUMPH");
  putAnswer(file, -1, "XXXXX");
  CHECK(file.size() == 20);
}

void testWordList() {
  const char list[] = "ABBEYCIGARKEBABPLANTSLOOP";
  const size_t count = 5;
  CHECK(isWord(list, count, "ABBEY"));
  CHECK(isWord(list, count, "SLOOP"));
  CHECK(isWord(list, count, "KEBAB"));
  CHECK(!isWord(list, count, "AAAAA"));
  CHECK(!isWord(list, count, "ZZZZZ"));
  CHECK(!isWord(list, count, "PLANS"));
  CHECK(!isWord(list, 0, "ABBEY"));
}

}  // namespace

int main() {
  testScoring();
  testKeys();
  testDates();
  testArchiveLines();
  testDailyJson();
  testAnswerFile();
  testWordList();
  std::printf("%s  wordle: %d checks, %d failed\n", failures ? "FAIL" : "ok  ", checks, failures);
  return failures == 0 ? 0 : 1;
}

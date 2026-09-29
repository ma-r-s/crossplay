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

bool anyWord(void*, const char* word) { return std::memcmp(word, "XXXXX", 5) != 0; }

void typeWord(Game& game, const char* word) {
  for (int i = 0; i < 5; ++i) game.type(word[i]);
}

void testGame() {
  Game game;
  game.start(1927, "PLANT");
  CHECK(!game.over() && game.guesses() == 0);
  // Typing stops at five, and erase takes one back.
  typeWord(game, "CRANES");
  CHECK(game.typed() == 5 && std::memcmp(game.typing(), "CRANE", 5) == 0);
  CHECK(game.erase() && game.typed() == 4);
  CHECK(game.submit(anyWord, nullptr) == Game::Submit::Short);
  CHECK(game.type('E'));
  // A word the list refuses stays in the row, and nothing is scored.
  game.erase();
  game.type('X');
  game.erase();
  CHECK(game.guesses() == 0);
  Game refused;
  refused.start(0, "PLANT");
  typeWord(refused, "XXXXX");
  CHECK(refused.submit(anyWord, nullptr) == Game::Submit::NotAWord);
  CHECK(refused.guesses() == 0 && refused.typed() == 5);

  game.type('E');
  CHECK(game.submit(anyWord, nullptr) == Game::Submit::Scored);
  CHECK(game.guesses() == 1 && game.typed() == 0);
  CHECK(game.key('A') == Mark::Correct && game.key('C') == Mark::Absent && game.key('Z') == Mark::Empty);
  typeWord(game, "TULIP");
  game.submit(anyWord, nullptr);
  CHECK(game.key('T') == Mark::Present);
  typeWord(game, "PLANT");
  CHECK(game.submit(anyWord, nullptr) == Game::Submit::Won);
  CHECK(game.over() && game.status() == Game::Status::Won && game.guesses() == 3);
  // Over is over: no typing, no erasing, no submitting.
  CHECK(!game.type('A') && !game.erase());
  CHECK(game.submit(anyWord, nullptr) == Game::Submit::Over);
  CHECK(game.key('T') == Mark::Correct);  // the key climbed when T was placed

  // Six misses lose, and the answer is still the answer.
  Game lost;
  lost.start(5, "PLANT");
  for (int i = 0; i < 5; ++i) {
    typeWord(lost, "CRANE");
    CHECK(lost.submit(anyWord, nullptr) == Game::Submit::Scored);
  }
  typeWord(lost, "CRANE");
  CHECK(lost.submit(anyWord, nullptr) == Game::Submit::Lost);
  CHECK(lost.status() == Game::Status::Lost && std::memcmp(lost.answer(), "PLANT", 5) == 0);
}

void testSave() {
  Game game;
  game.start(12, "PLANT");
  typeWord(game, "CRANE");
  game.submit(anyWord, nullptr);
  typeWord(game, "TULIP");
  game.submit(anyWord, nullptr);
  typeWord(game, "PL");  // the row being typed is not saved
  CHECK(game.save() == "PLANT CRANE TULIP");
  Game back;
  CHECK(back.load(12, game.save()));
  CHECK(back.day() == 12 && back.guesses() == 2 && back.typed() == 0);
  CHECK(back.marks(1)[0] == Mark::Present && back.key('A') == Mark::Correct);
  // A finished game loads finished.
  Game won;
  CHECK(won.load(3, "PLANT CRANE PLANT") && won.status() == Game::Status::Won);
  // Anything malformed is refused rather than half-loaded.
  CHECK(!back.load(1, ""));
  CHECK(!back.load(1, "PLAN"));
  CHECK(!back.load(1, "PLANT CRAN"));
  CHECK(!back.load(1, "PLANT CRANE,TULIP"));
  CHECK(!back.load(1, "PLANT CR4NE"));
  // Guesses after the game ended are not a game this device could have saved.
  CHECK(!back.load(1, "PLANT PLANT CRANE"));
}

void testRecord() {
  std::string res;
  CHECK(resultOf(res, 5) == 0);
  // Days 10-12 won on the day, day 13 won from the archive, 14 lost on the day,
  // 15-16 won on the day, 17 (today) not yet played.
  putResult(res, 10, kOnTheDay | 3);
  putResult(res, 11, kOnTheDay | 4);
  putResult(res, 12, kOnTheDay | 4);
  putResult(res, 13, 2);
  putResult(res, 14, kOnTheDay | kLost);
  putResult(res, 15, kOnTheDay | 1);
  putResult(res, 16, kOnTheDay | 6);
  putResult(res, 17, kOnTheDay | kStarted);
  CHECK(resultOf(res, 13) == 2);
  const Stats s = statsFor(res, 17);
  CHECK(s.played == 6);  // the archive win is not counted, nor today's open game
  CHECK(s.won == 5);
  CHECK(s.wins[0] == 1 && s.wins[2] == 1 && s.wins[3] == 2 && s.wins[5] == 1 && s.wins[1] == 0);
  CHECK(s.streak == 2);  // 15, 16; today still open does not break it
  // Winning today extends it; losing today ends it; a missed day ends it.
  std::string won = res;
  putResult(won, 17, kOnTheDay | 5);
  CHECK(statsFor(won, 17).streak == 3);
  std::string lostToday = res;
  putResult(lostToday, 17, kOnTheDay | kLost);
  CHECK(statsFor(lostToday, 17).streak == 0);
  CHECK(statsFor(res, 18).streak == 0);  // 17 was never finished
  CHECK(statsFor("", 100).streak == 0 && statsFor("", 100).played == 0);
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
  testGame();
  testSave();
  testRecord();
  std::printf("%s  wordle: %d checks, %d failed\n", failures ? "FAIL" : "ok  ", checks, failures);
  return failures == 0 ? 0 : 1;
}

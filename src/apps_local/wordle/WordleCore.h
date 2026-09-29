#pragma once

// Wordle's rules and data formats. Freestanding: no Arduino, no SD card, so
// host-tests/wordle checks every line of it on the Mac.

#include <cstddef>
#include <cstdint>
#include <string>

namespace wordle {

constexpr int kLetters = 5;
constexpr int kRows = 6;

// What a tile or key says about its letter. Empty and Typed belong to the row
// being written; Absent, Present and Correct to a submitted guess. The order
// is the order a key's mark can only climb.
enum class Mark : uint8_t { Empty, Typed, Absent, Present, Correct };

// The marks for one guess, with repeated letters handled the way the original
// does: exact matches first, then each remaining copy of a letter in the answer
// can mark at most one more copy in the guess. Both words are A-Z.
void score(const char* guess, const char* answer, Mark out[kLetters]);

// A key keeps the best thing any guess has said about its letter.
void mergeKey(Mark& key, Mark mark);

// Day 0 is 2021-06-19, the first puzzle. Dates are the proleptic Gregorian
// calendar; a date before day 0 comes back negative.
int dayIndex(int year, int month, int day);
void dateOfDay(int index, int& year, int& month, int& day);

// One line of the answer archive: "YYYY-MM-DD WORD", case-insensitive, CR and
// trailing spaces allowed. The word comes back upper case.
bool parseAnswerLine(const char* line, size_t length, int& dayOut, char wordOut[kLetters]);

// NYT's per-day JSON: reads "print_date" and "solution" and nothing else.
bool parseDailyJson(const std::string& body, int& dayOut, char wordOut[kLetters]);

// The answers file is five bytes per day from day 0, '.' for a day not known.
// Writing past the end pads with dots.
void putAnswer(std::string& file, int day, const char* word);
bool answerFor(const std::string& file, int day, char wordOut[kLetters]);
// The last day the file knows, or -1.
int lastKnownDay(const std::string& file);

// The guess list is sorted upper-case words, five bytes each, no separators.
// Binary search, so it can be read from memory without an index.
bool isWord(const char* list, size_t count, const char* word);

// One game: the answer, the guesses so far and the row being typed.
class Game {
 public:
  enum class Status : uint8_t { Playing, Won, Lost };
  enum class Submit : uint8_t { Short, NotAWord, Scored, Won, Lost, Over };

  void start(int day, const char* answer);
  int day() const { return day_; }
  const char* answer() const { return answer_; }
  Status status() const { return status_; }
  bool over() const { return status_ != Status::Playing; }
  int guesses() const { return count_; }
  const char* guess(int row) const { return rows_[row]; }
  const Mark* marks(int row) const { return marks_[row]; }
  Mark key(char letter) const { return keys_[letter - 'A']; }
  int typed() const { return typed_; }
  const char* typing() const { return typing_; }

  // False when there is no room or the game is over.
  bool type(char letter);
  bool erase();
  // `isWord` answers for a whole five-letter word. The row is kept as typed on
  // Short and NotAWord, so the player can correct it.
  Submit submit(bool (*isWord)(void* ctx, const char* word), void* ctx);

  // The guesses as text ("ANSWER GUESS GUESS"), and back. Marks and keys are
  // recomputed from the answer, so a save cannot disagree with the rules.
  std::string save() const;
  bool load(int day, const std::string& text);

 private:
  void apply(const char* guess);

  int day_ = -1;
  char answer_[kLetters + 1] = {};
  char rows_[kRows][kLetters + 1] = {};
  Mark marks_[kRows][kLetters] = {};
  Mark keys_[26] = {};
  char typing_[kLetters + 1] = {};
  int count_ = 0;
  int typed_ = 0;
  Status status_ = Status::Playing;
};

// Results, one byte per day from day 0: 0 never opened, 1-6 solved in that
// many, kLost, kStarted for opened and left. kOnTheDay is set when the result
// was reached on the puzzle's own date, which is what the record counts.
constexpr uint8_t kLost = 7;
constexpr uint8_t kStarted = 8;
constexpr uint8_t kOnTheDay = 0x80;
uint8_t resultOf(const std::string& results, int day);
void putResult(std::string& results, int day, uint8_t value);

struct Stats {
  int played = 0;
  int won = 0;
  int streak = 0;
  int wins[kRows] = {};  // wins[n] = solved in n + 1 guesses
};
// Only results reached on the day count, as on NYT's site. The streak runs
// back from `today`, or from yesterday while today is still unfinished.
Stats statsFor(const std::string& results, int today);

}  // namespace wordle

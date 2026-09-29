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

}  // namespace wordle

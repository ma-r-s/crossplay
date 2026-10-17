#include "WordleCore.h"

#include <cstring>

namespace wordle {

void score(const char* guess, const char* answer, Mark out[kLetters]) {
  int spare[26] = {};
  for (int i = 0; i < kLetters; ++i) {
    if (guess[i] == answer[i]) {
      out[i] = Mark::Correct;
    } else {
      out[i] = Mark::Absent;
      ++spare[answer[i] - 'A'];
    }
  }
  for (int i = 0; i < kLetters; ++i) {
    if (out[i] == Mark::Correct) continue;
    int& left = spare[guess[i] - 'A'];
    if (left > 0) {
      out[i] = Mark::Present;
      --left;
    }
  }
}

void mergeKey(Mark& key, const Mark mark) {
  if (static_cast<uint8_t>(mark) > static_cast<uint8_t>(key)) key = mark;
}

namespace {

// Howard Hinnant's days_from_civil: days since 1970-01-01.
long daysFromCivil(int y, const int m, const int d) {
  y -= m <= 2 ? 1 : 0;
  const long era = (y >= 0 ? y : y - 399) / 400;
  const long yoe = y - era * 400;
  const long doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

const long kLaunch = daysFromCivil(2021, 6, 19);

bool digits(const char* s, const int n, int& out) {
  out = 0;
  for (int i = 0; i < n; ++i) {
    if (s[i] < '0' || s[i] > '9') return false;
    out = out * 10 + (s[i] - '0');
  }
  return true;
}

bool isoDate(const char* s, int& dayOut) {
  int y = 0;
  int m = 0;
  int d = 0;
  if (!digits(s, 4, y) || s[4] != '-' || !digits(s + 5, 2, m) || s[7] != '-' || !digits(s + 8, 2, d)) return false;
  if (m < 1 || m > 12 || d < 1 || d > 31) return false;
  dayOut = dayIndex(y, m, d);
  return true;
}

bool word(const char* s, char out[kLetters]) {
  for (int i = 0; i < kLetters; ++i) {
    char c = s[i];
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    if (c < 'A' || c > 'Z') return false;
    out[i] = c;
  }
  return true;
}

// The string value of "key": "value" in a flat JSON object. Enough for NYT's
// one-level response; not a JSON parser.
bool jsonString(const std::string& body, const char* key, std::string& out) {
  const std::string quoted = std::string("\"") + key + "\"";
  size_t at = body.find(quoted);
  if (at == std::string::npos) return false;
  at = body.find(':', at + quoted.size());
  if (at == std::string::npos) return false;
  at = body.find('"', at);
  if (at == std::string::npos) return false;
  const size_t end = body.find('"', at + 1);
  if (end == std::string::npos) return false;
  out = body.substr(at + 1, end - at - 1);
  return true;
}

}  // namespace

int dayIndex(const int year, const int month, const int day) {
  return static_cast<int>(daysFromCivil(year, month, day) - kLaunch);
}

void dateOfDay(const int index, int& year, int& month, int& day) {
  // Hinnant's civil_from_days.
  const long z = index + kLaunch + 719468;
  const long era = (z >= 0 ? z : z - 146096) / 146097;
  const long doe = z - era * 146097;
  const long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const long mp = (5 * doy + 2) / 153;
  day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
  year = static_cast<int>(yoe + era * 400 + (month <= 2 ? 1 : 0));
}

bool parseAnswerLine(const char* line, size_t length, int& dayOut, char wordOut[kLetters]) {
  while (length > 0 && (line[length - 1] == '\r' || line[length - 1] == ' ' || line[length - 1] == '\n')) --length;
  if (length != 16 || line[10] != ' ') return false;
  return isoDate(line, dayOut) && word(line + 11, wordOut);
}

bool parseDailyJson(const std::string& body, int& dayOut, char wordOut[kLetters]) {
  std::string date;
  std::string solution;
  if (!jsonString(body, "print_date", date) || !jsonString(body, "solution", solution)) return false;
  if (date.size() != 10 || solution.size() != kLetters) return false;
  return isoDate(date.c_str(), dayOut) && word(solution.c_str(), wordOut);
}

void putAnswer(std::string& file, const int day, const char* answer) {
  if (day < 0) return;
  const size_t at = static_cast<size_t>(day) * kLetters;
  if (file.size() < at + kLetters) file.resize(at + kLetters, '.');
  file.replace(at, kLetters, answer, kLetters);
}

bool answerFor(const std::string& file, const int day, char wordOut[kLetters]) {
  if (day < 0) return false;
  const size_t at = static_cast<size_t>(day) * kLetters;
  if (file.size() < at + kLetters) return false;
  return word(file.data() + at, wordOut);
}

int lastKnownDay(const std::string& file) {
  for (int day = static_cast<int>(file.size() / kLetters) - 1; day >= 0; --day) {
    char w[kLetters];
    if (answerFor(file, day, w)) return day;
  }
  return -1;
}

bool isWord(const char* list, const size_t count, const char* guess) {
  size_t lo = 0;
  size_t hi = count;
  while (lo < hi) {
    const size_t mid = lo + (hi - lo) / 2;
    const int cmp = std::memcmp(list + mid * kLetters, guess, kLetters);
    if (cmp == 0) return true;
    if (cmp < 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return false;
}

void Game::start(const int day, const char* answer) {
  *this = Game{};
  day_ = day;
  std::memcpy(answer_, answer, kLetters);
}

bool Game::type(const char letter) {
  if (over() || typed_ >= kLetters || letter < 'A' || letter > 'Z') return false;
  typing_[typed_++] = letter;
  return true;
}

bool Game::erase() {
  if (over() || typed_ == 0) return false;
  typing_[--typed_] = '\0';
  return true;
}

void Game::apply(const char* guess) {
  std::memcpy(rows_[count_], guess, kLetters);
  score(guess, answer_, marks_[count_]);
  for (int i = 0; i < kLetters; ++i) mergeKey(keys_[guess[i] - 'A'], marks_[count_][i]);
  ++count_;
  if (std::memcmp(guess, answer_, kLetters) == 0) {
    status_ = Status::Won;
  } else if (count_ == kRows) {
    status_ = Status::Lost;
  }
}

Game::Submit Game::submit(bool (*isWordFn)(void* ctx, const char* word), void* ctx) {
  if (over()) return Submit::Over;
  if (typed_ < kLetters) return Submit::Short;
  if (!isWordFn(ctx, typing_)) return Submit::NotAWord;
  apply(typing_);
  std::memset(typing_, 0, sizeof(typing_));
  typed_ = 0;
  if (status_ == Status::Won) return Submit::Won;
  if (status_ == Status::Lost) return Submit::Lost;
  return Submit::Scored;
}

std::string Game::save() const {
  std::string out(answer_, kLetters);
  for (int r = 0; r < count_; ++r) {
    out += ' ';
    out.append(rows_[r], kLetters);
  }
  return out;
}

bool Game::load(const int day, const std::string& text) {
  if (text.size() < kLetters) return false;
  char answer[kLetters];
  if (!word(text.data(), answer)) return false;
  start(day, answer);
  size_t at = kLetters;
  while (at < text.size() && !over()) {
    if (text[at] != ' ' || at + 1 + kLetters > text.size()) return false;
    char guess[kLetters];
    if (!word(text.data() + at + 1, guess)) return false;
    apply(guess);
    at += 1 + kLetters;
  }
  return at == text.size();
}

uint8_t resultOf(const std::string& results, const int day) {
  if (day < 0 || static_cast<size_t>(day) >= results.size()) return 0;
  return static_cast<uint8_t>(results[static_cast<size_t>(day)]);
}

void putResult(std::string& results, const int day, const uint8_t value) {
  if (day < 0) return;
  if (results.size() <= static_cast<size_t>(day)) results.resize(static_cast<size_t>(day) + 1, '\0');
  results[static_cast<size_t>(day)] = static_cast<char>(value);
}

Stats statsFor(const std::string& results, const int today) {
  Stats stats;
  for (size_t day = 0; day < results.size(); ++day) {
    const uint8_t r = static_cast<uint8_t>(results[day]);
    if ((r & kOnTheDay) == 0) continue;
    const uint8_t v = r & 0x7F;
    if (v >= 1 && v <= kRows) {
      ++stats.played;
      ++stats.won;
      ++stats.wins[v - 1];
    } else if (v == kLost) {
      ++stats.played;
    }
  }
  const auto wonOnTheDay = [&results](const int day) {
    const uint8_t r = resultOf(results, day);
    return (r & kOnTheDay) != 0 && (r & 0x7F) >= 1 && (r & 0x7F) <= kRows;
  };
  int day = today;
  if (!wonOnTheDay(day)) {
    const uint8_t v = resultOf(results, day) & 0x7F;
    if (v == kLost) return stats;  // lost today: the streak is over
    --day;                         // today still open: count from yesterday
  }
  while (day >= 0 && wonOnTheDay(day)) {
    ++stats.streak;
    --day;
  }
  return stats;
}

}  // namespace wordle

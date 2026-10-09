#include "WorkoutsCore.h"

#include <cstdio>
#include <cstdlib>

namespace workouts {
namespace {

std::string trim(const std::string& text) {
  size_t start = 0;
  while (start < text.size() && (text[start] == ' ' || text[start] == '\t' || text[start] == '\r')) start++;
  size_t stop = text.size();
  while (stop > start && (text[stop - 1] == ' ' || text[stop - 1] == '\t' || text[stop - 1] == '\r')) stop--;
  return text.substr(start, stop - start);
}

// Splits a schedule line's "title | icon" on the LAST bar. A line with no bar
// is all title.
void splitBar(const std::string& line, std::string& left, std::string& right) {
  const size_t bar = line.rfind('|');
  if (bar == std::string::npos) {
    left = trim(line);
    right.clear();
    return;
  }
  left = trim(line.substr(0, bar));
  right = trim(line.substr(bar + 1));
}

// Digits only, clamped; anything else is the default. A count is never an
// error, because a plan that refuses to open over "4x" is worse than one that
// opens with 3.
int parseSets(const std::string& text) {
  if (text.empty()) return 3;
  int value = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') return 3;
    value = value * 10 + (c - '0');
    if (value > kMaxSets) return kMaxSets;
  }
  return value < 1 ? 1 : value;
}

// Whole kilograms, clamped; anything that is not digits is no weight.
int parseWeight(const std::string& text) {
  int value = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') return 0;
    value = value * 10 + (c - '0');
    if (value > kMaxWeight) return kMaxWeight;
  }
  return value;
}

bool parseInt(const std::string& text, int& out) {
  if (text.empty()) return false;
  char* end = nullptr;
  const long value = std::strtol(text.c_str(), &end, 10);
  if (end == nullptr || *end != '\0') return false;
  out = static_cast<int>(value);
  return true;
}

template <typename Fn>
void eachLine(const std::string& text, Fn&& fn) {
  size_t i = 0;
  while (i < text.size()) {
    size_t end = text.find('\n', i);
    if (end == std::string::npos) end = text.size();
    fn(text.substr(i, end - i));
    i = end + 1;
  }
}

bool hasTitle(const Plan& plan, const std::string& title) {
  for (const Schedule& schedule : plan.schedules) {
    if (schedule.title == title) return true;
  }
  return false;
}

}  // namespace

int iconIndex(const std::string& key) {
  for (int i = 0; i < kIconCount; i++) {
    if (key == kIcons[i]) return i;
  }
  return 0;
}

int Schedule::totalSets() const {
  int total = 0;
  for (const Exercise& exercise : exercises) total += exercise.sets;
  return total;
}

int Progress::total() const {
  int sum = 0;
  for (const int n : done) sum += n;
  return sum;
}

std::string cleanName(const std::string& raw, const size_t max) {
  std::string flat;
  flat.reserve(raw.size());
  for (const char c : raw) {
    if (c == '\n' || c == '\r' || c == '|') {
      flat.push_back(' ');
    } else if (c == '\t') {
      flat.push_back(' ');
    } else {
      flat.push_back(c);
    }
  }
  std::string out = trim(flat);
  if (out.size() <= max) return out;
  // Back off to the start of a UTF-8 character, never into the middle of one.
  size_t cut = max;
  while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) cut--;
  return trim(out.substr(0, cut));
}

Plan parsePlan(const std::string& text) {
  Plan plan;
  Schedule* current = nullptr;
  bool skipping = false;  // inside a schedule that was dropped
  eachLine(text, [&](const std::string& raw) {
    const std::string line = trim(raw);
    if (line.empty() || line[0] == '#') return;
    if (line[0] == '=') {
      current = nullptr;
      skipping = true;
      if (static_cast<int>(plan.schedules.size()) >= kMaxSchedules) return;
      std::string title;
      std::string icon;
      splitBar(line.substr(1), title, icon);
      title = cleanName(title, kMaxTitleBytes);
      if (title.empty() || hasTitle(plan, title)) return;
      Schedule schedule;
      schedule.title = title;
      schedule.icon = iconIndex(icon);
      plan.schedules.push_back(schedule);
      current = &plan.schedules.back();
      skipping = false;
      return;
    }
    if (current == nullptr || skipping) return;
    if (static_cast<int>(current->exercises.size()) >= kMaxExercises) return;
    // Fields left to right: names cannot hold a bar, so the first one ends the
    // name, and a plan written before weights existed reads with none.
    const size_t first = line.find('|');
    const size_t second = first == std::string::npos ? std::string::npos : line.find('|', first + 1);
    const std::string name = cleanName(line.substr(0, first), kMaxExerciseBytes);
    if (name.empty()) return;
    Exercise exercise;
    exercise.name = name;
    if (first != std::string::npos) {
      exercise.sets =
          parseSets(trim(line.substr(first + 1, second == std::string::npos ? std::string::npos : second - first - 1)));
    }
    if (second != std::string::npos) exercise.weight = parseWeight(trim(line.substr(second + 1)));
    current->exercises.push_back(exercise);
  });
  return plan;
}

std::string formatPlan(const Plan& plan) {
  std::string out;
  for (const Schedule& schedule : plan.schedules) {
    out += "= ";
    out += schedule.title;
    out += " | ";
    out += kIcons[schedule.icon >= 0 && schedule.icon < kIconCount ? schedule.icon : 0];
    out += '\n';
    for (const Exercise& exercise : schedule.exercises) {
      char numbers[32];
      // No weight is no third field, so a plan without weights is written
      // exactly as it was before there were any.
      if (exercise.weight > 0) {
        std::snprintf(numbers, sizeof(numbers), " | %d | %d\n", exercise.sets, exercise.weight);
      } else {
        std::snprintf(numbers, sizeof(numbers), " | %d\n", exercise.sets);
      }
      out += exercise.name;
      out += numbers;
    }
  }
  return out;
}

// --- Today ---------------------------------------------------------------

Today parseToday(const std::string& text) {
  Today today;
  bool sawDay = false;
  Progress* current = nullptr;
  eachLine(text, [&](const std::string& raw) {
    const std::string line = trim(raw);
    if (line.empty()) return;
    if (!sawDay) {
      // The first line is the day, and without one nothing after it can be
      // trusted to be today's.
      sawDay = true;
      if (line.compare(0, 4, "day ") != 0 || !parseInt(line.substr(4), today.day)) today.day = -2;
      return;
    }
    if (today.day == -2) return;
    if (line.compare(0, 2, "s ") == 0) {
      Progress progress;
      progress.title = cleanName(line.substr(2), kMaxTitleBytes);
      today.entries.push_back(progress);
      current = &today.entries.back();
      return;
    }
    if (current == nullptr) return;
    size_t i = 0;
    while (i <= line.size()) {
      size_t end = line.find(',', i);
      if (end == std::string::npos) end = line.size();
      int n = 0;
      if (!parseInt(trim(line.substr(i, end - i)), n)) n = 0;
      if (n < 0) n = 0;
      if (n > kMaxSets) n = kMaxSets;
      current->done.push_back(n);
      i = end + 1;
    }
    current = nullptr;
  });
  if (today.day == -2) {
    today.day = -1;
    today.entries.clear();
  }
  return today;
}

std::string formatToday(const Today& today) {
  char head[24];
  std::snprintf(head, sizeof(head), "day %d\n", today.day);
  std::string out = head;
  for (const Progress& progress : today.entries) {
    // A schedule opened and never ticked is not written: it would only be
    // noise for the next session to carry.
    if (progress.total() == 0) continue;
    out += "s ";
    out += progress.title;
    out += '\n';
    for (size_t i = 0; i < progress.done.size(); i++) {
      char n[8];
      std::snprintf(n, sizeof(n), "%s%d", i == 0 ? "" : ",", progress.done[i]);
      out += n;
    }
    out += '\n';
  }
  return out;
}

Progress& progressFor(Today& today, const Schedule& schedule) {
  for (Progress& progress : today.entries) {
    if (progress.title != schedule.title) continue;
    // Edited on the phone since: keep what lines up, and never let a count
    // exceed what the exercise now asks for.
    progress.done.resize(schedule.exercises.size(), 0);
    for (size_t i = 0; i < progress.done.size(); i++) {
      if (progress.done[i] > schedule.exercises[i].sets) progress.done[i] = schedule.exercises[i].sets;
    }
    return progress;
  }
  Progress progress;
  progress.title = schedule.title;
  progress.done.assign(schedule.exercises.size(), 0);
  today.entries.push_back(progress);
  return today.entries.back();
}

bool addSet(Progress& progress, const Schedule& schedule, const int index) {
  if (index < 0 || index >= static_cast<int>(schedule.exercises.size())) return false;
  if (static_cast<int>(progress.done.size()) <= index) progress.done.resize(schedule.exercises.size(), 0);
  int& done = progress.done[static_cast<size_t>(index)];
  if (done >= schedule.exercises[static_cast<size_t>(index)].sets) return false;
  done++;
  return true;
}

bool removeSet(Progress& progress, const int index) {
  if (index < 0 || index >= static_cast<int>(progress.done.size())) return false;
  int& done = progress.done[static_cast<size_t>(index)];
  if (done <= 0) return false;
  done--;
  return true;
}

bool resetProgress(Progress& progress) {
  if (progress.total() == 0) return false;
  for (int& done : progress.done) done = 0;
  return true;
}

bool adjustWeight(Exercise& exercise, const int delta) {
  int next = exercise.weight + delta;
  if (next < 0) next = 0;
  if (next > kMaxWeight) next = kMaxWeight;
  if (next == exercise.weight) return false;
  exercise.weight = next;
  return true;
}

// --- The log -------------------------------------------------------------

std::vector<LogEntry> parseLog(const std::string& text) {
  std::vector<LogEntry> entries;
  eachLine(text, [&](const std::string& raw) {
    const std::string line = trim(raw);
    if (line.empty() || line[0] == '#') return;
    const size_t first = line.find('|');
    if (first == std::string::npos) return;
    const size_t second = line.find('|', first + 1);
    if (second == std::string::npos) return;
    LogEntry entry;
    if (!parseInt(trim(line.substr(0, first)), entry.day)) return;
    entry.icon = iconIndex(trim(line.substr(first + 1, second - first - 1)));
    entry.title = cleanName(line.substr(second + 1), kMaxTitleBytes);
    entries.push_back(entry);
  });
  return entries;
}

std::string formatLogLine(const LogEntry& entry) {
  char day[16];
  std::snprintf(day, sizeof(day), "%d|", entry.day);
  std::string out = day;
  out += kIcons[entry.icon >= 0 && entry.icon < kIconCount ? entry.icon : 0];
  out += '|';
  out += entry.title;
  out += '\n';
  return out;
}

std::string formatLog(const std::vector<LogEntry>& entries) {
  std::string out;
  for (const LogEntry& entry : entries) out += formatLogLine(entry);
  return out;
}

bool logged(const std::vector<LogEntry>& entries, const int day, const std::string& title) {
  for (const LogEntry& entry : entries) {
    if (entry.day == day && entry.title == title) return true;
  }
  return false;
}

bool unlog(std::vector<LogEntry>& entries, const int day, const std::string& title) {
  for (size_t i = 0; i < entries.size(); i++) {
    if (entries[i].day == day && entries[i].title == title) {
      entries.erase(entries.begin() + static_cast<long>(i));
      return true;
    }
  }
  return false;
}

// --- The calendar --------------------------------------------------------

void calendarCells(const std::vector<LogEntry>& entries, const int today, WeekCell out[kCalendarDays]) {
  const int first = today - weekdayOf(today) - 7;
  for (int i = 0; i < kCalendarDays; i++) {
    WeekCell& cell = out[i];
    cell = WeekCell{};
    cell.day = first + i;
    cell.future = cell.day > today;
    cell.weekday = weekdayOf(cell.day);
    int y = 0;
    int m = 0;
    civilFromDays(cell.day, y, m, cell.dayOfMonth);
    // File order is the order they were trained, so the LAST match is the
    // most recent schedule of that day.
    for (const LogEntry& entry : entries) {
      if (entry.day != cell.day) continue;
      cell.icon = entry.icon;
      cell.sessions++;
    }
  }
}

int daysFromCivil(int year, const int month, const int day) {
  year -= month <= 2 ? 1 : 0;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const int yoe = year - era * 400;
  const int doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

void civilFromDays(int days, int& year, int& month, int& day) {
  days += 719468;
  const int era = (days >= 0 ? days : days - 146096) / 146097;
  const int doe = days - era * 146097;
  const int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int mp = (5 * doy + 2) / 153;
  day = doy - (153 * mp + 2) / 5 + 1;
  month = mp < 10 ? mp + 3 : mp - 9;
  year = yoe + era * 400 + (month <= 2 ? 1 : 0);
}

int weekdayOf(const int days) {
  // 1970-01-01 was a Thursday, which is 3 counting from Monday.
  const int wd = (days + 3) % 7;
  return wd < 0 ? wd + 7 : wd;
}

}  // namespace workouts

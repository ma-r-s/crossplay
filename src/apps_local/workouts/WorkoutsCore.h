#pragma once

// Workouts: schedules written on a phone, sets ticked on the reader.
//
// Freestanding C++17 -- no Arduino, no renderer, no SD card -- so that
// host-tests/workouts can drive every rule on a laptop. WorkoutsStore reads and
// writes the bytes these functions produce, and nothing else.
//
// ---------------------------------------------------------------------------
// Three files, each with one writer
// ---------------------------------------------------------------------------
//
// `plan.txt` is the schedules. The phone page is its usual author, but it is
// plain text so it can be fixed on a computer too:
//
//     = Upper Body | arms
//     Bench press | 4 | 60
//     Pull-ups | 3
//     = Lower Body | legs
//     Squat | 5
//
// An exercise is `name | sets | kg`, the weight optional and whole kilograms.
// The phone writes the starting weight and the reader's + and - rewrite it, so
// the file always holds the weight last lifted.
//
// `today.txt` is how many sets of each exercise are done today. It belongs to
// one day and is thrown away when the day changes, which is the whole of
// "starting a new workout": there is no reset button because tomorrow is one.
//
// `log.txt` is one line per schedule trained per day, `day|icon|title`, and is
// what the calendar on the opening screen is drawn from. Appended when the
// first set of a schedule is ticked, so a schedule opened and left untouched is
// not a workout.
//
// Progress is keyed by the schedule's TITLE, not its position, so reordering
// the schedules on the phone mid-session does not move ticks between them.

#include <cstdint>
#include <string>
#include <vector>

namespace workouts {

// Past this a schedule's boxes no longer fit one row of the panel at a size a
// finger can see. Ten covers every rep scheme short of a ladder.
constexpr int kMaxSets = 10;
constexpr int kMaxSchedules = 16;
constexpr int kMaxExercises = 24;
// Whole kilograms, the step the reader's + and - move by. 0 is no weight.
constexpr int kMaxWeight = 999;
constexpr size_t kMaxTitleBytes = 40;
constexpr size_t kMaxExerciseBytes = 48;
// The phone page is the only door bytes come through, and a page left open can
// paste anything. A plan at every limit above, written out, is about 22KB, so
// anything the reader itself writes always reads back.
constexpr size_t kMaxPlanBytes = 32 * 1024;

// The marks a schedule can carry. The KEY is what the files store, so an icon
// can be added or the list reordered without rewriting anybody's plan; an
// unknown key draws the first mark rather than nothing.
// The phone page carries the same keys, in the same order, with the drawing
// each bitmap was made from (WorkoutsPage.html, ICONS).
constexpr const char* kIcons[] = {
    "dumbbell", "arms", "legs", "push", "pull", "core", "cardio", "bike", "swim", "hike", "stretch", "power",
};
constexpr int kIconCount = static_cast<int>(sizeof(kIcons) / sizeof(kIcons[0]));
int iconIndex(const std::string& key);

struct Exercise {
  std::string name;
  int sets = 3;
  int weight = 0;  // kg; 0 for an exercise without one
};

struct Schedule {
  std::string title;
  int icon = 0;  // index into kIcons
  std::vector<Exercise> exercises;
  int totalSets() const;
};

struct Plan {
  std::vector<Schedule> schedules;
};

// Lenient by design: a hand-edited file with a stray line still opens, and
// every limit above is applied by clamping rather than refusing. Exercises
// before the first schedule line have nowhere to go and are dropped; so is a
// schedule whose title is already taken, because progress is keyed by title.
Plan parsePlan(const std::string& text);
std::string formatPlan(const Plan& plan);

// What a name may be once it is in the file: no line breaks and no '|', which
// are the format's own punctuation, and no surrounding space, capped at `max`
// bytes on a character boundary.
std::string cleanName(const std::string& raw, size_t max);

// --- Today ---------------------------------------------------------------

struct Progress {
  std::string title;
  std::vector<int> done;  // per exercise, in plan order
  int total() const;
};

struct Today {
  int day = -1;  // days since 1970-01-01, local; -1 when the clock is unset
  std::vector<Progress> entries;
};

Today parseToday(const std::string& text);
std::string formatToday(const Today& today);

// The progress for `schedule`, created empty if this is its first touch today
// and resized to the plan if the phone changed the exercises since.
Progress& progressFor(Today& today, const Schedule& schedule);
// The tick: one more set of exercise `index`, up to its count. False when it
// was already complete, which leaves `progress` unchanged.
bool addSet(Progress& progress, const Schedule& schedule, int index);
// The undo: one fewer. False at zero.
bool removeSet(Progress& progress, int index);
// Every set of the schedule back to empty. False when nothing was ticked.
bool resetProgress(Progress& progress);
// `exercise`'s weight moved by `delta` kg, held to 0..kMaxWeight. False when it
// was already at the limit it moved toward.
bool adjustWeight(Exercise& exercise, int delta);

// --- The log -------------------------------------------------------------

struct LogEntry {
  int day = 0;
  int icon = 0;
  std::string title;
};

std::vector<LogEntry> parseLog(const std::string& text);
std::string formatLogLine(const LogEntry& entry);
std::string formatLog(const std::vector<LogEntry>& entries);
bool logged(const std::vector<LogEntry>& entries, int day, const std::string& title);
// Removes the entry for (day, title). True when one was there.
bool unlog(std::vector<LogEntry>& entries, int day, const std::string& title);

// --- The calendar --------------------------------------------------------

// Last week and this one, Monday to Sunday: two rows that always start on a
// Monday, so a weekday is always in the same column.
constexpr int kCalendarDays = 14;

struct WeekCell {
  int day = 0;
  int weekday = 0;      // 0 Monday .. 6 Sunday
  int dayOfMonth = 1;   // 1..31
  int icon = -1;        // the last schedule trained that day; -1 for a rest day
  int sessions = 0;     // how many schedules were trained that day
  bool future = false;  // after today: nothing can be on it yet
};

// Monday of last week through Sunday of the week holding `today`.
void calendarCells(const std::vector<LogEntry>& entries, int today, WeekCell out[kCalendarDays]);

// Civil calendar arithmetic (Howard Hinnant's algorithms), so a day number
// needs no libc and no timezone conversation.
int daysFromCivil(int year, int month, int day);
void civilFromDays(int days, int& year, int& month, int& day);
int weekdayOf(int days);  // 0 Monday .. 6 Sunday

}  // namespace workouts

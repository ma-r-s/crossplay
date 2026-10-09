// The workout files, checked without a panel.
//
// What matters most is that nothing a phone or a hand-edited file can send
// stops the plan from opening, and that a day's ticks stay with the schedule
// they were made on even when the phone rearranges the schedules mid-session.

#include <cstdio>
#include <string>
#include <vector>

#include "WorkoutsCore.h"

using namespace workouts;

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

void testParsePlan() {
  const Plan plan = parsePlan(
      "# a comment\n"
      "orphan before any schedule | 3\n"
      "= Upper Body | arms\n"
      "Bench press | 4\n"
      "  Pull-ups|3  \r\n"
      "\n"
      "Dips\n"
      "= Lower Body | legs\n"
      "Squat | 5\n"
      "Lunges | 99\n"
      "Calf raises | 0\n"
      "Plank | 4x\n");
  CHECK(plan.schedules.size() == 2);
  CHECK(plan.schedules[0].title == "Upper Body");
  CHECK(plan.schedules[0].icon == iconIndex("arms"));
  CHECK(plan.schedules[0].exercises.size() == 3);
  CHECK(plan.schedules[0].exercises[0].name == "Bench press");
  CHECK(plan.schedules[0].exercises[0].sets == 4);
  CHECK(plan.schedules[0].exercises[1].name == "Pull-ups");
  CHECK(plan.schedules[0].exercises[1].sets == 3);
  // No count is the default, not an error.
  CHECK(plan.schedules[0].exercises[2].name == "Dips");
  CHECK(plan.schedules[0].exercises[2].sets == 3);
  CHECK(plan.schedules[0].totalSets() == 10);
  // Counts clamp; nonsense falls back to the default.
  CHECK(plan.schedules[1].exercises[1].sets == kMaxSets);
  CHECK(plan.schedules[1].exercises[2].sets == 1);
  CHECK(plan.schedules[1].exercises[3].sets == 3);
}

void testUnknownIconAndDuplicates() {
  const Plan plan = parsePlan(
      "= Push | nonsense\n"
      "A | 1\n"
      "= Push | pull\n"
      "B | 2\n"
      "= \n"
      "C | 2\n"
      "= Pull\n"
      "D | 2\n");
  // An unknown key draws the first mark rather than nothing.
  CHECK(plan.schedules.size() == 2);
  CHECK(plan.schedules[0].icon == 0);
  // The second "Push" would share the first one's progress, so it is dropped
  // with its exercises rather than merged into it.
  CHECK(plan.schedules[0].exercises.size() == 1);
  CHECK(plan.schedules[1].title == "Pull");
  CHECK(plan.schedules[1].exercises.size() == 1);
  CHECK(plan.schedules[1].exercises[0].name == "D");
}

void testLimits() {
  std::string text;
  for (int s = 0; s < kMaxSchedules + 3; s++) {
    char line[32];
    std::snprintf(line, sizeof(line), "= S%d | core\n", s);
    text += line;
    for (int e = 0; e < kMaxExercises + 5; e++) text += "Exercise | 2\n";
  }
  const Plan plan = parsePlan(text);
  CHECK(static_cast<int>(plan.schedules.size()) == kMaxSchedules);
  CHECK(static_cast<int>(plan.schedules.back().exercises.size()) == kMaxExercises);
  CHECK(plan.schedules.back().title == "S15");
}

void aFullPlanFitsTheFileCap() {
  // Every limit at once, every name at its longest: what the reader writes
  // must always be something it will read back.
  std::string text;
  for (int s = 0; s < kMaxSchedules; s++) {
    text += "= " + std::to_string(s) + std::string(kMaxTitleBytes, 'T') + " | stretch\n";
    for (int e = 0; e < kMaxExercises; e++) text += std::string(kMaxExerciseBytes, 'E') + " | 10 | 999\n";
  }
  const std::string written = formatPlan(parsePlan(text));
  CHECK(written.size() <= kMaxPlanBytes);
  CHECK(static_cast<int>(parsePlan(written).schedules.size()) == kMaxSchedules);
}

void testWeights() {
  const Plan plan = parsePlan(
      "= Upper | arms\n"
      "Bench press | 4 | 60\n"
      "Pull-ups | 3\n"
      "Curl | 3 | 12kg\n"
      "Deadlift | 5 | 5000\n"
      "Row || 40\n");
  const std::vector<Exercise>& e = plan.schedules[0].exercises;
  CHECK(e.size() == 5);
  CHECK(e[0].name == "Bench press");
  CHECK(e[0].sets == 4);
  CHECK(e[0].weight == 60);
  // A plan from before weights reads with none.
  CHECK(e[1].weight == 0);
  // Nonsense is no weight, not an error; too much is the most there can be.
  CHECK(e[2].weight == 0);
  CHECK(e[3].weight == kMaxWeight);
  CHECK(e[4].sets == 3);
  CHECK(e[4].weight == 40);
  // No weight is no third field.
  CHECK(formatPlan(plan) ==
        "= Upper | arms\nBench press | 4 | 60\nPull-ups | 3\nCurl | 3\nDeadlift | 5 | 999\nRow | 3 | 40\n");

  Exercise x;
  x.weight = 1;
  CHECK(adjustWeight(x, -1));
  CHECK(x.weight == 0);
  CHECK(!adjustWeight(x, -1));
  CHECK(x.weight == 0);
  CHECK(adjustWeight(x, 1));
  CHECK(x.weight == 1);
  x.weight = kMaxWeight;
  CHECK(!adjustWeight(x, 1));
  CHECK(x.weight == kMaxWeight);
}

void testReset() {
  const Plan plan = parsePlan("= A | arms\nX | 2\nY | 1\n");
  Today today;
  today.day = 9;
  Progress& a = progressFor(today, plan.schedules[0]);
  CHECK(!resetProgress(a));
  addSet(a, plan.schedules[0], 0);
  addSet(a, plan.schedules[0], 0);
  addSet(a, plan.schedules[0], 1);
  CHECK(a.total() == 3);
  CHECK(resetProgress(a));
  CHECK(a.total() == 0);
  CHECK(a.done.size() == 2);
  // Reset and untouched since, it is not written at all.
  CHECK(formatToday(today) == "day 9\n");
}

void testCleanName() {
  CHECK(cleanName("  a|b\nc  ", 40) == "a b c");
  CHECK(cleanName(std::string(60, 'x'), 40).size() == 40);
  // Never cut inside a UTF-8 sequence: "é" is two bytes.
  std::string accented(39, 'x');
  accented += "\xC3\xA9";
  CHECK(cleanName(accented, 40) == std::string(39, 'x'));
}

void testRoundTrip() {
  const std::string text =
      "= Upper Body | arms\n"
      "Bench press | 4\n"
      "= Cardio | cardio\n"
      "Run | 1\n";
  CHECK(formatPlan(parsePlan(text)) == text);
  CHECK(formatPlan(parsePlan("")) == "");
}

void testToday() {
  const Plan plan = parsePlan("= A | arms\nX | 2\nY | 3\n= B | legs\nZ | 1\n");
  Today today;
  today.day = 20000;
  Progress& a = progressFor(today, plan.schedules[0]);
  CHECK(a.done.size() == 2);
  CHECK(addSet(a, plan.schedules[0], 0));
  CHECK(addSet(a, plan.schedules[0], 0));
  // Full: a third tap changes nothing.
  CHECK(!addSet(a, plan.schedules[0], 0));
  CHECK(a.done[0] == 2);
  CHECK(addSet(a, plan.schedules[0], 1));
  CHECK(!addSet(a, plan.schedules[0], 7));
  CHECK(a.total() == 3);
  CHECK(removeSet(a, 1));
  CHECK(!removeSet(a, 1));
  CHECK(a.total() == 2);

  // B is opened and never ticked: it is not written.
  progressFor(today, plan.schedules[1]);
  const std::string saved = formatToday(today);
  CHECK(saved == "day 20000\ns A\n2,0\n");
  const Today back = parseToday(saved);
  CHECK(back.day == 20000);
  CHECK(back.entries.size() == 1);
  CHECK(back.entries[0].title == "A");
  CHECK(back.entries[0].done.size() == 2);
  CHECK(back.entries[0].done[0] == 2);
}

void testTodayFollowsTheTitle() {
  // The phone reordered the schedules and dropped an exercise from A. Today's
  // ticks follow the title, and a count never exceeds the plan's.
  Today today = parseToday("day 7\ns A\n2,3,1\n");
  const Plan plan = parsePlan("= B | legs\nZ | 1\n= A | arms\nX | 1\nY | 3\n");
  Progress& a = progressFor(today, plan.schedules[1]);
  CHECK(a.done.size() == 2);
  CHECK(a.done[0] == 1);
  CHECK(a.done[1] == 3);
  Progress& b = progressFor(today, plan.schedules[0]);
  CHECK(b.total() == 0);
}

void testTodayGarbage() {
  CHECK(parseToday("").day == -1);
  CHECK(parseToday("nonsense\ns A\n1\n").entries.empty());
  const Today t = parseToday("day 5\n1,2\ns A\nx,-4,99\n");
  CHECK(t.entries.size() == 1);
  CHECK(t.entries[0].done.size() == 3);
  CHECK(t.entries[0].done[0] == 0);
  CHECK(t.entries[0].done[1] == 0);
  CHECK(t.entries[0].done[2] == kMaxSets);
}

void testLog() {
  std::vector<LogEntry> entries = parseLog("100|arms|Upper Body\nbad line\n101|zzz|Odd\n102|legs|Lower|Body\n");
  CHECK(entries.size() == 3);
  CHECK(entries[0].day == 100);
  CHECK(entries[0].icon == iconIndex("arms"));
  CHECK(entries[1].icon == 0);
  // The title is everything after the second bar.
  CHECK(entries[2].title == "Lower Body");
  CHECK(logged(entries, 100, "Upper Body"));
  CHECK(!logged(entries, 101, "Upper Body"));
  CHECK(unlog(entries, 100, "Upper Body"));
  CHECK(!unlog(entries, 100, "Upper Body"));
  CHECK(entries.size() == 2);
  LogEntry e;
  e.day = 5;
  e.icon = iconIndex("swim");
  e.title = "Laps";
  CHECK(formatLogLine(e) == "5|swim|Laps\n");
  CHECK(parseLog(formatLog(entries)).size() == 2);
}

void testCalendar() {
  CHECK(daysFromCivil(1970, 1, 1) == 0);
  CHECK(daysFromCivil(2026, 10, 6) == 20732);
  int y = 0, m = 0, d = 0;
  civilFromDays(20732, y, m, d);
  CHECK(y == 2026 && m == 10 && d == 6);
  civilFromDays(daysFromCivil(2024, 2, 29), y, m, d);
  CHECK(y == 2024 && m == 2 && d == 29);
  // 1970-01-01 was a Thursday; 2026-10-06 is a Tuesday.
  CHECK(weekdayOf(0) == 3);
  CHECK(weekdayOf(20732) == 1);
  CHECK(weekdayOf(-1) == 2);
}

void testCalendar14() {
  // 2026-10-06 is a Tuesday, so the two weeks are Mon 2026-09-28 .. Sun 2026-10-11.
  const int today = daysFromCivil(2026, 10, 6);
  std::vector<LogEntry> entries = parseLog(
      "20720|arms|Old\n"  // before the first Monday
      "20724|legs|Lower\n"
      "20730|arms|Upper\n"
      "20730|cardio|Run\n"
      "20732|push|Push\n");
  WeekCell cells[kCalendarDays];
  calendarCells(entries, today, cells);
  CHECK(cells[0].day == daysFromCivil(2026, 9, 28));
  CHECK(cells[0].weekday == 0);
  CHECK(cells[0].dayOfMonth == 28);
  CHECK(cells[13].weekday == 6);
  CHECK(cells[13].dayOfMonth == 11);
  CHECK(cells[8].day == today);
  CHECK(!cells[8].future);
  CHECK(cells[9].future);
  CHECK(cells[13].future);
  CHECK(cells[0].icon == iconIndex("legs"));
  // Two schedules on one day: the later one is shown, and both are counted.
  CHECK(cells[6].icon == iconIndex("cardio"));
  CHECK(cells[6].sessions == 2);
  CHECK(cells[8].icon == iconIndex("push"));
  CHECK(cells[1].sessions == 0);
  // On a Monday the first row is the whole of last week.
  calendarCells(entries, daysFromCivil(2026, 10, 5), cells);
  CHECK(cells[7].day == daysFromCivil(2026, 10, 5));
  CHECK(cells[8].future);
  // On a Sunday nothing is in the future.
  calendarCells(entries, daysFromCivil(2026, 10, 11), cells);
  CHECK(!cells[13].future);
}

}  // namespace

int main() {
  testParsePlan();
  testUnknownIconAndDuplicates();
  testLimits();
  aFullPlanFitsTheFileCap();
  testWeights();
  testReset();
  testCleanName();
  testRoundTrip();
  testToday();
  testTodayFollowsTheTitle();
  testTodayGarbage();
  testLog();
  testCalendar();
  testCalendar14();

  std::printf("%s  workouts: %d checks, %d failed\n", failures ? "FAIL" : "ok  ", checks, failures);
  return failures == 0 ? 0 : 1;
}

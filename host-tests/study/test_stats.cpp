// Summarising the review log, driven with a log built record by record.
//
// Synthetic rather than a captured file on purpose: the properties worth
// asserting are about *time* -- what falls inside the window, what a gap does
// to a streak, what happens when the log is longer than the window -- and a
// captured log only ever exercises the day it was captured on.

#include <cstdio>
#include <cstring>
#include <vector>

#include "../../src/apps_local/study/StudyStats.h"

namespace {

int failures = 0;
int checks = 0;

void check(const bool ok, const char* what) {
  ++checks;
  if (!ok) {
    ++failures;
    std::printf("  FAIL: %s\n", what);
  }
}

constexpr int64_t kCreated = 1747558800;  // Mario's collection epoch
constexpr int kToday = 443;

// A revlog in memory, written in the same 32-byte layout the device appends.
class FakeLog final : public study::ByteSource {
 public:
  void add(const int day, const study::Rating rating, const int64_t cardId = 42) {
    const int64_t atMs = (kCreated + static_cast<int64_t>(day) * 86400 + 3600) * 1000;
    uint8_t record[32] = {};
    std::memcpy(record, &cardId, 8);
    std::memcpy(record + 8, &atMs, 8);
    record[16] = static_cast<uint8_t>(rating);
    record[17] = 2;
    bytes_.insert(bytes_.end(), record, record + sizeof(record));
  }
  bool read(const uint32_t offset, void* dst, const uint32_t length) override {
    if (offset + length > bytes_.size()) return false;
    std::memcpy(dst, bytes_.data() + offset, length);
    ++reads;
    return true;
  }
  uint32_t size() const override { return static_cast<uint32_t>(bytes_.size()); }
  int reads = 0;

 private:
  std::vector<uint8_t> bytes_;
};

void testEmptyLog() {
  FakeLog log;
  study::Stats stats;
  check(study::readStats(log, kToday, kCreated, stats), "an empty log is not an error");
  check(stats.totalReviews == 0, "no reviews");
  check(stats.streak == 0, "no streak");
  check(stats.retention() == -1, "retention is unknown, not zero");
}

void testCountsAndRetention() {
  FakeLog log;
  for (int i = 0; i < 7; ++i) log.add(kToday, study::Rating::Good);
  for (int i = 0; i < 3; ++i) log.add(kToday, study::Rating::Again);
  log.add(kToday - 1, study::Rating::Easy);

  study::Stats stats;
  check(study::readStats(log, kToday, kCreated, stats), "log reads");
  check(stats.reviewsPerDay[0] == 10, "today's reviews land on day 0");
  check(stats.reviewsPerDay[1] == 1, "yesterday's land on day 1");
  check(stats.totalReviews == 11, "everything in the window counts");
  check(stats.recalled == 8, "Again is the only rating that is not a recall");
  check(stats.retention() == 72, "8 of 11 is 72%");
  check(stats.lifetimeReviews == 11, "lifetime counts every record");
}

void testStreak() {
  {
    FakeLog log;
    for (int day = 0; day < 5; ++day) log.add(kToday - day, study::Rating::Good);
    study::Stats stats;
    study::readStats(log, kToday, kCreated, stats);
    check(stats.streak == 5, "five consecutive days is a streak of five");
    check(stats.daysStudied == 5, "and five days studied");
  }
  {
    // A gap ends it.
    FakeLog log;
    log.add(kToday, study::Rating::Good);
    log.add(kToday - 1, study::Rating::Good);
    log.add(kToday - 3, study::Rating::Good);
    study::Stats stats;
    study::readStats(log, kToday, kCreated, stats);
    check(stats.streak == 2, "a missed day ends the streak");
    check(stats.daysStudied == 3, "but it still counts as a day studied");
  }
  {
    // Today not started yet must not read as a streak already broken -- this
    // is the one people would notice, every morning.
    FakeLog log;
    log.add(kToday - 1, study::Rating::Good);
    log.add(kToday - 2, study::Rating::Good);
    study::Stats stats;
    study::readStats(log, kToday, kCreated, stats);
    check(stats.streak == 2, "an unstarted today does not break yesterday's streak");
  }
}

void testWindowAndEarlyExit() {
  FakeLog log;
  // A year of history, well past the window.
  for (int day = kToday - 364; day <= kToday; ++day) log.add(day, study::Rating::Good);

  study::Stats stats;
  check(study::readStats(log, kToday, kCreated, stats), "a long log reads");
  check(stats.totalReviews == study::kHistoryDays, "only the window is counted");
  check(stats.lifetimeReviews == 365, "but lifetime sees all of it");
  check(stats.reviewsPerDay[study::kHistoryDays - 1] == 1, "the oldest day in the window is included");

  // The scan must stop once it is past the window. Reading the whole year
  // would be 365 records; at 64 to a chunk the window needs about one.
  check(log.reads <= 2, "the scan stops at the window instead of walking the file");
}

void testFutureRecordsAreIgnored() {
  // A clock that moved backwards, or a log copied from a device set to
  // tomorrow. Must not index negatively.
  FakeLog log;
  log.add(kToday + 3, study::Rating::Good);
  log.add(kToday, study::Rating::Good);
  study::Stats stats;
  check(study::readStats(log, kToday, kCreated, stats), "a future record does not fail the read");
  check(stats.totalReviews == 1, "a future record is ignored");
  check(stats.reviewsPerDay[0] == 1, "today's is still counted");
}

void testGarbageRecordsAreSkipped() {
  FakeLog log;
  log.add(kToday, study::Rating::Good);
  log.add(kToday, static_cast<study::Rating>(0));  // an impossible rating
  log.add(kToday, study::Rating::Hard);
  study::Stats stats;
  study::readStats(log, kToday, kCreated, stats);
  check(stats.totalReviews == 2, "a record with an impossible rating is skipped");
}

}  // namespace

int main() {
  std::printf("StudyStats\n");
  testEmptyLog();
  testCountsAndRetention();
  testStreak();
  testWindowAndEarlyExit();
  testFutureRecordsAreIgnored();
  testGarbageRecordsAreSkipped();
  std::printf("%s (%d checks, %d failures)\n", failures == 0 ? "PASS" : "FAIL", checks, failures);
  return failures == 0 ? 0 : 1;
}

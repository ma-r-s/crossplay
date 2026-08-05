#include "StudyStats.h"

#include <cstring>

namespace study {

namespace {

constexpr uint32_t kRevlogRecordSize = 32;
constexpr int64_t kSecondsPerDay = 86400;

// Read the file in blocks rather than a record at a time: the log is on an SD
// card, and 2000 seeks to summarise a fortnight is the difference between a
// screen that opens and one that stalls.
constexpr uint32_t kChunkRecords = 64;

int64_t readI64(const uint8_t* p) {
  int64_t v;
  std::memcpy(&v, p, sizeof(v));
  return v;
}

}  // namespace

int Stats::retention() const {
  if (totalReviews <= 0) return -1;
  return recalled * 100 / totalReviews;
}

bool readStats(ByteSource& revlog, const int todayDay, const int64_t collectionCreated, Stats& out) {
  out = Stats{};
  const uint32_t size = revlog.size();
  if (size < kRevlogRecordSize) return true;  // an empty log is not an error

  const uint32_t records = size / kRevlogRecordSize;
  out.lifetimeReviews = static_cast<int>(records);

  // Walk backwards. Records are appended in answer order, so once a chunk is
  // entirely older than the window every earlier chunk is too, and the scan
  // stops -- which is what keeps this O(window) rather than O(history).
  uint8_t chunk[kChunkRecords * kRevlogRecordSize];
  uint32_t remaining = records;
  bool windowPassed = false;

  while (remaining > 0 && !windowPassed) {
    const uint32_t count = remaining < kChunkRecords ? remaining : kChunkRecords;
    const uint32_t first = remaining - count;
    if (!revlog.read(first * kRevlogRecordSize, chunk, count * kRevlogRecordSize)) return false;

    // Newest first within the chunk, so the early exit can trigger mid-chunk.
    for (int i = static_cast<int>(count) - 1; i >= 0; --i) {
      const uint8_t* record = chunk + static_cast<uint32_t>(i) * kRevlogRecordSize;
      const int64_t cardId = readI64(record);
      const int64_t atMs = readI64(record + 8);
      const uint8_t rating = record[16];
      if (cardId == 0 || rating < 1 || rating > 4) continue;

      const int day = static_cast<int>((atMs / 1000 - collectionCreated) / kSecondsPerDay);
      const int age = todayDay - day;
      if (age < 0) continue;  // a clock that moved backwards; ignore, never negative-index
      if (age >= kHistoryDays) {
        windowPassed = true;
        break;
      }

      out.reviewsPerDay[age] += 1;
      out.totalReviews += 1;
      if (rating != static_cast<uint8_t>(Rating::Again)) out.recalled += 1;
    }
    remaining = first;
  }

  for (const int count : out.reviewsPerDay) {
    if (count > 0) out.daysStudied += 1;
  }
  // The streak runs back from today. A day with no reviews ends it -- except
  // today itself, because a session that has not started yet must not read as
  // a streak already broken.
  for (int day = 0; day < kHistoryDays; ++day) {
    if (out.reviewsPerDay[day] > 0) {
      out.streak += 1;
    } else if (day > 0) {
      break;
    }
  }
  return true;
}

}  // namespace study

#include "ConnectionsResults.h"

namespace connections {

namespace {

bool isLeap(const int year) { return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0; }

bool validDate(const int year, const int month, const int day) {
  if (year < 1970 || year > 3000 || month < 1 || month > 12 || day < 1) return false;
  static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const int limit = (month == 2 && isLeap(year)) ? 29 : kDays[month - 1];
  return day <= limit;
}

// Howard Hinnant's days_from_civil: exact for any proleptic Gregorian date, no
// loops and no table, which matters because this runs on every archive row.
long civilDays(int year, const int month, const int day) {
  year -= month <= 2;
  const long era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const unsigned doy = static_cast<unsigned>((153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1);
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<long>(doe) - 719468;
}

}  // namespace

uint8_t encodeRecord(const Record& record) {
  if (!record.played) return 0;
  uint8_t byte = static_cast<uint8_t>(record.mistakes > 4 ? 4 : record.mistakes);
  byte |= 0x08;  // played
  if (record.lost) byte |= 0x10;
  if (record.hardestFirst) byte |= 0x20;
  return byte;
}

Record decodeRecord(const uint8_t byte) {
  Record record;
  // A fresh file is all zeroes, so "no bit set" has to mean "never played"
  // rather than "played with no mistakes".
  record.played = (byte & 0x08) != 0;
  if (!record.played) return record;
  record.mistakes = static_cast<uint8_t>(byte & 0x07);
  if (record.mistakes > 4) record.mistakes = 4;
  record.lost = (byte & 0x10) != 0;
  record.hardestFirst = (byte & 0x20) != 0;
  return record;
}

int dayOfWeek(const uint32_t date) {
  const int y = static_cast<int>(date / 10000);
  const int m = static_cast<int>((date / 100) % 100);
  const int d = static_cast<int>(date % 100);
  if (!validDate(y, m, d)) return -1;
  // 1970-01-01 was a Thursday, so the epoch sits 4 days after a Sunday.
  const long days = civilDays(y, m, d);
  return static_cast<int>(((days % 7) + 7 + 4) % 7);
}

int daysInMonth(const int year, const int month) {
  if (month < 1 || month > 12 || year < 1970 || year > 3000) return 0;
  static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && isLeap(year)) return 29;
  return kDays[month - 1];
}

int daysBetween(const uint32_t from, const uint32_t to) {
  const int fy = static_cast<int>(from / 10000);
  const int fm = static_cast<int>((from / 100) % 100);
  const int fd = static_cast<int>(from % 100);
  const int ty = static_cast<int>(to / 10000);
  const int tm = static_cast<int>((to / 100) % 100);
  const int td = static_cast<int>(to % 100);
  if (!validDate(fy, fm, fd) || !validDate(ty, tm, td)) return -1;
  const long diff = civilDays(ty, tm, td) - civilDays(fy, fm, fd);
  if (diff < 0) return -1;
  return static_cast<int>(diff);
}

Summary summarise(const uint8_t* results, const int count, const int newestIndex) {
  Summary summary;
  if (results == nullptr || count <= 0) return summary;

  const auto at = [&](const int index) -> Record {
    if (index < 0 || index >= count) return Record{};
    return decodeRecord(results[index]);
  };

  for (int i = 0; i < count; ++i) {
    const Record record = decodeRecord(results[i]);
    if (!record.played) continue;
    summary.played++;
    if (!record.lost && record.mistakes == 0) summary.perfect++;
  }

  const Record newest = at(newestIndex);
  summary.newestPlayed = newest.played;
  summary.newestMistakes = newest.mistakes;

  int from = newestIndex;
  if (!newest.played) from--;
  for (int i = from; i >= 0; --i) {
    const Record record = at(i);
    if (!record.played || record.lost) break;
    summary.streak++;
  }

  for (int i = 0; i < 16; ++i) {
    const Record record = at(newestIndex - 15 + i);
    if (!record.played) continue;
    summary.recent[i] = record.lost ? kDayLost : (record.mistakes == 0) ? kDayPerfect : kDayWon;
  }
  return summary;
}

}  // namespace connections

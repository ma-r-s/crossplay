#include "ClockFloor.h"

#include <cstring>

namespace clockfloor {

namespace {

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's
// days_from_civil), so no timezone or libc calendar is involved.
int64_t daysFromCivil(int64_t y, const unsigned m, const unsigned d) {
  y -= m <= 2 ? 1 : 0;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153u * (m > 2 ? m - 3 : m + 9) + 2u) / 5u + d - 1u;
  const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

bool digit(const char c) { return c >= '0' && c <= '9'; }

}  // namespace

int64_t epochOfBuildDate(const char* date) {
  if (date == nullptr || std::strlen(date) != 11 || date[3] != ' ' || date[6] != ' ') return 0;
  static const char kMonths[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  unsigned month = 0;
  for (unsigned i = 0; i < 12; ++i) {
    if (std::strncmp(date, kMonths + i * 3, 3) == 0) month = i + 1;
  }
  if (month == 0) return 0;
  if (!(date[4] == ' ' || digit(date[4])) || !digit(date[5])) return 0;
  const unsigned day =
      (date[4] == ' ' ? 0u : static_cast<unsigned>(date[4] - '0') * 10u) + static_cast<unsigned>(date[5] - '0');
  for (int i = 7; i < 11; ++i) {
    if (!digit(date[i])) return 0;
  }
  const int64_t year = (date[7] - '0') * 1000 + (date[8] - '0') * 100 + (date[9] - '0') * 10 + (date[10] - '0');
  if (day < 1 || day > 31) return 0;
  return daysFromCivil(year, month, day) * 86400;
}

int64_t floorFor(const char* buildDate) {
  const int64_t day = epochOfBuildDate(buildDate);
  // A build date that cannot be read leaves the oldest floor the fork had
  // (2023-11-14) rather than none.
  if (day == 0) return 1700000000;
  return day - 86400;
}

int64_t buildFloor() {
  static const int64_t floor = floorFor(__DATE__);
  return floor;
}

bool trusted(const int64_t now) { return now >= buildFloor(); }

}  // namespace clockfloor

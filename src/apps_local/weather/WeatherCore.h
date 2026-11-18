#pragma once

// WEATHER's rules, freestanding: no Arduino, no renderer, no card, no radio.
// host-tests/weather builds this file with nothing but the standard library
// and lib/Utf8 on the include path.
//
// What lives here is everything that can be wrong without a device:
//
//   * reading Open-Meteo's two answers -- the forecast (now plus seven days)
//     and the geocoder's list of places -- out of their JSON;
//   * the two files this app keeps on the card, the place and the last
//     forecast, written and read back;
//   * what a stale forecast still has to say: which of its days is today, and
//     how long ago it was read ("UPDATED YESTERDAY 08:12");
//   * Celsius or Fahrenheit, and which countries default to which;
//   * the names of the days, and the words for the sky;
//   * a place name cleaned where it enters, so the screens never meet a
//     character their face cannot draw.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace weather {

// Seven days, today first: what one forecast request asks for and what the
// main screen draws (today, then six rows).
constexpr int kDays = 7;
// The geocoder is asked for this many places; the search screen shows the ones
// that fit and says when there were more.
constexpr int kMaxMatches = 10;
// A temperature Open-Meteo did not give (a null in its arrays).
constexpr int16_t kNoTemp = INT16_MIN;
// The longest search the field takes, in bytes.
constexpr size_t kMaxQuery = 40;
// The longest place name kept, in bytes. Longer names are cut on a character
// boundary before anything measures them; the screens fit what is left.
constexpr size_t kMaxName = 64;

enum class Units : uint8_t { Celsius, Fahrenheit };

// The picture for a sky. Night only matters for the reading taken now: the
// daily rows are days.
enum class Icon : uint8_t { Sun, Moon, CloudSun, CloudMoon, Cloud, Fog, Drizzle, Rain, Snow, Thunder };

// One day of the forecast. Temperatures are tenths of a degree Celsius;
// rainPct is the day's highest chance of precipitation, -1 when not given.
struct Day {
  int16_t year = 0;
  int8_t month = 0;
  int8_t day = 0;
  int16_t code = -1;  // WMO weather code
  int16_t maxC = kNoTemp;
  int16_t minC = kNoTemp;
  int8_t rainPct = -1;
};

struct Forecast {
  // The reading now, in the place's own time ("2026-11-17T14:00").
  char localTime[17] = "";
  int16_t nowC = kNoTemp;
  int16_t nowCode = -1;
  bool isDay = true;
  // The place's offset from UTC when the forecast was made, in seconds.
  int32_t utcOffset = 0;
  Day days[kDays];
  int dayCount = 0;
};

// The forecast as it was last read, and when. fetchedUtc is 0 when the device
// clock could not be trusted at the time; localTime then stands in for it.
struct Cache {
  Forecast forecast;
  int64_t fetchedUtc = 0;
  // The place it belongs to, so a forecast for the last city is never drawn
  // under the name of the next one.
  double lat = 0;
  double lon = 0;
};

// The city, chosen once. `name` and `region` are already clean (cleanName).
struct Place {
  std::string name;
  std::string region;  // "Zurich, Switzerland"
  char country[3] = "";
  double lat = 0;
  double lon = 0;
  Units units = Units::Celsius;
  // Set when the player chose the units in the menu. Until then they follow
  // the country of whichever city is chosen.
  bool unitsChosen = false;
};

// One of the geocoder's answers, cleaned.
struct Match {
  std::string name;
  std::string region;
  char country[3] = "";
  double lat = 0;
  double lon = 0;
};

// Whether the face the screens draw names in has a glyph for `cp`. The
// activity answers from the real font; tests answer from a table.
using CanDraw = bool (*)(uint32_t cp);

// --- Reading Open-Meteo -----------------------------------------------------

// The forecast endpoint's answer. False when it is not JSON, or has no
// current temperature, or no days: a forecast missing those is not one.
bool parseForecast(const std::string& json, Forecast& out);

// The geocoder's answer. A search that found nothing answers with no
// "results" key at all, which is true with no matches; false only when the
// answer is not JSON. Places whose name cleans to nothing drawable are kept
// under the best name that remains (placeLabel).
bool parseMatches(const std::string& json, CanDraw canDraw, std::vector<Match>& out);

// The two requests, built on a base that the simulator can point elsewhere.
std::string forecastUrl(const char* base, double lat, double lon);
std::string geocodeUrl(const char* base, const std::string& query);
std::string urlEncode(const std::string& text);

// --- The card ---------------------------------------------------------------

std::string serializePlace(const Place& place);
bool parsePlace(const std::string& text, Place& out);
std::string serializeCache(const Cache& cache);
bool parseCache(const std::string& text, Cache& out);
// Whether a cache was read for this place.
bool cacheIsFor(const Cache& cache, const Place& place);

// --- What a forecast says, and when ----------------------------------------

// The index of today among the forecast's days: the first day not before
// today in the place's own time. 0 when the clock is unknown (nowUtc 0) or
// every day is already past, so a forecast is never drawn empty.
int firstDay(const Cache& cache, int64_t nowUtc);

// "UPDATED 14:07", "UPDATED YESTERDAY 14:07", "UPDATED MON 14:07" or
// "UPDATED 12 NOV": when the forecast was read, in the place's own time,
// against today when the clock knows it.
std::string updatedLabel(const Cache& cache, int64_t nowUtc, bool twelveHour);

// Every name weekdayShort() can answer, Sunday first. Exported so a screen can
// size its day column to the widest of them, whichever days are showing.
inline constexpr const char* kWeekdayNames[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

// "MON".."SUN" for a calendar date.
const char* weekdayShort(int year, int month, int day);
const char* weekdayShort(const Day& day);

// The words and the picture for a WMO weather code.
const char* skyWords(int code);
Icon skyIcon(int code, bool isDay);

// A temperature in whole degrees of `units`, rounded half away from zero.
int displayTemp(int16_t tenthsC, Units units);
// "7", "-3", or "--" for a temperature that was not given. The degree sign is
// drawn by the screen: the Jersey cuts carry no U+00B0.
std::string tempText(int16_t tenthsC, Units units);
// "40%" or "" when not given.
std::string rainText(int8_t pct);

// Fahrenheit for the United States, its territories, and the handful of
// countries that also use it; Celsius everywhere else.
Units defaultUnitsFor(const char* countryCode);

// --- Text from outside -----------------------------------------------------

// A place name as it enters: composed (NFC), typography folded to ASCII, then
// letters, digits, spaces and the punctuation names carry (- ' . , ( ) /)
// kept where `canDraw` has a glyph for them, everything else dropped. Runs of
// space collapse; the result is trimmed and cut to kMaxName bytes.
std::string cleanName(const std::string& raw, CanDraw canDraw);

// The name to show for a place: its name, or when nothing of it survived,
// its region, or when nothing of that did either, its coordinates
// ("35.69N 139.69E"). Never empty.
std::string placeLabel(const std::string& name, const std::string& region, double lat, double lon);

// The search as typed: ASCII the keyboard sent, trimmed, spaces collapsed.
// Two characters is the least the geocoder answers.
std::string cleanQuery(const std::string& typed);
constexpr size_t kMinQuery = 2;

}  // namespace weather

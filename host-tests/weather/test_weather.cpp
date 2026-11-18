// The weather core: Open-Meteo's two answers, the two files on the card, a
// stale forecast, units, day names and place names from outside.
//
// The fixtures are the shapes Open-Meteo really sends (field order, nulls in
// the daily arrays, a geocoder answer with no "results" key when nothing
// matched), not shapes convenient for the parser.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "../../src/apps_local/weather/WeatherCore.h"

// Every allocation counted, so the heap a parse needs is a measured number
// rather than a guess. A header in front of each block remembers its size.
// On this 64-bit host pointers and strings are larger than on the 32-bit
// device, so what is measured here is an upper bound for it.
namespace heap {
size_t current = 0;
size_t peak = 0;
}  // namespace heap

void* operator new(const std::size_t size) {
  void* block = std::malloc(size + 16);
  if (block == nullptr) throw std::bad_alloc();
  *static_cast<std::size_t*>(block) = size;
  heap::current += size;
  if (heap::current > heap::peak) heap::peak = heap::current;
  return static_cast<char*>(block) + 16;
}

void operator delete(void* p) noexcept {
  if (p == nullptr) return;
  void* block = static_cast<char*>(p) - 16;
  heap::current -= *static_cast<std::size_t*>(block);
  std::free(block);
}

void operator delete(void* p, std::size_t) noexcept { operator delete(p); }

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  ++checksRun;
  if (!condition) {
    ++checksFailed;
    std::printf("FAIL test_weather.cpp:%d  %s\n", line, what);
  }
}

#define CHECK(cond) check((cond), #cond, __LINE__)
#define CHECK_EQ_STR(a, b) check(std::string(a) == std::string(b), #a " == " #b, __LINE__)

// The face the screens draw names in: ASCII and Latin-1, as the reading cuts.
bool latin1(const uint32_t cp) { return (cp >= 0x20 && cp <= 0x7E) || (cp >= 0xA0 && cp <= 0xFF); }

// 2026-11-17 13:07 UTC, which is 14:07 in Zurich (UTC+1).
constexpr int64_t kNovember17 = 1794920820;
constexpr int64_t kDay = 86400;

const char* const kForecastJson =
    R"({"latitude":47.36,"longitude":8.539999,"generationtime_ms":0.0925,"utc_offset_seconds":3600,)"
    R"("timezone":"Europe/Zurich","timezone_abbreviation":"GMT+1","elevation":409.0,)"
    R"("current_units":{"time":"iso8601","interval":"seconds","temperature_2m":"°C","weather_code":"wmo code","is_day":""},)"
    R"("current":{"time":"2026-11-17T14:00","interval":900,"temperature_2m":7.3,"weather_code":2,"is_day":1},)"
    R"("daily_units":{"time":"iso8601","weather_code":"wmo code","temperature_2m_max":"°C","temperature_2m_min":"°C",)"
    R"("precipitation_probability_max":"%"},)"
    R"("daily":{"time":["2026-11-17","2026-11-18","2026-11-19","2026-11-20","2026-11-21","2026-11-22","2026-11-23"],)"
    R"("weather_code":[2,61,63,3,71,0,45],)"
    R"("temperature_2m_max":[9.1,8.4,6.0,5.2,1.9,4.4,3.0],)"
    R"("temperature_2m_min":[3.2,4.1,2.5,0.4,-2.6,-3.8,-1.0],)"
    R"("precipitation_probability_max":[20,65,90,35,70,5,null]}})";

const char* const kMatchesJson =
    R"({"results":[)"
    R"({"id":2657896,"name":"Zürich","latitude":47.36667,"longitude":8.55,"elevation":429.0,"feature_code":"PPLA",)"
    R"("country_code":"CH","admin1_id":2657895,"timezone":"Europe/Zurich","population":341730,)"
    R"("postcodes":["8000","8001"],"country_id":2658434,"country":"Switzerland","admin1":"Zurich","admin2":"Bezirk Zürich"},)"
    R"({"id":6185377,"name":"Zurich","latitude":43.41678,"longitude":-81.62498,"feature_code":"PPL","country_code":"CA",)"
    R"("country":"Canada","admin1":"Ontario"},)"
    R"({"id":4282757,"name":"Zurich","latitude":38.84,"longitude":-99.43,"feature_code":"PPL","country_code":"US",)"
    R"("country":"United States","admin1":"Kansas","admin2":"Rooks"}],"generationtime_ms":0.61})";

// --- The forecast ----------------------------------------------------------

void testForecastReadsNowAndSevenDays() {
  weather::Forecast f;
  CHECK(weather::parseForecast(kForecastJson, f));
  CHECK_EQ_STR(f.localTime, "2026-11-17T14:00");
  CHECK(f.nowC == 73);
  CHECK(f.nowCode == 2);
  CHECK(f.isDay);
  CHECK(f.utcOffset == 3600);
  CHECK(f.dayCount == 7);
  CHECK(f.days[0].year == 2026 && f.days[0].month == 11 && f.days[0].day == 17);
  CHECK(f.days[6].day == 23);
  CHECK(f.days[0].maxC == 91 && f.days[0].minC == 32);
  // Below zero rounds to the nearest tenth, not towards zero.
  CHECK(f.days[5].minC == -38);
  CHECK(f.days[4].code == 71);
  CHECK(f.days[1].rainPct == 65);
  // A null in Open-Meteo's arrays is "not given", never zero.
  CHECK(f.days[6].rainPct == -1);
}

void testForecastNullsAreNotZero() {
  const std::string json =
      R"({"utc_offset_seconds":-18000,"current":{"time":"2026-11-17T08:15","temperature_2m":-0.04,"weather_code":null,"is_day":0},)"
      R"("daily":{"time":["2026-11-17","2026-11-18"],"weather_code":[null,3],"temperature_2m_max":[null,2.5],)"
      R"("temperature_2m_min":[-4.45,null]}})";
  weather::Forecast f;
  CHECK(weather::parseForecast(json, f));
  CHECK(f.utcOffset == -18000);
  CHECK(!f.isDay);
  CHECK(f.nowC == 0);
  CHECK(f.nowCode == -1);
  CHECK(f.dayCount == 2);
  CHECK(f.days[0].code == -1);
  CHECK(f.days[0].maxC == weather::kNoTemp);
  CHECK(f.days[0].minC == -45);
  CHECK(f.days[1].minC == weather::kNoTemp);
  // No precipitation array at all: every day says "not given".
  CHECK(f.days[0].rainPct == -1 && f.days[1].rainPct == -1);
}

void testForecastRefusesWhatIsNotOne() {
  weather::Forecast f;
  CHECK(!weather::parseForecast("", f));
  CHECK(!weather::parseForecast("<html><body>502 Bad Gateway</body></html>", f));
  // Cut off mid-answer, the way a dropped connection leaves it.
  const std::string whole(kForecastJson);
  CHECK(!weather::parseForecast(whole.substr(0, whole.size() / 2), f));
  // Open-Meteo's own error shape.
  CHECK(!weather::parseForecast(R"({"error":true,"reason":"Latitude must be in range of -90 to 90°."})", f));
  // No current temperature: not a forecast worth drawing.
  CHECK(!weather::parseForecast(R"({"current":{"time":"2026-11-17T14:00"},"daily":{"time":["2026-11-17"]}})", f));
  // No days.
  CHECK(!weather::parseForecast(R"({"current":{"time":"2026-11-17T14:00","temperature_2m":3}})", f));
  // Nested past any sane depth, rather than recursing until the stack goes:
  // a forecast that is otherwise whole, carrying one balanced, very deep
  // member, is refused by the bound and by nothing else.
  std::string deep(kForecastJson);
  deep.pop_back();
  deep += ",\"deep\":";
  for (int i = 0; i < 5000; ++i) deep += "[";
  for (int i = 0; i < 5000; ++i) deep += "]";
  deep += "}";
  CHECK(!weather::parseForecast(deep, f));
  std::string shallow(kForecastJson);
  shallow.pop_back();
  shallow += ",\"deep\":[[[1]]]}";
  CHECK(weather::parseForecast(shallow, f));
  // Trailing garbage after a valid document.
  CHECK(!weather::parseForecast(std::string(kForecastJson) + "}", f));
}

void testForecastTakesAtMostSevenDays() {
  std::string json =
      R"({"current":{"time":"2026-11-17T14:00","temperature_2m":1,"weather_code":0,"is_day":1},"daily":{"time":[)";
  for (int i = 0; i < 9; ++i) json += std::string(i ? "," : "") + "\"2026-11-" + std::to_string(17 + i) + "\"";
  json += R"(],"temperature_2m_max":[1,2,3,4,5,6,7,8,9]}})";
  weather::Forecast f;
  CHECK(weather::parseForecast(json, f));
  CHECK(f.dayCount == weather::kDays);
  CHECK(f.days[6].maxC == 70);
}

void testForecastSkipsAMalformedDate() {
  const std::string json = R"({"current":{"time":"2026-11-17T14:00","temperature_2m":1,"weather_code":0,"is_day":1},)"
                           R"("daily":{"time":["2026-11-17","yesterday","2026-11-19"],"temperature_2m_max":[1,2,3]}})";
  weather::Forecast f;
  CHECK(weather::parseForecast(json, f));
  // The day with no date cannot be named, so it is not drawn; its neighbours
  // keep their own values.
  CHECK(f.dayCount == 2);
  CHECK(f.days[1].day == 19 && f.days[1].maxC == 30);
}

// --- The geocoder ----------------------------------------------------------

void testMatchesReadNameRegionAndPlace() {
  std::vector<weather::Match> m;
  CHECK(weather::parseMatches(kMatchesJson, latin1, m));
  CHECK(m.size() == 3);
  if (m.size() != 3) return;
  CHECK_EQ_STR(m[0].name, "Zürich");
  // The canton is the city's name without its accent, so the region does not
  // say it again.
  CHECK_EQ_STR(m[0].region, "Switzerland");
  CHECK_EQ_STR(m[0].country, "CH");
  CHECK(std::fabs(m[0].lat - 47.36667) < 1e-6);
  CHECK(std::fabs(m[0].lon - 8.55) < 1e-6);
  CHECK_EQ_STR(m[1].region, "Ontario, Canada");
  CHECK(m[1].lon < 0);
  CHECK_EQ_STR(m[2].country, "US");
}

void testNoMatchIsAnAnswerNotAFailure() {
  std::vector<weather::Match> m;
  m.push_back(weather::Match{});
  CHECK(weather::parseMatches(R"({"generationtime_ms":0.53})", latin1, m));
  CHECK(m.empty());
  CHECK(weather::parseMatches(R"({"results":[]})", latin1, m));
  CHECK(m.empty());
}

void testMalformedMatchesFail() {
  std::vector<weather::Match> m;
  CHECK(!weather::parseMatches("", latin1, m));
  CHECK(!weather::parseMatches(R"({"results":[{"name":"Zur)", latin1, m));
  CHECK(!weather::parseMatches("Too many requests", latin1, m));
}

void testAMatchWithoutCoordinatesIsDropped() {
  std::vector<weather::Match> m;
  CHECK(weather::parseMatches(R"({"results":[{"name":"Nowhere"},{"name":"Bern","latitude":46.9,"longitude":7.4}]})",
                              latin1, m));
  CHECK(m.size() == 1);
  if (!m.empty()) CHECK_EQ_STR(m[0].name, "Bern");
}

void testMatchesAreCapped() {
  std::string json = R"({"results":[)";
  for (int i = 0; i < 25; ++i) {
    json += std::string(i ? "," : "") + R"({"name":"Springfield","latitude":39.8,"longitude":-89.6})";
  }
  json += "]}";
  std::vector<weather::Match> m;
  CHECK(weather::parseMatches(json, latin1, m));
  CHECK(static_cast<int>(m.size()) == weather::kMaxMatches);
}

void testARegionDoesNotRepeatTheName() {
  std::vector<weather::Match> m;
  CHECK(weather::parseMatches(
      R"({"results":[{"name":"Berlin","admin1":"Berlin","country":"Germany","latitude":52.5,"longitude":13.4},)"
      R"({"name":"Singapore","admin1":"","country":"Singapore","latitude":1.29,"longitude":103.85}]})",
      latin1, m));
  CHECK(m.size() == 2);
  if (m.size() != 2) return;
  CHECK_EQ_STR(m[0].region, "Germany");
  // A city-state: the region says nothing the name has not said.
  CHECK_EQ_STR(m[1].region, "");
  // Nor does one spelled without the name's accents or in another case.
  CHECK(weather::parseMatches(
      R"({"results":[{"name":"São Paulo","admin1":"Sao Paulo","country":"Brazil","latitude":-23.5,"longitude":-46.6},)"
      R"({"name":"Münster","admin1":"North Rhine-Westphalia","country":"MÜNSTER","latitude":51.9,"longitude":7.6}]})",
      latin1, m));
  CHECK(m.size() == 2);
  if (m.size() != 2) return;
  CHECK_EQ_STR(m[0].region, "Brazil");
  CHECK_EQ_STR(m[1].region, "North Rhine-Westphalia");
}

void testNonLatinNamesKeepWhatCanBeDrawn() {
  std::vector<weather::Match> m;
  // The last name arrives as JSON escapes, which decode before the cleaning.
  // Custom delimiters, because "(...)" inside a name ends a plain raw string.
  const std::string json =
      R"j({"results":[{"name":"東京都","latitude":35.6895,"longitude":139.69171,"country_code":"JP","country":"日本","admin1":"東京都"},)j"
      R"j({"name":"Москва","latitude":55.75222,"longitude":37.61556,"country_code":"RU","country":"Russia","admin1":"Москва"},)j"
      R"j({"name":"Zürich (Цюрих)","latitude":47.36667,"longitude":8.55,"country_code":"CH","country":"Switzerland"},)j"
      "{\"name\":\"\\u0410\\u0431\\u0430\\u043a\\u0430\\u043d\",\"latitude\":53.72,\"longitude\":91.43,"
      "\"country\":\"Russia\"}]}";
  CHECK(weather::parseMatches(json, latin1, m));
  CHECK(m.size() == 4);
  if (m.size() != 4) return;
  // Nothing of the name or the region can be drawn: the coordinates, which
  // always can.
  CHECK_EQ_STR(m[0].name, "35.69N 139.69E");
  CHECK_EQ_STR(m[0].region, "");
  // The name cannot be drawn and the country can: the country names it.
  CHECK_EQ_STR(m[1].name, "Russia");
  CHECK_EQ_STR(m[1].region, "");
  // The drawable part stays, and the brackets that held the rest go with it.
  CHECK_EQ_STR(m[2].name, "Zürich");
  // \u escapes decode before the cleaning, so they are cleaned like the rest.
  CHECK_EQ_STR(m[3].name, "Russia");
}

// --- Names from outside ----------------------------------------------------

void testCleanNameKeepsLettersAndDropsTheRest() {
  CHECK_EQ_STR(weather::cleanName("Zürich", latin1), "Zürich");
  // Decomposed, as a macOS filename or some feeds send it.
  CHECK_EQ_STR(weather::cleanName("Zu\xCC\x88rich", latin1), "Zürich");
  CHECK_EQ_STR(weather::cleanName("N\xE2\x80\x99"
                                  "Djamena",
                                  latin1),
               "N'Djamena");
  CHECK_EQ_STR(weather::cleanName("Paris \xF0\x9F\x97\xBC", latin1), "Paris");
  CHECK_EQ_STR(weather::cleanName("  Saint-\tDenis \n", latin1), "Saint- Denis");
  CHECK_EQ_STR(weather::cleanName("St. John's (Avalon)", latin1), "St. John's (Avalon)");
  // Markup is not a name: the symbols go and the words they separated stay apart.
  CHECK_EQ_STR(weather::cleanName("Rio <b>&amp;</b>", latin1), "Rio b amp /b");
  CHECK_EQ_STR(weather::cleanName("Hamburg\xC2\xAE", latin1), "Hamburg");
  // A letter the face lacks keeps its base letter where it has one.
  // Łódź: the z-acute decomposes; the L-stroke does not, and keeps its L.
  CHECK_EQ_STR(weather::cleanName("\xC5\x81\xC3\xB3\x64\xC5\xBA", latin1), "Lódz");
  // Dotless i, as in Turkish names, keeps an i rather than vanishing.
  CHECK_EQ_STR(weather::cleanName("Diyarbak\xC4\xB1r", latin1), "Diyarbakir");
  // A letter with no base at all still goes: Greek has no Latin-1 reading.
  CHECK_EQ_STR(weather::cleanName("\xCE\x91\xCE\xB8\xCE\xAE\xCE\xBD\xCE\xB1", latin1), "");
  CHECK_EQ_STR(weather::cleanName("Bra\xC8\x99ov", latin1), "Brasov");  // ș -> s
  CHECK_EQ_STR(weather::cleanName("", latin1), "");
  CHECK_EQ_STR(weather::cleanName("\xF0\x9F\x8C\xA7", latin1), "");
  // Malformed UTF-8 is dropped, not passed to the renderer.
  CHECK_EQ_STR(weather::cleanName("Bad\xC3", latin1), "Bad");
  CHECK_EQ_STR(weather::cleanName("Bad\xFF\xFE"
                                  "Name",
                                  latin1),
               "BadName");
  // Cut on a character boundary.
  std::string longName;
  for (int i = 0; i < 40; ++i) longName += "\xC3\xBC";  // ü, two bytes each
  const std::string cut = weather::cleanName(longName, latin1);
  CHECK(cut.size() <= weather::kMaxName);
  CHECK(cut.size() % 2 == 0);
}

void testPlaceLabelIsNeverEmpty() {
  CHECK_EQ_STR(weather::placeLabel("Bern", "Switzerland", 46.9, 7.4), "Bern");
  CHECK_EQ_STR(weather::placeLabel("", "Switzerland", 46.9, 7.4), "Switzerland");
  CHECK_EQ_STR(weather::placeLabel("", "", -33.87, 151.21), "33.87S 151.21E");
  CHECK_EQ_STR(weather::placeLabel("", "", 40.71, -74.01), "40.71N 74.01W");
}

void testPlaceTitleTellsSameNamesApart() {
  // The first part of the region: the state for a Springfield, the country
  // where the region is only that.
  CHECK_EQ_STR(weather::placeTitle("Springfield", "Illinois, United States", 39.8, -89.6), "Springfield, Illinois");
  CHECK_EQ_STR(weather::placeTitle("Springfield", "Missouri, United States", 37.2, -93.3), "Springfield, Missouri");
  CHECK_EQ_STR(weather::placeTitle("Lisbon", "Portugal", 38.7, -9.1), "Lisbon, Portugal");
  // A region that opens with the name again skips to what follows it.
  CHECK_EQ_STR(weather::placeTitle("Zürich", "Zurich, Switzerland", 47.4, 8.6), "Zürich, Switzerland");
  CHECK_EQ_STR(weather::placeTitle("Singapore", "", 1.3, 103.9), "Singapore");
  CHECK_EQ_STR(weather::placeTitle("Singapore", "Singapore", 1.3, 103.9), "Singapore");
  // A name that could not be drawn is already the region or the coordinates:
  // nothing is said twice.
  CHECK_EQ_STR(weather::placeTitle("", "Russia", 55.8, 37.6), "Russia");
  CHECK_EQ_STR(weather::placeTitle("", "", 35.69, 139.69), "35.69N 139.69E");
}

void testCleanQuery() {
  CHECK_EQ_STR(weather::cleanQuery("  new   york "), "new york");
  CHECK_EQ_STR(weather::cleanQuery("z"), "z");
  CHECK(weather::cleanQuery("z").size() < weather::kMinQuery);
  std::string longQuery(80, 'a');
  CHECK(weather::cleanQuery(longQuery).size() == weather::kMaxQuery);
}

// --- Requests ----------------------------------------------------------------

void testUrls() {
  const std::string f = weather::forecastUrl("https://api.open-meteo.com/v1/forecast", 47.36667, 8.55);
  CHECK(f.rfind("https://api.open-meteo.com/v1/forecast?", 0) == 0);
  CHECK(f.find("latitude=47.3667") != std::string::npos);
  CHECK(f.find("longitude=8.5500") != std::string::npos);
  CHECK(f.find("timezone=auto") != std::string::npos);
  CHECK(f.find("forecast_days=7") != std::string::npos);
  CHECK(f.find("current=temperature_2m,weather_code,is_day") != std::string::npos);
  CHECK(f.find("daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max") !=
        std::string::npos);
  // Celsius always: the units are the screen's business, so switching them
  // needs no new request.
  CHECK(f.find("fahrenheit") == std::string::npos);
  CHECK(weather::forecastUrl("http://x/f", -33.87, -151.21).find("latitude=-33.8700&longitude=-151.2100") !=
        std::string::npos);

  const std::string g = weather::geocodeUrl("https://geocoding-api.open-meteo.com/v1/search", "new york");
  CHECK(g.rfind("https://geocoding-api.open-meteo.com/v1/search?name=new%20york", 0) == 0);
  CHECK(g.find("count=10") != std::string::npos);
  CHECK(g.find("language=en") != std::string::npos);
  CHECK(g.find("format=json") != std::string::npos);
  CHECK_EQ_STR(weather::urlEncode("a&b=c/d?é"), "a%26b%3Dc%2Fd%3F%C3%A9");
  CHECK_EQ_STR(weather::urlEncode("Saint-Jean_du.Gard~"), "Saint-Jean_du.Gard~");
}

// --- The card ----------------------------------------------------------------

void testPlaceRoundTrips() {
  weather::Place p;
  p.name = "Zürich";
  p.region = "Zurich, Switzerland";
  std::strcpy(p.country, "CH");
  p.lat = 47.36667;
  p.lon = 8.55;
  p.units = weather::Units::Fahrenheit;
  p.unitsChosen = true;
  weather::Place q;
  CHECK(weather::parsePlace(weather::serializePlace(p), q));
  CHECK(q.name == p.name);
  CHECK(q.region == p.region);
  CHECK_EQ_STR(q.country, "CH");
  CHECK(std::fabs(q.lat - p.lat) < 1e-5);
  CHECK(std::fabs(q.lon - p.lon) < 1e-5);
  CHECK(q.units == weather::Units::Fahrenheit);
  CHECK(q.unitsChosen);

  // An empty region survives as empty, not as the next line.
  p.region.clear();
  CHECK(weather::parsePlace(weather::serializePlace(p), q));
  CHECK(q.region.empty());
  CHECK(q.name == "Zürich");
}

void testPlaceRefusesWhatItCannotRead() {
  weather::Place q;
  CHECK(!weather::parsePlace("", q));
  CHECK(!weather::parsePlace("weather-place 9\nlat 1\nlon 2\nname X\n", q));
  CHECK(!weather::parsePlace("weather-place 1\nname X\n", q));        // no coordinates
  CHECK(!weather::parsePlace("weather-place 1\nlat 1\nlon 2\n", q));  // no name
  CHECK(!weather::parsePlace("weather-place 1\nlat 91\nlon 2\nname X\n", q));
  CHECK(!weather::parsePlace("weather-place 1\nlat abc\nlon 2\nname X\n", q));
  // Carriage returns from a card edited on another machine are not part of
  // the name.
  CHECK(weather::parsePlace("weather-place 1\r\nlat 1\r\nlon 2\r\nname Bern\r\n", q));
  CHECK(q.name == "Bern");
}

weather::Cache zurichCache() {
  weather::Cache c;
  weather::parseForecast(kForecastJson, c.forecast);
  c.fetchedUtc = kNovember17;
  c.lat = 47.36667;
  c.lon = 8.55;
  return c;
}

void testCacheRoundTrips() {
  const weather::Cache c = zurichCache();
  weather::Cache d;
  CHECK(weather::parseCache(weather::serializeCache(c), d));
  CHECK(d.fetchedUtc == c.fetchedUtc);
  CHECK_EQ_STR(d.forecast.localTime, c.forecast.localTime);
  CHECK(d.forecast.nowC == c.forecast.nowC);
  CHECK(d.forecast.nowCode == c.forecast.nowCode);
  CHECK(d.forecast.isDay == c.forecast.isDay);
  CHECK(d.forecast.utcOffset == c.forecast.utcOffset);
  CHECK(d.forecast.dayCount == c.forecast.dayCount);
  bool same = true;
  for (int i = 0; i < c.forecast.dayCount; ++i) {
    const weather::Day& a = c.forecast.days[i];
    const weather::Day& b = d.forecast.days[i];
    same = same && a.year == b.year && a.month == b.month && a.day == b.day && a.code == b.code && a.maxC == b.maxC &&
           a.minC == b.minC && a.rainPct == b.rainPct;
  }
  CHECK(same);
  // The null survives as a null.
  CHECK(d.forecast.days[6].rainPct == -1);
  CHECK(std::fabs(d.lat - c.lat) < 1e-5 && std::fabs(d.lon - c.lon) < 1e-5);
}

void testCacheRefusesDamage() {
  weather::Cache d;
  CHECK(!weather::parseCache("", d));
  CHECK(!weather::parseCache("weather-forecast 2\n", d));
  const std::string whole = weather::serializeCache(zurichCache());
  // A write cut off before any day: not a forecast.
  const size_t firstDay = whole.find("\nday ");
  CHECK(firstDay != std::string::npos);
  CHECK(!weather::parseCache(whole.substr(0, firstDay + 1), d));
  CHECK(!weather::parseCache("garbage\nnow 1 2 3\n", d));
}

void testCacheBelongsToItsPlace() {
  const weather::Cache c = zurichCache();
  weather::Place here;
  here.lat = 47.36667;
  here.lon = 8.55;
  CHECK(weather::cacheIsFor(c, here));
  weather::Place there = here;
  there.lat = 43.41678;
  there.lon = -81.62498;
  CHECK(!weather::cacheIsFor(c, there));
}

// --- Staleness ---------------------------------------------------------------

void testFirstDayFollowsTheClock() {
  const weather::Cache c = zurichCache();
  CHECK(weather::firstDay(c, 0) == 0);
  CHECK(weather::firstDay(c, kNovember17) == 0);
  // 23:30 in Zurich is still the 17th; 00:30 is the 18th.
  CHECK(weather::firstDay(c, kNovember17 + 9 * 3600 + 23 * 60) == 0);
  CHECK(weather::firstDay(c, kNovember17 + 10 * 3600 + 23 * 60) == 1);
  CHECK(weather::firstDay(c, kNovember17 + 3 * kDay) == 3);
  CHECK(weather::firstDay(c, kNovember17 + 6 * kDay) == 6);
  // Past the whole forecast: no day of it is today or later, and a week
  // that has happened is not drawn as the coming one.
  CHECK(weather::firstDay(c, kNovember17 + 7 * kDay) == -1);
  CHECK(weather::firstDay(c, kNovember17 + 30 * kDay) == -1);
  // A clock behind the forecast (set wrong) does not skip anything.
  CHECK(weather::firstDay(c, kNovember17 - 5 * kDay) == 0);
}

// A reading is "now" only on the day it was taken, in the place's own time.
void testNowIsOnlyTodaysReading() {
  const weather::Cache c = zurichCache();
  // The day it was read: the reading, as it was.
  weather::Now n = weather::nowFor(c, kNovember17 + 3600);
  CHECK(n.reading);
  CHECK(n.tempC == c.forecast.nowC);
  CHECK(n.code == c.forecast.nowCode);
  // No clock: nothing says it is old, so it stands.
  CHECK(weather::nowFor(c, 0).reading);
  // The next day, and three days on: no reading for now, and the sky is
  // today's row's, as a day.
  for (const int later : {1, 3}) {
    n = weather::nowFor(c, kNovember17 + later * kDay);
    CHECK(!n.reading);
    CHECK(n.tempC == weather::kNoTemp);
    CHECK(n.code == c.forecast.days[later].code);
    CHECK(n.isDay);
  }
  // 00:30 in Zurich is already the next day there, though 23:30 UTC is not.
  CHECK(!weather::nowFor(c, kNovember17 + 10 * 3600 + 23 * 60).reading);
  // Every day past: no reading and no sky.
  n = weather::nowFor(c, kNovember17 + 30 * kDay);
  CHECK(!n.reading);
  CHECK(n.code == -1);
}

void testUpdatedLabel() {
  weather::Cache c = zurichCache();
  // 13:07 UTC is 14:07 in Zurich.
  CHECK_EQ_STR(weather::updatedLabel(c, kNovember17, false), "UPDATED 14:07");
  CHECK_EQ_STR(weather::updatedLabel(c, kNovember17 + 3600, false), "UPDATED 14:07");
  CHECK_EQ_STR(weather::updatedLabel(c, 0, false), "UPDATED 14:07");
  CHECK_EQ_STR(weather::updatedLabel(c, kNovember17, true), "UPDATED 2:07 PM");
  CHECK_EQ_STR(weather::updatedLabel(c, kNovember17 + kDay, false), "UPDATED YESTERDAY 14:07");
  CHECK_EQ_STR(weather::updatedLabel(c, kNovember17 + 2 * kDay, false), "UPDATED TUE 14:07");
  CHECK_EQ_STR(weather::updatedLabel(c, kNovember17 + 6 * kDay, false), "UPDATED TUE 14:07");
  CHECK_EQ_STR(weather::updatedLabel(c, kNovember17 + 7 * kDay, false), "UPDATED 17 NOV");
  // The device clock was not trusted when this was read: the forecast's own
  // reading time stands in for it.
  c.fetchedUtc = 0;
  CHECK_EQ_STR(weather::updatedLabel(c, 0, false), "UPDATED 14:00");
  CHECK_EQ_STR(weather::updatedLabel(c, kNovember17 + kDay, false), "UPDATED YESTERDAY 14:00");
  // Midnight and noon in twelve-hour time.
  c.fetchedUtc = kNovember17 - 14 * 3600 - 7 * 60;  // 00:00 in Zurich
  CHECK_EQ_STR(weather::updatedLabel(c, 0, true), "UPDATED 12:00 AM");
  c.fetchedUtc += 12 * 3600;
  CHECK_EQ_STR(weather::updatedLabel(c, 0, true), "UPDATED 12:00 PM");
}

// --- Days, skies, degrees ----------------------------------------------------

void testWeekdays() {
  CHECK_EQ_STR(weather::weekdayShort(2026, 11, 17), "TUE");
  CHECK_EQ_STR(weather::weekdayShort(2026, 11, 22), "SUN");
  CHECK_EQ_STR(weather::weekdayShort(2000, 2, 29), "TUE");
  CHECK_EQ_STR(weather::weekdayShort(1970, 1, 1), "THU");
  CHECK_EQ_STR(weather::weekdayShort(2027, 1, 1), "FRI");
  weather::Day d;
  d.year = 2026;
  d.month = 11;
  d.day = 21;
  CHECK_EQ_STR(weather::weekdayShort(d), "SAT");
  CHECK_EQ_STR(weather::weekdayShort(2026, 13, 1), "");
}

void testSkies() {
  CHECK_EQ_STR(weather::skyWords(0), "CLEAR");
  CHECK_EQ_STR(weather::skyWords(2), "PARTLY CLOUDY");
  CHECK_EQ_STR(weather::skyWords(3), "OVERCAST");
  CHECK_EQ_STR(weather::skyWords(48), "FOG");
  CHECK_EQ_STR(weather::skyWords(63), "RAIN");
  CHECK_EQ_STR(weather::skyWords(65), "HEAVY RAIN");
  CHECK_EQ_STR(weather::skyWords(75), "HEAVY SNOW");
  CHECK_EQ_STR(weather::skyWords(81), "SHOWERS");
  CHECK_EQ_STR(weather::skyWords(95), "THUNDERSTORM");
  CHECK_EQ_STR(weather::skyWords(-1), "");
  CHECK_EQ_STR(weather::skyWords(42), "");
  CHECK(weather::skyIcon(0, true) == weather::Icon::Sun);
  CHECK(weather::skyIcon(0, false) == weather::Icon::Moon);
  CHECK(weather::skyIcon(2, false) == weather::Icon::CloudMoon);
  CHECK(weather::skyIcon(3, false) == weather::Icon::Cloud);
  CHECK(weather::skyIcon(45, true) == weather::Icon::Fog);
  CHECK(weather::skyIcon(55, true) == weather::Icon::Drizzle);
  CHECK(weather::skyIcon(82, true) == weather::Icon::Rain);
  CHECK(weather::skyIcon(86, true) == weather::Icon::Snow);
  CHECK(weather::skyIcon(99, true) == weather::Icon::Thunder);
  // A null code, or one nobody documents, has no picture: not a cloud.
  CHECK(weather::skyIcon(-1, true) == weather::Icon::None);
  CHECK(weather::skyIcon(42, true) == weather::Icon::None);
  // Under snow the chance is a chance of snow.
  CHECK_EQ_STR(weather::chanceWord(71), "SNOW");
  CHECK_EQ_STR(weather::chanceWord(77), "SNOW");
  CHECK_EQ_STR(weather::chanceWord(85), "SNOW");
  CHECK_EQ_STR(weather::chanceWord(86), "SNOW");
  CHECK_EQ_STR(weather::chanceWord(61), "RAIN");
  CHECK_EQ_STR(weather::chanceWord(67), "RAIN");
  CHECK_EQ_STR(weather::chanceWord(99), "RAIN");
  CHECK_EQ_STR(weather::chanceWord(-1), "RAIN");
  // Every code Open-Meteo documents has words; nothing it documents is blank.
  const int documented[] = {0,  1,  2,  3,  45, 48, 51, 53, 55, 56, 57, 61, 63, 65,
                            66, 67, 71, 73, 75, 77, 80, 81, 82, 85, 86, 95, 96, 99};
  bool allWorded = true;
  for (const int code : documented) allWorded = allWorded && std::strlen(weather::skyWords(code)) > 0;
  CHECK(allWorded);
}

void testUnits() {
  CHECK(weather::displayTemp(73, weather::Units::Celsius) == 7);
  CHECK(weather::displayTemp(75, weather::Units::Celsius) == 8);
  CHECK(weather::displayTemp(-25, weather::Units::Celsius) == -3);
  CHECK(weather::displayTemp(-24, weather::Units::Celsius) == -2);
  CHECK(weather::displayTemp(-4, weather::Units::Celsius) == 0);
  CHECK(weather::displayTemp(0, weather::Units::Fahrenheit) == 32);
  CHECK(weather::displayTemp(1000, weather::Units::Fahrenheit) == 212);
  CHECK(weather::displayTemp(-400, weather::Units::Fahrenheit) == -40);
  CHECK(weather::displayTemp(73, weather::Units::Fahrenheit) == 45);  // 45.14
  CHECK_EQ_STR(weather::tempText(-38, weather::Units::Celsius), "-4");
  CHECK_EQ_STR(weather::tempText(weather::kNoTemp, weather::Units::Celsius), "--");
  CHECK_EQ_STR(weather::rainText(65), "65%");
  CHECK_EQ_STR(weather::rainText(-1), "");
  CHECK(weather::defaultUnitsFor("US") == weather::Units::Fahrenheit);
  CHECK(weather::defaultUnitsFor("us") == weather::Units::Fahrenheit);
  CHECK(weather::defaultUnitsFor("PR") == weather::Units::Fahrenheit);
  CHECK(weather::defaultUnitsFor("LR") == weather::Units::Fahrenheit);
  CHECK(weather::defaultUnitsFor("BS") == weather::Units::Fahrenheit);
  CHECK(weather::defaultUnitsFor("CH") == weather::Units::Celsius);
  CHECK(weather::defaultUnitsFor("GB") == weather::Units::Celsius);
  CHECK(weather::defaultUnitsFor("") == weather::Units::Celsius);
  CHECK(weather::defaultUnitsFor(nullptr) == weather::Units::Celsius);
}

// The most heap a request's answer costs while it is read: the tree the
// reader builds, held only while the answer is parsed. Bounded so a change
// that makes it balloon fails here rather than on a device mid-request.
void testParsingFitsTheHeap() {
  std::string tenPlaces = R"({"results":[)";
  for (int i = 0; i < 10; ++i) {
    tenPlaces += std::string(i ? "," : "") +
                 R"({"id":4250542,"name":"Springfield","latitude":39.80172,"longitude":-89.64371,"elevation":182.0,)"
                 R"("feature_code":"PPLA","country_code":"US","admin1_id":4896861,"admin2_id":4250542,)"
                 R"("timezone":"America/Chicago","population":116565,"postcodes":["62701","62702","62703",)"
                 R"("62704","62705","62706","62707","62708"],"country_id":6252001,"country":"United States",)"
                 R"("admin1":"Illinois","admin2":"Sangamon"})";
  }
  tenPlaces += R"(],"generationtime_ms":0.9})";

  weather::Forecast f;
  heap::peak = heap::current;
  const size_t before = heap::current;
  CHECK(weather::parseForecast(kForecastJson, f));
  const size_t forecastPeak = heap::peak - before;

  std::vector<weather::Match> m;
  m.reserve(weather::kMaxMatches);
  heap::peak = heap::current;
  const size_t before2 = heap::current;
  CHECK(weather::parseMatches(tenPlaces, latin1, m));
  const size_t matchesPeak = heap::peak - before2;
  CHECK(m.size() == 10);
  std::printf("heap while parsing: forecast %zu bytes (answer %zu), ten places %zu bytes (answer %zu)\n", forecastPeak,
              std::strlen(kForecastJson), matchesPeak, tenPlaces.size());
  CHECK(forecastPeak < 48 * 1024);
  CHECK(matchesPeak < 96 * 1024);
  // Nothing outlives the parse but what it returns.
  CHECK(heap::current >= before2);
}

}  // namespace

int main() {
  testForecastReadsNowAndSevenDays();
  testForecastNullsAreNotZero();
  testForecastRefusesWhatIsNotOne();
  testForecastTakesAtMostSevenDays();
  testForecastSkipsAMalformedDate();
  testMatchesReadNameRegionAndPlace();
  testNoMatchIsAnAnswerNotAFailure();
  testMalformedMatchesFail();
  testAMatchWithoutCoordinatesIsDropped();
  testMatchesAreCapped();
  testARegionDoesNotRepeatTheName();
  testNonLatinNamesKeepWhatCanBeDrawn();
  testCleanNameKeepsLettersAndDropsTheRest();
  testPlaceLabelIsNeverEmpty();
  testPlaceTitleTellsSameNamesApart();
  testCleanQuery();
  testUrls();
  testPlaceRoundTrips();
  testPlaceRefusesWhatItCannotRead();
  testCacheRoundTrips();
  testCacheRefusesDamage();
  testCacheBelongsToItsPlace();
  testFirstDayFollowsTheClock();
  testNowIsOnlyTodaysReading();
  testUpdatedLabel();
  testWeekdays();
  testSkies();
  testUnits();
  testParsingFitsTheHeap();
  std::printf("%d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}

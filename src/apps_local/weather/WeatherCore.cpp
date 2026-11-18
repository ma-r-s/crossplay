#include "WeatherCore.h"

#include <Utf8.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace weather {
namespace {

// --- A small JSON reader -----------------------------------------------------
//
// Both answers are a few kilobytes, so they are read whole into a tree and
// walked by key. Bounded three ways so a hostile or broken answer costs a
// refusal rather than the stack or the heap: nesting depth, node count, and
// the caller's cap on how many bytes it downloaded at all.

struct Json {
  enum class Type : uint8_t { Null, Bool, Number, String, Array, Object };
  Type type = Type::Null;
  bool boolean = false;
  double number = 0;
  std::string text;
  std::vector<Json> items;
  std::vector<std::pair<std::string, Json>> members;

  const Json* get(const char* key) const {
    if (type != Type::Object) return nullptr;
    for (const auto& member : members) {
      if (member.first == key) return &member.second;
    }
    return nullptr;
  }
  bool isNumber() const { return type == Type::Number; }
};

class JsonReader {
 public:
  explicit JsonReader(const std::string& text) : p_(text.data()), end_(text.data() + text.size()) {}

  bool document(Json& out) {
    space();
    if (!value(out, 0)) return false;
    space();
    return p_ == end_;
  }

 private:
  static constexpr int kMaxDepth = 24;
  static constexpr int kMaxNodes = 6000;

  void space() {
    while (p_ < end_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r')) ++p_;
  }

  bool literal(const char* word) {
    const size_t n = std::strlen(word);
    if (static_cast<size_t>(end_ - p_) < n || std::memcmp(p_, word, n) != 0) return false;
    p_ += n;
    return true;
  }

  bool value(Json& out, const int depth) {
    if (depth > kMaxDepth || ++nodes_ > kMaxNodes || p_ >= end_) return false;
    switch (*p_) {
      case '{':
        return object(out, depth);
      case '[':
        return array(out, depth);
      case '"':
        out.type = Json::Type::String;
        return string(out.text);
      case 't':
        out.type = Json::Type::Bool;
        out.boolean = true;
        return literal("true");
      case 'f':
        out.type = Json::Type::Bool;
        out.boolean = false;
        return literal("false");
      case 'n':
        out.type = Json::Type::Null;
        return literal("null");
      default:
        return number(out);
    }
  }

  static bool digit(const char c) { return c >= '0' && c <= '9'; }

  bool number(Json& out) {
    const char* start = p_;
    if (p_ < end_ && *p_ == '-') ++p_;
    if (p_ >= end_ || !digit(*p_)) return false;
    while (p_ < end_ && digit(*p_)) ++p_;
    if (p_ < end_ && *p_ == '.') {
      ++p_;
      if (p_ >= end_ || !digit(*p_)) return false;
      while (p_ < end_ && digit(*p_)) ++p_;
    }
    if (p_ < end_ && (*p_ == 'e' || *p_ == 'E')) {
      ++p_;
      if (p_ < end_ && (*p_ == '+' || *p_ == '-')) ++p_;
      if (p_ >= end_ || !digit(*p_)) return false;
      while (p_ < end_ && digit(*p_)) ++p_;
    }
    char buffer[40];
    const size_t length = static_cast<size_t>(p_ - start);
    if (length >= sizeof(buffer)) return false;
    std::memcpy(buffer, start, length);
    buffer[length] = '\0';
    out.type = Json::Type::Number;
    out.number = std::strtod(buffer, nullptr);
    return std::isfinite(out.number);
  }

  bool hex4(uint32_t& out) {
    if (end_ - p_ < 4) return false;
    out = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = *p_++;
      out <<= 4;
      if (c >= '0' && c <= '9') {
        out |= static_cast<uint32_t>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        out |= static_cast<uint32_t>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        out |= static_cast<uint32_t>(c - 'A' + 10);
      } else {
        return false;
      }
    }
    return true;
  }

  bool string(std::string& out) {
    ++p_;  // the opening quote
    while (p_ < end_) {
      const char c = *p_++;
      if (c == '"') return true;
      if (static_cast<unsigned char>(c) < 0x20) return false;
      if (c != '\\') {
        out += c;
        continue;
      }
      if (p_ >= end_) return false;
      const char e = *p_++;
      switch (e) {
        case '"':
        case '\\':
        case '/':
          out += e;
          break;
        case 'b':
        case 'f':
        case 'n':
        case 'r':
        case 't':
          // Control characters a name has no use for; a space keeps the words
          // either side apart.
          out += ' ';
          break;
        case 'u': {
          uint32_t cp = 0;
          if (!hex4(cp)) return false;
          if (cp >= 0xD800 && cp <= 0xDBFF) {
            uint32_t low = 0;
            if (end_ - p_ >= 6 && p_[0] == '\\' && p_[1] == 'u') {
              p_ += 2;
              if (!hex4(low)) return false;
            }
            cp = (low >= 0xDC00 && low <= 0xDFFF) ? 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00) : 0xFFFD;
          } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            cp = 0xFFFD;
          }
          utf8AppendCodepoint(cp, out);
          break;
        }
        default:
          return false;
      }
    }
    return false;
  }

  bool array(Json& out, const int depth) {
    out.type = Json::Type::Array;
    ++p_;
    space();
    if (p_ < end_ && *p_ == ']') {
      ++p_;
      return true;
    }
    while (true) {
      out.items.emplace_back();
      space();
      if (!value(out.items.back(), depth + 1)) return false;
      space();
      if (p_ >= end_) return false;
      if (*p_ == ',') {
        ++p_;
        continue;
      }
      if (*p_ == ']') {
        ++p_;
        return true;
      }
      return false;
    }
  }

  bool object(Json& out, const int depth) {
    out.type = Json::Type::Object;
    ++p_;
    space();
    if (p_ < end_ && *p_ == '}') {
      ++p_;
      return true;
    }
    while (true) {
      space();
      if (p_ >= end_ || *p_ != '"') return false;
      out.members.emplace_back();
      if (!string(out.members.back().first)) return false;
      space();
      if (p_ >= end_ || *p_ != ':') return false;
      ++p_;
      space();
      if (!value(out.members.back().second, depth + 1)) return false;
      space();
      if (p_ >= end_) return false;
      if (*p_ == ',') {
        ++p_;
        continue;
      }
      if (*p_ == '}') {
        ++p_;
        return true;
      }
      return false;
    }
  }

  const char* p_;
  const char* end_;
  int nodes_ = 0;
};

// --- Calendar arithmetic ------------------------------------------------------
// Howard Hinnant's days-from-civil, so no time zone database and no libc
// time functions are involved: the offset Open-Meteo sends is the whole story.

int64_t daysFromCivil(int64_t y, const int m, const int d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const int64_t yoe = y - era * 400;
  const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

void civilFromDays(int64_t z, int& y, int& m, int& d) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const int64_t doe = z - era * 146097;
  const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int64_t mp = (5 * doy + 2) / 153;
  d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
  y = static_cast<int>(yoe + era * 400 + (m <= 2));
}

int64_t floorDiv(const int64_t a, const int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

constexpr const char* kMonths[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

const char* weekdayOf(const int64_t days) {
  const int64_t w = (days + 4) % 7;  // 1970-01-01 was a Thursday
  return kWeekdayNames[w < 0 ? w + 7 : w];
}

bool validDate(const int y, const int m, const int d) {
  return y >= 1900 && y <= 2200 && m >= 1 && m <= 12 && d >= 1 && d <= 31;
}

// "2026-11-17", exactly ten characters.
bool parseDate(const std::string& text, int& y, int& m, int& d) {
  if (text.size() != 10 || text[4] != '-' || text[7] != '-') return false;
  for (const size_t i : {0u, 1u, 2u, 3u, 5u, 6u, 8u, 9u}) {
    if (text[i] < '0' || text[i] > '9') return false;
  }
  y = std::atoi(text.substr(0, 4).c_str());
  m = std::atoi(text.substr(5, 2).c_str());
  d = std::atoi(text.substr(8, 2).c_str());
  return validDate(y, m, d);
}

// "2026-11-17T14:00".
bool parseLocalTime(const char* text, int64_t& day, int& hour, int& minute) {
  const std::string s(text);
  int y = 0, m = 0, d = 0;
  if (s.size() < 16 || s[10] != 'T' || s[13] != ':' || !parseDate(s.substr(0, 10), y, m, d)) return false;
  for (const size_t i : {11u, 12u, 14u, 15u}) {
    if (s[i] < '0' || s[i] > '9') return false;
  }
  hour = std::atoi(s.substr(11, 2).c_str());
  minute = std::atoi(s.substr(14, 2).c_str());
  if (hour > 23 || minute > 59) return false;
  day = daysFromCivil(y, m, d);
  return true;
}

int16_t tenths(const double celsius) {
  const double t = std::round(celsius * 10.0);
  if (!(t > -32767.0 && t < 32767.0)) return kNoTemp;
  return static_cast<int16_t>(t);
}

const Json* at(const Json* array, const size_t i) {
  if (array == nullptr || array->type != Json::Type::Array || i >= array->items.size()) return nullptr;
  return &array->items[i];
}

// --- Names ---------------------------------------------------------------------

// One codepoint off the front of [p, end), strictly: an invalid or truncated
// sequence answers 0xFFFFFFFF and consumes one byte.
uint32_t nextCodepoint(const unsigned char*& p, const unsigned char* end) {
  constexpr uint32_t kBad = 0xFFFFFFFFu;
  const unsigned char c = *p++;
  if (c < 0x80) return c;
  int extra = 0;
  uint32_t cp = 0;
  if ((c & 0xE0) == 0xC0) {
    extra = 1;
    cp = c & 0x1F;
  } else if ((c & 0xF0) == 0xE0) {
    extra = 2;
    cp = c & 0x0F;
  } else if ((c & 0xF8) == 0xF0) {
    extra = 3;
    cp = c & 0x07;
  } else {
    return kBad;
  }
  if (end - p < extra) return kBad;
  for (int i = 0; i < extra; ++i) {
    if ((p[i] & 0xC0) != 0x80) return kBad;
  }
  for (int i = 0; i < extra; ++i) cp = (cp << 6) | (*p++ & 0x3F);
  // Overlong forms and surrogates are not text.
  if ((extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) || (extra == 3 && cp < 0x10000) || cp > 0x10FFFF ||
      (cp >= 0xD800 && cp <= 0xDFFF)) {
    return kBad;
  }
  return cp;
}

bool isNamePunctuation(const uint32_t cp) {
  return cp == '-' || cp == '\'' || cp == '.' || cp == ',' || cp == '(' || cp == ')' || cp == '/';
}

bool isSpace(const uint32_t cp) {
  return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == 0x0B || cp == 0x0C || cp == 0xA0;
}

// A letter of some script, as far as a place name needs to know: everything
// from U+00C0 up that is not a symbol, a mark, a format character or private.
// Letters the face cannot draw are then dropped by the caller, so this only
// has to keep symbols from passing as letters.
bool isLetterlike(const uint32_t cp) {
  if (cp < 0x80) return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || (cp >= '0' && cp <= '9');
  if (cp < 0xC0 || cp == 0xD7 || cp == 0xF7) return false;  // Latin-1's symbols
  if (cp >= 0x0300 && cp <= 0x036F) return false;           // combining marks left over
  if (cp >= 0x2000 && cp <= 0x2BFF) return false;           // punctuation, arrows, symbols
  if (cp >= 0x3000 && cp <= 0x303F) return false;           // CJK punctuation
  if (cp >= 0xE000 && cp <= 0xF8FF) return false;           // private use
  if (cp >= 0xFE00 && cp <= 0xFE0F) return false;           // variation selectors
  if (cp >= 0xFFF0 && cp <= 0xFFFF) return false;           // specials
  if (cp >= 0x1F000 && cp <= 0x1FFFF) return false;         // emoji and pictographs
  if (cp >= 0xE0000) return false;                          // tags
  return true;
}

// Letters that are a base letter with a stroke or without a dot, which
// Unicode gives no decomposition, so utf8DecomposedBase cannot reduce them:
// without this "Łódź" would lose its first letter, not its stroke.
uint32_t strokeBase(const uint32_t cp) {
  struct Pair {
    uint32_t letter;
    uint32_t base;
  };
  static constexpr Pair kStroked[] = {
      {0x0110, 'D'}, {0x0111, 'd'}, {0x0126, 'H'}, {0x0127, 'h'}, {0x0131, 'i'}, {0x013F, 'L'},
      {0x0140, 'l'}, {0x0141, 'L'}, {0x0142, 'l'}, {0x0166, 'T'}, {0x0167, 't'}, {0x0180, 'b'},
      {0x0197, 'I'}, {0x01B5, 'Z'}, {0x01B6, 'z'}, {0x01E4, 'G'}, {0x01E5, 'g'}, {0x0268, 'i'},
  };
  for (const Pair& pair : kStroked) {
    if (pair.letter == cp) return pair.base;
  }
  return 0;
}

void removeEmptyBrackets(std::string& s) {
  for (size_t open = s.find('('); open != std::string::npos; open = s.find('(', open)) {
    size_t close = open + 1;
    while (close < s.size() && s[close] == ' ') ++close;
    if (close < s.size() && s[close] == ')') {
      s.erase(open, close - open + 1);
    } else {
      ++open;
    }
  }
}

std::string collapseAndTrim(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  bool pendingSpace = false;
  for (const char c : s) {
    if (c == ' ') {
      pendingSpace = !out.empty();
      continue;
    }
    if (pendingSpace) out += ' ';
    pendingSpace = false;
    out += c;
  }
  return out;
}

// One name spelled two ways (the canton "Zurich" for the city with the
// u-umlaut, "SAO PAULO" for the one with the a-tilde): compared letter by
// letter on base letters, case folded, so the region never repeats the place
// it qualifies.
bool sameName(const std::string& a, const std::string& b) {
  const auto key = [](const std::string& s) {
    std::string out;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s.data());
    const unsigned char* end = p + s.size();
    while (p < end) {
      uint32_t cp = nextCodepoint(p, end);
      if (cp == 0xFFFFFFFFu) continue;
      if (const uint32_t base = utf8DecomposedBase(cp)) cp = base;
      if (const uint32_t base = strokeBase(cp)) cp = base;
      if (cp >= 'A' && cp <= 'Z') cp = cp - 'A' + 'a';
      utf8AppendCodepoint(cp, out);
    }
    return out;
  };
  return key(a) == key(b);
}

std::string joinRegion(const std::string& name, const std::string& admin, const std::string& country) {
  std::string region;
  if (!admin.empty() && !sameName(admin, name)) region = admin;
  if (!country.empty() && !sameName(country, name) && !sameName(country, admin)) {
    if (!region.empty()) region += ", ";
    region += country;
  }
  return region;
}

std::string coordinates(const double lat, const double lon) {
  char buffer[40];
  std::snprintf(buffer, sizeof(buffer), "%.2f%c %.2f%c", std::fabs(lat), lat < 0 ? 'S' : 'N', std::fabs(lon),
                lon < 0 ? 'W' : 'E');
  return buffer;
}

bool validLatLon(const double lat, const double lon) {
  return std::isfinite(lat) && std::isfinite(lon) && lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180;
}

void copyCountry(char out[3], const std::string& code) {
  out[0] = '\0';
  if (code.size() != 2) return;
  for (int i = 0; i < 2; ++i) {
    char c = code[static_cast<size_t>(i)];
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    if (c < 'A' || c > 'Z') return;
    out[i] = c;
  }
  out[2] = '\0';
}

// --- The card's line format ----------------------------------------------------

std::vector<std::string> lines(const std::string& text) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    std::string line = text.substr(start, end - start);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    out.push_back(line);
    start = end + 1;
  }
  return out;
}

// The value after "key ", or false when the line is not that key.
bool valueOf(const std::string& line, const char* key, std::string& value) {
  const size_t n = std::strlen(key);
  if (line.size() < n || line.compare(0, n, key) != 0) return false;
  if (line.size() == n) {
    value.clear();
    return true;
  }
  if (line[n] != ' ') return false;
  value = line.substr(n + 1);
  return true;
}

bool toDouble(const std::string& text, double& out) {
  if (text.empty()) return false;
  char* end = nullptr;
  out = std::strtod(text.c_str(), &end);
  return end != nullptr && *end == '\0' && std::isfinite(out);
}

bool toLong(const std::string& text, long long& out) {
  if (text.empty()) return false;
  char* end = nullptr;
  out = std::strtoll(text.c_str(), &end, 10);
  return end != nullptr && *end == '\0';
}

std::vector<std::string> words(const std::string& text) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < text.size()) {
    while (i < text.size() && text[i] == ' ') ++i;
    if (i >= text.size()) break;
    const size_t start = i;
    while (i < text.size() && text[i] != ' ') ++i;
    out.push_back(text.substr(start, i - start));
  }
  return out;
}

std::string tempToken(const int16_t t) { return t == kNoTemp ? std::string("-") : std::to_string(t); }

bool tempFromToken(const std::string& token, int16_t& out) {
  if (token == "-") {
    out = kNoTemp;
    return true;
  }
  long long v = 0;
  if (!toLong(token, v) || v <= -32768 || v > 32767) return false;
  out = static_cast<int16_t>(v);
  return true;
}

constexpr const char* kPlaceHeader = "weather-place 1";
constexpr const char* kCacheHeader = "weather-forecast 1";

}  // namespace

// --- Reading Open-Meteo ---------------------------------------------------------

bool parseForecast(const std::string& json, Forecast& out) {
  out = Forecast{};
  Json root;
  if (!JsonReader(json).document(root) || root.type != Json::Type::Object) return false;

  const Json* current = root.get("current");
  const Json* now = current ? current->get("temperature_2m") : nullptr;
  if (now == nullptr || !now->isNumber()) return false;
  out.nowC = tenths(now->number);
  if (const Json* code = current->get("weather_code"); code && code->isNumber()) {
    out.nowCode = static_cast<int16_t>(code->number);
  }
  if (const Json* isDay = current->get("is_day"); isDay && isDay->isNumber()) out.isDay = isDay->number != 0;
  if (const Json* time = current->get("time"); time && time->type == Json::Type::String) {
    std::snprintf(out.localTime, sizeof(out.localTime), "%s", time->text.c_str());
  }
  if (const Json* offset = root.get("utc_offset_seconds"); offset && offset->isNumber()) {
    out.utcOffset = static_cast<int32_t>(offset->number);
  }

  const Json* daily = root.get("daily");
  const Json* dates = daily ? daily->get("time") : nullptr;
  if (dates == nullptr || dates->type != Json::Type::Array) return false;
  const Json* codes = daily->get("weather_code");
  const Json* highs = daily->get("temperature_2m_max");
  const Json* lows = daily->get("temperature_2m_min");
  const Json* rain = daily->get("precipitation_probability_max");
  for (size_t i = 0; i < dates->items.size() && out.dayCount < kDays; ++i) {
    const Json& date = dates->items[i];
    int y = 0, m = 0, d = 0;
    if (date.type != Json::Type::String || !parseDate(date.text, y, m, d)) continue;
    Day& day = out.days[out.dayCount++];
    day.year = static_cast<int16_t>(y);
    day.month = static_cast<int8_t>(m);
    day.day = static_cast<int8_t>(d);
    if (const Json* v = at(codes, i); v && v->isNumber()) day.code = static_cast<int16_t>(v->number);
    if (const Json* v = at(highs, i); v && v->isNumber()) day.maxC = tenths(v->number);
    if (const Json* v = at(lows, i); v && v->isNumber()) day.minC = tenths(v->number);
    if (const Json* v = at(rain, i); v && v->isNumber()) {
      const double pct = std::round(v->number);
      day.rainPct = static_cast<int8_t>(pct < 0 ? 0 : pct > 100 ? 100 : pct);
    }
  }
  return out.dayCount > 0;
}

bool parseMatches(const std::string& json, const CanDraw canDraw, std::vector<Match>& out) {
  out.clear();
  Json root;
  if (!JsonReader(json).document(root) || root.type != Json::Type::Object) return false;
  const Json* results = root.get("results");
  if (results == nullptr || results->type != Json::Type::Array) return true;
  out.reserve(results->items.size() < static_cast<size_t>(kMaxMatches) ? results->items.size() : kMaxMatches);
  const auto text = [](const Json& object, const char* key) {
    const Json* v = object.get(key);
    return v && v->type == Json::Type::String ? v->text : std::string();
  };
  for (const Json& result : results->items) {
    if (static_cast<int>(out.size()) >= kMaxMatches) break;
    const Json* lat = result.get("latitude");
    const Json* lon = result.get("longitude");
    if (!lat || !lon || !lat->isNumber() || !lon->isNumber() || !validLatLon(lat->number, lon->number)) continue;
    Match match;
    match.lat = lat->number;
    match.lon = lon->number;
    const std::string name = cleanName(text(result, "name"), canDraw);
    const std::string region =
        joinRegion(name, cleanName(text(result, "admin1"), canDraw), cleanName(text(result, "country"), canDraw));
    match.name = placeLabel(name, region, match.lat, match.lon);
    // The region stands in for a name that could not be drawn; it is not
    // said twice.
    match.region = name.empty() ? std::string() : region;
    copyCountry(match.country, text(result, "country_code"));
    out.push_back(std::move(match));
  }
  return true;
}

std::string urlEncode(const std::string& text) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(text.size() * 3);
  for (const char ch : text) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += kHex[c >> 4];
      out += kHex[c & 0x0F];
    }
  }
  return out;
}

std::string forecastUrl(const char* base, const double lat, const double lon) {
  char coords[64];
  std::snprintf(coords, sizeof(coords), "?latitude=%.4f&longitude=%.4f", lat, lon);
  return std::string(base) + coords +
         "&current=temperature_2m,weather_code,is_day"
         "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
         "&timezone=auto&forecast_days=7";
}

std::string geocodeUrl(const char* base, const std::string& query) {
  return std::string(base) + "?name=" + urlEncode(query) + "&count=" + std::to_string(kMaxMatches) +
         "&language=en&format=json";
}

// --- The card ---------------------------------------------------------------------

std::string serializePlace(const Place& place) {
  char coords[80];
  std::snprintf(coords, sizeof(coords), "lat %.6f\nlon %.6f\n", place.lat, place.lon);
  std::string out = std::string(kPlaceHeader) + "\n" + coords;
  out += std::string("country ") + place.country + "\n";
  out += std::string("units ") + (place.units == Units::Fahrenheit ? "F" : "C") + "\n";
  out += std::string("chosen ") + (place.unitsChosen ? "1" : "0") + "\n";
  out += "name " + place.name + "\n";
  out += "region " + place.region + "\n";
  return out;
}

bool parsePlace(const std::string& text, Place& out) {
  const std::vector<std::string> all = lines(text);
  if (all.empty() || all[0] != kPlaceHeader) return false;
  Place place;
  bool haveLat = false, haveLon = false;
  for (size_t i = 1; i < all.size(); ++i) {
    std::string v;
    if (valueOf(all[i], "lat", v)) {
      haveLat = toDouble(v, place.lat);
    } else if (valueOf(all[i], "lon", v)) {
      haveLon = toDouble(v, place.lon);
    } else if (valueOf(all[i], "country", v)) {
      copyCountry(place.country, v);
    } else if (valueOf(all[i], "units", v)) {
      place.units = v == "F" ? Units::Fahrenheit : Units::Celsius;
    } else if (valueOf(all[i], "chosen", v)) {
      place.unitsChosen = v == "1";
    } else if (valueOf(all[i], "name", v)) {
      place.name = v.substr(0, kMaxName);
    } else if (valueOf(all[i], "region", v)) {
      place.region = v.substr(0, kMaxName * 2);
    }
  }
  if (!haveLat || !haveLon || !validLatLon(place.lat, place.lon) || place.name.empty()) return false;
  out = place;
  return true;
}

std::string serializeCache(const Cache& cache) {
  const Forecast& f = cache.forecast;
  char line[96];
  std::string out = std::string(kCacheHeader) + "\n";
  out += "fetched " + std::to_string(cache.fetchedUtc) + "\n";
  out += "offset " + std::to_string(f.utcOffset) + "\n";
  std::snprintf(line, sizeof(line), "place %.6f %.6f\n", cache.lat, cache.lon);
  out += line;
  out += std::string("now ") + (f.localTime[0] ? f.localTime : "-") + " " + tempToken(f.nowC) + " " +
         std::to_string(f.nowCode) + " " + (f.isDay ? "1" : "0") + "\n";
  for (int i = 0; i < f.dayCount; ++i) {
    const Day& d = f.days[i];
    std::snprintf(line, sizeof(line), "day %04d-%02d-%02d %d %s %s %s\n", d.year, d.month, d.day, d.code,
                  tempToken(d.maxC).c_str(), tempToken(d.minC).c_str(),
                  d.rainPct < 0 ? "-" : std::to_string(d.rainPct).c_str());
    out += line;
  }
  return out;
}

bool parseCache(const std::string& text, Cache& out) {
  const std::vector<std::string> all = lines(text);
  if (all.empty() || all[0] != kCacheHeader) return false;
  Cache cache;
  Forecast& f = cache.forecast;
  bool haveNow = false, havePlace = false;
  for (size_t i = 1; i < all.size(); ++i) {
    std::string v;
    long long n = 0;
    if (valueOf(all[i], "fetched", v)) {
      if (!toLong(v, n)) return false;
      cache.fetchedUtc = n;
    } else if (valueOf(all[i], "offset", v)) {
      if (!toLong(v, n) || n < -18 * 3600 || n > 18 * 3600) return false;
      f.utcOffset = static_cast<int32_t>(n);
    } else if (valueOf(all[i], "place", v)) {
      const std::vector<std::string> w = words(v);
      havePlace = w.size() == 2 && toDouble(w[0], cache.lat) && toDouble(w[1], cache.lon);
    } else if (valueOf(all[i], "now", v)) {
      const std::vector<std::string> w = words(v);
      long long code = 0;
      if (w.size() != 4 || !tempFromToken(w[1], f.nowC) || !toLong(w[2], code)) return false;
      std::snprintf(f.localTime, sizeof(f.localTime), "%s", w[0] == "-" ? "" : w[0].c_str());
      f.nowCode = static_cast<int16_t>(code);
      f.isDay = w[3] != "0";
      haveNow = true;
    } else if (valueOf(all[i], "day", v)) {
      if (f.dayCount >= kDays) continue;
      const std::vector<std::string> w = words(v);
      int y = 0, m = 0, d = 0;
      long long code = 0, rain = -1;
      Day day;
      if (w.size() != 5 || !parseDate(w[0], y, m, d) || !toLong(w[1], code) || !tempFromToken(w[2], day.maxC) ||
          !tempFromToken(w[3], day.minC) || (w[4] != "-" && (!toLong(w[4], rain) || rain < 0 || rain > 100))) {
        return false;
      }
      day.year = static_cast<int16_t>(y);
      day.month = static_cast<int8_t>(m);
      day.day = static_cast<int8_t>(d);
      day.code = static_cast<int16_t>(code);
      day.rainPct = static_cast<int8_t>(w[4] == "-" ? -1 : rain);
      f.days[f.dayCount++] = day;
    }
  }
  if (!haveNow || !havePlace || f.dayCount == 0) return false;
  out = cache;
  return true;
}

bool cacheIsFor(const Cache& cache, const Place& place) {
  return std::fabs(cache.lat - place.lat) < 1e-4 && std::fabs(cache.lon - place.lon) < 1e-4;
}

// --- What a forecast says, and when --------------------------------------------

int firstDay(const Cache& cache, const int64_t nowUtc) {
  if (nowUtc <= 0) return 0;
  const Forecast& f = cache.forecast;
  const int64_t today = floorDiv(nowUtc + f.utcOffset, 86400);
  for (int i = 0; i < f.dayCount; ++i) {
    if (daysFromCivil(f.days[i].year, f.days[i].month, f.days[i].day) >= today) return i;
  }
  return -1;
}

Now nowFor(const Cache& cache, const int64_t nowUtc) {
  const Forecast& f = cache.forecast;
  Now now;
  bool today = true;
  if (nowUtc > 0) {
    int64_t readingDay = 0;
    int hour = 0, minute = 0;
    bool dated = parseLocalTime(f.localTime, readingDay, hour, minute);
    if (!dated && cache.fetchedUtc > 0) {
      readingDay = floorDiv(cache.fetchedUtc + f.utcOffset, 86400);
      dated = true;
    }
    // A reading nothing dates is not drawn as now either.
    today = dated && readingDay >= floorDiv(nowUtc + f.utcOffset, 86400);
  }
  if (today) {
    now.reading = true;
    now.tempC = f.nowC;
    now.code = f.nowCode;
    now.isDay = f.isDay;
    return now;
  }
  const int first = firstDay(cache, nowUtc);
  if (first >= 0) now.code = f.days[first].code;
  return now;
}

std::string updatedLabel(const Cache& cache, const int64_t nowUtc, const bool twelveHour) {
  const Forecast& f = cache.forecast;
  int64_t fetchDay = 0;
  int hour = 0, minute = 0;
  if (cache.fetchedUtc > 0) {
    const int64_t local = cache.fetchedUtc + f.utcOffset;
    fetchDay = floorDiv(local, 86400);
    const int64_t seconds = local - fetchDay * 86400;
    hour = static_cast<int>(seconds / 3600);
    minute = static_cast<int>((seconds % 3600) / 60);
  } else if (!parseLocalTime(f.localTime, fetchDay, hour, minute)) {
    return std::string();
  }
  const int64_t today = nowUtc > 0 ? floorDiv(nowUtc + f.utcOffset, 86400) : fetchDay;
  const int64_t age = today - fetchDay;

  char clock[24];
  if (twelveHour) {
    std::snprintf(clock, sizeof(clock), "%d:%02d %s", hour % 12 == 0 ? 12 : hour % 12, minute, hour < 12 ? "AM" : "PM");
  } else {
    std::snprintf(clock, sizeof(clock), "%02d:%02d", hour, minute);
  }
  if (age <= 0) return std::string("UPDATED ") + clock;
  if (age == 1) return std::string("UPDATED YESTERDAY ") + clock;
  if (age <= 6) return std::string("UPDATED ") + weekdayOf(fetchDay) + " " + clock;
  int y = 0, m = 0, d = 0;
  civilFromDays(fetchDay, y, m, d);
  char date[32];
  std::snprintf(date, sizeof(date), "UPDATED %d %s", d, kMonths[m - 1]);
  return date;
}

const char* weekdayShort(const int year, const int month, const int day) {
  if (!validDate(year, month, day)) return "";
  return weekdayOf(daysFromCivil(year, month, day));
}

const char* weekdayShort(const Day& day) { return weekdayShort(day.year, day.month, day.day); }

const char* skyWords(const int code) {
  switch (code) {
    case 0:
      return "CLEAR";
    case 1:
      return "MOSTLY CLEAR";
    case 2:
      return "PARTLY CLOUDY";
    case 3:
      return "OVERCAST";
    case 45:
    case 48:
      return "FOG";
    case 51:
      return "LIGHT DRIZZLE";
    case 53:
      return "DRIZZLE";
    case 55:
      return "HEAVY DRIZZLE";
    case 56:
    case 57:
      return "FREEZING DRIZZLE";
    case 61:
      return "LIGHT RAIN";
    case 63:
      return "RAIN";
    case 65:
      return "HEAVY RAIN";
    case 66:
    case 67:
      return "FREEZING RAIN";
    case 71:
      return "LIGHT SNOW";
    case 73:
      return "SNOW";
    case 75:
      return "HEAVY SNOW";
    case 77:
      return "SNOW GRAINS";
    case 80:
      return "LIGHT SHOWERS";
    case 81:
      return "SHOWERS";
    case 82:
      return "HEAVY SHOWERS";
    case 85:
    case 86:
      return "SNOW SHOWERS";
    case 95:
      return "THUNDERSTORM";
    case 96:
    case 99:
      return "THUNDER AND HAIL";
    default:
      return "";
  }
}

Icon skyIcon(const int code, const bool isDay) {
  if (code == 0 || code == 1) return isDay ? Icon::Sun : Icon::Moon;
  if (code == 2) return isDay ? Icon::CloudSun : Icon::CloudMoon;
  if (code == 45 || code == 48) return Icon::Fog;
  if (code >= 51 && code <= 57) return Icon::Drizzle;
  if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return Icon::Rain;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return Icon::Snow;
  if (code >= 95 && code <= 99) return Icon::Thunder;
  // A code with no words has no picture either: a null in the answer is not
  // a cloud.
  return code == 3 ? Icon::Cloud : Icon::None;
}

const char* chanceWord(const int code) {
  return (code >= 71 && code <= 77) || code == 85 || code == 86 ? "SNOW" : "RAIN";
}

int displayTemp(const int16_t tenthsC, const Units units) {
  const double c = tenthsC / 10.0;
  const double v = units == Units::Fahrenheit ? c * 9.0 / 5.0 + 32.0 : c;
  const long rounded = std::lround(v);
  return rounded == 0 ? 0 : static_cast<int>(rounded);
}

std::string tempText(const int16_t tenthsC, const Units units) {
  if (tenthsC == kNoTemp) return "--";
  return std::to_string(displayTemp(tenthsC, units));
}

std::string rainText(const int8_t pct) {
  if (pct < 0) return std::string();
  return std::to_string(pct) + "%";
}

Units defaultUnitsFor(const char* countryCode) {
  if (countryCode == nullptr) return Units::Celsius;
  char code[3];
  copyCountry(code, countryCode);
  // The United States and its territories, and the countries that also read
  // the weather in Fahrenheit.
  static constexpr const char* kFahrenheit[] = {"US", "PR", "GU", "VI", "AS", "MP", "UM",
                                                "BS", "BZ", "KY", "PW", "LR", "MH", "FM"};
  for (const char* f : kFahrenheit) {
    if (std::strcmp(code, f) == 0) return Units::Fahrenheit;
  }
  return Units::Celsius;
}

// --- Text from outside ------------------------------------------------------------

std::string cleanName(const std::string& raw, const CanDraw canDraw) {
  const std::string folded = utf8FoldTypography(utf8ComposeNfc(raw));
  std::string out;
  out.reserve(folded.size());
  bool pendingSpace = false;
  const unsigned char* p = reinterpret_cast<const unsigned char*>(folded.data());
  const unsigned char* end = p + folded.size();
  while (p < end) {
    const uint32_t cp = nextCodepoint(p, end);
    if (cp == 0xFFFFFFFFu) continue;  // not text: dropped like a letter, joining nothing
    if (isSpace(cp)) {
      pendingSpace = true;
      continue;
    }
    if (!isLetterlike(cp) && !isNamePunctuation(cp)) {
      // A symbol between two words still separates them.
      pendingSpace = true;
      continue;
    }
    uint32_t draw = cp;
    if (canDraw != nullptr && !canDraw(draw)) {
      // A letter the face lacks keeps its base letter when it has one the
      // face can draw (s-comma for s, z-acute for z, L-stroke for L);
      // otherwise it goes.
      uint32_t base = utf8DecomposedBase(cp);
      if (base == 0) base = strokeBase(cp);
      if (base == 0 || !canDraw(base)) continue;
      draw = base;
    }
    if (pendingSpace && !out.empty()) out += ' ';
    pendingSpace = false;
    utf8AppendCodepoint(draw, out);
  }
  removeEmptyBrackets(out);
  out = collapseAndTrim(out);
  while (out.size() > kMaxName) utf8RemoveLastChar(out);
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

std::string placeLabel(const std::string& name, const std::string& region, const double lat, const double lon) {
  if (!name.empty()) return name;
  if (!region.empty()) return region;
  return coordinates(lat, lon);
}

std::string placeTitle(const std::string& name, const std::string& region, const double lat, const double lon) {
  const std::string label = placeLabel(name, region, lat, lon);
  if (name.empty()) return label;
  // The first part of the region that is not the name again: the state, the
  // province, or the country when the region is only that.
  size_t start = 0;
  while (start < region.size()) {
    size_t comma = region.find(", ", start);
    if (comma == std::string::npos) comma = region.size();
    const std::string part = region.substr(start, comma - start);
    if (!part.empty() && !sameName(part, name)) return label + ", " + part;
    start = comma + 2;
  }
  return label;
}

std::string cleanQuery(const std::string& typed) {
  std::string out;
  out.reserve(typed.size());
  bool pendingSpace = false;
  for (const char ch : typed) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      pendingSpace = !out.empty();
      continue;
    }
    if (c < 0x21 || c > 0x7E) continue;
    if (pendingSpace) out += ' ';
    pendingSpace = false;
    out += static_cast<char>(c);
  }
  if (out.size() > kMaxQuery) out.resize(kMaxQuery);
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

}  // namespace weather

#include "RssCore.h"

#include <Utf8.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#include "../hackernews/HackerNewsSaved.h"

namespace rss {
namespace {

bool isSpace(const char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

std::string_view trim(std::string_view text) {
  while (!text.empty() && isSpace(text.front())) text.remove_prefix(1);
  while (!text.empty() && isSpace(text.back())) text.remove_suffix(1);
  return text;
}

char lower(const char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

bool startsWithFold(const std::string_view text, const std::string_view prefix) {
  if (text.size() < prefix.size()) return false;
  for (size_t i = 0; i < prefix.size(); ++i) {
    if (lower(text[i]) != lower(prefix[i])) return false;
  }
  return true;
}

bool equalsFold(const std::string_view a, const std::string_view b) {
  return a.size() == b.size() && startsWithFold(a, b);
}

// Cut at a byte budget without splitting a UTF-8 sequence.
void clipUtf8(std::string& text, const size_t maxBytes) {
  if (text.size() <= maxBytes) return;
  size_t cut = maxBytes;
  while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
  text.resize(cut);
}

// --- URL pieces ----------------------------------------------------------

struct UrlParts {
  std::string_view scheme;     // "https"
  std::string_view authority;  // "example.com:8080"
  std::string_view path;       // "/a/b"
  std::string_view query;      // "?x=1", with its mark
};

bool splitUrl(const std::string_view url, UrlParts& out) {
  const size_t colon = url.find("://");
  if (colon == std::string_view::npos || colon == 0) return false;
  out.scheme = url.substr(0, colon);
  std::string_view rest = url.substr(colon + 3);
  const size_t hash = rest.find('#');
  if (hash != std::string_view::npos) rest = rest.substr(0, hash);
  const size_t pathStart = rest.find_first_of("/?");
  out.authority = rest.substr(0, pathStart);
  if (pathStart == std::string_view::npos) {
    out.path = {};
    out.query = {};
    return !out.authority.empty();
  }
  rest = rest.substr(pathStart);
  const size_t q = rest.find('?');
  out.path = rest.substr(0, q);
  out.query = q == std::string_view::npos ? std::string_view{} : rest.substr(q);
  return !out.authority.empty();
}

bool hasScheme(const std::string_view href) {
  for (size_t i = 0; i < href.size(); ++i) {
    const char c = href[i];
    if (c == ':') return i > 0;
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '+' || c == '-' || c == '.')) return false;
  }
  return false;
}

// "/a/b/../c/./d" -> "/a/c/d". The leading slash stays; ".." never climbs past
// the root.
std::string removeDotSegments(const std::string_view path) {
  std::vector<std::string_view> segments;
  size_t i = 0;
  const auto endsWith = [&path](const std::string_view suffix) {
    return path.size() >= suffix.size() && path.substr(path.size() - suffix.size()) == suffix;
  };
  const bool trailing = !path.empty() && (path.back() == '/' || endsWith("/.") || endsWith("/.."));
  while (i <= path.size()) {
    const size_t slash = path.find('/', i);
    const std::string_view seg = path.substr(i, (slash == std::string_view::npos ? path.size() : slash) - i);
    if (seg == "..") {
      if (!segments.empty()) segments.pop_back();
    } else if (!seg.empty() && seg != ".") {
      segments.push_back(seg);
    }
    if (slash == std::string_view::npos) break;
    i = slash + 1;
  }
  std::string out;
  for (const std::string_view seg : segments) {
    out.push_back('/');
    out.append(seg);
  }
  if (out.empty() || trailing) out.push_back('/');
  return out;
}

// --- Dates ---------------------------------------------------------------

int64_t daysFromCivil(int year, const unsigned month, const unsigned day) {
  // Howard Hinnant's algorithm: days since 1970-01-01 for a proleptic
  // Gregorian date, with no table and no timegm(), which is not freestanding.
  year -= month <= 2 ? 1 : 0;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const unsigned doy = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

uint32_t toUnix(const int year, const int month, const int day, const int hour, const int minute, const int second,
                const int offsetMinutes) {
  if (year < 1970 || year > 2105 || month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 24 ||
      minute < 0 || minute > 59 || second < 0 || second > 60) {
    return 0;
  }
  const int64_t days = daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
  const int64_t seconds = days * 86400 + hour * 3600 + minute * 60 + second - static_cast<int64_t>(offsetMinutes) * 60;
  if (seconds <= 0 || seconds > 0xFFFFFFFFll) return 0;
  return static_cast<uint32_t>(seconds);
}

// Reads up to `maxDigits` digits; -1 when there are none.
int readNumber(const std::string_view text, size_t& i, const int maxDigits) {
  int value = 0;
  int digits = 0;
  while (i < text.size() && digits < maxDigits && std::isdigit(static_cast<unsigned char>(text[i]))) {
    value = value * 10 + (text[i] - '0');
    ++i;
    ++digits;
  }
  return digits == 0 ? -1 : value;
}

// "+0530", "+05:30", "-07", "Z". Minutes east of UTC; false when it is none of
// those.
bool readNumericOffset(const std::string_view text, size_t& i, int& minutes) {
  if (i >= text.size()) return false;
  if (text[i] == 'Z' || text[i] == 'z') {
    ++i;
    minutes = 0;
    return true;
  }
  if (text[i] != '+' && text[i] != '-') return false;
  const int sign = text[i] == '-' ? -1 : 1;
  ++i;
  const int hours = readNumber(text, i, 2);
  if (hours < 0) return false;
  if (i < text.size() && text[i] == ':') ++i;
  int mins = 0;
  if (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) mins = readNumber(text, i, 2);
  minutes = sign * (hours * 60 + (mins < 0 ? 0 : mins));
  return true;
}

uint32_t parseIso(const std::string_view text) {
  size_t i = 0;
  const int year = readNumber(text, i, 4);
  if (year < 0 || i != 4 || i >= text.size() || text[i] != '-') return 0;
  ++i;
  const int month = readNumber(text, i, 2);
  if (month < 0 || i >= text.size() || text[i] != '-') return 0;
  ++i;
  const int day = readNumber(text, i, 2);
  if (day < 0) return 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  int offset = 0;
  if (i < text.size() && (text[i] == 'T' || text[i] == 't' || text[i] == ' ')) {
    ++i;
    hour = readNumber(text, i, 2);
    if (hour < 0 || i >= text.size() || text[i] != ':') return 0;
    ++i;
    minute = readNumber(text, i, 2);
    if (minute < 0) return 0;
    if (i < text.size() && text[i] == ':') {
      ++i;
      second = readNumber(text, i, 2);
      if (second < 0) return 0;
      if (i < text.size() && (text[i] == '.' || text[i] == ',')) {
        ++i;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
      }
    }
    while (i < text.size() && text[i] == ' ') ++i;
    readNumericOffset(text, i, offset);
  }
  return toUnix(year, month, day, hour, minute, second, offset);
}

int monthFromName(const std::string_view name) {
  static constexpr const char* kMonths[] = {"jan", "feb", "mar", "apr", "may", "jun",
                                            "jul", "aug", "sep", "oct", "nov", "dec"};
  if (name.size() < 3) return -1;
  for (int m = 0; m < 12; ++m) {
    if (startsWithFold(name, kMonths[m])) return m + 1;
  }
  return -1;
}

// The zone names RFC 822 allows, plus UTC, which feeds write anyway.
bool zoneOffset(const std::string_view zone, int& minutes) {
  struct Zone {
    const char* name;
    int hours;
  };
  static constexpr Zone kZones[] = {{"GMT", 0},  {"UTC", 0},  {"UT", 0},   {"Z", 0},    {"EST", -5}, {"EDT", -4},
                                    {"CST", -6}, {"CDT", -5}, {"MST", -7}, {"MDT", -6}, {"PST", -8}, {"PDT", -7}};
  for (const Zone& z : kZones) {
    if (equalsFold(zone, z.name)) {
      minutes = z.hours * 60;
      return true;
    }
  }
  return false;
}

uint32_t parseRfc822(std::string_view text) {
  // An optional weekday, with or without its comma.
  const size_t comma = text.find(',');
  if (comma != std::string_view::npos && comma < 12) text.remove_prefix(comma + 1);
  text = trim(text);
  if (!text.empty() && std::isalpha(static_cast<unsigned char>(text[0]))) {
    // "Fri 17 Oct 2026": a weekday without the comma.
    const size_t space = text.find(' ');
    if (space == std::string_view::npos) return 0;
    text = trim(text.substr(space));
  }

  size_t i = 0;
  const int day = readNumber(text, i, 2);
  if (day < 0) return 0;
  while (i < text.size() && (text[i] == ' ' || text[i] == '-')) ++i;
  const size_t monthStart = i;
  while (i < text.size() && std::isalpha(static_cast<unsigned char>(text[i]))) ++i;
  const int month = monthFromName(text.substr(monthStart, i - monthStart));
  if (month < 0) return 0;
  while (i < text.size() && (text[i] == ' ' || text[i] == '-')) ++i;
  const size_t yearStart = i;
  int year = readNumber(text, i, 4);
  if (year < 0) return 0;
  if (i - yearStart <= 2) year += year < 50 ? 2000 : 1900;

  int hour = 0;
  int minute = 0;
  int second = 0;
  int offset = 0;
  while (i < text.size() && text[i] == ' ') ++i;
  if (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
    hour = readNumber(text, i, 2);
    if (i >= text.size() || text[i] != ':') return 0;
    ++i;
    minute = readNumber(text, i, 2);
    if (minute < 0) return 0;
    if (i < text.size() && text[i] == ':') {
      ++i;
      second = readNumber(text, i, 2);
      if (second < 0) return 0;
    }
    while (i < text.size() && text[i] == ' ') ++i;
    if (!readNumericOffset(text, i, offset)) {
      size_t end = i;
      while (end < text.size() && std::isalpha(static_cast<unsigned char>(text[end]))) ++end;
      // An unknown zone name reads as UTC rather than failing the date: off by
      // a few hours still sorts the post among its neighbours.
      if (!zoneOffset(text.substr(i, end - i), offset)) offset = 0;
    }
  }
  return toUnix(year, month, day, hour, minute, second, offset);
}

// --- Entities ------------------------------------------------------------

// U+00A0..U+00FF in order, so a name's index is its codepoint minus 160.
constexpr const char* kLatin1Names[96] = {
    "nbsp",   "iexcl",  "cent",   "pound",  "curren", "yen",    "brvbar", "sect",   "uml",    "copy",   "ordf",
    "laquo",  "not",    "shy",    "reg",    "macr",   "deg",    "plusmn", "sup2",   "sup3",   "acute",  "micro",
    "para",   "middot", "cedil",  "sup1",   "ordm",   "raquo",  "frac14", "frac12", "frac34", "iquest", "Agrave",
    "Aacute", "Acirc",  "Atilde", "Auml",   "Aring",  "AElig",  "Ccedil", "Egrave", "Eacute", "Ecirc",  "Euml",
    "Igrave", "Iacute", "Icirc",  "Iuml",   "ETH",    "Ntilde", "Ograve", "Oacute", "Ocirc",  "Otilde", "Ouml",
    "times",  "Oslash", "Ugrave", "Uacute", "Ucirc",  "Uuml",   "Yacute", "THORN",  "szlig",  "agrave", "aacute",
    "acirc",  "atilde", "auml",   "aring",  "aelig",  "ccedil", "egrave", "eacute", "ecirc",  "euml",   "igrave",
    "iacute", "icirc",  "iuml",   "eth",    "ntilde", "ograve", "oacute", "ocirc",  "otilde", "ouml",   "divide",
    "oslash", "ugrave", "uacute", "ucirc",  "uuml",   "yacute", "thorn",  "yuml"};

struct Named {
  const char* name;
  uint32_t cp;
};

// Outside Latin-1: the punctuation and symbols posts are written with.
constexpr Named kNamed[] = {
    {"amp", '&'},       {"lt", '<'},        {"gt", '>'},        {"quot", '"'},      {"apos", '\''},
    {"OElig", 0x152},   {"oelig", 0x153},   {"Scaron", 0x160},  {"scaron", 0x161},  {"Yuml", 0x178},
    {"fnof", 0x192},    {"circ", 0x2C6},    {"tilde", 0x2DC},   {"ensp", 0x2002},   {"emsp", 0x2003},
    {"thinsp", 0x2009}, {"zwnj", 0x200C},   {"zwj", 0x200D},    {"lrm", 0x200E},    {"rlm", 0x200F},
    {"hyphen", 0x2010}, {"ndash", 0x2013},  {"mdash", 0x2014},  {"lsquo", 0x2018},  {"rsquo", 0x2019},
    {"sbquo", 0x201A},  {"ldquo", 0x201C},  {"rdquo", 0x201D},  {"bdquo", 0x201E},  {"dagger", 0x2020},
    {"Dagger", 0x2021}, {"bull", 0x2022},   {"hellip", 0x2026}, {"permil", 0x2030}, {"prime", 0x2032},
    {"Prime", 0x2033},  {"lsaquo", 0x2039}, {"rsaquo", 0x203A}, {"euro", 0x20AC},   {"trade", 0x2122},
    {"larr", 0x2190},   {"uarr", 0x2191},   {"rarr", 0x2192},   {"darr", 0x2193},   {"harr", 0x2194},
    {"minus", 0x2212},  {"le", 0x2264},     {"ge", 0x2265},     {"ne", 0x2260},
};

// Windows-1252's 0x80..0x9F, which feeds converted from it write as numeric
// references to C1 control codes. &#146; is an apostrophe, not a control code.
constexpr uint16_t kCp1252High[32] = {0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                      0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017D, 0,
                                      0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                      0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178};

uint32_t namedCodepoint(const std::string_view name) {
  for (size_t i = 0; i < 96; ++i) {
    if (name == kLatin1Names[i]) return static_cast<uint32_t>(160 + i);
  }
  for (const Named& n : kNamed) {
    if (name == n.name) return n.cp;
  }
  return 0;
}

// --- Markup --------------------------------------------------------------

bool tagIs(const std::string_view name, std::initializer_list<const char*> names) {
  for (const char* n : names) {
    if (name == n) return true;
  }
  return false;
}

bool isBlockTag(const std::string_view name) {
  return tagIs(name, {"p",          "div",     "br",      "hr",      "h1",     "h2",         "h3",    "h4",   "h5",
                      "h6",         "li",      "ul",      "ol",      "dl",     "dt",         "dd",    "pre",  "table",
                      "tr",         "section", "article", "header",  "footer", "aside",      "nav",   "main", "figure",
                      "figcaption", "address", "details", "summary", "center", "blockquote", "tbody", "thead"});
}

// Elements whose contents are never words for a reader.
bool isSkippedTag(const std::string_view name) {
  return tagIs(name, {"script", "style", "noscript", "template", "svg", "math", "iframe", "object", "head", "select"});
}

// Finds "</name" (any case) at or after `from`, and returns the index just past
// its '>', or npos.
size_t findClose(const std::string_view html, const std::string_view name, size_t from) {
  while (from < html.size()) {
    const size_t lt = html.find("</", from);
    if (lt == std::string_view::npos) return std::string_view::npos;
    if (startsWithFold(html.substr(lt + 2), name)) {
      const size_t gt = html.find('>', lt);
      return gt == std::string_view::npos ? std::string_view::npos : gt + 1;
    }
    from = lt + 2;
  }
  return std::string_view::npos;
}

// The end of a tag starting at `lt`, quoted attribute values respected, or npos
// when the '<' never closes -- which makes it a character somebody typed.
size_t tagEnd(const std::string_view html, const size_t lt) {
  char quote = 0;
  for (size_t i = lt + 1; i < html.size(); ++i) {
    const char c = html[i];
    if (quote != 0) {
      if (c == quote) quote = 0;
      continue;
    }
    if (c == '"' || c == '\'') {
      quote = c;
    } else if (c == '>') {
      return i;
    } else if (c == '<') {
      return std::string_view::npos;
    }
  }
  return std::string_view::npos;
}

// Pictographs: arrows and signs the fold did not turn into ASCII, geometric
// shapes, dingbats, emoji and their variation selectors, private use. (The
// zero-width joiner between emoji is the fold's already.) No cut
// on this device draws any of them, and a glyph a cut lacks draws as NOTHING
// -- so a headline marked with a star kept a hole where the star was. Dropping
// them loses decoration. Letters of every script are left alone, because a
// wrong letter is worse than a missing one (lib/Utf8/Utf8.h says why).
bool isPictograph(const uint32_t cp) {
  return (cp >= 0x2190 && cp <= 0x2BFF) || (cp >= 0x1F000 && cp <= 0x1FAFF) || (cp >= 0xFE00 && cp <= 0xFE0F) ||
         (cp >= 0xE000 && cp <= 0xF8FF) || cp == 0xFFFD;
}

std::string withoutPictographs(const std::string& text) {
  bool any = false;
  for (const char c : text) {
    if (static_cast<unsigned char>(c) >= 0xE2) {
      any = true;
      break;
    }
  }
  if (!any) return text;
  std::string out;
  out.reserve(text.size());
  const unsigned char* p = reinterpret_cast<const unsigned char*>(text.c_str());
  while (*p != 0) {
    const unsigned char* start = p;
    const uint32_t cp = utf8NextCodepoint(&p);
    if (p == start) break;
    if (!isPictograph(cp)) out.append(reinterpret_cast<const char*>(start), static_cast<size_t>(p - start));
  }
  return out;
}

std::string finishParagraph(const std::string& raw) {
  return utf8CollapseWhitespace(withoutPictographs(utf8FoldTypography(decodeEntities(raw))));
}

}  // namespace

// --- Keys and addresses --------------------------------------------------

std::string keyFor(const std::string_view text) { return hn::savedIdFor(text); }

std::string normalizeAddress(const std::string_view typed) {
  std::string_view text = trim(typed);
  if (text.empty()) return {};
  std::string out;
  if (startsWithFold(text, "feed://")) {
    out = "https://";
    out.append(text.substr(7));
  } else if (startsWithFold(text, "feed:")) {
    out = std::string(text.substr(5));
  } else if (startsWithFold(text, "//")) {
    out = "https:";
    out.append(text);
  } else {
    out = std::string(text);
  }
  if (out.find("://") == std::string::npos) {
    // A bare host, which is how people type addresses. Anything with a scheme
    // that is not the web's ("javascript:", "mailto:") is refused below.
    if (hasScheme(out) && out.find(':') < out.find('.')) return {};
    out = "https://" + out;
  }
  if (!startsWithFold(out, "http://") && !startsWithFold(out, "https://")) return {};
  for (const char c : out) {
    if (isSpace(c)) return {};
  }
  UrlParts parts;
  if (!splitUrl(out, parts)) return {};
  // Lowercase the scheme and host; the path is the server's business.
  for (size_t i = 0; i < parts.scheme.size() + 3 + parts.authority.size() && i < out.size(); ++i) {
    out[i] = lower(out[i]);
  }
  return out;
}

std::string resolveUrl(const std::string_view base, const std::string_view rawHref) {
  const std::string_view href = trim(rawHref);
  if (href.empty()) return std::string(base);
  if (hasScheme(href)) return std::string(href);
  UrlParts parts;
  if (!splitUrl(base, parts)) return std::string(href);
  std::string origin(parts.scheme);
  origin += "://";
  origin.append(parts.authority);

  if (href.size() >= 2 && href[0] == '/' && href[1] == '/') return std::string(parts.scheme) + ":" + std::string(href);
  if (href[0] == '#') {
    std::string out = origin + (parts.path.empty() ? std::string("/") : std::string(parts.path));
    out.append(parts.query);
    return out + std::string(href);
  }
  if (href[0] == '?')
    return origin + (parts.path.empty() ? std::string("/") : std::string(parts.path)) + std::string(href);

  // Split the reference's own query and fragment off before resolving its path.
  const size_t tail = href.find_first_of("?#");
  const std::string_view refPath = href.substr(0, tail);
  const std::string_view refTail = tail == std::string_view::npos ? std::string_view{} : href.substr(tail);

  std::string path;
  if (refPath[0] == '/') {
    path = std::string(refPath);
  } else {
    const std::string_view dir =
        parts.path.empty() ? std::string_view("/") : parts.path.substr(0, parts.path.rfind('/') + 1);
    path = std::string(dir) + std::string(refPath);
  }
  return origin + removeDotSegments(path) + std::string(refTail);
}

std::string hostOf(const std::string_view url) {
  UrlParts parts;
  if (!splitUrl(url, parts)) return {};
  std::string_view host = parts.authority;
  const size_t at = host.rfind('@');
  if (at != std::string_view::npos) host.remove_prefix(at + 1);
  const size_t colon = host.find(':');
  if (colon != std::string_view::npos) host = host.substr(0, colon);
  std::string out(host);
  for (char& c : out) c = lower(c);
  if (out.rfind("www.", 0) == 0) out.erase(0, 4);
  return out;
}

std::string shortName(const std::string_view title) {
  static constexpr std::string_view kSeparators[] = {
      ": ", " - ", " -- ", " | ", " \xE2\x80\x94 ", " \xE2\x80\x93 ", " \xC2\xBB ", " :: "};
  size_t cut = std::string_view::npos;
  for (const std::string_view sep : kSeparators) {
    const size_t at = title.find(sep);
    if (at != std::string_view::npos && at < cut) cut = at;
  }
  if (cut == std::string_view::npos || cut < 3) return std::string(trim(title));
  return std::string(trim(title.substr(0, cut)));
}

// --- Dates ---------------------------------------------------------------

uint32_t parseDate(const std::string_view raw) {
  const std::string_view text = trim(raw);
  if (text.size() < 8) return 0;
  // ISO opens with a four-digit year and a dash; RFC 822 never does.
  if (text.size() >= 5 && std::isdigit(static_cast<unsigned char>(text[0])) &&
      std::isdigit(static_cast<unsigned char>(text[3])) && text[4] == '-') {
    return parseIso(text);
  }
  return parseRfc822(text);
}

std::string formatDate(const uint32_t unixSeconds) {
  if (unixSeconds == 0) return {};
  // Howard Hinnant's civil_from_days, the inverse of daysFromCivil above.
  const int64_t z = static_cast<int64_t>(unixSeconds / 86400) + 719468;
  const int64_t era = z / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned day = doy - (153 * mp + 2) / 5 + 1;
  const unsigned month = mp < 10 ? mp + 3 : mp - 9;
  const int64_t year = static_cast<int64_t>(yoe) + era * 400 + (month <= 2 ? 1 : 0);
  static constexpr const char* kNames[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  char out[24];
  std::snprintf(out, sizeof(out), "%u %s %lld", day, kNames[month - 1], static_cast<long long>(year));
  return std::string(out);
}

// --- Text ----------------------------------------------------------------

std::string decodeEntities(const std::string_view text) {
  std::string out;
  out.reserve(text.size());
  size_t i = 0;
  while (i < text.size()) {
    if (text[i] != '&') {
      out.push_back(text[i++]);
      continue;
    }
    const size_t semi = text.find(';', i + 1);
    if (semi == std::string_view::npos || semi - i > 12 || semi == i + 1) {
      out.push_back(text[i++]);
      continue;
    }
    const std::string_view body = text.substr(i + 1, semi - i - 1);
    uint32_t cp = 0;
    if (body[0] == '#') {
      const bool hex = body.size() > 1 && (body[1] == 'x' || body[1] == 'X');
      const std::string digits(body.substr(hex ? 2 : 1));
      char* end = nullptr;
      const unsigned long value = digits.empty() ? 0 : std::strtoul(digits.c_str(), &end, hex ? 16 : 10);
      if (end != nullptr && *end == '\0' && value > 0 && value <= 0x10FFFF && !(value >= 0xD800 && value <= 0xDFFF)) {
        cp = static_cast<uint32_t>(value);
        if (cp >= 0x80 && cp <= 0x9F) cp = kCp1252High[cp - 0x80];
      }
    } else {
      cp = namedCodepoint(body);
    }
    if (cp == 0) {
      // Not an entity this knows: left exactly as written.
      out.push_back(text[i++]);
      continue;
    }
    utf8AppendCodepoint(cp, out);
    i = semi + 1;
  }
  return out;
}

std::vector<std::string> paragraphsFromHtml(const std::string_view html, const size_t maxBytes) {
  std::vector<std::string> out;
  std::string current;
  size_t kept = 0;
  int preDepth = 0;

  const auto flush = [&out, &current, &kept, maxBytes]() {
    if (kept >= maxBytes) {
      current.clear();
      return;
    }
    std::string paragraph = finishParagraph(current);
    current.clear();
    if (paragraph.empty() || paragraph == "-") return;
    if (kept + paragraph.size() > maxBytes) clipUtf8(paragraph, maxBytes - kept);
    kept += paragraph.size();
    if (!paragraph.empty()) out.push_back(std::move(paragraph));
  };

  size_t i = 0;
  while (i < html.size() && kept < maxBytes) {
    const char c = html[i];
    if (c != '<') {
      if (preDepth > 0 && c == '\n') {
        flush();
      } else {
        current.push_back(c);
      }
      ++i;
      continue;
    }
    const std::string_view rest = html.substr(i);
    if (rest.substr(0, 4) == "<!--") {
      const size_t end = html.find("-->", i + 4);
      i = end == std::string_view::npos ? html.size() : end + 3;
      continue;
    }
    if (rest.substr(0, 9) == "<![CDATA[") {
      const size_t end = html.find("]]>", i + 9);
      const size_t stop = end == std::string_view::npos ? html.size() : end;
      current.append(html.substr(i + 9, stop - i - 9));
      i = end == std::string_view::npos ? html.size() : end + 3;
      continue;
    }
    const bool closing = rest.size() > 1 && rest[1] == '/';
    const size_t nameStart = closing ? 2 : 1;
    const bool tagLike = rest.size() > nameStart && (std::isalpha(static_cast<unsigned char>(rest[nameStart])) ||
                                                     (!closing && rest[nameStart] == '!'));
    const size_t gt = tagLike ? tagEnd(html, i) : std::string_view::npos;
    if (gt == std::string_view::npos) {
      // A '<' that opens nothing is a character somebody wrote.
      current.push_back(c);
      ++i;
      continue;
    }
    std::string name;
    for (size_t j = i + nameStart; j < gt; ++j) {
      const char n = html[j];
      if (!std::isalnum(static_cast<unsigned char>(n))) break;
      name.push_back(lower(n));
    }
    i = gt + 1;
    if (name.empty()) continue;  // <!DOCTYPE>, or something stranger

    if (!closing && isSkippedTag(name)) {
      const size_t end = findClose(html, name, i);
      i = end == std::string_view::npos ? html.size() : end;
      continue;
    }
    if (name == "pre") preDepth += closing ? (preDepth > 0 ? -1 : 0) : 1;
    if (isBlockTag(name)) {
      flush();
      if (!closing && name == "li") current = "- ";
      continue;
    }
    if (name == "td" || name == "th") current.push_back(' ');
  }
  flush();
  return out;
}

std::string plainLine(const std::string_view htmlish) {
  std::string out;
  for (const std::string& paragraph : paragraphsFromHtml(htmlish, kMaxTitleBytes * 2)) {
    if (!out.empty()) out.push_back(' ');
    out += paragraph;
  }
  clipUtf8(out, kMaxTitleBytes);
  return out;
}

std::string joinParagraphs(const std::vector<std::string>& paragraphs) {
  std::string out;
  for (const std::string& paragraph : paragraphs) {
    if (!out.empty()) out += "\n\n";
    out += paragraph;
  }
  return out;
}

// --- Keeping the newest --------------------------------------------------

bool Keeper::newer(const uint32_t dateA, const uint32_t orderA, const uint32_t dateB, const uint32_t orderB) {
  if (dateA != dateB) return dateA > dateB;
  return orderA < orderB;
}

bool Keeper::offer(Post&& post, std::string& evicted) {
  evicted.clear();
  const uint32_t order = seen_++;
  for (const Entry& entry : kept_) {
    // A feed that lists one post twice keeps the first.
    if (entry.post.id == post.id) return false;
  }
  if (static_cast<int>(kept_.size()) < capacity_) {
    kept_.push_back(Entry{std::move(post), order});
    refusedInARow_ = 0;
    return true;
  }
  size_t oldest = 0;
  for (size_t i = 1; i < kept_.size(); ++i) {
    if (newer(kept_[oldest].post.date, kept_[oldest].order, kept_[i].post.date, kept_[i].order)) oldest = i;
  }
  if (!kept_.empty() && newer(post.date, order, kept_[oldest].post.date, kept_[oldest].order)) {
    evicted = kept_[oldest].post.id;
    kept_[oldest] = Entry{std::move(post), order};
    refusedInARow_ = 0;
    return true;
  }
  ++refusedInARow_;
  return false;
}

bool Keeper::saturated() const {
  return static_cast<int>(kept_.size()) >= capacity_ && refusedInARow_ >= kRefusalsToStop;
}

std::vector<Post> Keeper::take() {
  std::stable_sort(kept_.begin(), kept_.end(),
                   [](const Entry& a, const Entry& b) { return newer(a.post.date, a.order, b.post.date, b.order); });
  std::vector<Post> out;
  out.reserve(kept_.size());
  for (Entry& entry : kept_) out.push_back(std::move(entry.post));
  kept_.clear();
  refusedInARow_ = 0;
  return out;
}

void settleDates(std::vector<Post>& fresh, const std::vector<Post>& previous, const uint32_t now) {
  const bool clockSet = now >= kClockFloor;
  uint32_t undated = 0;
  for (Post& post : fresh) {
    if (post.date != 0) {
      if (clockSet && post.date > now) post.date = now;
      continue;
    }
    const auto old = std::find_if(previous.begin(), previous.end(),
                                  [&post](const Post& p) { return p.id == post.id && p.date != 0; });
    if (old != previous.end()) {
      post.date = old->date;
      continue;
    }
    // One second apart, in the feed's own order, so an undated feed keeps its
    // order among its neighbours.
    if (clockSet) post.date = now - undated;
    ++undated;
  }
}

void sortNewestFirst(std::vector<Post>& posts) {
  std::stable_sort(posts.begin(), posts.end(), [](const Post& a, const Post& b) { return a.date > b.date; });
}

std::vector<Post> combine(const std::vector<Feed>& feeds, const std::vector<Post>& previous,
                          const std::vector<Post>& fresh) {
  std::vector<Post> out;
  out.reserve(std::min(fresh.size() + previous.size(), feeds.size() * static_cast<size_t>(kPostsPerFeed)));
  std::vector<Post> mine;
  for (const Feed& feed : feeds) {
    const std::string key = keyFor(feed.url);
    mine.clear();
    for (const Post& post : fresh) {
      if (post.feed == key) mine.push_back(post);
    }
    const size_t freshCount = mine.size();
    for (const Post& post : previous) {
      if (post.feed != key) continue;
      bool seen = false;
      for (size_t i = 0; i < freshCount && !seen; ++i) seen = mine[i].id == post.id;
      if (!seen) mine.push_back(post);
    }
    sortNewestFirst(mine);
    if (mine.size() > static_cast<size_t>(kPostsPerFeed)) mine.resize(kPostsPerFeed);
    for (Post& post : mine) out.push_back(std::move(post));
  }
  sortNewestFirst(out);
  return out;
}

// --- Read state ----------------------------------------------------------

bool ReadSet::contains(const std::string_view id) const {
  return std::binary_search(ids_.begin(), ids_.end(), id,
                            [](const auto& a, const auto& b) { return std::string_view(a) < std::string_view(b); });
}

bool ReadSet::add(const std::string_view id) {
  if (id.empty()) return false;
  const auto at = std::lower_bound(ids_.begin(), ids_.end(), id,
                                   [](const std::string& a, const std::string_view b) { return a < b; });
  if (at != ids_.end() && *at == id) return false;
  ids_.insert(at, std::string(id));
  return true;
}

void ReadSet::retainOnly(const std::vector<Post>& posts) {
  std::vector<std::string> kept;
  kept.reserve(ids_.size());
  for (const std::string& id : ids_) {
    for (const Post& post : posts) {
      if (post.id == id) {
        kept.push_back(id);
        break;
      }
    }
  }
  ids_ = std::move(kept);
}

int unreadCount(const std::vector<Post>& posts, const ReadSet& read, const std::string_view feedKey) {
  int count = 0;
  for (const Post& post : posts) {
    if (!feedKey.empty() && post.feed != feedKey) continue;
    if (!read.contains(post.id)) ++count;
  }
  return count;
}

// --- On the card ---------------------------------------------------------

namespace {

constexpr char kFeedsMagic[] = "rssfeeds";
constexpr char kPostsMagic[] = "rssposts";
constexpr char kReadMagic[] = "rssread";
constexpr int kVersion = 1;

std::string header(const char* magic) { return std::string(magic) + " " + std::to_string(kVersion) + "\n"; }

// The text after a valid header line, or false when the file is not this
// format or comes from a newer build (left alone rather than half-read).
bool body(const std::string_view text, const char* magic, std::string_view& rows) {
  const size_t magicLen = std::strlen(magic);
  if (text.size() < magicLen + 2 || text.compare(0, magicLen, magic) != 0 || text[magicLen] != ' ') return false;
  const size_t nl = text.find('\n');
  const std::string version(
      text.substr(magicLen + 1, (nl == std::string_view::npos ? text.size() : nl) - magicLen - 1));
  const long v = std::strtol(version.c_str(), nullptr, 10);
  if (v < 1 || v > kVersion) return false;
  rows = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
  return true;
}

template <typename Fn>
void eachRow(std::string_view rows, Fn&& fn) {
  size_t i = 0;
  while (i < rows.size()) {
    const size_t nl = rows.find('\n', i);
    std::string_view row = rows.substr(i, (nl == std::string_view::npos ? rows.size() : nl) - i);
    i = nl == std::string_view::npos ? rows.size() : nl + 1;
    if (!row.empty() && row.back() == '\r') row.remove_suffix(1);
    if (!row.empty()) fn(row);
  }
}

std::string_view nextField(const std::string_view row, size_t& cursor) {
  if (cursor > row.size()) return {};
  const size_t tab = row.find('\t', cursor);
  const size_t end = tab == std::string_view::npos ? row.size() : tab;
  const std::string_view value = row.substr(cursor, end - cursor);
  cursor = tab == std::string_view::npos ? row.size() + 1 : tab + 1;
  return value;
}

uint32_t toUint(const std::string_view text) {
  if (text.empty()) return 0;
  return static_cast<uint32_t>(std::strtoul(std::string(text).c_str(), nullptr, 10));
}

}  // namespace

std::string flatField(const std::string_view text) { return hn::sanitizeField(text); }

std::string serializeFeeds(const std::vector<Feed>& feeds) {
  std::string out = header(kFeedsMagic);
  for (const Feed& feed : feeds) {
    out += flatField(feed.url);
    out.push_back('\t');
    out += flatField(feed.title);
    out.push_back('\t');
    out.push_back(feed.failed ? '1' : '0');
    out.push_back('\n');
  }
  return out;
}

bool parseFeeds(const std::string_view text, std::vector<Feed>& out) {
  out.clear();
  std::string_view rows;
  if (!body(text, kFeedsMagic, rows)) return false;
  eachRow(rows, [&out](const std::string_view row) {
    size_t cursor = 0;
    Feed feed;
    feed.url = std::string(nextField(row, cursor));
    feed.title = std::string(nextField(row, cursor));
    feed.failed = nextField(row, cursor) == "1";
    if (feed.url.empty()) return;
    if (feed.title.empty()) feed.title = hostOf(feed.url);
    for (const Feed& existing : out) {
      if (existing.url == feed.url) return;
    }
    if (static_cast<int>(out.size()) < kMaxFeeds) out.push_back(std::move(feed));
  });
  return true;
}

std::string serializePosts(const std::vector<Post>& posts) {
  std::string out = header(kPostsMagic);
  for (const Post& post : posts) {
    out += post.id;
    out.push_back('\t');
    out += post.feed;
    out.push_back('\t');
    out += std::to_string(post.date);
    out.push_back('\t');
    out.push_back(static_cast<char>('0' + static_cast<int>(post.body)));
    out.push_back('\t');
    out += flatField(post.title);
    out.push_back('\t');
    out += flatField(post.link);
    out.push_back('\n');
  }
  return out;
}

bool parsePosts(const std::string_view text, std::vector<Post>& out) {
  out.clear();
  std::string_view rows;
  if (!body(text, kPostsMagic, rows)) return false;
  eachRow(rows, [&out](const std::string_view row) {
    size_t cursor = 0;
    Post post;
    post.id = std::string(nextField(row, cursor));
    post.feed = std::string(nextField(row, cursor));
    post.date = toUint(nextField(row, cursor));
    const std::string_view kind = nextField(row, cursor);
    post.body = kind == "1" ? Body::Full : kind == "2" ? Body::Summary : Body::None;
    post.title = std::string(nextField(row, cursor));
    post.link = std::string(nextField(row, cursor));
    // A row with no id, no feed or no headline is damage rather than data.
    if (post.id.empty() || post.feed.empty() || post.title.empty()) return;
    out.push_back(std::move(post));
  });
  return true;
}

std::string serializeRead(const ReadSet& read) {
  std::string out = header(kReadMagic);
  for (const std::string& id : read.ids()) {
    out += id;
    out.push_back('\n');
  }
  return out;
}

bool parseRead(const std::string_view text, ReadSet& out) {
  out.clear();
  std::string_view rows;
  if (!body(text, kReadMagic, rows)) return false;
  eachRow(rows, [&out](const std::string_view row) {
    if (row.find('\t') == std::string_view::npos) out.add(row);
  });
  return true;
}

}  // namespace rss

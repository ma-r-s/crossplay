#include "TrmnlCore.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace trmnl {
namespace {

bool isSpace(const char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::string trim(const std::string& text) {
  size_t start = 0;
  size_t end = text.size();
  while (start < end && isSpace(text[start])) start++;
  while (end > start && isSpace(text[end - 1])) end--;
  return text.substr(start, end - start);
}

// A value as it may be stored: one line, printable, bounded.
std::string cleanValue(const std::string& value) {
  std::string out;
  out.reserve(value.size() < kMaxFieldBytes ? value.size() : kMaxFieldBytes);
  for (const char c : trim(value)) {
    if (out.size() >= kMaxFieldBytes) break;
    const auto byte = static_cast<unsigned char>(c);
    if (byte < 0x20 || byte == 0x7f) continue;
    out.push_back(c);
  }
  return out;
}

int clampInt(const long value, const int lo, const int hi) {
  if (value < lo) return lo;
  if (value > hi) return hi;
  return static_cast<int>(value);
}

bool startsWith(const std::string& text, const char* prefix) { return text.rfind(prefix, 0) == 0; }

bool equalsNoCase(const std::string& a, const char* b) {
  const size_t n = std::strlen(b);
  if (a.size() != n) return false;
  for (size_t i = 0; i < n; i++) {
    char x = a[i];
    char y = b[i];
    if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
    if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
    if (x != y) return false;
  }
  return true;
}

// The scheme and host of a base URL, "https://trmnl.app".
std::string origin(const std::string& server) {
  const size_t scheme = server.find("://");
  if (scheme == std::string::npos) return server;
  const size_t path = server.find('/', scheme + 3);
  return path == std::string::npos ? server : server.substr(0, path);
}

// --- A flat JSON object, read just far enough ------------------------------

void skipSpace(const std::string& json, size_t& i) {
  while (i < json.size() && isSpace(json[i])) i++;
}

void appendUtf8(std::string& out, const unsigned cp) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

// At an opening quote; leaves `i` past the closing one.
bool readString(const std::string& json, size_t& i, std::string& out) {
  if (i >= json.size() || json[i] != '"') return false;
  i++;
  out.clear();
  while (i < json.size()) {
    const char c = json[i++];
    if (c == '"') return true;
    if (c != '\\') {
      out.push_back(c);
      continue;
    }
    if (i >= json.size()) return false;
    const char e = json[i++];
    switch (e) {
      case 'n':
        out.push_back('\n');
        break;
      case 't':
        out.push_back('\t');
        break;
      case 'r':
      case 'b':
      case 'f':
        break;
      case 'u': {
        if (i + 4 > json.size()) return false;
        unsigned cp = 0;
        for (int k = 0; k < 4; k++) {
          const char h = json[i++];
          cp <<= 4;
          if (h >= '0' && h <= '9') {
            cp |= static_cast<unsigned>(h - '0');
          } else if (h >= 'a' && h <= 'f') {
            cp |= static_cast<unsigned>(h - 'a' + 10);
          } else if (h >= 'A' && h <= 'F') {
            cp |= static_cast<unsigned>(h - 'A' + 10);
          } else {
            return false;
          }
        }
        // A lone surrogate half has no character to stand for.
        if (cp < 0xD800 || cp > 0xDFFF) appendUtf8(out, cp);
        break;
      }
      default:  // \" \\ \/
        out.push_back(e);
        break;
    }
  }
  return false;
}

// Skips any value, nested ones included; leaves `i` just past it.
bool skipValue(const std::string& json, size_t& i) {
  skipSpace(json, i);
  if (i >= json.size()) return false;
  const char c = json[i];
  if (c == '"') {
    std::string ignored;
    return readString(json, i, ignored);
  }
  if (c == '{' || c == '[') {
    int depth = 0;
    while (i < json.size()) {
      const char d = json[i];
      if (d == '"') {
        std::string ignored;
        if (!readString(json, i, ignored)) return false;
        continue;
      }
      if (d == '{' || d == '[') depth++;
      if (d == '}' || d == ']') depth--;
      i++;
      if (depth == 0) return true;
    }
    return false;
  }
  while (i < json.size() && json[i] != ',' && json[i] != '}' && json[i] != ']' && !isSpace(json[i])) i++;
  return true;
}

// Finds `key` at the top level and leaves `at` on the first byte of its value.
bool findValue(const std::string& json, const char* key, size_t& at) {
  size_t i = 0;
  skipSpace(json, i);
  if (i >= json.size() || json[i] != '{') return false;
  i++;
  std::string name;
  while (true) {
    skipSpace(json, i);
    if (i >= json.size() || json[i] == '}') return false;
    if (!readString(json, i, name)) return false;
    skipSpace(json, i);
    if (i >= json.size() || json[i] != ':') return false;
    i++;
    skipSpace(json, i);
    if (name == key) {
      at = i;
      return true;
    }
    if (!skipValue(json, i)) return false;
    skipSpace(json, i);
    if (i < json.size() && json[i] == ',') i++;
  }
}

}  // namespace

// --- Settings -------------------------------------------------------------

Config parseConfig(const std::string& text) {
  Config config;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    const std::string line = text.substr(start, end - start);
    start = end + 1;
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = trim(line.substr(0, eq));
    const std::string value = cleanValue(line.substr(eq + 1));
    const long number = std::strtol(value.c_str(), nullptr, 10);
    if (key == "server") {
      const std::string server = normalizeServer(value);
      config.server = server.empty() ? kDefaultServer : server;
    } else if (key == "device_id") {
      config.deviceId = value;
    } else if (key == "api_key") {
      config.apiKey = value;
    } else if (key == "refresh_minutes") {
      config.refreshMinutes = clampInt(number, 0, kMaxRefreshMinutes);
    } else if (key == "orientation") {
      config.orientation = static_cast<Orientation>(clampInt(number, 0, kOrientationCount - 1));
    } else if (key == "clean_every") {
      config.cleanEvery = clampInt(number, 1, 100);
    } else if (key == "wifi_off") {
      config.wifiOff = value != "0";
    } else if (key == "keep_awake") {
      config.keepAwake = value != "0";
    }
  }
  return config;
}

std::string formatConfig(const Config& config) {
  std::string out;
  out.reserve(256);
  out += "server=" + cleanValue(config.server) + "\n";
  out += "device_id=" + cleanValue(config.deviceId) + "\n";
  out += "api_key=" + cleanValue(config.apiKey) + "\n";
  out += "refresh_minutes=" + std::to_string(clampInt(config.refreshMinutes, 0, kMaxRefreshMinutes)) + "\n";
  out += "orientation=" + std::to_string(static_cast<int>(config.orientation)) + "\n";
  out += "clean_every=" + std::to_string(clampInt(config.cleanEvery, 1, 100)) + "\n";
  out += std::string("wifi_off=") + (config.wifiOff ? "1" : "0") + "\n";
  out += std::string("keep_awake=") + (config.keepAwake ? "1" : "0") + "\n";
  return out;
}

State parseState(const std::string& text) {
  State state;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    const std::string line = text.substr(start, end - start);
    start = end + 1;
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = trim(line.substr(0, eq));
    const std::string value = cleanValue(line.substr(eq + 1));
    if (key == "friendly_id") {
      state.friendlyId = value;
    } else if (key == "filename") {
      state.filename = value;
    } else if (key == "refresh_rate") {
      state.refreshRate = clampInt(std::strtol(value.c_str(), nullptr, 10), 0, 7 * 86400);
    } else if (key == "message") {
      state.message = value;
    } else if (key == "key_for") {
      state.keyFor = value;
    }
  }
  return state;
}

std::string formatState(const State& state) {
  std::string out;
  out.reserve(160);
  out += "friendly_id=" + cleanValue(state.friendlyId) + "\n";
  out += "filename=" + cleanValue(state.filename) + "\n";
  out += "refresh_rate=" + std::to_string(state.refreshRate) + "\n";
  out += "message=" + cleanValue(state.message) + "\n";
  out += "key_for=" + cleanValue(state.keyFor) + "\n";
  return out;
}

std::string normalizeServer(const std::string& typed) {
  std::string server = trim(typed);
  for (const char c : server) {
    if (static_cast<unsigned char>(c) <= 0x20) return "";
  }
  if (server.empty()) return "";
  const size_t scheme = server.find("://");
  if (scheme == std::string::npos) {
    server = "https://" + server;
  } else {
    const std::string name = server.substr(0, scheme);
    if (!equalsNoCase(name, "http") && !equalsNoCase(name, "https")) return "";
    server = (equalsNoCase(name, "http") ? "http" : "https") + server.substr(scheme);
  }
  const size_t host = server.find("://") + 3;
  while (server.size() > host && server.back() == '/') server.pop_back();
  if (server.size() >= host + 4 && server.compare(server.size() - 4, 4, "/api") == 0) server.resize(server.size() - 4);
  while (server.size() > host && server.back() == '/') server.pop_back();
  // Nothing after the scheme is no server at all.
  if (host >= server.size() || server[host] == '/') return "";
  return server;
}

std::string serverLabel(const std::string& server) {
  const size_t scheme = server.find("://");
  return scheme == std::string::npos ? server : server.substr(scheme + 3);
}

std::string apiUrl(const std::string& server, const char* path) { return server + path; }

std::string resolveUrl(const std::string& server, const std::string& url) {
  if (startsWith(url, "http://") || startsWith(url, "https://")) return url;
  if (startsWith(url, "//")) {
    const size_t scheme = server.find("://");
    return (scheme == std::string::npos ? std::string("https:") : server.substr(0, scheme + 1)) + url;
  }
  if (startsWith(url, "/")) return origin(server) + url;
  return server + "/" + url;
}

std::string formatMac(const uint8_t mac[6]) {
  char text[56];
  std::snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return text;
}

std::string effectiveDeviceId(const Config& config, const std::string& mac) {
  return config.deviceId.empty() ? mac : config.deviceId;
}

bool usableDeviceId(const std::string& id) {
  for (const char c : id) {
    if (c != '0' && c != ':' && c != '-' && c != ' ') return true;
  }
  return false;
}

bool keyBelongsTo(const Config& config, const State& state, const std::string& deviceId) {
  return !config.apiKey.empty() && state.keyFor == deviceId;
}

bool applyPhoneSave(const Config& before, Config& after, State& state, const std::string& mac) {
  bool rewrite = false;
  const std::string id = effectiveDeviceId(after, mac);
  const bool newKey = after.apiKey != before.apiKey;
  if (before.server != after.server || effectiveDeviceId(before, mac) != id) {
    state.friendlyId.clear();
    state.filename.clear();
    state.refreshRate = 0;
    if (!newKey && !after.apiKey.empty()) {
      after.apiKey.clear();
      rewrite = true;
    }
  }
  if (newKey) state.keyFor = after.apiKey.empty() ? std::string() : id;
  if (before.orientation != after.orientation) state.filename.clear();
  state.message.clear();
  return rewrite;
}

int requestWidth(const Orientation orientation) {
  return orientation == Orientation::Portrait || orientation == Orientation::PortraitFlipped ? 480 : 800;
}

int requestHeight(const Orientation orientation) {
  return orientation == Orientation::Portrait || orientation == Orientation::PortraitFlipped ? 800 : 480;
}

const char* orientationName(const Orientation orientation) {
  switch (orientation) {
    case Orientation::Landscape:
      return "landscape";
    case Orientation::LandscapeFlipped:
      return "landscape, flipped";
    case Orientation::Portrait:
      return "portrait";
    case Orientation::PortraitFlipped:
      return "portrait, flipped";
  }
  return "landscape";
}

// --- What the server says ---------------------------------------------------

bool jsonString(const std::string& json, const char* key, std::string& out) {
  size_t at = 0;
  if (!findValue(json, key, at)) return false;
  if (at >= json.size() || json[at] != '"') return false;
  return readString(json, at, out);
}

bool jsonNumber(const std::string& json, const char* key, long long& out) {
  size_t at = 0;
  if (!findValue(json, key, at)) return false;
  // Some servers quote their numbers.
  std::string quoted;
  const char* begin = json.c_str() + at;
  if (json[at] == '"') {
    if (!readString(json, at, quoted)) return false;
    begin = quoted.c_str();
  }
  char* end = nullptr;
  const double value = std::strtod(begin, &end);
  if (end == begin) return false;
  out = static_cast<long long>(value);
  return true;
}

bool jsonBool(const std::string& json, const char* key, bool& out) {
  size_t at = 0;
  if (!findValue(json, key, at)) return false;
  if (json.compare(at, 4, "true") == 0) {
    out = true;
    return true;
  }
  if (json.compare(at, 5, "false") == 0) {
    out = false;
    return true;
  }
  return false;
}

SetupReply parseSetup(const std::string& json) {
  SetupReply reply;
  jsonString(json, "message", reply.message);
  long long status = 200;
  jsonNumber(json, "status", status);
  if (jsonString(json, "api_key", reply.apiKey) && !reply.apiKey.empty() && (status == 200 || status == 0)) {
    reply.ok = true;
  }
  jsonString(json, "friendly_id", reply.friendlyId);
  return reply;
}

DisplayReply parseDisplay(const std::string& json) {
  DisplayReply reply;
  long long number = 0;
  if (jsonNumber(json, "status", number)) reply.status = static_cast<int>(number);
  if (jsonNumber(json, "refresh_rate", number) && number > 0 && number < 0x7fffffff) {
    reply.refreshRate = static_cast<int>(number);
  }
  jsonString(json, "image_url", reply.imageUrl);
  jsonString(json, "filename", reply.filename);
  bool reset = false;
  if (jsonBool(json, "reset_firmware", reset)) reply.resetCredentials = reset;
  if (!jsonString(json, "error", reply.message)) jsonString(json, "message", reply.message);
  // TRMNL's own API answers status 0; self-hosted servers often answer 200.
  reply.ok = !reply.imageUrl.empty() && (reply.status == 0 || reply.status == 200);
  return reply;
}

// --- Images ---------------------------------------------------------------

ImageKind sniffImage(const uint8_t* head, const size_t len) {
  if (len >= 2 && head[0] == 'B' && head[1] == 'M') return ImageKind::Bmp;
  static constexpr uint8_t kPng[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  if (len >= sizeof(kPng) && std::memcmp(head, kPng, sizeof(kPng)) == 0) return ImageKind::Png;
  return ImageKind::Unknown;
}

// --- Time ---------------------------------------------------------------

uint32_t intervalSeconds(const Config& config, const int serverRefreshRate) {
  uint32_t seconds = kDefaultIntervalSeconds;
  if (config.refreshMinutes > 0) {
    seconds = static_cast<uint32_t>(config.refreshMinutes) * 60;
  } else if (serverRefreshRate > 0) {
    seconds = static_cast<uint32_t>(serverRefreshRate);
  }
  if (seconds < kMinIntervalSeconds) seconds = kMinIntervalSeconds;
  if (seconds > kMaxIntervalSeconds) seconds = kMaxIntervalSeconds;
  return seconds;
}

uint32_t retrySeconds(const int failures, const uint32_t interval) {
  if (failures <= 0) return interval;
  uint32_t delay = 60;
  for (int i = 1; i < failures && delay < 1800; i++) delay *= 2;
  if (delay > 1800) delay = 1800;
  if (delay > interval) delay = interval;
  if (delay < kMinIntervalSeconds) delay = kMinIntervalSeconds;
  return delay;
}

bool cleanRefresh(const int shown, const int cleanEvery) {
  if (cleanEvery <= 1 || shown <= 1) return true;
  return (shown - 1) % cleanEvery == 0;
}

std::string everyPhrase(const uint32_t seconds) {
  char text[32];
  if (seconds >= 86400 && seconds % 86400 == 0) {
    const unsigned days = seconds / 86400;
    if (days == 1) return "every day";
    std::snprintf(text, sizeof(text), "every %u days", days);
  } else if (seconds >= 3600 && seconds % 3600 == 0) {
    const unsigned hours = seconds / 3600;
    if (hours == 1) return "every hour";
    std::snprintf(text, sizeof(text), "every %u hours", hours);
  } else if (seconds >= 60) {
    const unsigned minutes = (seconds + 30) / 60;
    if (minutes == 1) return "every minute";
    std::snprintf(text, sizeof(text), "every %u min", minutes);
  } else {
    std::snprintf(text, sizeof(text), "every %u s", static_cast<unsigned>(seconds));
  }
  return text;
}

std::string inPhrase(const uint32_t seconds) {
  char text[32];
  if (seconds < 60) return "in under a minute";
  if (seconds < 5400) {
    std::snprintf(text, sizeof(text), "in %u min", static_cast<unsigned>((seconds + 30) / 60));
  } else if (seconds < 86400 + 43200) {
    std::snprintf(text, sizeof(text), "in %u hours", static_cast<unsigned>((seconds + 1800) / 3600));
  } else {
    std::snprintf(text, sizeof(text), "in %u days", static_cast<unsigned>((seconds + 43200) / 86400));
  }
  return text;
}

}  // namespace trmnl

#include "HeartbeatCore.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace heartbeat {

namespace {

// --- SHA-256 (FIPS 180-4), because the device id must be the same hash on the
// device and on the host that tests it, and mbedtls is not on the host.

constexpr uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t rotr(const uint32_t x, const int n) { return (x >> n) | (x << (32 - n)); }

void compress(uint32_t h[8], const uint8_t block[64]) {
  uint32_t w[64];
  for (int i = 0; i < 16; ++i) {
    w[i] = (static_cast<uint32_t>(block[4 * i]) << 24) | (static_cast<uint32_t>(block[4 * i + 1]) << 16) |
           (static_cast<uint32_t>(block[4 * i + 2]) << 8) | static_cast<uint32_t>(block[4 * i + 3]);
  }
  for (int i = 16; i < 64; ++i) {
    const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
  for (int i = 0; i < 64; ++i) {
    const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    const uint32_t ch = (e & f) ^ (~e & g);
    const uint32_t t1 = hh + s1 + ch + kK[i] + w[i];
    const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t t2 = s0 + maj;
    hh = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  h[0] += a;
  h[1] += b;
  h[2] += c;
  h[3] += d;
  h[4] += e;
  h[5] += f;
  h[6] += g;
  h[7] += hh;
}

// Fixed for the life of the id. Changing it renames every device on the board.
constexpr char kSalt[] = "crossplay-heartbeat-2026";

// --- A JSON writer that cannot overrun: one bounded buffer, and a single
// "did it all fit" answer at the end instead of a check per field.

class Writer {
 public:
  Writer(char* buf, const size_t size) : buf_(buf), size_(size) {
    if (size_ > 0) buf_[0] = '\0';
  }

  void raw(const char* s) {
    while (*s != '\0') put(*s++);
  }
  void num(const long v) {
    char t[24];
    std::snprintf(t, sizeof(t), "%ld", v);
    raw(t);
  }
  void num64(const long long v) {
    char t[24];
    std::snprintf(t, sizeof(t), "%lld", v);
    raw(t);
  }
  void boolean(const bool b) { raw(b ? "true" : "false"); }
  void str(const char* s) {
    put('"');
    for (; *s != '\0'; ++s) escaped(*s);
    put('"');
  }
  void key(const char* name) {
    str(name);
    put(':');
  }
  size_t finish() {
    if (!ok_ || size_ == 0) {
      if (size_ > 0) buf_[0] = '\0';
      return 0;
    }
    buf_[used_] = '\0';
    return used_;
  }

 private:
  void put(const char c) {
    if (used_ + 1 >= size_) {
      ok_ = false;
      return;
    }
    buf_[used_++] = c;
  }
  void escaped(const char c) {
    switch (c) {
      case '"':
        raw("\\\"");
        break;
      case '\\':
        raw("\\\\");
        break;
      case '\n':
        raw("\\n");
        break;
      case '\r':
        raw("\\r");
        break;
      case '\t':
        raw("\\t");
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char t[8];
          std::snprintf(t, sizeof(t), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
          raw(t);
        } else {
          put(c);
        }
    }
  }

  char* buf_;
  size_t size_;
  size_t used_ = 0;
  bool ok_ = true;
};

// --- A JSON reader for the two fixed shapes this module writes and receives.
// Keys are looked up by name anywhere in the text: every key the two files
// use is unique, and a value can never contain a bare `"name":` because every
// quote inside a value is written escaped.

const char* skipWs(const char* p) {
  while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') ++p;
  return p;
}

const char* findKey(const char* json, const char* key) {
  char pattern[48];
  const int n = std::snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  if (n <= 0 || static_cast<size_t>(n) >= sizeof(pattern)) return nullptr;
  for (const char* p = std::strstr(json, pattern); p != nullptr; p = std::strstr(p + 1, pattern)) {
    const char* after = skipWs(p + n);
    if (*after == ':') return skipWs(after + 1);
  }
  return nullptr;
}

unsigned hexValue(const char c) {
  if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
  if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
  if (c >= 'A' && c <= 'F') return static_cast<unsigned>(c - 'A' + 10);
  return 0;
}

// Reads the quoted string at `p` into `out`, truncating to fit. Returns the
// text after the closing quote, or nullptr when the string never closes.
const char* readString(const char* p, char* out, const size_t outSize) {
  p = skipWs(p);
  if (*p != '"') return nullptr;
  ++p;
  size_t n = 0;
  while (*p != '\0' && *p != '"') {
    char c = *p++;
    if (c == '\\') {
      const char e = *p++;
      switch (e) {
        case '\0':
          return nullptr;
        case 'n':
          c = '\n';
          break;
        case 'r':
          c = '\r';
          break;
        case 't':
          c = '\t';
          break;
        case 'b':
          c = '\b';
          break;
        case 'f':
          c = '\f';
          break;
        case 'u': {
          unsigned v = 0;
          for (int i = 0; i < 4; ++i) {
            if (*p == '\0') return nullptr;
            v = v * 16 + hexValue(*p++);
          }
          c = v < 0x80 ? static_cast<char>(v) : '?';
          break;
        }
        default:
          c = e;
      }
    }
    if (n + 1 < outSize) out[n++] = c;
  }
  if (outSize > 0) out[n] = '\0';
  if (*p != '"') return nullptr;
  return p + 1;
}

void readStringField(const char* json, const char* key, char* out, const size_t outSize) {
  const char* p = findKey(json, key);
  if (p != nullptr) readString(p, out, outSize);
}

void writeApps(Writer& w, const State& s) {
  w.raw("[");
  for (int i = 0; i < s.appCount; ++i) {
    if (i > 0) w.raw(",");
    w.str(s.apps[i]);
  }
  w.raw("]");
}

}  // namespace

void sha256(const uint8_t* data, const size_t len, uint8_t out[32]) {
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  size_t done = 0;
  for (; done + 64 <= len; done += 64) compress(h, data + done);

  uint8_t block[64];
  size_t rem = len - done;
  if (rem > 0) std::memcpy(block, data + done, rem);
  block[rem++] = 0x80;
  if (rem > 56) {
    std::memset(block + rem, 0, 64 - rem);
    compress(h, block);
    rem = 0;
  }
  std::memset(block + rem, 0, 56 - rem);
  const uint64_t bits = static_cast<uint64_t>(len) * 8;
  for (int i = 0; i < 8; ++i) block[56 + i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
  compress(h, block);

  for (int i = 0; i < 8; ++i) {
    out[4 * i] = static_cast<uint8_t>(h[i] >> 24);
    out[4 * i + 1] = static_cast<uint8_t>(h[i] >> 16);
    out[4 * i + 2] = static_cast<uint8_t>(h[i] >> 8);
    out[4 * i + 3] = static_cast<uint8_t>(h[i]);
  }
}

void deviceId(const uint8_t mac[6], char out[kIdLen + 1]) {
  uint8_t input[6 + sizeof(kSalt) - 1];
  std::memcpy(input, mac, 6);
  std::memcpy(input + 6, kSalt, sizeof(kSalt) - 1);
  uint8_t digest[32];
  sha256(input, sizeof(input), digest);
  static constexpr char kHex[] = "0123456789abcdef";
  for (int i = 0; i < 32; ++i) {
    out[2 * i] = kHex[digest[i] >> 4];
    out[2 * i + 1] = kHex[digest[i] & 0x0f];
  }
  out[kIdLen] = '\0';
}

bool appKey(const char* title, char out[kMaxAppKey + 1]) {
  out[0] = '\0';
  if (title == nullptr) return false;
  size_t n = 0;
  for (const char* p = title; *p != '\0' && n < kMaxAppKey; ++p) {
    const char c = *p;
    if (c >= 'a' && c <= 'z') {
      out[n++] = c;
    } else if (c >= 'A' && c <= 'Z') {
      out[n++] = static_cast<char>(c - 'A' + 'a');
    } else if (c >= '0' && c <= '9') {
      out[n++] = c;
    }
  }
  out[n] = '\0';
  return n > 0;
}

bool addApp(State& s, const char* key) {
  if (key == nullptr || key[0] == '\0' || std::strlen(key) > kMaxAppKey) return false;
  for (int i = 0; i < s.appCount; ++i) {
    if (std::strcmp(s.apps[i], key) == 0) return false;
  }
  if (s.appCount >= kMaxApps) return false;
  std::snprintf(s.apps[s.appCount], sizeof(s.apps[0]), "%s", key);
  ++s.appCount;
  return true;
}

bool parseState(const char* json, State& out) {
  out = State{};
  if (json == nullptr) return false;

  const char* p = findKey(json, "day");
  if (p == nullptr) return false;
  char* end = nullptr;
  const long day = std::strtol(p, &end, 10);
  if (end == p) return false;
  out.lastDay = day < 0 ? -1 : day;

  if (const char* a = findKey(json, "apps"); a != nullptr && *a == '[') {
    ++a;
    for (;;) {
      a = skipWs(a);
      if (*a == ']' || *a == '\0') break;
      char raw[kMaxAppKey + 1];
      a = readString(a, raw, sizeof(raw));
      if (a == nullptr) break;
      // Normalised again on the way in: the file outlives firmware versions,
      // and a word the board would not count is not worth carrying.
      char key[kMaxAppKey + 1];
      if (appKey(raw, key)) addApp(out, key);
      a = skipWs(a);
      if (*a != ',') break;
      ++a;
    }
  }

  readStringField(json, "from", out.otaFrom, sizeof(out.otaFrom));
  readStringField(json, "error", out.otaError, sizeof(out.otaError));
  readStringField(json, "message", out.crashMessage, sizeof(out.crashMessage));
  readStringField(json, "trace", out.crashTrace, sizeof(out.crashTrace));
  readStringField(json, "version", out.crashVersion, sizeof(out.crashVersion));

  if (const char* r = findKey(json, "retry"); r != nullptr) {
    const long long retryAt = std::strtoll(r, &end, 10);
    if (end != r && retryAt > 0) out.retryAt = retryAt;
  }
  if (const char* f = findKey(json, "fails"); f != nullptr) {
    const long fails = std::strtol(f, &end, 10);
    if (end != f && fails > 0) out.fails = fails > 1000 ? 1000 : static_cast<int>(fails);
  }
  return true;
}

size_t formatState(const State& s, char* out, const size_t outSize) {
  Writer w(out, outSize);
  w.raw("{");
  w.key("day");
  w.num(s.lastDay);
  w.raw(",");
  w.key("apps");
  writeApps(w, s);
  w.raw(",");
  w.key("retry");
  w.num64(s.retryAt);
  w.raw(",");
  w.key("fails");
  w.num(static_cast<long>(s.fails));
  w.raw(",");
  w.key("ota");
  w.raw("{");
  w.key("from");
  w.str(s.otaFrom);
  w.raw(",");
  w.key("error");
  w.str(s.otaError);
  w.raw("},");
  w.key("crash");
  w.raw("{");
  w.key("message");
  w.str(s.crashMessage);
  w.raw(",");
  w.key("trace");
  w.str(s.crashTrace);
  w.raw(",");
  w.key("version");
  w.str(s.crashVersion);
  w.raw("}}");
  return w.finish();
}

long dayFromEpoch(const long long epochSeconds) {
  constexpr long long kFirstPlausible = 1735689600LL;  // 2025-01-01T00:00:00Z
  if (epochSeconds < kFirstPlausible) return -1;
  return static_cast<long>(epochSeconds / 86400);
}

bool backingOff(const unsigned long nowMs, const unsigned long notBeforeMs) {
  if (notBeforeMs == 0) return false;
  const uint32_t elapsed = static_cast<uint32_t>(nowMs) - static_cast<uint32_t>(notBeforeMs);
  return static_cast<int32_t>(elapsed) < 0;
}

void noteFailed(State& s, const long long epochNow) {
  if (s.fails < 1000) ++s.fails;
  if (s.fails >= 2) {
    // The rest of the UTC day: the day after today's, at midnight.
    s.retryAt = (epochNow / 86400 + 1) * 86400;
  } else {
    s.retryAt = epochNow + kRetryS;
  }
}

void clearBackoff(State& s) {
  s.retryAt = 0;
  s.fails = 0;
}

bool backingOffAt(const long long epochNow, const long long retryAt) {
  if (retryAt <= epochNow) return false;
  return retryAt - epochNow <= kMaxBackoffS;
}

Decision decide(const bool enabled, const long today, const long long epochNow, const State& s,
                const bool crashPending) {
  if (!enabled) return Decision::Off;
  if (today < 0) return Decision::NoClock;
  if (backingOffAt(epochNow, s.retryAt)) return Decision::Backoff;
  if (crashPending) return Decision::Send;
  // Not "today > lastDay": a clock that stepped backwards (a re-sync, a
  // replaced battery) would otherwise silence the device until it caught up.
  if (today == s.lastDay) return Decision::AlreadyToday;
  return Decision::Send;
}

const char* decisionName(const Decision d) {
  switch (d) {
    case Decision::Send:
      return "send";
    case Decision::Off:
      return "off";
    case Decision::NoClock:
      return "no clock";
    case Decision::Backoff:
      return "backoff";
    case Decision::AlreadyToday:
      return "already today";
  }
  return "?";
}

OtaProps otaProps(const State& s, const char* runningVersion) {
  OtaProps o;
  o.attempted = s.otaFrom[0] != '\0';
  o.error = s.otaError;
  // An install that reported no error and still boots the same version did
  // not happen, whatever the screen said; "ok" is the version having moved.
  o.ok = o.attempted && s.otaError[0] == '\0' && runningVersion != nullptr && std::strcmp(s.otaFrom, runningVersion) != 0;
  return o;
}

size_t formatHeartbeat(const char* device, const Sample& sample, const State& s, char* out, const size_t outSize) {
  const OtaProps ota = otaProps(s, sample.version);
  Writer w(out, outSize);
  w.raw("{");
  w.key("service");
  w.str("firmware");
  w.raw(",");
  w.key("event");
  w.str("heartbeat");
  w.raw(",");
  w.key("device");
  w.str(device);
  w.raw(",");
  w.key("version");
  w.str(sample.version);
  w.raw(",");
  w.key("board");
  w.str(sample.board);
  w.raw(",");
  w.key("props");
  w.raw("{");
  w.key("apps");
  writeApps(w, s);
  w.raw(",");
  w.key("uptime_h");
  w.num(static_cast<long>(sample.uptimeHours));
  w.raw(",");
  w.key("battery_pct");
  w.num(static_cast<long>(sample.batteryPct));
  w.raw(",");
  w.key("heap_min_kb");
  w.num(static_cast<long>(sample.heapMinKb));
  w.raw(",");
  w.key("ota");
  w.raw("{");
  w.key("attempted");
  w.boolean(ota.attempted);
  w.raw(",");
  w.key("ok");
  w.boolean(ota.ok);
  w.raw(",");
  w.key("error");
  w.str(ota.error);
  w.raw("}}}");
  return w.finish();
}

size_t formatCrashMessage(const char* reason, const char* reset, const char* lastTag, char* out,
                          const size_t outSize) {
  if (outSize == 0) return 0;
  const bool hasReason = reason != nullptr && reason[0] != '\0';
  const bool hasTag = lastTag != nullptr && lastTag[0] != '\0';
  if (reset == nullptr || reset[0] == '\0') reset = "unknown";
  int n;
  if (hasReason) {
    n = std::snprintf(out, outSize, "%s (reset: %s)", reason, reset);
  } else if (hasTag) {
    n = std::snprintf(out, outSize, "panic without a recorded reason (reset: %s; last log: %s)", reset, lastTag);
  } else {
    n = std::snprintf(out, outSize, "panic without a recorded reason (reset: %s)", reset);
  }
  if (n < 0) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n) < outSize ? static_cast<size_t>(n) : outSize - 1;
}

bool lastLogTagBeforeReset(const char* panicInfo, char* out, const size_t outSize) {
  if (outSize == 0) return false;
  out[0] = '\0';
  if (panicInfo == nullptr) return false;
  static constexpr char kHeader[] = "Last logs:\n";
  const char* p = std::strstr(panicInfo, kHeader);
  if (p == nullptr) return false;
  p += sizeof(kHeader) - 1;

  unsigned long prevMs = 0;
  bool havePrev = false;
  const char* prevTagStart = nullptr;
  size_t prevTagLen = 0;
  const char* found = nullptr;
  size_t foundLen = 0;
  for (; *p != '\0'; ) {
    const char* lineEnd = std::strchr(p, '\n');
    const size_t lineLen = lineEnd == nullptr ? std::strlen(p) : static_cast<size_t>(lineEnd - p);
    if (lineLen == 0) break;  // the blank line before "Stack memory:"
    // "[ms] [LVL] [TAG] ..."
    const char* tagStart = nullptr;
    size_t tagLen = 0;
    unsigned long ms = 0;
    if (p[0] == '[') {
      char* end = nullptr;
      ms = std::strtoul(p + 1, &end, 10);
      if (end != p + 1 && *end == ']') {
        const char* q = end + 1;
        int bracket = 0;
        for (; q < p + lineLen && bracket < 2; ++q) {
          if (*q == '[') {
            ++bracket;
            if (bracket == 2) {
              const char* close = static_cast<const char*>(std::memchr(q + 1, ']', lineLen - (q + 1 - p)));
              if (close != nullptr) {
                tagStart = q + 1;
                tagLen = static_cast<size_t>(close - tagStart);
              }
              break;
            }
          }
        }
      }
    }
    if (tagStart != nullptr) {
      if (havePrev && ms < prevMs && prevTagStart != nullptr) {
        found = prevTagStart;
        foundLen = prevTagLen;
      }
      prevMs = ms;
      havePrev = true;
      prevTagStart = tagStart;
      prevTagLen = tagLen;
    }
    if (lineEnd == nullptr) break;
    p = lineEnd + 1;
  }
  if (found == nullptr || foundLen == 0) return false;
  if (foundLen >= outSize) foundLen = outSize - 1;
  std::memcpy(out, found, foundLen);
  out[foundLen] = '\0';
  return true;
}

size_t formatCrash(const char* device, const char* runningVersion, const char* board, const State& s, char* out,
                   const size_t outSize) {
  if (s.crashMessage[0] == '\0') {
    if (outSize > 0) out[0] = '\0';
    return 0;
  }
  Writer w(out, outSize);
  w.raw("{");
  w.key("service");
  w.str("firmware");
  w.raw(",");
  w.key("event");
  w.str("crash");
  w.raw(",");
  w.key("level");
  w.str("error");
  w.raw(",");
  w.key("device");
  w.str(device);
  w.raw(",");
  w.key("version");
  w.str(s.crashVersion[0] != '\0' ? s.crashVersion : runningVersion);
  w.raw(",");
  w.key("board");
  w.str(board);
  w.raw(",");
  w.key("props");
  w.raw("{");
  w.key("message");
  w.str(s.crashMessage);
  w.raw(",");
  w.key("backtrace");
  w.str(s.crashTrace);
  w.raw("}}");
  return w.finish();
}

void noteSent(State& s, const long today) {
  s.lastDay = today;
  s.appCount = 0;
  std::memset(s.apps, 0, sizeof(s.apps));
  s.otaFrom[0] = '\0';
  s.otaError[0] = '\0';
  clearBackoff(s);
}

void clearCrash(State& s) {
  s.crashMessage[0] = '\0';
  s.crashTrace[0] = '\0';
  s.crashVersion[0] = '\0';
}

bool parseBoardConfig(const char* json, char* url, const size_t urlSize, char* key, const size_t keySize) {
  if (json == nullptr || urlSize == 0 || keySize == 0) return false;
  url[0] = '\0';
  key[0] = '\0';
  const char* u = findKey(json, "url");
  const char* k = findKey(json, "anonKey");
  if (u == nullptr || k == nullptr) return false;
  if (readString(u, url, urlSize) == nullptr || readString(k, key, keySize) == nullptr) return false;
  if (std::strncmp(url, "https://", 8) != 0 || url[8] == '\0' || key[0] == '\0') return false;
  // A value that did not fit is a value we would send wrong; refuse it.
  if (std::strlen(url) + 1 >= urlSize || std::strlen(key) + 1 >= keySize) return false;
  return true;
}

}  // namespace heartbeat

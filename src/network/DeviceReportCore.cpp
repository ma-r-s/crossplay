#include "DeviceReportCore.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace devreport {

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

// The fallback when the device has no secret. Fixed for the life of the id:
// changing it renames every such device on the board. The word "heartbeat"
// stays because the ids made under it must stay.
constexpr char kSalt[] = "crossplay-heartbeat-2026";

// --- A JSON writer that cannot overrun: one bounded buffer, and a single
// "did it all fit" answer at the end instead of a check per field.
//
// `asciiOnly` is for a value that travels as an HTTP header: every byte
// outside printable ASCII is escaped, so the value is one line of 7-bit text
// whatever a panic message held.

class Writer {
 public:
  Writer(char* buf, const size_t size, const bool asciiOnly = false) : buf_(buf), size_(size), asciiOnly_(asciiOnly) {
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
  void boolean(const bool b) { raw(b ? "true" : "false"); }
  void str(const char* s) { str(s, std::strlen(s)); }
  void str(const char* s, const size_t len) {
    put('"');
    for (size_t i = 0; i < len; ++i) escaped(s[i]);
    put('"');
  }
  void key(const char* name) {
    str(name);
    put(':');
  }
  bool fits() const { return ok_; }
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
    const unsigned char u = static_cast<unsigned char>(c);
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
        if (u < 0x20 || (asciiOnly_ && u >= 0x7f)) {
          char t[8];
          std::snprintf(t, sizeof(t), "\\u%04x", static_cast<unsigned>(u));
          raw(t);
        } else {
          put(c);
        }
    }
  }

  char* buf_;
  size_t size_;
  bool asciiOnly_;
  size_t used_ = 0;
  bool ok_ = true;
};

// --- A JSON reader for the one fixed shape this module writes. Keys are
// looked up by name anywhere in the text: every key the file uses is unique,
// and a value can never contain a bare `"name":` because every quote inside a
// value is written escaped.

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

char lower(const char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

// One attempt at the report. `messageLen` is how much of the crash message to
// carry (0 with `withCrash` still true is a message cut to nothing, which is
// still a crash); `withTrace` whether the backtrace rides along.
size_t formatReport(const State& s, const char* runningVersion, const unsigned batteryPct, const unsigned heapMinKb,
                    const unsigned uptimeHours, const bool withCrash, const bool withTrace, const size_t messageLen,
                    char* out, const size_t outSize) {
  const OtaProps ota = otaProps(s, runningVersion);
  Writer w(out, outSize, true);
  w.raw("{");
  w.key("battery_pct");
  w.num(static_cast<long>(batteryPct));
  w.raw(",");
  w.key("heap_min_kb");
  w.num(static_cast<long>(heapMinKb));
  w.raw(",");
  w.key("uptime_h");
  w.num(static_cast<long>(uptimeHours));
  if (withCrash) {
    w.raw(",");
    w.key("crash");
    w.raw("{");
    w.key("message");
    w.str(s.crashMessage, messageLen);
    w.raw(",");
    w.key("version");
    w.str(s.crashVersion[0] != '\0' ? s.crashVersion : (runningVersion == nullptr ? "" : runningVersion));
    w.raw(",");
    w.key("backtrace");
    w.str(withTrace ? s.crashTrace : "");
    w.raw("}");
  }
  if (ota.attempted) {
    w.raw(",");
    w.key("ota");
    w.raw("{");
    w.key("attempted");
    w.boolean(true);
    w.raw(",");
    w.key("ok");
    w.boolean(ota.ok);
    w.raw(",");
    w.key("error");
    w.str(ota.error);
    w.raw(",");
    w.key("path");
    w.str(ota.path);
    w.raw("}");
  }
  w.raw("}");
  return w.finish();
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

void deviceId(const uint8_t mac[6], const uint8_t* secret, size_t secretLen, char out[kIdLen + 1]) {
  constexpr size_t kMaxSecret = 64;
  uint8_t input[6 + kMaxSecret];
  std::memcpy(input, mac, 6);
  size_t len = 6;
  if (secret != nullptr && secretLen > 0) {
    if (secretLen > kMaxSecret) secretLen = kMaxSecret;
    std::memcpy(input + 6, secret, secretLen);
    len += secretLen;
  } else {
    std::memcpy(input + 6, kSalt, sizeof(kSalt) - 1);
    len += sizeof(kSalt) - 1;
  }
  uint8_t digest[32];
  sha256(input, len, digest);
  static constexpr char kHex[] = "0123456789abcdef";
  for (int i = 0; i < 32; ++i) {
    out[2 * i] = kHex[digest[i] >> 4];
    out[2 * i + 1] = kHex[digest[i] & 0x0f];
  }
  out[kIdLen] = '\0';
}

bool recordCrash(State& s, const bool enabled, const char* message, const char* trace, const char* version) {
  if (!enabled || message == nullptr || message[0] == '\0') return false;
  std::snprintf(s.crashMessage, sizeof(s.crashMessage), "%s", message);
  std::snprintf(s.crashTrace, sizeof(s.crashTrace), "%s", trace == nullptr ? "" : trace);
  std::snprintf(s.crashVersion, sizeof(s.crashVersion), "%s", version == nullptr ? "" : version);
  return true;
}

bool recordOtaAttempt(State& s, const bool enabled, const char* from, const char* path) {
  if (!enabled) return false;
  std::snprintf(s.otaFrom, sizeof(s.otaFrom), "%s", from == nullptr ? "" : from);
  s.otaError[0] = '\0';
  std::snprintf(s.otaPath, sizeof(s.otaPath), "%s", path == nullptr ? "" : path);
  return true;
}

bool recordOtaFailure(State& s, const bool enabled, const char* error) {
  if (!enabled) return false;
  std::snprintf(s.otaError, sizeof(s.otaError), "%s", error == nullptr || error[0] == '\0' ? "unknown" : error);
  return true;
}

void noteSwitchedOn(State& s) {
  clearOta(s);
  clearCrash(s);
}

bool hasPending(const State& s) { return s.otaFrom[0] != '\0' || s.crashMessage[0] != '\0'; }

bool noteDelivered(State& s, const int httpStatus) {
  if (!accepted(httpStatus) || !hasPending(s)) return false;
  clearOta(s);
  clearCrash(s);
  return true;
}

void clearCrash(State& s) {
  s.crashMessage[0] = '\0';
  s.crashTrace[0] = '\0';
  s.crashVersion[0] = '\0';
}

void clearOta(State& s) {
  s.otaFrom[0] = '\0';
  s.otaError[0] = '\0';
  s.otaPath[0] = '\0';
}

bool parseState(const char* json, State& out) {
  out = State{};
  if (json == nullptr) return false;
  if (findKey(json, "ota") == nullptr && findKey(json, "crash") == nullptr) return false;
  readStringField(json, "from", out.otaFrom, sizeof(out.otaFrom));
  readStringField(json, "error", out.otaError, sizeof(out.otaError));
  readStringField(json, "path", out.otaPath, sizeof(out.otaPath));
  readStringField(json, "message", out.crashMessage, sizeof(out.crashMessage));
  readStringField(json, "trace", out.crashTrace, sizeof(out.crashTrace));
  readStringField(json, "version", out.crashVersion, sizeof(out.crashVersion));
  return true;
}

size_t formatState(const State& s, char* out, const size_t outSize) {
  Writer w(out, outSize);
  w.raw("{");
  w.key("ota");
  w.raw("{");
  w.key("from");
  w.str(s.otaFrom);
  w.raw(",");
  w.key("error");
  w.str(s.otaError);
  w.raw(",");
  w.key("path");
  w.str(s.otaPath);
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

OtaProps otaProps(const State& s, const char* runningVersion) {
  OtaProps o;
  o.attempted = s.otaFrom[0] != '\0';
  o.error = s.otaError;
  o.path = s.otaPath;
  // An install that reported no error and still boots the same version did
  // not happen, whatever the screen said; "ok" is the version having moved.
  o.ok =
      o.attempted && s.otaError[0] == '\0' && runningVersion != nullptr && std::strcmp(s.otaFrom, runningVersion) != 0;
  return o;
}

size_t buildReportHeader(const State& s, const char* runningVersion, const unsigned batteryPct,
                         const unsigned heapMinKb, const unsigned uptimeHours, char* out, const size_t outSize) {
  // The cap is on the bytes written, so the writer sees one byte more for the
  // terminator; a caller's smaller buffer is honoured below that.
  const size_t size = outSize < kMaxReportBytes + 1 ? outSize : kMaxReportBytes + 1;
  const bool crash = s.crashMessage[0] != '\0';
  size_t messageLen = std::strlen(s.crashMessage);
  // Full first; then without the backtrace; then the message halved until it
  // fits; then, if even a bare crash object cannot fit, without the crash.
  size_t n = formatReport(s, runningVersion, batteryPct, heapMinKb, uptimeHours, crash, true, messageLen, out, size);
  if (n > 0 || !crash) return n;
  n = formatReport(s, runningVersion, batteryPct, heapMinKb, uptimeHours, true, false, messageLen, out, size);
  while (n == 0 && messageLen > 0) {
    messageLen /= 2;
    n = formatReport(s, runningVersion, batteryPct, heapMinKb, uptimeHours, true, false, messageLen, out, size);
  }
  if (n > 0) return n;
  return formatReport(s, runningVersion, batteryPct, heapMinKb, uptimeHours, false, false, 0, out, size);
}

bool hostOf(const char* url, char* out, const size_t outSize) {
  if (outSize == 0) return false;
  out[0] = '\0';
  if (url == nullptr) return false;
  const char* p = url;
  if (const char* scheme = std::strstr(url, "://"); scheme != nullptr) p = scheme + 3;
  // The authority ends at the path, the query or the fragment.
  size_t len = 0;
  while (p[len] != '\0' && p[len] != '/' && p[len] != '?' && p[len] != '#') ++len;
  // Userinfo, if any, ends at the last '@' of the authority.
  for (size_t i = len; i > 0; --i) {
    if (p[i - 1] == '@') {
      p += i;
      len -= i;
      break;
    }
  }
  size_t hostLen = 0;
  if (len > 0 && p[0] == '[') {
    // A bracketed IPv6 literal: the host is what the brackets hold.
    ++p;
    --len;
    while (hostLen < len && p[hostLen] != ']') ++hostLen;
    if (hostLen == len) return false;
  } else {
    while (hostLen < len && p[hostLen] != ':') ++hostLen;
  }
  while (hostLen > 0 && p[hostLen - 1] == '.') --hostLen;
  if (hostLen == 0 || hostLen >= outSize) return false;
  for (size_t i = 0; i < hostLen; ++i) out[i] = lower(p[i]);
  out[hostLen] = '\0';
  return true;
}

bool isOwnHost(const char* host) {
  if (host == nullptr) return false;
  const size_t hostLen = std::strlen(host);
  constexpr size_t zoneLen = sizeof(kOwnZone) - 1;
  if (hostLen < zoneLen) return false;
  // The zone itself, or a name whose last label boundary is exactly at it.
  const size_t at = hostLen - zoneLen;
  if (at > 0 && host[at - 1] != '.') return false;
  for (size_t i = 0; i < zoneLen; ++i) {
    if (lower(host[at + i]) != kOwnZone[i]) return false;
  }
  return true;
}

bool reportsTo(const bool enabled, const char* url) {
  if (!enabled) return false;
  char host[kMaxHost];
  return hostOf(url, host, sizeof(host)) && isOwnHost(host);
}

size_t formatCrashMessage(const bool reasonRecorded, const char* reason, const char* reset, const char* lastTag,
                          char* out, const size_t outSize) {
  if (outSize == 0) return 0;
  const bool hasReason = reasonRecorded && reason != nullptr && reason[0] != '\0';
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
  for (; *p != '\0';) {
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

}  // namespace devreport

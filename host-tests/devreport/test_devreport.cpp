// The device report's rules, tested rather than trusted. See DeviceReportCore.h.
#include <cstdio>
#include <cstring>

#include "DeviceReportCore.h"

namespace {

int failures = 0;
int checks = 0;

void check(const bool ok, const char* what) {
  ++checks;
  if (!ok) {
    ++failures;
    std::printf("FAIL: %s\n", what);
  }
}

void checkStr(const char* got, const char* want, const char* what) {
  ++checks;
  if (got == nullptr || std::strcmp(got, want) != 0) {
    ++failures;
    std::printf("FAIL: %s\n  want: %s\n  got:  %s\n", what, want, got == nullptr ? "(null)" : got);
  }
}

void hex(const uint8_t* bytes, const size_t n, char* out) {
  static constexpr char kHex[] = "0123456789abcdef";
  for (size_t i = 0; i < n; ++i) {
    out[2 * i] = kHex[bytes[i] >> 4];
    out[2 * i + 1] = kHex[bytes[i] & 15];
  }
  out[2 * n] = '\0';
}

void testSha256() {
  uint8_t digest[32];
  char text[65];

  devreport::sha256(reinterpret_cast<const uint8_t*>(""), 0, digest);
  hex(digest, 32, text);
  checkStr(text, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "sha256 of nothing");

  devreport::sha256(reinterpret_cast<const uint8_t*>("abc"), 3, digest);
  hex(digest, 32, text);
  checkStr(text, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256 abc");

  // 56 bytes: the padding needs a second block.
  const char* two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  devreport::sha256(reinterpret_cast<const uint8_t*>(two), std::strlen(two), digest);
  hex(digest, 32, text);
  checkStr(text, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "sha256 two-block");

  // 64 bytes exactly: a full block and then padding alone.
  char full[65];
  std::memset(full, 'a', 64);
  full[64] = '\0';
  devreport::sha256(reinterpret_cast<const uint8_t*>(full), 64, digest);
  hex(digest, 32, text);
  checkStr(text, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb", "sha256 64 x a");
}

void testDeviceId() {
  const uint8_t mac[6] = {0x34, 0x85, 0x18, 0xab, 0xcd, 0xef};
  const uint8_t secret[devreport::kSecretLen] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  char a[devreport::kIdLen + 1];
  char b[devreport::kIdLen + 1];
  devreport::deviceId(mac, secret, sizeof(secret), a);
  devreport::deviceId(mac, secret, sizeof(secret), b);
  check(std::strlen(a) == 64, "id is 64 hex chars");
  checkStr(a, b, "the same device and secret hash to the same id");
  for (const char* p = a; *p; ++p) check((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'), "id is lowercase hex");
  // Neither the MAC's bytes nor its hex spelling appear in the id.
  check(std::strstr(a, "348518abcdef") == nullptr, "id does not contain the MAC");
  check(std::strstr(a, "34:85:18") == nullptr, "id does not contain the MAC with colons");
  check(std::strstr(a, "0102030405060708090a0b0c0d0e0f10") == nullptr, "id does not contain the secret");

  const uint8_t other[6] = {0x34, 0x85, 0x18, 0xab, 0xcd, 0xee};
  char c[devreport::kIdLen + 1];
  devreport::deviceId(other, secret, sizeof(secret), c);
  check(std::strcmp(a, c) != 0, "one bit of MAC changes the id");

  // Two secrets are two ids for one MAC: that is what stops the 2^24 MACs
  // behind a vendor prefix being tried against the table.
  uint8_t secret2[devreport::kSecretLen];
  std::memcpy(secret2, secret, sizeof(secret2));
  secret2[15] ^= 1;
  char d[devreport::kIdLen + 1];
  devreport::deviceId(mac, secret2, sizeof(secret2), d);
  check(std::strcmp(a, d) != 0, "one bit of secret changes the id");

  // No secret: the fixed-salt fallback. Pinned, because a changed salt or
  // hash renames every such device on the board.
  char e[devreport::kIdLen + 1];
  devreport::deviceId(mac, nullptr, 0, e);
  checkStr(e, "2b369f70d4d9337c721a4bd06c0528b709ef31df646752166f47b809d9038a01", "fallback id pinned");
  devreport::deviceId(mac, secret, 0, e);
  checkStr(e, "2b369f70d4d9337c721a4bd06c0528b709ef31df646752166f47b809d9038a01", "a zero-length secret is no secret");
  check(std::strcmp(a, e) != 0, "with a secret the id is not the fallback");

  // An oversized secret is cut, never overrun, and still hashes.
  uint8_t big[200];
  std::memset(big, 0xab, sizeof(big));
  char f[devreport::kIdLen + 1];
  devreport::deviceId(mac, big, sizeof(big), f);
  check(std::strlen(f) == 64, "an oversized secret still gives an id");
}

void testStateRoundTrip() {
  devreport::State s;
  std::snprintf(s.otaFrom, sizeof(s.otaFrom), "1.12.11");
  std::snprintf(s.otaError, sizeof(s.otaError), "too_large");
  std::snprintf(s.otaPath, sizeof(s.otaPath), "sd");
  std::snprintf(s.crashMessage, sizeof(s.crashMessage), "assert failed: q \"x\"\\ line\n1709");
  std::snprintf(s.crashTrace, sizeof(s.crashTrace), "0x3FCA: 0x1 0x2|0x3FCB: 0x4");
  std::snprintf(s.crashVersion, sizeof(s.crashVersion), "1.12.10");

  char text[devreport::kStateSize];
  const size_t n = devreport::formatState(s, text, sizeof(text));
  check(n > 0 && n == std::strlen(text), "state formats");
  checkStr(text,
           "{\"ota\":{\"from\":\"1.12.11\",\"error\":\"too_large\",\"path\":\"sd\"},\"crash\":{\"message\":\"assert "
           "failed: q \\\"x\\\"\\\\ line\\n1709\",\"trace\":\"0x3FCA: 0x1 0x2|0x3FCB: 0x4\",\"version\":\"1.12.10\"}}",
           "state file is the documented shape");

  devreport::State back;
  check(devreport::parseState(text, back), "state parses");
  checkStr(back.otaFrom, "1.12.11", "ota from survives");
  checkStr(back.otaError, "too_large", "ota error survives");
  checkStr(back.otaPath, "sd", "ota path survives");
  checkStr(back.crashMessage, "assert failed: q \"x\"\\ line\n1709", "crash message survives its own escaping");
  checkStr(back.crashTrace, "0x3FCA: 0x1 0x2|0x3FCB: 0x4", "trace survives");
  checkStr(back.crashVersion, "1.12.10", "the crashed version survives");

  // Defaults write as defaults and read back as defaults.
  devreport::State fresh;
  devreport::formatState(fresh, text, sizeof(text));
  checkStr(text,
           "{\"ota\":{\"from\":\"\",\"error\":\"\",\"path\":\"\"},\"crash\":{\"message\":\"\",\"trace\":\"\","
           "\"version\":\"\"}}",
           "fresh state");
  devreport::State freshBack;
  std::snprintf(freshBack.otaFrom, sizeof(freshBack.otaFrom), "stale");
  check(devreport::parseState(text, freshBack), "fresh parses");
  check(!devreport::hasPending(freshBack), "fresh reads back as nothing pending");

  // The widest record the device can write fits the file buffer: a message
  // made entirely of characters that escape to six bytes, a full hex trace,
  // and every other field at its limit and escaping.
  devreport::State widest;
  std::memset(widest.crashMessage, '\x02', sizeof(widest.crashMessage) - 1);
  std::memset(widest.crashTrace, 'F', sizeof(widest.crashTrace) - 1);
  std::memset(widest.crashVersion, '"', sizeof(widest.crashVersion) - 1);
  std::memset(widest.otaFrom, '"', sizeof(widest.otaFrom) - 1);
  std::memset(widest.otaError, '\\', sizeof(widest.otaError) - 1);
  std::memset(widest.otaPath, '\n', sizeof(widest.otaPath) - 1);
  check(devreport::formatState(widest, text, sizeof(text)) > 0, "the widest state fits the file buffer");
  // A record only a hand-edited file could hold (the trace escaping too)
  // formats nothing, and overruns nothing.
  std::memset(widest.crashTrace, '\x02', sizeof(widest.crashTrace) - 1);
  check(devreport::formatState(widest, text, sizeof(text)) == 0, "an impossible record is refused");
  check(text[0] == '\0', "and leaves an empty string");

  // A file that does not fit is refused whole, not written half.
  char tiny[40];
  check(devreport::formatState(s, tiny, sizeof(tiny)) == 0, "too small a buffer formats nothing");
  check(tiny[0] == '\0', "and leaves an empty string");
}

void testStateSurvivesDamage() {
  devreport::State s;
  std::snprintf(s.otaFrom, sizeof(s.otaFrom), "stale");
  check(!devreport::parseState("", s), "empty file does not parse");
  check(!devreport::hasPending(s), "and resets to defaults");
  check(!devreport::parseState("garbage", s), "garbage does not parse");
  check(!devreport::parseState(nullptr, s), "null does not parse");
  check(!devreport::parseState("{\"day\":3,\"apps\":[]}", s), "a file with neither record is no state");

  // Truncated mid-crash: the OTA record that was complete is kept.
  check(devreport::parseState("{\"ota\":{\"from\":\"1.0.0\",\"error\":\"http\",\"path\":\"ota\"},\"crash\":{\"mess", s),
        "truncated file parses what it can");
  checkStr(s.otaFrom, "1.0.0", "complete record kept from a truncated file");
  check(s.crashMessage[0] == '\0', "the cut record is empty");

  // The heartbeat-era file: the same two records beside keys nobody reads
  // now. A pending crash recorded under the old firmware still rides out.
  check(devreport::parseState("{\"day\":20699,\"apps\":[\"trivia\"],\"retry\":0,\"fails\":2,\"ota\":{\"from\":\"\","
                              "\"error\":\"\",\"path\":\"\"},\"crash\":{\"message\":\"boom\",\"trace\":\"0x1\","
                              "\"version\":\"1.12.13\"}}",
                              s),
        "the heartbeat-era file parses");
  checkStr(s.crashMessage, "boom", "its pending crash is kept");
  checkStr(s.crashVersion, "1.12.13", "with the version that crashed");
  check(s.otaFrom[0] == '\0', "and its empty ota reads as none");

  // Whitespace.
  check(devreport::parseState("{ \"ota\" : { \"from\" : \"1.0.0\" , \"path\" : \"sd\" } }", s), "spaced file parses");
  checkStr(s.otaPath, "sd", "spaced field read");
}

void testOtaProps() {
  devreport::State s;
  devreport::OtaProps o = devreport::otaProps(s, "1.12.12");
  check(!o.attempted && !o.ok && o.error[0] == '\0' && o.path[0] == '\0', "no attempt");

  std::snprintf(s.otaFrom, sizeof(s.otaFrom), "1.12.12");
  std::snprintf(s.otaPath, sizeof(s.otaPath), "sd");
  o = devreport::otaProps(s, "1.12.12");
  check(o.attempted && !o.ok, "attempted, same version afterwards: did not take");
  checkStr(o.path, "sd", "the path is carried");

  o = devreport::otaProps(s, "1.12.13");
  check(o.attempted && o.ok, "attempted, version moved: ok");

  std::snprintf(s.otaError, sizeof(s.otaError), "too_large");
  o = devreport::otaProps(s, "1.12.13");
  check(o.attempted && !o.ok, "an error is never ok, whatever the version");
  checkStr(o.error, "too_large", "the error is carried");
}

void testReportHeader() {
  char out[devreport::kMaxReportBytes + 1];
  devreport::State quiet;
  size_t n = devreport::buildReportHeader(quiet, "1.12.13", 84, 112, 31, out, sizeof(out));
  check(n > 0 && n == std::strlen(out), "the quiet report formats");
  checkStr(out, "{\"battery_pct\":84,\"heap_min_kb\":112,\"uptime_h\":31}", "nothing pending: the three numbers only");
  check(std::strstr(out, "crash") == nullptr && std::strstr(out, "ota") == nullptr,
        "no crash and no ota keys while nothing is pending");

  // Both pending: the documented shape, with the crash blamed on the version
  // that crashed and the ota judged against the version running now.
  devreport::State both;
  std::snprintf(both.crashMessage, sizeof(both.crashMessage), "assert failed: xQueueSemaphoreTake queue.c:1709");
  std::snprintf(both.crashTrace, sizeof(both.crashTrace), "0x3FCEBD40: 0x00000001|0x3FCEBD60: 0x00000002");
  std::snprintf(both.crashVersion, sizeof(both.crashVersion), "1.12.12");
  std::snprintf(both.otaFrom, sizeof(both.otaFrom), "1.12.13");
  std::snprintf(both.otaError, sizeof(both.otaError), "too_large");
  std::snprintf(both.otaPath, sizeof(both.otaPath), "ota");
  n = devreport::buildReportHeader(both, "1.12.13", 84, 112, 31, out, sizeof(out));
  check(n > 0 && n <= devreport::kMaxReportBytes, "the full report fits the cap");
  checkStr(
      out,
      "{\"battery_pct\":84,\"heap_min_kb\":112,\"uptime_h\":31,\"crash\":{\"message\":\"assert failed: "
      "xQueueSemaphoreTake queue.c:1709\",\"version\":\"1.12.12\",\"backtrace\":\"0x3FCEBD40: 0x00000001|0x3FCEBD60: "
      "0x00000002\"},\"ota\":{\"attempted\":true,\"ok\":false,\"error\":\"too_large\",\"path\":\"ota\"}}",
      "crash and ota ride along while pending");

  // One at a time.
  devreport::State crashOnly = both;
  devreport::clearOta(crashOnly);
  devreport::buildReportHeader(crashOnly, "1.12.13", 84, 112, 31, out, sizeof(out));
  check(std::strstr(out, "\"crash\":{") != nullptr && std::strstr(out, "\"ota\"") == nullptr, "crash only");
  devreport::State otaOnly = both;
  devreport::clearCrash(otaOnly);
  devreport::buildReportHeader(otaOnly, "1.12.14", 84, 112, 31, out, sizeof(out));
  check(std::strstr(out, "\"crash\"") == nullptr, "ota only: no crash key");
  check(std::strstr(out, "\"ota\":{\"attempted\":true,\"ok\":false,\"error\":\"too_large\"") != nullptr,
        "an error is never ok even after the version moved");
  otaOnly.otaError[0] = '\0';
  devreport::buildReportHeader(otaOnly, "1.12.14", 84, 112, 31, out, sizeof(out));
  check(std::strstr(out, "\"ok\":true,\"error\":\"\",\"path\":\"ota\"") != nullptr, "no error, version moved: ok");
  devreport::buildReportHeader(otaOnly, "1.12.13", 84, 112, 31, out, sizeof(out));
  check(std::strstr(out, "\"ok\":false") != nullptr, "no error, same version: did not take");

  // A record from a build that wrote no version: the running one is all
  // there is.
  crashOnly.crashVersion[0] = '\0';
  devreport::buildReportHeader(crashOnly, "1.12.13", 84, 112, 31, out, sizeof(out));
  check(std::strstr(out, "\"version\":\"1.12.13\"") != nullptr, "no crashed version recorded: the running one");

  // The value travels as one header line: whatever the panic text held, the
  // result is printable 7-bit ASCII with no line break in it.
  devreport::State odd;
  std::snprintf(odd.crashMessage, sizeof(odd.crashMessage), "Guru \"Meditation\"\\ Error\r\n\ttab\x01\xc3\xa9");
  devreport::buildReportHeader(odd, "1.12.13", 84, 112, 31, out, sizeof(out));
  check(
      std::strstr(out, "\"message\":\"Guru \\\"Meditation\\\"\\\\ Error\\r\\n\\ttab\\u0001\\u00c3\\u00a9\"") != nullptr,
      "the message is escaped for a header");
  for (const char* p = out; *p; ++p) {
    check(static_cast<unsigned char>(*p) >= 0x20 && static_cast<unsigned char>(*p) < 0x7f,
          "a header byte is printable ASCII");
  }

  // The cap. The widest record the file can hold (every message character
  // escaping to six bytes, a full trace, the widest ota) is far over 600
  // bytes; the backtrace goes first, then the message is cut, and the crash
  // still rides.
  devreport::State widest;
  std::memset(widest.crashMessage, '\x02', sizeof(widest.crashMessage) - 1);
  std::memset(widest.crashTrace, 'F', sizeof(widest.crashTrace) - 1);
  std::memset(widest.crashVersion, 'v', sizeof(widest.crashVersion) - 1);
  std::memset(widest.otaFrom, 'w', sizeof(widest.otaFrom) - 1);
  std::memset(widest.otaError, 'e', sizeof(widest.otaError) - 1);
  std::memset(widest.otaPath, 'p', sizeof(widest.otaPath) - 1);
  n = devreport::buildReportHeader(widest, "1.12.13", 100, 8388608, 4294967295u, out, sizeof(out));
  check(n > 0 && n <= devreport::kMaxReportBytes, "the widest record is cut to the cap");
  check(n == std::strlen(out), "and terminated where it says");
  check(std::strstr(out, "\"crash\":{\"message\":\"\\u0002") != nullptr, "the crash still rides, cut");
  check(std::strstr(out, "\"backtrace\":\"\"") != nullptr, "the backtrace went first");
  check(std::strstr(out, "\"ota\":{\"attempted\":true") != nullptr, "the ota record is whole");
  // The cap is the rule, not the buffer: a roomier buffer gets the same cut.
  // (A first version of this test used a 601-byte buffer, and a mutant that
  // dropped the cap survived it.)
  char roomy[devreport::kStateSize];
  n = devreport::buildReportHeader(widest, "1.12.13", 100, 8388608, 4294967295u, roomy, sizeof(roomy));
  check(n > 0 && n <= devreport::kMaxReportBytes, "a roomy buffer is still cut to the cap");
  checkStr(roomy, out, "and gets the same report");

  // A long plain message with a full trace: the trace alone is what does not
  // fit, so the message stays whole.
  devreport::State longMessage;
  std::memset(longMessage.crashMessage, 'm', sizeof(longMessage.crashMessage) - 1);
  std::memset(longMessage.crashTrace, 't', sizeof(longMessage.crashTrace) - 1);
  std::snprintf(longMessage.otaFrom, sizeof(longMessage.otaFrom), "1.12.12");
  std::snprintf(longMessage.otaError, sizeof(longMessage.otaError), "too_large");
  std::snprintf(longMessage.otaPath, sizeof(longMessage.otaPath), "sd");
  n = devreport::buildReportHeader(longMessage, "1.12.13", 100, 8388608, 4294967295u, out, sizeof(out));
  check(n > 0 && n <= devreport::kMaxReportBytes, "a long message with a trace fits the cap");
  char wholeMessage[devreport::kMaxCrashMessage + 2];
  std::snprintf(wholeMessage, sizeof(wholeMessage), "%s\"", longMessage.crashMessage);
  check(std::strstr(out, wholeMessage) != nullptr, "the whole message stays when dropping the trace is enough");

  // A caller's smaller buffer is honoured below the cap; one that cannot hold
  // the three numbers gets nothing.
  char small[200];
  n = devreport::buildReportHeader(widest, "1.12.13", 100, 8388608, 4294967295u, small, sizeof(small));
  check(n > 0 && n < sizeof(small), "a smaller buffer still gets a report");
  char tiny[30];
  check(devreport::buildReportHeader(quiet, "1.12.13", 84, 112, 31, tiny, sizeof(tiny)) == 0,
        "too small for the numbers: nothing");
  check(tiny[0] == '\0', "and an empty string");
}

void testHostOf() {
  char host[devreport::kMaxHost];
  check(devreport::hostOf("https://books.ma-r-s.com/opds", host, sizeof(host)), "a plain url has a host");
  checkStr(host, "books.ma-r-s.com", "the host, without scheme or path");
  devreport::hostOf("https://sync.ma-r-s.com", host, sizeof(host));
  checkStr(host, "sync.ma-r-s.com", "no path");
  devreport::hostOf("https://read.ma-r-s.com:8443/api/sync?x=1#f", host, sizeof(host));
  checkStr(host, "read.ma-r-s.com", "port, query and fragment are not the host");
  devreport::hostOf("https://user:pa@ss@books.ma-r-s.com/opds", host, sizeof(host));
  checkStr(host, "books.ma-r-s.com", "userinfo is not the host, even with an @ in the password");
  devreport::hostOf("HTTPS://Books.MA-R-S.com./opds", host, sizeof(host));
  checkStr(host, "books.ma-r-s.com", "lowercased, trailing dot dropped");
  devreport::hostOf("http://192.168.4.1:8080/x", host, sizeof(host));
  checkStr(host, "192.168.4.1", "an address is a host");
  devreport::hostOf("http://[::1]:8080/x", host, sizeof(host));
  checkStr(host, "::1", "a bracketed IPv6 literal is a host");
  devreport::hostOf("news.ycombinator.com/item?id=1", host, sizeof(host));
  checkStr(host, "news.ycombinator.com", "no scheme: the text up to the path");
  check(!devreport::hostOf("https:///path", host, sizeof(host)), "no host is no host");
  check(!devreport::hostOf("", host, sizeof(host)), "empty is no host");
  check(!devreport::hostOf(nullptr, host, sizeof(host)), "null is no host");
  check(!devreport::hostOf("http://[::1/x", host, sizeof(host)), "an unclosed bracket is no host");
  char tiny[8];
  check(!devreport::hostOf("https://books.ma-r-s.com/", tiny, sizeof(tiny)), "a host that does not fit is refused");
  check(tiny[0] == '\0', "and leaves nothing");
}

void testIsOwnHost() {
  check(devreport::isOwnHost("books.ma-r-s.com"), "books is ours");
  check(devreport::isOwnHost("sync.ma-r-s.com"), "sync is ours");
  check(devreport::isOwnHost("read.ma-r-s.com"), "read is ours");
  check(devreport::isOwnHost("crossplay.ma-r-s.com"), "the site is ours");
  check(devreport::isOwnHost("ma-r-s.com"), "the zone itself is ours");
  check(devreport::isOwnHost("BOOKS.MA-R-S.COM"), "case does not matter");
  check(!devreport::isOwnHost("news.ycombinator.com"), "hacker news is not ours");
  check(!devreport::isOwnHost("xkcd.com"), "xkcd is not ours");
  check(!devreport::isOwnHost("api.github.com"), "github is not ours");
  check(!devreport::isOwnHost("ma-r-s.com.evil.example"), "a name that continues past the zone is not ours");
  check(!devreport::isOwnHost("xma-r-s.com"), "a name that merely ends in the letters is not ours");
  check(!devreport::isOwnHost("ma-r-s.co"), "a prefix of the zone is not ours");
  check(!devreport::isOwnHost(""), "empty is not ours");
  check(!devreport::isOwnHost(nullptr), "null is not ours");
}

void testReportsTo() {
  check(devreport::reportsTo(true, "https://books.ma-r-s.com/opds"), "on, our host: report");
  check(devreport::reportsTo(true, "https://sync.ma-r-s.com/api/sync"), "on, the sync bridge: report");
  check(devreport::reportsTo(true, "https://read.ma-r-s.com/api/sync"), "on, the read bridge: report");
  check(!devreport::reportsTo(true, "https://news.ycombinator.com/"), "on, hacker news: nothing");
  check(!devreport::reportsTo(true, "https://xkcd.com/info.0.json"), "on, xkcd: nothing");
  check(!devreport::reportsTo(true, "https://api.github.com/repos/ma-r-s/crossplay/releases/latest"),
        "on, github: nothing, even with the name in the path");
  check(!devreport::reportsTo(true, "https://ma-r-s.com.evil.example/"), "on, a look-alike: nothing");
  check(!devreport::reportsTo(false, "https://books.ma-r-s.com/opds"), "off, our host: nothing");
  check(!devreport::reportsTo(false, "https://sync.ma-r-s.com/api/sync"), "off, the sync bridge: nothing");
  check(!devreport::reportsTo(true, nullptr), "on, no url: nothing");
}

void testDelivered() {
  devreport::State s;
  check(!devreport::noteDelivered(s, 200), "nothing pending: a 200 changes nothing");
  std::snprintf(s.crashMessage, sizeof(s.crashMessage), "boom");
  std::snprintf(s.crashVersion, sizeof(s.crashVersion), "1.0.0");
  std::snprintf(s.otaFrom, sizeof(s.otaFrom), "1.0.0");
  std::snprintf(s.otaError, sizeof(s.otaError), "http");
  std::snprintf(s.otaPath, sizeof(s.otaPath), "ota");
  check(devreport::hasPending(s), "something is pending");
  check(!devreport::noteDelivered(s, 0), "no answer: still pending");
  check(!devreport::noteDelivered(s, -1), "no request: still pending");
  check(!devreport::noteDelivered(s, 401), "refused: still pending");
  check(!devreport::noteDelivered(s, 500), "a server error: still pending");
  check(!devreport::noteDelivered(s, 302), "a redirect: still pending");
  check(devreport::hasPending(s), "and nothing was cleared by a failure");
  checkStr(s.crashMessage, "boom", "the crash is intact");
  check(devreport::noteDelivered(s, 201), "a 201 delivers");
  check(!devreport::hasPending(s), "and clears everything");
  check(s.crashMessage[0] == '\0' && s.crashTrace[0] == '\0' && s.crashVersion[0] == '\0', "crash cleared");
  check(s.otaFrom[0] == '\0' && s.otaError[0] == '\0' && s.otaPath[0] == '\0', "ota cleared");
  std::snprintf(s.otaFrom, sizeof(s.otaFrom), "1.0.0");
  check(devreport::noteDelivered(s, 200), "a 200 delivers an ota record alone");
  check(!devreport::hasPending(s), "and it is gone");
}

void testCrashMessage() {
  char msg[devreport::kMaxCrashMessage];
  // An assert keeps its own words in front, where the fingerprint reads them.
  devreport::formatCrashMessage(true, "assert failed: xQueueSemaphoreTake queue.c:1709", "panic", "READER", msg,
                                sizeof(msg));
  checkStr(msg, "assert failed: xQueueSemaphoreTake queue.c:1709 (reset: panic)", "an assert keeps its reason");
  // A CPU exception on the S3 has no reason: the reset and the last logger
  // are what tells two apart.
  devreport::formatCrashMessage(true, "", "panic", "READER", msg, sizeof(msg));
  checkStr(msg, "panic without a recorded reason (reset: panic; last log: READER)", "no reason: reset and last log");
  devreport::formatCrashMessage(true, "", "panic", "TRIVIA", msg, sizeof(msg));
  checkStr(msg, "panic without a recorded reason (reset: panic; last log: TRIVIA)", "another subsystem, another card");
  devreport::formatCrashMessage(true, nullptr, "cpu_lockup", "", msg, sizeof(msg));
  checkStr(msg, "panic without a recorded reason (reset: cpu_lockup)", "no last log: the reset alone");
  devreport::formatCrashMessage(true, "", "", nullptr, msg, sizeof(msg));
  checkStr(msg, "panic without a recorded reason (reset: unknown)", "nothing at all still says so");
  // The text in RTC memory outlives the crash that wrote it: an assert, then
  // a CPU exception before any clean boot, leaves the assert's words under
  // the exception. Only a set marker says the words are this crash's.
  devreport::formatCrashMessage(false, "assert failed: xQueueSemaphoreTake queue.c:1709", "panic", "READER", msg,
                                sizeof(msg));
  checkStr(msg, "panic without a recorded reason (reset: panic; last log: READER)",
           "marker not set: the stored reason is a previous crash's and is not used");
  devreport::formatCrashMessage(false, "assert failed: xQueueSemaphoreTake queue.c:1709", "int_wdt", "", msg,
                                sizeof(msg));
  checkStr(msg, "panic without a recorded reason (reset: int_wdt)", "marker not set, no last log: the reset alone");
  // Cut, never overrun.
  char tiny[24];
  const size_t n =
      devreport::formatCrashMessage(true, "assert failed: something long", "panic", "", tiny, sizeof(tiny));
  check(n == sizeof(tiny) - 1 && std::strlen(tiny) == sizeof(tiny) - 1, "a long message is cut to the buffer");

  char tag[16];
  // This boot (ms 12, 40, 900) after the previous one (ms 5000, 61000, 61010):
  // the previous boot's last line is the one before the stamp drops.
  const char* info =
      "CrossPlay version: 1.12.12\n\nPanic reason: \n\nLast logs:\n"
      "[5000] [INF] [MAIN] Device: x4pro\n"
      "[61000] [INF] [SHELF] open trivia\n"
      "[61010] [ERR] [TRIVIA] deck missing\n"
      "[12] [INF] [MAIN] Hardware detect: X4\n"
      "[40] [INF] [SYS] Dumped panic info to SD card\n"
      "[900] [INF] [DEVREPORT] on; device abcdef12..\n"
      "\n\nStack memory:\n";
  check(devreport::lastLogTagBeforeReset(info, tag, sizeof(tag)), "a boundary is found");
  checkStr(tag, "TRIVIA", "the last logger before the reset");
  // A ring that is all this boot's (the previous boot's lines scrolled out).
  const char* allNew =
      "Last logs:\n[12] [INF] [MAIN] a\n[40] [INF] [SYS] b\n[900] [INF] [DEVREPORT] c\n\n\nStack memory:\n";
  check(!devreport::lastLogTagBeforeReset(allNew, tag, sizeof(tag)), "no drop, no tag");
  check(tag[0] == '\0', "and the tag is empty");
  // Two boots ago is not the answer: the LAST drop wins.
  const char* twoBoots =
      "Last logs:\n[90000] [INF] [XKCD] old\n[10] [INF] [MAIN] up\n[7000] [ERR] [CHESS] bad\n[20] [INF] [MAIN] up "
      "again\n\n";
  check(devreport::lastLogTagBeforeReset(twoBoots, tag, sizeof(tag)), "two boots parse");
  checkStr(tag, "CHESS", "the most recent boundary");
  // Lines that are not log lines are skipped, not misread.
  const char* odd = "Last logs:\n[5000] [INF] [MAIN] a\ngarbage line\n[7] [INF] [SYS] b\n\n";
  check(devreport::lastLogTagBeforeReset(odd, tag, sizeof(tag)), "a foreign line is skipped");
  checkStr(tag, "MAIN", "the tag is still read");
  check(!devreport::lastLogTagBeforeReset("no logs section", tag, sizeof(tag)), "no section, no tag");
  check(!devreport::lastLogTagBeforeReset(nullptr, tag, sizeof(tag)), "null, no tag");
  // A tag longer than the buffer is cut, never overrun.
  char small[4];
  check(devreport::lastLogTagBeforeReset(info, small, sizeof(small)), "a small buffer still answers");
  checkStr(small, "TRI", "cut to fit");
}

void testToggle() {
  devreport::State s;
  // Off records nothing: not merely sends nothing, because a backlog
  // gathered while off would go out the moment it came back on.
  check(!devreport::recordCrash(s, false, "boom", "0x1|0x2", "1.0.0"), "off: a panic is not recorded");
  check(s.crashMessage[0] == '\0' && s.crashTrace[0] == '\0' && s.crashVersion[0] == '\0', "off: crash stays empty");
  check(!devreport::recordOtaAttempt(s, false, "1.0.0", "sd"), "off: an install attempt is not recorded");
  check(!devreport::recordOtaFailure(s, false, "http"), "off: an install failure is not recorded");
  check(s.otaFrom[0] == '\0' && s.otaError[0] == '\0' && s.otaPath[0] == '\0', "off: ota stays empty");
  check(!devreport::hasPending(s), "off: nothing pending");

  // On records, and says when the card needs writing.
  check(devreport::recordCrash(s, true, "boom", "0x1|0x2", "1.0.0"), "on: a panic is recorded");
  checkStr(s.crashMessage, "boom", "on: the message");
  checkStr(s.crashTrace, "0x1|0x2", "on: the trace");
  checkStr(s.crashVersion, "1.0.0", "on: the version");
  check(!devreport::recordCrash(s, true, "", "t", "1.0.0"), "an empty message is no record");
  check(devreport::recordOtaAttempt(s, true, "1.0.0", "ota"), "on: an install attempt is recorded");
  checkStr(s.otaFrom, "1.0.0", "on: from");
  checkStr(s.otaPath, "ota", "on: the path");
  check(devreport::recordOtaFailure(s, true, "http"), "on: an ota failure is recorded");
  checkStr(s.otaError, "http", "on: the error");
  devreport::recordOtaFailure(s, true, nullptr);
  checkStr(s.otaError, "unknown", "a null error is unknown");
  devreport::recordOtaAttempt(s, true, "1.0.1", "sd");
  check(s.otaError[0] == '\0', "a new attempt clears the old error");
  checkStr(s.otaPath, "sd", "and names its own path");
  check(devreport::hasPending(s), "on: something is pending");

  // The off-to-on edge forgets what the file still held.
  devreport::noteSwitchedOn(s);
  check(s.otaFrom[0] == '\0' && s.otaError[0] == '\0' && s.otaPath[0] == '\0', "on edge: ota forgotten");
  check(s.crashMessage[0] == '\0' && s.crashTrace[0] == '\0' && s.crashVersion[0] == '\0', "on edge: crash forgotten");
  check(!devreport::hasPending(s), "on edge: nothing pending");
}

void testAccepted() {
  check(devreport::accepted(201), "201 is accepted");
  check(devreport::accepted(200), "200 is accepted");
  check(devreport::accepted(299), "299 is accepted");
  check(!devreport::accepted(300), "300 is not");
  check(!devreport::accepted(0), "0 is not");
  check(!devreport::accepted(400), "400 is not");
  check(!devreport::accepted(401), "401 is not");
  check(!devreport::accepted(-1), "-1 is not");
}

void testHeaderNames() {
  // The names are the contract with the services; a typo here is a report
  // nobody reads.
  checkStr(devreport::kDeviceHeader, "X-CrossPlay-Device", "device header name");
  checkStr(devreport::kBoardHeader, "X-CrossPlay-Board", "board header name");
  checkStr(devreport::kReportHeader, "X-CrossPlay-Report", "report header name");
  checkStr(devreport::kOwnZone, "ma-r-s.com", "the zone");
  check(devreport::kMaxReportBytes == 600, "the cap is the documented 600 bytes");
}

}  // namespace

int main() {
  testSha256();
  testDeviceId();
  testStateRoundTrip();
  testStateSurvivesDamage();
  testOtaProps();
  testReportHeader();
  testHostOf();
  testIsOwnHost();
  testReportsTo();
  testDelivered();
  testCrashMessage();
  testToggle();
  testAccepted();
  testHeaderNames();
  std::printf("devreport: %d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}

// The heartbeat's rules, tested rather than trusted. See HeartbeatCore.h.
#include <cstdio>
#include <cstring>

#include "HeartbeatCore.h"

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

  heartbeat::sha256(reinterpret_cast<const uint8_t*>(""), 0, digest);
  hex(digest, 32, text);
  checkStr(text, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "sha256 of nothing");

  heartbeat::sha256(reinterpret_cast<const uint8_t*>("abc"), 3, digest);
  hex(digest, 32, text);
  checkStr(text, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256 abc");

  // 56 bytes: the padding needs a second block.
  const char* two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  heartbeat::sha256(reinterpret_cast<const uint8_t*>(two), std::strlen(two), digest);
  hex(digest, 32, text);
  checkStr(text, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "sha256 two-block");

  // 64 bytes exactly: a full block and then padding alone.
  char full[65];
  std::memset(full, 'a', 64);
  full[64] = '\0';
  heartbeat::sha256(reinterpret_cast<const uint8_t*>(full), 64, digest);
  hex(digest, 32, text);
  checkStr(text, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb", "sha256 64 x a");
}

void testDeviceId() {
  const uint8_t mac[6] = {0x34, 0x85, 0x18, 0xab, 0xcd, 0xef};
  char a[heartbeat::kIdLen + 1];
  char b[heartbeat::kIdLen + 1];
  heartbeat::deviceId(mac, a);
  heartbeat::deviceId(mac, b);
  check(std::strlen(a) == 64, "id is 64 hex chars");
  checkStr(a, b, "the same device hashes to the same id");
  for (const char* p = a; *p; ++p) check((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'), "id is lowercase hex");
  // Neither the MAC's bytes nor its hex spelling appear in the id.
  check(std::strstr(a, "348518abcdef") == nullptr, "id does not contain the MAC");
  check(std::strstr(a, "34:85:18") == nullptr, "id does not contain the MAC with colons");

  const uint8_t other[6] = {0x34, 0x85, 0x18, 0xab, 0xcd, 0xee};
  char c[heartbeat::kIdLen + 1];
  heartbeat::deviceId(other, c);
  check(std::strcmp(a, c) != 0, "one bit of MAC changes the id");

  // Pinned: a changed salt or hash renames every device on the board, and
  // this is the line that would say so.
  checkStr(a, "2b369f70d4d9337c721a4bd06c0528b709ef31df646752166f47b809d9038a01", "id pinned");
}

void testAppKey() {
  char out[heartbeat::kMaxAppKey + 1];
  check(heartbeat::appKey("HACKER NEWS", out), "hacker news has a key");
  checkStr(out, "hackernews", "HACKER NEWS -> hackernews");
  heartbeat::appKey("Connect Four", out);
  checkStr(out, "connectfour", "Connect Four -> connectfour");
  heartbeat::appKey("TOY BATTLE", out);
  checkStr(out, "toybattle", "TOY BATTLE -> toybattle");
  heartbeat::appKey("Get Books", out);
  checkStr(out, "getbooks", "Get Books -> getbooks");
  heartbeat::appKey("xkcd", out);
  checkStr(out, "xkcd", "xkcd stays");
  check(!heartbeat::appKey("---", out), "punctuation alone is no key");
  check(!heartbeat::appKey(nullptr, out), "null is no key");
  heartbeat::appKey("ABCDEFGHIJKLMNOPQRSTUVWXYZABC", out);
  check(std::strlen(out) == heartbeat::kMaxAppKey, "a long title is cut, not refused");
}

void testAddApp() {
  heartbeat::State s;
  check(heartbeat::addApp(s, "trivia"), "first add");
  check(!heartbeat::addApp(s, "trivia"), "second add of the same app is not a change");
  check(heartbeat::addApp(s, "chess"), "a different app adds");
  check(s.appCount == 2, "two apps counted");
  check(!heartbeat::addApp(s, ""), "empty key refused");
  check(!heartbeat::addApp(s, "abcdefghijklmnopqrstuvwxyz"), "too long refused");
  for (int i = 0; i < heartbeat::kMaxApps + 4; ++i) {
    char k[16];
    std::snprintf(k, sizeof(k), "app%d", i);
    heartbeat::addApp(s, k);
  }
  check(s.appCount == heartbeat::kMaxApps, "the set is capped");
  check(!heartbeat::addApp(s, "onemore"), "a full set adds nothing");
}

void testStateRoundTrip() {
  heartbeat::State s;
  s.lastDay = 20699;
  heartbeat::addApp(s, "trivia");
  heartbeat::addApp(s, "hackernews");
  std::snprintf(s.otaFrom, sizeof(s.otaFrom), "1.12.11");
  std::snprintf(s.otaError, sizeof(s.otaError), "too_large");
  std::snprintf(s.crashMessage, sizeof(s.crashMessage), "assert failed: q \"x\"\\ line\n1709");
  std::snprintf(s.crashTrace, sizeof(s.crashTrace), "0x3FCA: 0x1 0x2|0x3FCB: 0x4");

  char text[heartbeat::kBodySize];
  const size_t n = heartbeat::formatState(s, text, sizeof(text));
  check(n > 0 && n == std::strlen(text), "state formats");
  checkStr(text,
           "{\"day\":20699,\"apps\":[\"trivia\",\"hackernews\"],\"ota\":{\"from\":\"1.12.11\",\"error\":\"too_large\"},"
           "\"crash\":{\"message\":\"assert failed: q \\\"x\\\"\\\\ line\\n1709\",\"trace\":\"0x3FCA: 0x1 0x2|0x3FCB: "
           "0x4\"}}",
           "state file is the documented shape");

  heartbeat::State back;
  check(heartbeat::parseState(text, back), "state parses");
  check(back.lastDay == 20699, "day survives");
  check(back.appCount == 2, "apps survive");
  checkStr(back.apps[0], "trivia", "first app survives");
  checkStr(back.apps[1], "hackernews", "second app survives");
  checkStr(back.otaFrom, "1.12.11", "ota from survives");
  checkStr(back.otaError, "too_large", "ota error survives");
  checkStr(back.crashMessage, "assert failed: q \"x\"\\ line\n1709", "crash message survives its own escaping");
  checkStr(back.crashTrace, "0x3FCA: 0x1 0x2|0x3FCB: 0x4", "trace survives");

  // Defaults write as defaults and read back as defaults.
  heartbeat::State fresh;
  heartbeat::formatState(fresh, text, sizeof(text));
  checkStr(text, "{\"day\":-1,\"apps\":[],\"ota\":{\"from\":\"\",\"error\":\"\"},\"crash\":{\"message\":\"\",\"trace\":\"\"}}",
           "fresh state");
  heartbeat::State freshBack;
  freshBack.lastDay = 5;
  check(heartbeat::parseState(text, freshBack), "fresh parses");
  check(freshBack.lastDay == -1 && freshBack.appCount == 0, "fresh reads back as never sent");

  // A file that does not fit is refused whole, not written half.
  char tiny[40];
  check(heartbeat::formatState(s, tiny, sizeof(tiny)) == 0, "too small a buffer formats nothing");
  check(tiny[0] == '\0', "and leaves an empty string");
}

void testStateSurvivesDamage() {
  heartbeat::State s;
  s.lastDay = 7;
  check(!heartbeat::parseState("", s), "empty file does not parse");
  check(s.lastDay == -1, "and resets to defaults");
  check(!heartbeat::parseState("{\"apps\":[\"trivia\"]}", s), "no day means no state");
  check(!heartbeat::parseState("garbage", s), "garbage does not parse");
  check(!heartbeat::parseState(nullptr, s), "null does not parse");

  // Truncated mid-apps: the day is kept, the apps that were complete are kept.
  check(heartbeat::parseState("{\"day\":20699,\"apps\":[\"trivia\",\"hack", s), "truncated file parses what it can");
  check(s.lastDay == 20699, "day kept from a truncated file");
  check(s.appCount == 1, "complete apps kept from a truncated file");
  checkStr(s.apps[0], "trivia", "the complete app");

  // A file written by a build that knew fewer fields.
  check(heartbeat::parseState("{\"day\":3,\"apps\":[]}", s), "older file parses");
  check(s.otaFrom[0] == '\0' && s.crashMessage[0] == '\0', "missing fields are empty");

  // Foreign words in apps are normalised on the way in, and junk is dropped.
  check(heartbeat::parseState("{\"day\":3,\"apps\":[\"Hacker News\",\"---\",\"trivia\"]}", s), "odd apps parse");
  check(s.appCount == 2, "junk app dropped");
  checkStr(s.apps[0], "hackernews", "app normalised on read");

  // Whitespace and a negative day.
  check(heartbeat::parseState("{ \"day\" : -4 , \"apps\" : [ \"chess\" ] }", s), "spaced file parses");
  check(s.lastDay == -1, "negative day is never");
  checkStr(s.apps[0], "chess", "spaced app read");
}

void testDay() {
  check(heartbeat::dayFromEpoch(0) == -1, "epoch zero is no clock");
  check(heartbeat::dayFromEpoch(946684800) == -1, "2000-01-01 is the RTC default, not a date");
  check(heartbeat::dayFromEpoch(1735689600) == 20089, "2025-01-01 is a date");
  check(heartbeat::dayFromEpoch(1788393600) == 20699, "2026-09-03 00:00Z");
  check(heartbeat::dayFromEpoch(1788393600 + 86399) == 20699, "2026-09-03 23:59Z is the same day");
  check(heartbeat::dayFromEpoch(1788393600 + 86400) == 20700, "2026-09-04 is the next day");
}

void testDecide() {
  using heartbeat::Decision;
  using heartbeat::decide;
  check(decide(false, 100, 99, 0, 0) == Decision::Off, "off wins over everything");
  check(decide(true, -1, 99, 0, 0) == Decision::NoClock, "no clock, no heartbeat");
  check(decide(true, 100, -1, 0, 0) == Decision::Send, "never sent: send");
  check(decide(true, 100, 99, 0, 0) == Decision::Send, "a new day: send");
  check(decide(true, 100, 100, 0, 0) == Decision::AlreadyToday, "same day: not again");
  check(decide(true, 98, 100, 0, 0) == Decision::Send, "clock stepped back: still one per day");
  check(decide(true, 100, 99, 1000, 5000) == Decision::Backoff, "inside the backoff: wait");
  check(decide(true, 100, 99, 5000, 5000) == Decision::Send, "backoff over: send");
  check(decide(true, 100, 99, 6000, 5000) == Decision::Send, "past the backoff: send");
  // millis() wraps every 49 days; the comparison is signed for exactly that.
  check(decide(true, 100, 99, 10, 0xFFFFFFF0UL) == Decision::Send, "backoff across the millis wrap");
  check(decide(true, 100, 100, 1000, 5000) == Decision::Backoff, "backoff reported before already-today");
  check(!heartbeat::backingOff(1000, 0), "no backoff set");
  check(heartbeat::backingOff(1000, 5000), "before the backoff");
  check(!heartbeat::backingOff(5000, 5000), "at the backoff");
  check(heartbeat::backingOff(0xFFFFFF00UL, 0x00000010UL), "a backoff set just after the wrap, now just before it");
  check(!heartbeat::backingOff(0x00000020UL, 0xFFFFFF00UL), "a backoff set before the wrap, now past it");
  checkStr(heartbeat::decisionName(Decision::AlreadyToday), "already today", "decision has a name");
}

void testOtaProps() {
  heartbeat::State s;
  heartbeat::OtaProps o = heartbeat::otaProps(s, "1.12.12");
  check(!o.attempted && !o.ok && o.error[0] == '\0', "no attempt");

  std::snprintf(s.otaFrom, sizeof(s.otaFrom), "1.12.12");
  o = heartbeat::otaProps(s, "1.12.12");
  check(o.attempted && !o.ok, "attempted, same version afterwards: did not take");

  o = heartbeat::otaProps(s, "1.12.13");
  check(o.attempted && o.ok, "attempted, version moved: ok");

  std::snprintf(s.otaError, sizeof(s.otaError), "too_large");
  o = heartbeat::otaProps(s, "1.12.13");
  check(o.attempted && !o.ok, "an error is never ok, whatever the version");
  checkStr(o.error, "too_large", "the error is carried");
}

void testHeartbeatBody() {
  heartbeat::State s;
  heartbeat::addApp(s, "trivia");
  heartbeat::addApp(s, "hackernews");
  std::snprintf(s.otaFrom, sizeof(s.otaFrom), "1.12.11");
  heartbeat::Sample sample;
  sample.version = "1.12.12";
  sample.board = "x4pro";
  sample.uptimeHours = 31;
  sample.batteryPct = 84;
  sample.heapMinKb = 112;
  char body[heartbeat::kBodySize];
  const size_t n = heartbeat::formatHeartbeat("abc123", sample, s, body, sizeof(body));
  check(n > 0 && n == std::strlen(body), "heartbeat formats");
  checkStr(body,
           "{\"service\":\"firmware\",\"event\":\"heartbeat\",\"device\":\"abc123\",\"version\":\"1.12.12\","
           "\"board\":\"x4pro\",\"props\":{\"apps\":[\"trivia\",\"hackernews\"],\"uptime_h\":31,\"battery_pct\":84,"
           "\"heap_min_kb\":112,\"ota\":{\"attempted\":true,\"ok\":true,\"error\":\"\"}}}",
           "heartbeat body is the documented shape");

  // Nothing opened, nothing attempted: the common day.
  heartbeat::State quiet;
  heartbeat::formatHeartbeat("abc123", sample, quiet, body, sizeof(body));
  check(std::strstr(body, "\"apps\":[]") != nullptr, "empty apps is an empty array");
  check(std::strstr(body, "\"ota\":{\"attempted\":false,\"ok\":false,\"error\":\"\"}") != nullptr, "no ota");

  // The worst day: every slot full, and it still fits the device's buffer.
  heartbeat::State full;
  for (int i = 0; i < heartbeat::kMaxApps; ++i) {
    char k[heartbeat::kMaxAppKey + 1];
    std::memset(k, 'a' + (i % 26), heartbeat::kMaxAppKey);
    k[heartbeat::kMaxAppKey - 1] = static_cast<char>('0' + (i % 10));
    k[heartbeat::kMaxAppKey - 2] = static_cast<char>('0' + (i / 10));
    k[heartbeat::kMaxAppKey] = '\0';
    heartbeat::addApp(full, k);
  }
  std::memset(full.otaFrom, 'v', sizeof(full.otaFrom) - 1);
  std::memset(full.otaError, 'e', sizeof(full.otaError) - 1);
  char id[heartbeat::kIdLen + 1];
  std::memset(id, 'f', heartbeat::kIdLen);
  id[heartbeat::kIdLen] = '\0';
  sample.uptimeHours = 4294967295u;
  sample.batteryPct = 100;
  sample.heapMinKb = 8388608;
  check(heartbeat::formatHeartbeat(id, sample, full, body, sizeof(body)) > 0, "the fullest heartbeat fits the body");

  char small[100];
  check(heartbeat::formatHeartbeat("abc123", sample, s, small, sizeof(small)) == 0, "too small: nothing");
}

void testCrashBody() {
  heartbeat::State s;
  char body[heartbeat::kBodySize];
  check(heartbeat::formatCrash("abc", "1.12.12", "sticky", s, body, sizeof(body)) == 0, "no crash, no body");

  std::snprintf(s.crashMessage, sizeof(s.crashMessage), "assert failed: xQueueSemaphoreTake queue.c:1709 (( pxQueue ))");
  std::snprintf(s.crashTrace, sizeof(s.crashTrace), "0x3FCEBD40: 0x00000001|0x3FCEBD60: 0x00000002");
  const size_t n = heartbeat::formatCrash("abc", "1.12.12", "sticky", s, body, sizeof(body));
  check(n > 0, "crash formats");
  checkStr(body,
           "{\"service\":\"firmware\",\"event\":\"crash\",\"level\":\"error\",\"device\":\"abc\",\"version\":\"1.12.12\","
           "\"board\":\"sticky\",\"props\":{\"message\":\"assert failed: xQueueSemaphoreTake queue.c:1709 (( pxQueue "
           "))\",\"backtrace\":\"0x3FCEBD40: 0x00000001|0x3FCEBD60: 0x00000002\"}}",
           "crash body is level=error with props.message");

  // A panic reason with the characters JSON cares about.
  std::snprintf(s.crashMessage, sizeof(s.crashMessage), "Guru \"Meditation\"\\ Error\n\ttab\x01");
  heartbeat::formatCrash("abc", "1.12.12", "sticky", s, body, sizeof(body));
  check(std::strstr(body, "\"message\":\"Guru \\\"Meditation\\\"\\\\ Error\\n\\ttab\\u0001\"") != nullptr,
        "panic reason is escaped");

  // The longest crash that can be recorded fits the buffer.
  std::memset(s.crashMessage, 'm', sizeof(s.crashMessage) - 1);
  std::memset(s.crashTrace, 't', sizeof(s.crashTrace) - 1);
  check(heartbeat::formatCrash("abc", "1.12.12", "sticky", s, body, sizeof(body)) > 0, "the longest crash fits");
  // And so does the widest foreign input: a message made entirely of
  // characters that escape to six bytes, with a full hex trace beside it.
  std::memset(s.crashMessage, '\x02', sizeof(s.crashMessage) - 1);
  std::memset(s.crashTrace, 'F', sizeof(s.crashTrace) - 1);
  check(heartbeat::formatCrash("abc", "1.12.12", "sticky", s, body, sizeof(body)) > 0, "the widest escaping fits");
  // A record only a hand-edited file could hold formats nothing, and overruns nothing.
  std::memset(s.crashTrace, '\x02', sizeof(s.crashTrace) - 1);
  check(heartbeat::formatCrash("abc", "1.12.12", "sticky", s, body, sizeof(body)) == 0, "an impossible record is refused");
  check(body[0] == '\0', "and leaves an empty body");
}

void testNoteSent() {
  heartbeat::State s;
  heartbeat::addApp(s, "trivia");
  std::snprintf(s.otaFrom, sizeof(s.otaFrom), "1.0.0");
  std::snprintf(s.otaError, sizeof(s.otaError), "http");
  std::snprintf(s.crashMessage, sizeof(s.crashMessage), "boom");
  heartbeat::noteSent(s, 20334);
  check(s.lastDay == 20334, "sent day recorded");
  check(s.appCount == 0 && s.apps[0][0] == '\0', "apps forgotten");
  check(s.otaFrom[0] == '\0' && s.otaError[0] == '\0', "ota forgotten");
  checkStr(s.crashMessage, "boom", "a pending crash is not the heartbeat's to clear");
  heartbeat::clearCrash(s);
  check(s.crashMessage[0] == '\0' && s.crashTrace[0] == '\0', "crash cleared on its own");
}

void testBoardConfig() {
  char url[160];
  char key[320];
  check(heartbeat::parseBoardConfig("{\"url\":\"https://abc.supabase.co\",\"anonKey\":\"eyJ.public.key\"}", url,
                                    sizeof(url), key, sizeof(key)),
        "the site's answer parses");
  checkStr(url, "https://abc.supabase.co", "url");
  checkStr(key, "eyJ.public.key", "key");

  check(!heartbeat::parseBoardConfig("{\"error\":\"The board is not set up on this deployment.\"}", url, sizeof(url),
                                     key, sizeof(key)),
        "the 503 body is refused");
  check(url[0] == '\0' && key[0] == '\0', "and leaves nothing behind");
  check(!heartbeat::parseBoardConfig("{\"url\":\"http://abc.supabase.co\",\"anonKey\":\"k\"}", url, sizeof(url), key,
                                     sizeof(key)),
        "plain http is refused");
  check(!heartbeat::parseBoardConfig("{\"url\":\"https://x\",\"anonKey\":\"\"}", url, sizeof(url), key, sizeof(key)),
        "an empty key is refused");
  check(!heartbeat::parseBoardConfig("{\"url\":\"https://x\",\"anonKey\":\"k", url, sizeof(url), key, sizeof(key)),
        "an unterminated answer is refused");
  char tinyKey[4];
  check(!heartbeat::parseBoardConfig("{\"url\":\"https://x\",\"anonKey\":\"toolong\"}", url, sizeof(url), tinyKey,
                                     sizeof(tinyKey)),
        "a key that does not fit is refused rather than cut");
  check(!heartbeat::parseBoardConfig(nullptr, url, sizeof(url), key, sizeof(key)), "null refused");
}

void testAccepted() {
  check(heartbeat::accepted(201), "201 is accepted");
  check(heartbeat::accepted(200), "200 is accepted");
  check(!heartbeat::accepted(0), "0 is not");
  check(!heartbeat::accepted(400), "400 is not");
  check(!heartbeat::accepted(401), "401 is not");
  check(!heartbeat::accepted(-1), "-1 is not");
}

}  // namespace

int main() {
  testSha256();
  testDeviceId();
  testAppKey();
  testAddApp();
  testStateRoundTrip();
  testStateSurvivesDamage();
  testDay();
  testDecide();
  testOtaProps();
  testHeartbeatBody();
  testCrashBody();
  testNoteSent();
  testBoardConfig();
  testAccepted();
  std::printf("heartbeat: %d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}

// Live's arithmetic, on a laptop.
//
// Everything asserted here is a decision that takes real days to observe on the
// device: a backoff whose whole purpose is a battery three weeks out, a due-now
// rule that only misbehaves on the sleep after the one you watched, and an
// ETag round trip whose failure mode is a redundant 48KB write nobody sees.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../../src/apps_local/live/LiveCore.h"

static int failures = 0;

static void check(const bool ok, const char* what) {
  if (!ok) {
    std::printf("  FAIL %s\n", what);
    ++failures;
  }
}

static void checkEq(const long long got, const long long want, const char* what) {
  if (got != want) {
    std::printf("  FAIL %s: got %lld want %lld\n", what, got, want);
    ++failures;
  }
}

// --------------------------------------------------------------------------
// The interval a header asks for.

static void testClampInterval() {
  std::printf("interval clamp\n");
  checkEq(live::clampInterval(3600), 3600, "an hour is taken as given");
  checkEq(live::clampInterval(0), live::kMinIntervalSeconds, "zero is refused");
  checkEq(live::clampInterval(-1), live::kMinIntervalSeconds, "negative is refused");
  checkEq(live::clampInterval(60), live::kMinIntervalSeconds, "under the service's own floor");
  checkEq(live::clampInterval(30LL * 24 * 3600), live::kMaxIntervalSeconds, "a month is capped at a week");
  // The exact boundaries, because a clamp written with the wrong comparison
  // passes every test that only samples the middle.
  checkEq(live::clampInterval(live::kMinIntervalSeconds), live::kMinIntervalSeconds, "the floor itself survives");
  checkEq(live::clampInterval(live::kMaxIntervalSeconds), live::kMaxIntervalSeconds, "the ceiling itself survives");
}

// --------------------------------------------------------------------------
// The backoff. This is the test the feature exists behind: without a cap a
// device whose Wi-Fi went away is flat in three weeks.

static void testBackoff() {
  std::printf("backoff\n");
  checkEq(live::retryDelaySeconds(0), 0, "no failures is not a retry");
  checkEq(live::retryDelaySeconds(-3), 0, "a negative count is not a retry");
  checkEq(live::retryDelaySeconds(1), 15 * 60, "the first retry is 15 minutes");
  checkEq(live::retryDelaySeconds(2), 30 * 60, "then 30");
  checkEq(live::retryDelaySeconds(3), 60 * 60, "then an hour");
  checkEq(live::retryDelaySeconds(4), 2 * 3600, "then two");
  checkEq(live::retryDelaySeconds(5), 4 * 3600, "then four");
  checkEq(live::retryDelaySeconds(6), 8 * 3600, "then eight");
  checkEq(live::retryDelaySeconds(7), 16 * 3600, "then sixteen");
  checkEq(live::retryDelaySeconds(8), live::kMaxRetrySeconds, "and then the cap");

  // The cap HOLDS, at counts a real device reaches. 32 is where a shift-based
  // implementation is undefined; 1000 is a device left in a drawer for years.
  for (int n = 8; n <= 40; ++n) {
    checkEq(live::retryDelaySeconds(n), live::kMaxRetrySeconds, "the cap holds through the shift hazard");
  }
  checkEq(live::retryDelaySeconds(1000), live::kMaxRetrySeconds, "and holds absurdly far out");

  // Monotone: never a delay shorter than the one before it. A doubling loop
  // that overflows produces a SMALL number, which is the failure that reads as
  // good news, so this asserts the shape rather than the values.
  uint32_t previous = 0;
  for (int n = 1; n <= 64; ++n) {
    const uint32_t delay = live::retryDelaySeconds(n);
    check(delay >= previous, "the delay never shrinks");
    check(delay <= live::kMaxRetrySeconds, "the delay never passes the cap");
    previous = delay;
  }

  // The budget the comment claims, computed rather than asserted in prose. A
  // full week of failures from the first one, in wakes.
  int wakes = 0;
  long long elapsed = 0;
  int failureCount = 0;
  while (elapsed < 7 * 24 * 3600) {
    ++failureCount;
    elapsed += live::retryDelaySeconds(failureCount);
    ++wakes;
  }
  std::printf("  a bad week costs %d wakes (~%.1f mAh at 0.55 each)\n", wakes, wakes * 0.55);
  check(wakes <= 16, "a week unreachable stays inside a dozen-odd wakes");
}

// --------------------------------------------------------------------------
// The ETag, which is the difference between a 304 and 48KB.

static void checkStr(const std::string& got, const std::string& want, const char* what) {
  if (got != want) {
    std::printf("  FAIL %s: got '%s' want '%s'\n", what, got.c_str(), want.c_str());
    ++failures;
  }
}

static void testEtag() {
  std::printf("etag\n");
  checkStr(live::unquoteEtag("\"e97a98773543db75\""), "e97a98773543db75", "quotes come off");
  checkStr(live::unquoteEtag("e97a98773543db75"), "e97a98773543db75", "an unquoted one survives");
  checkStr(live::unquoteEtag("  \"abc\"  "), "abc", "surrounding space goes");
  checkStr(live::unquoteEtag("W/\"abc\""), "abc", "a weak marker is dropped, the value kept");
  checkStr(live::unquoteEtag(""), "", "nothing in, nothing out");
  // The round trip the transport actually performs: what pull() stores is what
  // the next request must send back inside quotes, and the service compares it
  // with its quotes stripped. Asserting the pair rather than one direction is
  // what stops a fix to one half from breaking the other.
  const std::string stored = live::unquoteEtag("\"e97a98773543db75\"");
  checkStr(live::unquoteEtag("\"" + stored + "\""), stored, "storing and re-sending is idempotent");
  // A value with an inner quote is left alone rather than mangled: only a
  // matching outer PAIR is stripped.
  checkStr(live::unquoteEtag("\"ab"), "\"ab", "a lone leading quote is not a pair");
}

// --------------------------------------------------------------------------
// The clock.

static void testClock() {
  std::printf("clock plausibility\n");
  check(!live::clockIsUsable(0), "a cold boot at the epoch is not a clock");
  check(!live::clockIsUsable(1000000000), "2001 is not this device's clock");
  check(live::clockIsUsable(1789000000), "a set clock is usable");
}

// --------------------------------------------------------------------------
// The one rule, over every case Mario named.

static live::Schedule paired(const int64_t last, const uint32_t interval, const int fails) {
  live::Schedule s;
  s.on = true;
  s.paired = true;
  s.lastAttemptEpoch = last;
  s.intervalSeconds = interval;
  s.consecutiveFailures = fails;
  return s;
}

static void testDecide() {
  std::printf("the wake rule\n");
  const int64_t now = 1789000000;

  // OFF is the whole point of the toggle: no timer, so the battery cost is
  // identical to a build without Live in it.
  {
    live::Schedule s = paired(now - 100000, 3600, 0);
    s.on = false;
    const live::Decision d = live::decide(s, now);
    check(!d.fetchNow, "off never fetches");
    checkEq(d.timerSeconds, 0, "off arms NO timer");
  }
  {
    live::Schedule s = paired(now - 100000, 3600, 0);
    s.paired = false;
    const live::Decision d = live::decide(s, now);
    check(!d.fetchNow, "unpaired never fetches");
    checkEq(d.timerSeconds, 0, "unpaired arms no timer");
  }

  // A fridge nobody touches: the timer ends the sleep exactly on the interval,
  // so the wake that arrives is always due.
  {
    const live::Decision d = live::decide(paired(now - 3600, 3600, 0), now);
    check(d.fetchNow, "due on the nose fetches");
    checkEq(d.timerSeconds, 3600, "and re-arms for the interval");
  }
  {
    const live::Decision d = live::decide(paired(now - 7200, 3600, 0), now);
    check(d.fetchNow, "overdue fetches");
  }

  // Picked up and put back down: not due, so the timer carries the REMAINDER
  // rather than a fresh interval. A fresh one here is how a device in daily use
  // never refreshes at all -- every pick-up would push the fetch further out.
  {
    const live::Decision d = live::decide(paired(now - 600, 3600, 0), now);
    check(!d.fetchNow, "not due does not fetch");
    checkEq(d.timerSeconds, 3000, "and arms only what is left");
  }
  {
    const live::Decision d = live::decide(paired(now - 1, 3600, 0), now);
    checkEq(d.timerSeconds, 3599, "one second in leaves the rest");
  }

  // Never asked.
  {
    const live::Decision d = live::decide(paired(0, 3600, 0), now);
    check(d.fetchNow, "a device that never asked is due");
  }

  // No clock: fetch once rather than arm a schedule against 1970. Arming
  // against a bogus clock is the expensive direction -- every sleep looks
  // overdue and every sleep spends the radio.
  {
    const live::Decision d = live::decide(paired(1789000000, 3600, 0), 50);
    check(d.fetchNow, "an implausible clock fetches once");
    checkEq(d.timerSeconds, 3600, "and arms the interval");
  }

  // Failing: the schedule follows the BACKOFF, not the interval. Without this
  // a device with a 15-minute cadence retries every 15 minutes forever, which
  // is exactly the drain the backoff exists to stop.
  {
    const live::Decision d = live::decide(paired(now - 1000, 900, 6), now);
    check(!d.fetchNow, "a recent failure waits the backoff, not the interval");
    checkEq(d.timerSeconds, 8 * 3600 - 1000, "and arms what is left of it");
  }
  {
    const live::Decision d = live::decide(paired(now - 8 * 3600, 900, 6), now);
    check(d.fetchNow, "the backoff elapsed, so it tries again");
  }

  // A clock that moved backwards past the last attempt. Bounded by the wait, so
  // the worst case is one early wake rather than a week-long timer.
  {
    const live::Decision d = live::decide(paired(now + 500000, 3600, 0), now);
    check(!d.fetchNow, "a future last-attempt does not fetch");
    check(d.timerSeconds <= 3600, "and can never arm longer than the interval");
  }

  // Every armed timer is inside what the RTC can be asked for and inside what
  // the service allows. Swept rather than sampled.
  for (int fails = 0; fails <= 12; ++fails) {
    for (int64_t ago = 0; ago <= 8 * 24 * 3600; ago += 997) {
      const live::Decision d = live::decide(paired(now - ago, 3600, fails), now);
      check(d.timerSeconds <= live::kMaxRetrySeconds || d.timerSeconds <= live::kMaxIntervalSeconds,
            "no armed timer is out of range");
    }
  }
}

int main() {
  testClampInterval();
  testBackoff();
  testEtag();
  testClock();
  testDecide();
  if (failures != 0) {
    std::printf("live: %d FAILED\n", failures);
    return 1;
  }
  std::printf("live: ok\n");
  return 0;
}

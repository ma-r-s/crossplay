// Live's arithmetic, on a laptop.
//
// Everything asserted here is a decision that takes real days to observe on the
// device: a backoff whose whole purpose is a battery three weeks out, a due-now
// rule that only misbehaves on the sleep after the one you watched, and an
// ETag round trip whose failure mode is a redundant 48KB write nobody sees.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../../src/apps_local/live/LiveCore.h"

static int failures = 0;
// Counted so the last line can say how many assertions ran, in the PHRASE the
// gate counts: check.sh reads sub-suites with `grep -c "checks, 0 failed"`, so
// a suite that only says "ok" reports ZERO assertions to the one instrument
// that is meant to notice a suite which stopped asserting anything.
static int checks = 0;

static void check(const bool ok, const char* what) {
  ++checks;
  if (!ok) {
    std::printf("  FAIL %s\n", what);
    ++failures;
  }
}

static void checkEq(const long long got, const long long want, const char* what) {
  ++checks;
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
  ++checks;
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
// Did the whole picture arrive?
//
// The size is NOT fixed. The X4 Pro's sleep screen is 2bpp four-level (96070
// bytes at 480x800) and the one-bit file (48062) is the other thing the same
// reader handles, so this asserts that BOTH are accepted -- and that the check
// is derived from the file rather than from either number.

static void headerFor(uint8_t* out, const uint32_t declared) {
  out[0] = 'B';
  out[1] = 'M';
  out[2] = static_cast<uint8_t>(declared & 0xff);
  out[3] = static_cast<uint8_t>((declared >> 8) & 0xff);
  out[4] = static_cast<uint8_t>((declared >> 16) & 0xff);
  out[5] = static_cast<uint8_t>((declared >> 24) & 0xff);
}

static void testImageCompleteness() {
  std::printf("image completeness\n");
  uint8_t h[6];

  // THE TWO REAL FORMATS. Neither is privileged and neither is hardcoded in
  // the code under test: both pass because the file says how long it is.
  headerFor(h, 48062);
  check(live::bmpIsComplete(h, sizeof(h), 48062), "the 1-bit image is accepted");
  headerFor(h, 96070);
  check(live::bmpIsComplete(h, sizeof(h), 96070), "the 2bpp four-level image is accepted");

  // And a depth this firmware has not been handed yet. The point of deriving
  // the length from the file is that the website can learn a new one without
  // a firmware release, so a size nobody has written down still works.
  headerFor(h, 384054);  // 8bpp
  check(live::bmpIsComplete(h, sizeof(h), 384054), "a depth nobody wrote down is accepted");

  // TRUNCATION, which is the thing this exists to catch, at both real sizes.
  headerFor(h, 96070);
  check(!live::bmpIsComplete(h, sizeof(h), 96069), "one byte short is refused");
  check(!live::bmpIsComplete(h, sizeof(h), 48062), "the 2bpp header with a 1-bit body is refused");
  headerFor(h, 48062);
  check(!live::bmpIsComplete(h, sizeof(h), 96070), "and the other way round");

  // Not a BMP at all. A service that answered an error page with a 200 would
  // otherwise have it renamed over the sleep screen.
  h[0] = '<';
  h[1] = 'h';
  check(!live::bmpIsComplete(h, sizeof(h), 48062), "something that is not a BMP is refused");

  // Degenerate inputs, none of which may be read as "complete".
  headerFor(h, 48062);
  check(!live::bmpIsComplete(nullptr, 6, 48062), "no header is not a picture");
  check(!live::bmpIsComplete(h, 3, 48062), "too few header bytes is not a picture");
  headerFor(h, 10);
  check(!live::bmpIsComplete(h, sizeof(h), 10), "a file too small to be a BMP is refused");
  headerFor(h, 0);
  check(!live::bmpIsComplete(h, sizeof(h), 0), "an empty body is refused");
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

  // NO CLOCK, which is a device whose battery went flat: it comes back with a
  // token and no way to measure elapsed time.
  {
    // Nothing failing: one attempt is worth making, and its answer sets the
    // clock, which ends this branch for good.
    const live::Decision d = live::decide(paired(1789000000, 3600, 0), 50);
    check(d.fetchNow, "with no clock and nothing failing, it tries once");
    checkEq(d.timerSeconds, 3600, "and arms the interval");
  }
  {
    // In backoff, it does NOT. This is the drain the backoff exists to
    // prevent: a device that cannot reach the service and cannot measure time
    // would otherwise fetch every time its owner put it down.
    const live::Decision d = live::decide(paired(1789000000, 3600, 4), 50);
    check(!d.fetchNow, "with no clock and a failure standing, a user's sleep does not fetch");
    checkEq(d.timerSeconds, 2 * 3600, "but the timer still carries the schedule");
  }
  {
    // And the timer wake fetches regardless, because the timer IS the
    // schedule. Without this the two rules above would strand a clockless
    // device in backoff forever: nothing would ever try again.
    const live::Decision d = live::decide(paired(1789000000, 3600, 4), 50, true);
    check(d.fetchNow, "the timer wake fetches with no clock and a failure standing");
  }
  {
    const live::Decision d = live::decide(paired(now - 10, 3600, 0), now, true);
    check(d.fetchNow, "a timer wake is due by construction, even seconds after the last attempt");
  }
  {
    live::Schedule s = paired(now - 10, 3600, 0);
    s.on = false;
    const live::Decision d = live::decide(s, now, true);
    check(!d.fetchNow, "a timer wake with Live off still fetches nothing");
    checkEq(d.timerSeconds, 0, "and arms nothing");
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

// --------------------------------------------------------------------------
// The date a sender's row carries.
//
// One row per phone, and the question the date answers is "how long has this
// person had access", so a day and a month is the whole of it. What is asserted
// here is what the SCREEN cannot assert: that an epoch the device never really
// had comes back EMPTY rather than as a plausible-looking "1 Jan", because a
// row drawing a date computed from a zero is a fact the screen would be
// inventing, and nothing downstream could tell it apart from a real one.

static void testShortDate() {
  std::printf("sender dates\n");
  // 2026-09-12T00:00:00Z and 2026-09-12T23:59:59Z: both are the 12th, which is
  // the whole point of asking only for a day. Computed from the epoch rather
  // than typed, so the expectation is not a second implementation of the bug.
  checkStr(live::shortDate(1789171200), "12 Sep", "midnight on the 12th");
  checkStr(live::shortDate(1789171200 + 86399), "12 Sep", "one second before the 13th");
  checkStr(live::shortDate(1789171200 + 86400), "13 Sep", "and the second after it rolls");

  // Every month spells itself, and none of them comes from strftime's %b: the
  // firmware sets no locale and the simulator inherits the shell's, so a
  // locale-dependent month would differ between the laptop the layout was
  // measured on and the panel it ships to.
  const char* kMonths[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  for (int m = 0; m < 12; ++m) {
    // The 1st of each month of 2026, walked forward from 2026-01-01T00:00:00Z.
    static const int kDays[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    const std::string got = live::shortDate(1767225600 + static_cast<int64_t>(kDays[m]) * 86400);
    checkStr(got, std::string("1 ") + kMonths[m], "the first of a month");
  }

  // NOT A DATE. A reader that has never had a clock is not a reader that was
  // paired in 1970, and the row draws the name alone rather than a number it
  // made up.
  check(live::shortDate(0).empty(), "the epoch is not a pairing date");
  check(live::shortDate(-1).empty(), "a negative stamp is not a pairing date");
  check(live::shortDate(live::kPlausibleEpochFloor - 1).empty(), "one second under the floor is still no clock");
  check(!live::shortDate(live::kPlausibleEpochFloor).empty(), "and the floor itself is usable");

  // The same floor the schedule uses. Two copies of "is this clock worth
  // believing" would be two things to edit alone.
  for (int64_t e = live::kPlausibleEpochFloor - 5000; e < live::kPlausibleEpochFloor + 5000; e += 37) {
    check(live::shortDate(e).empty() != live::clockIsUsable(e),
          "shortDate and clockIsUsable disagree about whether there is a clock");
  }
}

// --------------------------------------------------------------------------
// THE TWO LINES THE SCREEN LEADS WITH.
//
// The headline used to be printed from the INTERVAL -- "In about 24 hours"
// whether the last check was a minute ago or twenty-three hours ago -- which is
// why the screen's two largest facts read as one fact typed twice. It is
// computed from live::decide now, and what is asserted here is the two things a
// screenshot cannot show: that the figure tracks the time REMAINING, and that
// every phrase it can produce is short enough for the display cut.
//
// THE LENGTH IS AN ASSERTION AND NOT A STYLE NOTE. "In about 45 minutes"
// measures 464px at toybox_30 against a 448px body, so a phrasing an inch
// longer does not fail: fittedTitle steps it down a rung and the headline
// quietly becomes the same size as the small line under it. Measured in the
// real face by host-tests/wallcaption; bounded by character count here, which
// is the coarse guard the suite that cannot link a font can carry.
static const size_t kHeroBudget = 16;  // "In 45 minutes" is 13; 16 is the headroom, not a target

static live::Schedule pairedEvery(const uint32_t interval, const int64_t lastAttempt) {
  live::Schedule s;
  s.on = true;
  s.paired = true;
  s.intervalSeconds = interval;
  s.lastAttemptEpoch = lastAttempt;
  return s;
}

static void testNextCheckPhrase() {
  std::printf("next check phrase\n");
  const int64_t base = live::kPlausibleEpochFloor + 1000000;

  // THE SAME SCHEDULE AT TWO MOMENTS SAYS TWO THINGS. This is the whole defect:
  // the old line answered with the interval and could not tell these apart.
  const live::Schedule daily = pairedEvery(86400, base);
  checkStr(live::nextCheckPhrase(daily, base + 60), "In a day", "a check a minute ago is a day away");
  checkStr(live::nextCheckPhrase(daily, base + 82800), "In an hour", "and the same schedule 23 hours later is not");
  check(live::nextCheckPhrase(daily, base + 60) != live::nextCheckPhrase(daily, base + 82800),
        "the headline does not move as the day passes, which is the line it replaces");

  // Every band, at its own scale.
  checkStr(live::nextCheckPhrase(pairedEvery(3600, base), base + 1800), "In 30 minutes", "half an hour to go");
  // 44 minutes and 50 seconds left: the top of the minutes band, one rounding
  // step under the 45-minute edge where the singular hour takes over.
  checkStr(live::nextCheckPhrase(pairedEvery(3600, base), base + 910), "In 45 minutes", "the widest minutes phrase");
  checkStr(live::nextCheckPhrase(pairedEvery(3600, base), base + 900), "In an hour", "and one second past the edge");
  checkStr(live::nextCheckPhrase(pairedEvery(7200, base), base + 3600), "In an hour", "the singular hour");
  checkStr(live::nextCheckPhrase(pairedEvery(21600, base), base + 3600), "In 5 hours", "five hours");
  checkStr(live::nextCheckPhrase(pairedEvery(604800, base), base + 60), "In 7 days", "a week");

  // MINUTES STEP IN FIVES and never below five. A device whose clock comes from
  // one response header cannot honour a figure to the minute.
  for (uint32_t left = 3 * 60; left < 45 * 60; left += 7) {
    const std::string phrase = live::nextCheckPhrase(pairedEvery(3600, base), base + 3600 - left);
    check(phrase.compare(0, 3, "In ") == 0, "a minutes phrase is not a phrase");
    if (phrase.find("minutes") == std::string::npos) continue;
    const int minutes = std::atoi(phrase.c_str() + 3);
    check(minutes % 5 == 0, "the minutes figure is not rounded to five");
    check(minutes >= 5, "the minutes figure went below five");
  }

  // THE ANSWERS THAT ARE NOT FIGURES, each of them a state a figure would lie
  // about.
  live::Schedule off = pairedEvery(86400, base);
  off.on = false;
  checkStr(live::nextCheckPhrase(off, base + 60), "Paused",
           "a stopped reader still names a next check, which is a promise it is not keeping");
  live::Schedule unpaired = pairedEvery(86400, base);
  unpaired.paired = false;
  check(live::nextCheckPhrase(unpaired, base).empty(), "an unpaired reader invents a schedule it does not have");
  checkStr(live::nextCheckPhrase(pairedEvery(86400, 0), base), "Soon", "nothing asked yet is not a figure");
  checkStr(live::nextCheckPhrase(daily, live::kPlausibleEpochFloor - 1), "Soon",
           "a 1970 clock produces a figure the screen would be inventing");
  checkStr(live::nextCheckPhrase(daily, base + 86400), "Any moment", "an overdue check is not in the future");
  checkStr(live::nextCheckPhrase(daily, base + 86400 - 60), "Any moment", "the last minute rounds to a figure");

  // THE BACKOFF IS THE SCHEDULE. A reader that cannot reach the service is not
  // checking again in six hours, and the headline may not say it is.
  live::Schedule failing = pairedEvery(21600, base);
  failing.consecutiveFailures = 1;
  check(live::nextCheckPhrase(failing, base + 60) != live::nextCheckPhrase(pairedEvery(21600, base), base + 60),
        "the headline ignores the backoff, so it promises a check the schedule is not making");

  // AND NOTHING IT CAN SAY OVERFLOWS THE HEADLINE'S CUT. Walked over every
  // interval the service may ask for and every moment inside it, rather than
  // over the handful of phrases written above: the phrase that would have been
  // too long is the one nobody thought to type.
  const uint32_t intervals[] = {live::kMinIntervalSeconds, 900, 1800, 3600, 7200, 21600, 43200, 86400, 172800,
                                live::kMaxIntervalSeconds};
  for (const uint32_t interval : intervals) {
    for (uint32_t elapsed = 0; elapsed <= interval; elapsed += 31) {
      const std::string phrase = live::nextCheckPhrase(pairedEvery(interval, base), base + elapsed);
      check(!phrase.empty(), "a paired reader has nothing to put in its headline");
      check(phrase.size() <= kHeroBudget, "a next-check phrase is too long for the display cut");
    }
  }
}

static void testCadencePhrase() {
  std::printf("cadence phrase\n");
  checkStr(live::cadencePhrase(900), "Every 15 minutes", "minutes");
  checkStr(live::cadencePhrase(3600), "Every hour", "the singular hour");
  checkStr(live::cadencePhrase(21600), "Every 6 hours", "hours");
  checkStr(live::cadencePhrase(86400), "Every day", "the singular day");
  checkStr(live::cadencePhrase(172800), "Every 2 days", "days");
  // A WEEK IS A WEEK. The cap is seven days and the hours branch would have
  // answered "Every 168 hours", which is a number nobody reads.
  checkStr(live::cadencePhrase(604800), "Every week", "the cap");
  check(live::cadencePhrase(live::kMaxIntervalSeconds).find("168") == std::string::npos,
        "the longest cadence is reported in hours");
  // And nothing in the range is longer than the small line it sits on.
  for (uint32_t s = live::kMinIntervalSeconds; s <= live::kMaxIntervalSeconds; s += 60) {
    check(live::cadencePhrase(s).size() <= 20, "a cadence phrase is too long for the line under the headline");
  }
}

int main() {
  testClampInterval();
  testBackoff();
  testEtag();
  testImageCompleteness();
  testClock();
  testDecide();
  testShortDate();
  testNextCheckPhrase();
  testCadencePhrase();
  if (failures != 0) {
    std::printf("live: %d checks, %d failed\n", checks, failures);
    return 1;
  }
  std::printf("live: %d checks, 0 failed\n", checks);
  return 0;
}

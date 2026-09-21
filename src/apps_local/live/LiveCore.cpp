#include "LiveCore.h"

#include <cstdio>
#include <ctime>

namespace live {

uint32_t clampInterval(const int64_t seconds) {
  if (seconds < static_cast<int64_t>(kMinIntervalSeconds)) return kMinIntervalSeconds;
  if (seconds > static_cast<int64_t>(kMaxIntervalSeconds)) return kMaxIntervalSeconds;
  return static_cast<uint32_t>(seconds);
}

uint32_t retryDelaySeconds(const int consecutiveFailures) {
  if (consecutiveFailures <= 0) return 0;
  uint32_t delay = kFirstRetrySeconds;
  // Doubling in a loop rather than a shift, because the shift is the bug: at
  // 32 failures -- five days at the cap, which a device left unplugged reaches
  // without trying -- `1u << n` is undefined and the ones that do not trap
  // wrap to a small number, turning the ceiling into a 15-minute retry loop
  // with no evidence anywhere that it happened.
  for (int i = 1; i < consecutiveFailures; ++i) {
    if (delay >= kMaxRetrySeconds / 2) return kMaxRetrySeconds;
    delay *= 2;
  }
  return delay > kMaxRetrySeconds ? kMaxRetrySeconds : delay;
}

std::string unquoteEtag(const std::string& raw) {
  std::string out = raw;
  // A weak validator is still a validator. This service does not send W/, but
  // dropping the MARKER rather than the whole value is the difference between a
  // comparison that works and one that can never match.
  size_t begin = 0;
  while (begin < out.size() && (out[begin] == ' ' || out[begin] == '\t')) ++begin;
  if (out.compare(begin, 2, "W/") == 0) begin += 2;
  out.erase(0, begin);
  while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) out.pop_back();
  if (out.size() >= 2 && out.front() == '"' && out.back() == '"') {
    out = out.substr(1, out.size() - 2);
  }
  return out;
}

bool bmpIsComplete(const uint8_t* header, const size_t headerLen, const size_t received) {
  // 14 bytes of file header and 40 of info is the smallest thing that can call
  // itself a BMP; anything shorter is not a short download, it is not a BMP.
  if (header == nullptr || headerLen < 6 || received < 54) return false;
  if (header[0] != 'B' || header[1] != 'M') return false;
  const uint32_t declared = static_cast<uint32_t>(header[2]) | (static_cast<uint32_t>(header[3]) << 8) |
                            (static_cast<uint32_t>(header[4]) << 16) | (static_cast<uint32_t>(header[5]) << 24);
  return declared == received;
}

bool clockIsUsable(const int64_t nowEpoch) { return nowEpoch >= kPlausibleEpochFloor; }

std::string shortDate(const int64_t epoch) {
  // The same floor the schedule uses, for the same reason: a number below it is
  // not an early date, it is a device that never had a clock.
  if (!clockIsUsable(epoch)) return std::string();
  const std::time_t t = static_cast<std::time_t>(epoch);
  std::tm parts{};
#if defined(_WIN32)
  if (gmtime_s(&parts, &t) != 0) return std::string();
#else
  if (gmtime_r(&t, &parts) == nullptr) return std::string();
#endif
  // Spelled out rather than taken from strftime's %b, which is LOCALE
  // dependent: the firmware sets no locale and the simulator inherits the
  // shell's, so the one place this is read would differ between the laptop the
  // layout was measured on and the panel it ships to.
  static const char* kMonths[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  if (parts.tm_mon < 0 || parts.tm_mon > 11) return std::string();
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%d %s", parts.tm_mday, kMonths[parts.tm_mon]);
  return std::string(buf);
}

Decision decide(const Schedule& schedule, const int64_t nowEpoch, const bool timerFired) {
  Decision out;
  // Off, or nothing to ask: no timer at all. Not "a long timer" -- a device
  // with Live off must cost what it costs today, and a wake that exists only
  // to discover there is nothing to do is still a wake.
  if (!schedule.on || !schedule.paired) return out;

  const uint32_t wait =
      schedule.consecutiveFailures > 0 ? retryDelaySeconds(schedule.consecutiveFailures) : schedule.intervalSeconds;

  // Our own timer ended this sleep, so the refresh it was armed for is due.
  // No clock is consulted because none is needed.
  if (timerFired) {
    out.fetchNow = true;
    out.timerSeconds = wait;
    return out;
  }

  // Never asked: due, whatever the clock says.
  if (schedule.lastAttemptEpoch <= 0) {
    out.fetchNow = true;
    out.timerSeconds = wait;
    return out;
  }

  // No clock to measure against. One attempt is worth making -- its answer
  // carries X-Server-Time and sets the clock, which ends this branch for good
  // -- but only while nothing is failing. In backoff, a device with no clock
  // would otherwise fetch on every sleep its owner caused, which is the drain
  // the backoff exists to prevent, bypassed in the one situation that reaches
  // it. The armed timer still carries the schedule: the RTC counts correctly
  // with no wall clock at all.
  if (!clockIsUsable(nowEpoch)) {
    out.fetchNow = schedule.consecutiveFailures == 0;
    out.timerSeconds = wait;
    return out;
  }

  const int64_t due = schedule.lastAttemptEpoch + static_cast<int64_t>(wait);
  if (nowEpoch >= due) {
    out.fetchNow = true;
    out.timerSeconds = wait;
    return out;
  }

  // A clock that ran BACKWARDS past the last attempt (a settimeofday from a
  // server whose time moved, or a cold boot that was set late) would otherwise
  // arm a timer of up to a week. Bounded by the wait itself:
  // the worst case is one early wake, which costs a single check.
  int64_t remaining = due - nowEpoch;
  if (remaining > static_cast<int64_t>(wait)) remaining = static_cast<int64_t>(wait);
  out.fetchNow = false;
  out.timerSeconds = static_cast<uint32_t>(remaining);
  return out;
}

namespace {

// "45 minutes", "an hour", "5 hours", "a day", "2 days": the coarsest unit the
// figure survives in.
//
// The band edges are the ones every humanised duration has used since moment.js
// picked them -- 45 minutes, 90 minutes, 22 hours, 36 hours -- rather than
// edges invented here. They exist because the singular forms have to cover the
// gap: without the 45..90 band, 80 minutes is either "80 minutes", a figure
// nobody needs, or "an hour", which is wrong by a third and says so.
//
// Minutes step in fives, and never below five. A device whose clock comes from
// one response header cannot honour a figure to the minute, and a number that
// is visibly rounded says so without spending a word on saying it.
std::string roughSpan(const uint32_t seconds) {
  char buf[24];
  if (seconds < 45u * 60u) {
    unsigned minutes = (seconds + 150u) / 300u * 5u;
    if (minutes < 5u) minutes = 5u;
    std::snprintf(buf, sizeof(buf), "%u minutes", minutes);
    return std::string(buf);
  }
  if (seconds < 90u * 60u) return "an hour";
  if (seconds < 22u * 3600u) {
    const unsigned hours = (seconds + 1800u) / 3600u;
    std::snprintf(buf, sizeof(buf), "%u hours", hours);
    return std::string(buf);
  }
  if (seconds < 36u * 3600u) return "a day";
  const unsigned days = (seconds + 43200u) / 86400u;
  std::snprintf(buf, sizeof(buf), "%u days", days);
  return std::string(buf);
}

}  // namespace

std::string nextCheckPhrase(const Schedule& schedule, const int64_t nowEpoch) {
  // Not paired: there is no schedule, and the caller is drawing the code screen
  // rather than this line. Empty rather than a sentence, so a screen that drew
  // it anyway shows nothing instead of a claim.
  if (!schedule.paired) return std::string();
  if (!schedule.on) return "Paused";
  // No clock, or nothing asked yet. `decide` answers a figure in both cases --
  // it falls back to the whole interval -- and a figure measured against a 1970
  // clock is a number the screen would be inventing.
  if (schedule.lastAttemptEpoch <= 0 || !clockIsUsable(nowEpoch)) return "Soon";
  const Decision decision = decide(schedule, nowEpoch);
  // Three minutes, not one: the screen is repainted by events and not by a
  // clock, so whatever it says is already a little old by the time it is read.
  if (decision.fetchNow || decision.timerSeconds < 3u * 60u) return "Any moment";
  return "In " + roughSpan(decision.timerSeconds);
}

std::string cadencePhrase(const uint32_t intervalSeconds) {
  char buf[24];
  if (intervalSeconds >= 604800u && intervalSeconds % 604800u == 0u) {
    const unsigned weeks = intervalSeconds / 604800u;
    std::snprintf(buf, sizeof(buf), weeks == 1u ? "Every week" : "Every %u weeks", weeks);
  } else if (intervalSeconds >= 86400u && intervalSeconds % 86400u == 0u) {
    const unsigned days = intervalSeconds / 86400u;
    std::snprintf(buf, sizeof(buf), days == 1u ? "Every day" : "Every %u days", days);
  } else if (intervalSeconds >= 3600u && intervalSeconds % 3600u == 0u) {
    const unsigned hours = intervalSeconds / 3600u;
    std::snprintf(buf, sizeof(buf), hours == 1u ? "Every hour" : "Every %u hours", hours);
  } else {
    std::snprintf(buf, sizeof(buf), "Every %u minutes", intervalSeconds / 60u);
  }
  return std::string(buf);
}

}  // namespace live

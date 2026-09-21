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

}  // namespace live

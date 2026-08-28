#pragma once

// The pairing decision, as a pure function.
//
// This is the third design for it, and the first two were both wrong -- in
// different ways, each caught by a cold review rather than by anything that
// could fail. It is the gate in front of replacing the device's firmware, so it
// should be answerable without a device.
//
// The two mistakes are worth naming, because both look right in isolation:
//
//   1. Comparing the code BEFORE consulting the timer. Then every guess is
//      fully evaluated and pays off if it hits, and the timer only chooses
//      which log line prints. A limiter you have already answered is not a
//      limiter; the search falls at the device's request rate.
//   2. Extending the window on a refused attempt. Then a flood holds it open
//      forever, which locks the owner out and puts rotation behind a return
//      that is never reached.
//
// So: gate first, and a gated attempt changes nothing. One evaluated guess per
// backoff interval, and the owner's worst case is waiting out one interval.

#include <cstdint>

namespace devmode {
namespace pairing {

constexpr unsigned long kRetryMs = 1000;
constexpr unsigned long kRetryCeilingMs = 30000;
// Never rotate faster than the owner can read six digits off the panel and type
// them. Rotation throws away accumulated guesses; it is NOT what bounds the
// search, and two versions of this comment claimed it was.
constexpr unsigned long kMinRotateIntervalMs = 60000;
constexpr int kFailuresBeforeRotate = 5;

struct State {
  int failures = 0;
  unsigned long notBefore = 0;
  unsigned long lastRotate = 0;
};

enum class Verdict : uint8_t {
  Accept,
  RefusedClosed,  // inside the window; the code was not even looked at
  RefusedWrong,
};

struct Outcome {
  Verdict verdict = Verdict::Accept;
  State next;
  bool rotate = false;
  // Whether this refusal is worth a log line. The RTC ring is 16 entries and
  // this path is reachable unauthenticated, so a flood must not be able to
  // erase the pre-panic tail that /api/dev/crash exists to deliver.
  bool log = false;
};

inline Outcome decide(const bool codeMatches, const unsigned long now, const State& s) {
  Outcome o;
  o.next = s;

  // GATE BEFORE COMPARING, and change nothing on the way out.
  if (s.notBefore != 0 && static_cast<long>(now - s.notBefore) < 0) {
    o.verdict = Verdict::RefusedClosed;
    return o;
  }

  if (!codeMatches) {
    o.verdict = Verdict::RefusedWrong;
    o.log = s.failures < kFailuresBeforeRotate;
    // No saturation guard here, and none is needed: rotation resets failures and
    // becomes allowed 60s after the last one, so the counter cannot climb past a
    // handful before it is zeroed. A cap would be a constant guarding nothing,
    // which this branch has already shipped twice. What keeps it bounded is
    // tested in host-tests/devpair -- break rotation and that test fails.
    o.next.failures++;
    unsigned long wait = kRetryMs;
    for (int i = 1; i < o.next.failures && wait < kRetryCeilingMs; ++i) wait <<= 1;
    if (wait > kRetryCeilingMs) wait = kRetryCeilingMs;
    o.next.notBefore = now + wait;
    const bool due = o.next.failures >= kFailuresBeforeRotate;
    const bool allowed = static_cast<long>(now - (s.lastRotate + kMinRotateIntervalMs)) >= 0;
    if (due && allowed) {
      o.rotate = true;
      o.next.failures = 0;
      o.next.lastRotate = now;
    }
    return o;
  }

  o.verdict = Verdict::Accept;
  o.next.failures = 0;
  o.next.notBefore = 0;
  return o;
}

}  // namespace pairing
}  // namespace devmode

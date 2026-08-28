// See run.sh for why this exists.

#include <cstdio>
#include <string>

#include "DevModePairing.h"

namespace {

using namespace devmode::pairing;

int checks = 0;
int failed = 0;
void ok() { checks++; }
void bad(const std::string& what) {
  checks++;
  failed++;
  std::printf("FAIL devpair  %s\n", what.c_str());
}
void want(bool cond, const std::string& what) {
  if (cond)
    ok();
  else
    bad(what);
}

const char* name(Verdict v) {
  switch (v) {
    case Verdict::Accept:
      return "Accept";
    case Verdict::RefusedClosed:
      return "RefusedClosed";
    case Verdict::RefusedWrong:
      return "RefusedWrong";
  }
  return "?";
}

void wantVerdict(Verdict got, Verdict expect, const std::string& what) {
  if (got == expect) {
    ok();
  } else {
    bad(what + ": got " + name(got) + ", wanted " + name(expect));
  }
}

}  // namespace

int main() {
  // -- the bug that shipped twice: the gate must precede the comparison -----
  //
  // A CORRECT code inside a closed window is refused. That is the whole
  // mechanism: if a right answer is honoured inside the window, then so is a
  // lucky guess, and the limiter bounds nothing at all.
  {
    State s;
    s.notBefore = 5000;
    const Outcome o = decide(/*codeMatches=*/true, 1000, s);
    wantVerdict(o.verdict, Verdict::RefusedClosed, "correct code inside the window");
  }

  // -- and a gated attempt must change NOTHING ------------------------------
  //
  // The other half. Extending on refusal let a flood hold the window open for
  // ever, which locked the owner out and made rotation unreachable.
  {
    State s;
    s.failures = 3;
    s.notBefore = 5000;
    s.lastRotate = 100;
    const Outcome o = decide(false, 1000, s);
    want(o.next.notBefore == s.notBefore, "a gated attempt extended the window");
    want(o.next.failures == s.failures, "a gated attempt counted");
    want(o.next.lastRotate == s.lastRotate, "a gated attempt moved lastRotate");
    want(!o.rotate, "a gated attempt rotated");
    want(!o.log, "a gated attempt logged (the ring is 16 lines and this path is open)");
  }

  // -- THE OWNER IS NEVER LOCKED OUT ----------------------------------------
  //
  // The property the whole design turns on. Flood with wrong guesses as fast as
  // the device answers, then let the window expire once: a correct code is
  // accepted. If this ever fails, the feature is unusable under attack.
  {
    State s;
    unsigned long now = 0;
    for (int i = 0; i < 500; ++i) {
      const Outcome o = decide(false, now, s);
      s = o.next;
      now += 1;  // as fast as HTTP answers
    }
    now = s.notBefore;  // the owner waits out exactly one interval
    const Outcome o = decide(true, now, s);
    wantVerdict(o.verdict, Verdict::Accept, "owner locked out after a 500-request flood");
    want(o.next.failures == 0, "accepting did not clear failures");
    want(o.next.notBefore == 0, "accepting did not clear the window");
  }

  // -- an attacker gets ONE evaluated guess per interval --------------------
  {
    State s;
    unsigned long now = 0;
    int evaluated = 0;
    for (int i = 0; i < 100000; ++i) {  // 100s of flooding at 1kHz
      const Outcome o = decide(false, now, s);
      if (o.verdict == Verdict::RefusedWrong) evaluated++;
      s = o.next;
      now += 1;
    }
    // 100s at a ceiling of 30s, with the early rungs at 1/2/4/8/16s: single
    // figures, not thousands. The exact number is not the contract; the order
    // of magnitude is.
    want(evaluated < 15, "flood got " + std::to_string(evaluated) + " evaluated guesses in 100s");
  }

  // -- backoff doubles to the ceiling and stops -----------------------------
  {
    State s;
    unsigned long now = 0;
    const unsigned long expect[] = {1000, 2000, 4000, 8000, 16000, 30000, 30000};
    for (int i = 0; i < 7; ++i) {
      const Outcome o = decide(false, now, s);
      const unsigned long wait = o.next.notBefore - now;
      if (o.rotate) {  // rotation resets failures, so only check before it fires
        s = o.next;
        now = o.next.notBefore;
        continue;
      }
      want(wait == expect[i], "backoff step " + std::to_string(i) + " was " + std::to_string(wait));
      s = o.next;
      now = o.next.notBefore;
    }
  }

  // -- failures stay bounded, and ROTATION is what bounds them --------------
  //
  // failures is advanced by an unauthenticated request, so an unbounded counter
  // would eventually overflow -- signed overflow is UB, and in practice would
  // collapse the backoff to 1s and stop rotation. There is deliberately no cap:
  // rotation zeroes the counter and becomes allowed 60s after the last one, so
  // it cannot climb far. This test IS that guarantee. Break rotation and it
  // fails, which is the point -- a cap would have hidden the breakage.
  {
    State s;
    unsigned long now = 0;
    int peak = 0;
    for (int i = 0; i < 20000; ++i) {
      const Outcome o = decide(false, now, s);
      s = o.next;
      if (s.failures > peak) peak = s.failures;
      now = o.next.notBefore;
    }
    want(peak < 32, "failures reached " + std::to_string(peak) + "; rotation is not bounding them");
    want(s.failures >= 0, "failures went negative");
  }

  // -- rotation happens, and not more than once per minute ------------------
  {
    State s;
    unsigned long now = 0;
    int rotations = 0;
    while (now < 600000) {  // ten minutes
      const Outcome o = decide(false, now, s);
      if (o.rotate) rotations++;
      s = o.next;
      now = o.next.notBefore > now ? o.next.notBefore : now + 1;
    }
    want(rotations > 0, "rotation never fired in ten minutes of wrong guesses");
    want(rotations <= 11, "rotated " + std::to_string(rotations) + " times in ten minutes");
  }

  // -- the economics the DOCS quote, pinned here so they cannot drift -------
  //
  // docs/developer-mode.md says ~118 days to walk 10^6. That number is only
  // honest if something fails when the ladder changes. A previous version of
  // that bullet said 347 days, which was the pinned-ceiling figure -- but
  // rotation restarts the ladder, so the ceiling is never reached.
  {
    State s;
    unsigned long now = 0;
    long evaluated = 0;
    for (long i = 0; i < 3600L * 1000; ++i) {  // one hour at 1kHz
      const Outcome o = decide(false, now, s);
      if (o.verdict == Verdict::RefusedWrong) evaluated++;
      s = o.next;
      now += 1;
    }
    const double perMin = evaluated / 60.0;
    want(perMin > 4.0 && perMin < 8.0, "evaluated guess rate is " + std::to_string(perMin) +
                                           "/min; docs quote ~118 days for 10^6, which assumes about 5.9");
  }

  // -- only rotations are logged, so a flood cannot wipe the 16-line ring ---
  {
    State s;
    unsigned long now = 0;
    long logs = 0;
    for (long i = 0; i < 3600L * 1000; ++i) {
      const Outcome o = decide(false, now, s);
      if (o.log) logs++;
      s = o.next;
      now += 1;
    }
    want(logs <= 60, "a one-hour flood emitted " + std::to_string(logs) + " log lines into a 16-entry RTC ring");
  }

  // -- a deadline must never land on the no-window sentinel ------------------
  {
    State s;
    s.failures = 4;
    const unsigned long now = 0UL - 16000UL;  // 16s before the wrap
    const Outcome o = decide(false, now, s);
    want(o.next.notBefore != 0, "a backoff deadline landed on 0, which reads as no window at all");
  }

  // -- a clean device accepts the right code immediately --------------------
  {
    State s;
    wantVerdict(decide(true, 12345, s).verdict, Verdict::Accept, "clean device refused the right code");
    wantVerdict(decide(false, 12345, s).verdict, Verdict::RefusedWrong, "clean device accepted a wrong code");
  }

  // -- millis() wrap ---------------------------------------------------------
  {
    State s;
    s.notBefore = 10;             // just wrapped
    s.lastRotate = 0xFFFFFF00UL;  // before the wrap
    const unsigned long now = 20;
    const Outcome o = decide(false, now, s);
    wantVerdict(o.verdict, Verdict::RefusedWrong, "wrap made an open window look closed");
  }

  std::printf("%d checks, %d failed\n", checks, failed);
  return failed == 0 ? 0 : 1;
}

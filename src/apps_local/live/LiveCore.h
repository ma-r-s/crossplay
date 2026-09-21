#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Live's arithmetic, with nothing in it that needs a card, a radio or a panel.
//
// Everything here is a decision the device makes with the power off in front of
// it: when to wake, how long to wait after a failure, and whether the number a
// server handed it is one it is allowed to believe. All of it is testable on a
// laptop (host-tests/live), which is the only way the backoff can be proved --
// the failure it exists to prevent takes three weeks of real time to observe.
//
// The engine that uses these lives in LiveEngine; the card in LiveStore.

namespace live {

// ---------------------------------------------------------------------------
// The interval the service asks for.
//
// The server answers X-Next-Wake on every /api/pull, including 304 and 204, and
// the website is where it is changed. The device clamps it because a header is
// an input: a zero or a negative would busy-wake the radio flat in a night, and
// a number past a week is indistinguishable from Live being off while still
// costing a wake to discover.
constexpr uint32_t kMinIntervalSeconds = 15 * 60;        // the service's own floor
constexpr uint32_t kMaxIntervalSeconds = 7 * 24 * 3600;  // and its own ceiling
constexpr uint32_t kDefaultIntervalSeconds = 6 * 3600;   // until a server says otherwise

// Clamped, and an unparseable or absent header keeps what we already had --
// never the default, because a single bad reply would otherwise reset a
// deliberate weekly cadence to six hours and nothing would say so.
uint32_t clampInterval(int64_t seconds);

// ---------------------------------------------------------------------------
// What a failure costs, and why the retry is not a retry loop.
//
// A wake that cannot reach the service still pays for itself: the radio comes
// up, DevMode's join allows 20 seconds before it gives up, and the request on
// top of that. Call it 25 seconds at ~80mA, which is ~0.55mAh per failed wake.
//
// Retrying every 15 minutes is 96 failures a day, ~53mAh/day. An X4 Pro cell is
// ~1100mAh, so a device whose Wi-Fi password changed while its owner was away
// is flat in under three weeks having done nothing at all -- and the owner's
// first evidence is a device that will not turn on (see the
// devmode-flattens-the-battery memory; this is the same failure with a
// different cause).
//
// So the delay doubles from 15 minutes and stops at a day: 15m, 30m, 1h, 2h,
// 4h, 8h, 16h, then 24h forever. The whole of a first bad week costs about
// 6mAh, and a device that stays unreachable settles at one wake a day, which is
// under 0.6mAh/day -- less than the sleep floor itself.
//
// The cap is a day rather than "give up" on purpose: giving up means a device
// that never comes back when the router does, and the only way a user could
// tell is by finding this setting again.
constexpr uint32_t kFirstRetrySeconds = 15 * 60;
constexpr uint32_t kMaxRetrySeconds = 24 * 3600;

// `consecutiveFailures` counts the failures BEFORE this delay: 1 after the
// first, so the first retry is kFirstRetrySeconds. 0 or less is not a failure
// and returns 0, which no caller should be asking for -- it is the identity
// rather than a clamp, so a miscount shows up as an immediate retry in a test
// rather than as a silent 15-minute one in the field.
uint32_t retryDelaySeconds(int consecutiveFailures);

// ---------------------------------------------------------------------------
// Is the clock worth believing?
//
// The ESP32 keeps its system clock across deep sleep on the RTC, and Live sets
// that clock from X-Server-Time on every successful pull. Before the first one
// there is nothing to set it from, and a cold boot starts at the epoch.
//
// A schedule computed against a 1970 clock is not conservative, it is wrong in
// the expensive direction: every sleep looks overdue and every sleep spends the
// radio. So an implausible clock is treated as "no clock", and the engine falls
// back to fetching once and then trusting the timer.
constexpr int64_t kPlausibleEpochFloor = 1735689600;  // 2025-01-01
bool clockIsUsable(int64_t nowEpoch);

// ---------------------------------------------------------------------------
// The ETag, as it is stored.
//
// It arrives quoted (`"3f1c..."`) and is compared UNQUOTED, which is what the
// service does with the If-None-Match it receives. Storing the quotes makes
// every comparison fail and every wake download the same image again -- the
// exact cost the header exists to avoid, and invisible from the outside because
// the picture on the glass would still be right.
//
// Here rather than beside the transport so it can be walked on a laptop: the
// failure it prevents is a silent 48KB, not a wrong screen.
std::string unquoteEtag(const std::string& raw);

// ---------------------------------------------------------------------------
// Did the whole picture arrive?
//
// Live's images are BMPs the website makes, and their SIZE IS NOT FIXED: the
// X4 Pro's sleep screen is not one bit. The panel driver declares AbsolutePlanes
// grayscale and renderCustomSleepScreen takes the grayscale path, so the
// device's own format is 2bpp four-level (96070 bytes at 480x800) and the
// one-bit file (48062) is merely the other thing it reads. lib/GfxRenderer's
// Bitmap reader handles 1, 2, 4, 8, 24 and 32.
//
// So the download cannot be bounded by a magic number. A BMP declares its own
// total length in bytes 2..5, and comparing that with what actually arrived
// catches a truncated download at ANY colour depth -- including depths this
// firmware has not met yet. A constant would have to be revisited every time
// the website learns a new one, and the revision that gets forgotten ships a
// torn picture to a fridge.
bool bmpIsComplete(const uint8_t* header, size_t headerLen, size_t received);

// ---------------------------------------------------------------------------
// The one rule, in one function.
//
// "On every sleep, if a refresh is due, fetch it; otherwise arm the timer for
// when it will be." Every case Mario named is this rule with different inputs:
// a fridge nobody touches wakes on its own timer and fetches; a device in daily
// use is put down before its timer and arms the remainder; a device picked up
// and put back down does the same thing twice.
struct Schedule {
  bool on = false;      // the toggle; off means no timer at all
  bool paired = false;  // no token, nothing to ask
  // The last time we ASKED, successful or not, in the server's clock. 0 means
  // never, which is always due.
  int64_t lastAttemptEpoch = 0;
  int consecutiveFailures = 0;                         // 0 after any success
  uint32_t intervalSeconds = kDefaultIntervalSeconds;  // last X-Next-Wake, clamped
};

struct Decision {
  bool fetchNow = false;
  // Seconds to arm the RTC timer for. 0 means arm nothing, which is the state
  // every build shipped until now and therefore costs exactly what it costs
  // today -- that is the whole point of the toggle.
  uint32_t timerSeconds = 0;
};

// `timerFired` is true only on the wake our own RTC timer ended. It is not a
// special case bolted onto the rule, it IS the rule's other half: the timer was
// armed for the moment the next refresh comes due, so a wake it caused is due
// by construction and needs no clock to prove it.
//
// That distinction is load-bearing rather than tidy. Live sets its clock from
// X-Server-Time and nothing else, so a device whose battery went flat comes
// back with a token, no clock, and no way to measure elapsed time. Without this
// parameter the no-clock branch had to fetch on EVERY sleep to avoid missing
// the schedule -- which, on a device that also cannot reach the service, is a
// fetch every time its owner puts it down, with the backoff bypassed in exactly
// the situation the backoff exists for. The RTC timer keeps counting correctly
// with no wall clock at all, so it can carry the schedule on its own and the
// user's own sleeps can decline.
Decision decide(const Schedule& schedule, int64_t nowEpoch, bool timerFired = false);

}  // namespace live

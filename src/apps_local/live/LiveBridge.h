#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Live's half of the conversation with fridge.ma-r-s.com.
//
// Three calls, and the reader makes no others: two to be introduced to a phone
// once, and one it repeats for the rest of its life from inside a sleep.
//
// Transport is bridge::request / bridge::streamToFile (verified TLS on the
// device, curl in the simulator) and explicitly NOT HttpDownloader, which calls
// setInsecure() on every device build -- a sleeping fridge accepting whatever
// answers is a picture anyone on the path can choose.

namespace live {

// What /api/pull answered, headers included. Every field is filled on all three
// statuses, because X-Next-Wake and X-Server-Time arrive on 200, 304 and 204
// alike: a wake that finds nothing still learns when to come back.
struct PullResult {
  int status = 0;                // 200, 304, 204, 401, or 0 for "could not reach"
  std::string etag;              // quotes stripped; empty on anything but a 200
  uint32_t nextWakeSeconds = 0;  // clamped by LiveCore, 0 when the header was absent
  // X-Cadence: how often it repeats, which a clock-time schedule makes a
  // different number from the sleep above. 0 when the header was absent, which
  // the caller resolves through Schedule::cadence rather than guessing here.
  uint32_t cadenceSeconds = 0;
  int64_t serverEpoch = 0;  // 0 when absent
  size_t bytes = 0;         // what a 200 actually wrote, for the completeness check
  std::string message;      // a sentence for a screen, when there is one to show
};

struct PairStart {
  std::string code;  // six digits
  std::string pollToken;
  int expiresIn = 0;  // seconds
};

// POST /api/pair/start. No token: this is the call that mints one.
bool pairStart(PairStart& out, std::string& message);

// POST /api/pair/join. The SAME shape of answer as pairStart and a completely
// different meaning: this code adds a phone to THIS fridge, where pairStart's
// makes a new one.
//
// Wiring "add somebody" to pairStart would have handed the browser a different
// fridge, orphaning the phone already sending and the picture already on the
// glass, with nothing anywhere saying so. That is why the two are separate
// endpoints on the service and separate calls here rather than one call with a
// flag: a flag defaulted the wrong way is the same bug back.
//
// The refusal at four phones arrives as a 409 carrying the service's OWN
// sentence, and this returns false with that sentence verbatim in `message`.
// The device does not get to reword a decision the service made, and it does
// not get to guess the cap either -- the number in the sentence is the
// service's (see kMaxSenders below).
bool pairJoin(const std::string& deviceToken, PairStart& out, std::string& message);

// One phone that may send to this reader, as the reader's own screen needs it.
struct Sender {
  std::string name;      // what the browser called itself, "A phone" when it did not
  std::string id;        // the first 16 chars of the token hash. NEVER the token
  int64_t pairedAt = 0;  // epoch seconds; 0 when the service had no stamp
};

// GET /api/senders. Four is the cap and THE SERVICE IS THE ONE THAT DECIDES: it
// refuses the fifth before a code exists, and `max` comes back from it on every
// call. kMaxSenders here is only the size of the array this reader can hold,
// kept equal to the service's so a list that arrives full fits, and the two are
// compared out loud in listSenders so a service that raised its own cap shows
// up in the log rather than as senders silently missing from the one screen
// that can revoke them.
constexpr int kMaxSenders = 4;

struct SenderList {
  Sender items[kMaxSenders];
  int count = 0;
  // What the SERVICE says its cap is. Reported rather than assumed, because the
  // number the screen would use in a sentence is not ours to pick.
  int max = kMaxSenders;
};

bool listSenders(const std::string& deviceToken, SenderList& out, std::string& message);

// POST /api/senders/revoke with {"id": "<Sender::id>"}.
//
// Destructive, remote, and silent to the person it happens to: they are in
// another country and the service tells them nothing. So the confirmation is
// the DEVICE's job and this call does no asking -- by the time it runs, somebody
// standing in front of the reader has already said the name out loud.
//
// `remaining` is the service's count afterwards, which is what makes "and then
// refresh the list" checkable rather than hopeful. A 404 means the id is not on
// this reader, which is what a stale list looks like, and it comes back as
// false with the service's sentence.
bool revokeSender(const std::string& deviceToken, const std::string& id, int& remaining, std::string& message);

// GET /api/pair/poll. Returns 1 paired (and fills `deviceToken`), 0 still
// waiting, -1 failed (and fills `message`).
//
// Zero is NOT an error and never sets a message: a poll that is still waiting
// is the normal answer for as long as somebody is walking to a phone.
int pairPoll(const std::string& pollToken, std::string& deviceToken, std::string& fridgeId, std::string& message);

// POST /api/off: Live was switched off on this reader.
//
// FIRE AND FORGET, and the caller is expected to treat it that way. The whole
// value is that the website can say "off on the reader" instead of guessing
// from silence -- and silence already means a flat battery, a router that
// moved, or a reader carried to another house, none of which this service can
// tell apart. If the call does not get out, the fridge simply goes quiet and
// the page falls back to the deadline passing, which is the same answer it
// gives a device whose battery died. Nothing is at risk either way: the toggle
// is already written to the card before this is attempted.
bool reportOff(const std::string& deviceToken, std::string& message);

// GET /api/pull with the bearer token and, when we have one, If-None-Match.
//
// `liveOn` goes out as X-Live-On, on this request rather than in a call of its
// own because a wake is one round trip by design. It is the one fact about the
// schedule only the device has: the service can work out WHEN the next check
// is (it is the interval in its own reply), but not whether somebody has
// switched Live off since the last one.
//
// The reader deliberately does NOT report its own alarm. It cannot: the
// headers are composed before the reply is read, and a pull the reader gets an
// answer to clears its failures and makes it adopt the interval in that reply,
// so any figure sent here is the alarm for the state it was in BEFORE the
// request. Sending one made the website count down to moments the reader was
// never going to wake at.
//
// On 200 the body is written to `destPath`, bounded by kMaxImageBytes and then
// judged by its own BMP header rather than by a magic length: a short write is
// how a half-arrived image reaches the glass, and the image has more than one
// valid size. On 304 and 204 NOTHING is written and nothing is repainted: that
// is the wake this whole design is built around, and a card write on it would
// be the cost the 304 exists to avoid.
bool pull(const std::string& deviceToken, const std::string& knownEtag, bool liveOn, const char* destPath,
          PullResult& out);

// The CEILING on what a pull may write to the card, not an expected size.
//
// The sleep screen is not one bit. The X4 Pro's panel driver declares
// AbsolutePlanes grayscale and renderCustomSleepScreen takes the grayscale
// path, so the device's own format is 2bpp four-level: 480x800 at two bits is
// 96000 bytes of pixels over a 70-byte header and palette. The one-bit file
// (48062) is the other thing the same reader handles, and lib/GfxRenderer's
// Bitmap takes 1, 2, 4, 8, 24 and 32.
//
// So this bounds the DOWNLOAD -- a runaway response must not fill the card --
// and live::bmpIsComplete decides whether what arrived is a whole picture.
// Bounding by an exact size instead would refuse the format the panel actually
// wants the moment the website started sending it.
constexpr size_t kMaxImageBytes = 96070;

}  // namespace live

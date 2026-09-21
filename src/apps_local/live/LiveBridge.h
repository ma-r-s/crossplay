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
  int64_t serverEpoch = 0;       // 0 when absent
  size_t bytes = 0;              // what a 200 actually wrote, for the completeness check
  std::string message;           // a sentence for a screen, when there is one to show
};

struct PairStart {
  std::string code;  // six digits
  std::string pollToken;
  int expiresIn = 0;  // seconds
};

// POST /api/pair/start. No token: this is the call that mints one.
bool pairStart(PairStart& out, std::string& message);

// GET /api/pair/poll. Returns 1 paired (and fills `deviceToken`), 0 still
// waiting, -1 failed (and fills `message`).
//
// Zero is NOT an error and never sets a message: a poll that is still waiting
// is the normal answer for as long as somebody is walking to a phone.
int pairPoll(const std::string& pollToken, std::string& deviceToken, std::string& fridgeId, std::string& message);

// GET /api/pull with the bearer token and, when we have one, If-None-Match.
//
// On 200 the body is written to `destPath`, bounded by kMaxImageBytes and then
// judged by its own BMP header rather than by a magic length: a short write is
// how a half-arrived image reaches the glass, and the image has more than one
// valid size. On 304 and 204 NOTHING is written and nothing is repainted: that
// is the wake this whole design is built around, and a card write on it would
// be the cost the 304 exists to avoid.
bool pull(const std::string& deviceToken, const std::string& knownEtag, const char* destPath, PullResult& out);

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

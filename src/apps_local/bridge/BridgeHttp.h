#pragma once

// Talking to one of Mario's bridge services: verified TLS on the device,
// curl in the simulator, and a sentence for every failure.
//
// ---------------------------------------------------------------------------
// Why this file exists, and what is still duplicated.
//
// The Study app got here first and grew this transport inside StudySync.cpp:
// the root bundle loader, the SD override, the heap floor TLS needs, the
// dev-build diagnosis probe, and the two shapes of request. All of it is
// service-agnostic, and a second bridge would have meant a second copy of a
// certificate bundle and a second copy of a heap threshold -- the kind of
// twin that gets fixed on one path and not the other (see the fix-the-twin-too
// memory; every instance of that pattern in this repo started here).
//
// So the Instapaper app uses this and Study does not, YET. Study's copy is
// deliberately untouched because app/studyradio is a long-lived branch sitting
// on top of StudySync.cpp, and refactoring under it would turn a merge into
// an archaeology session. Moving Study onto this file is listed in
// docs/open-items.md and should happen the week that branch lands.
//
// The one thing that is NOT duplicated is the certificate bundle: this
// includes Study's, because both services sit behind the same operator's
// Cloudflare tunnel and a CA rotation must be one edit rather than two.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <string>

namespace bridge {

// Everything that differs between one bridge and another.
struct Endpoint {
  const char* host;  // "read.ma-r-s.com"
  const char* tag;   // log tag
  // Simulator only: an env var that overrides the whole base URL, so a test
  // runs against a local bridge with a throwaway account. A real account must
  // never receive a simulator's writes.
  const char* urlEnv;
  // An SD-card PEM bundle that wins over the baked roots, so a CA change is a
  // file copy rather than a reflash.
  const char* rootsOverridePath;
};

std::string base(const Endpoint& endpoint);

// Extra request headers, and the response headers a caller needs back.
//
// Live is the reason this exists. Its whole design rests on two headers the
// buffered transport below could neither send nor read: If-None-Match on the
// way out, so a wake that finds nothing costs a few hundred bytes instead of
// 48KB, and X-Next-Wake on the way back, so a sleeping device learns when to
// come up again without a clock of its own to reason against.
//
// Fixed arrays rather than vectors, because this is built on the path INTO deep
// sleep, where the heap is already tight enough that this file checks a floor
// before it opens a socket. Four is more than any caller here needs, and a
// fifth is dropped rather than grown -- silently on purpose, because the
// alternative is an allocation on the one path that must not fail.
struct Headers {
  static constexpr int kMax = 4;
  struct Pair {
    std::string name;
    std::string value;
  };

  // Sent with the request.
  void add(const char* name, const std::string& value);
  // Named before the call, filled in by it. A header that was not asked for is
  // not collected: the device transport can read any of them back and the
  // simulator's curl needs to be told which to dump, and a collection that
  // quietly differs between the two is how a feature passes on a laptop and
  // fails on the glass.
  void collect(const char* name);
  // "" when the header was absent, which every caller has to handle: an absent
  // X-Next-Wake means "keep the cadence you had", never "zero".
  std::string value(const char* name) const;

  Pair send[kMax];
  int sendCount = 0;
  const char* wanted[kMax] = {};
  int wantedCount = 0;
  Pair got[kMax];
  int gotCount = 0;
};

// One request, buffered response. Returns the HTTP status, or 0 on a
// transport failure (in which case `message` is a sentence for the screen).
int request(const Endpoint& endpoint, const char* method, const std::string& path, const std::string& token,
            const uint8_t* body, size_t bodyLen, std::string& response, std::string& message,
            Headers* headers = nullptr);

// A GET that may legitimately answer "nothing changed".
//
// streamToFile treats anything but a 200 as a failure, which is right for a
// download somebody asked for and wrong for Live: 304 and 204 ARE the answer,
// and both have to reach the caller with their headers intact. So this returns
// the status instead of a bool.
//
// `destPart` is opened LAZILY, on the first byte of body. That is not a
// micro-optimisation: 304 and 204 carry no body, so on those statuses the card
// is never opened, never truncated and never written -- which is the entire
// point of the conditional request. Opening it up front would put an SD write
// on exactly the wake that exists to avoid one.
//
// `maxBytes` is a CEILING, not an expected size, and it is the difference
// between this and streamToFile. Live's images have more than one valid length
// -- the sleep screen is not one bit -- so the body is bounded here, to stop a
// runaway response filling the card, and judged by its own content afterwards.
// `received` reports what arrived so the caller can do that judging.
int getToFile(const Endpoint& endpoint, const std::string& path, const std::string& token, const std::string& destPart,
              size_t maxBytes, std::string& message, Headers* headers, size_t* received = nullptr);

// Stream a GET into `destPart`. No rename: the caller decides when a set of
// files becomes visible together, because per-file atomicity is not the same
// as a consistent set.
//
// `incompleteMessage` is what the user is told when the bytes do not all
// arrive, because only the caller knows what was being fetched.
bool streamToFile(const Endpoint& endpoint, const std::string& path, const std::string& token,
                  const std::string& destPart, size_t expectedSize, const char* incompleteMessage, bool* cancel,
                  std::string& message);

// The services' polite refusals arrive as {"error": "sentence"}. Surfacing
// them verbatim is a rule, not a shortcut: the device must never invent its
// own wording for a decision a server made.
bool takeServerError(const std::string& response, std::string& message);

}  // namespace bridge

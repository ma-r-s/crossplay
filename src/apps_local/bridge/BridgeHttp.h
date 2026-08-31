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

// One request, buffered response. Returns the HTTP status, or 0 on a
// transport failure (in which case `message` is a sentence for the screen).
int request(const Endpoint& endpoint, const char* method, const std::string& path, const std::string& token,
            const uint8_t* body, size_t bodyLen, std::string& response, std::string& message);

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

#pragma once

// One GET for a feed or a page, streamed to a callback, that can be stopped
// while it is still connecting.
//
// HttpDownloader::fetchUrl streams too, but it asks nothing of its caller until
// body bytes arrive, and it waits sixty seconds a socket operation with three
// stalled attempts behind that. A refresh reads every feed in one pass with a
// STOP button on the panel, so a host that never answers would hold that button
// dead for minutes. This is the same SecureHttpClient request -- resumed the
// same way, with the same agent string -- with a poll that runs while it waits
// and a shorter timeout. Kept here rather than added to HttpDownloader, which is
// CrossPoint's file (LOCAL_SCOPE.md).

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace rss {

struct FetchOutcome {
  int status = 0;         // last HTTP status, 0 when no response arrived
  size_t bytes = 0;       // body bytes handed to onData
  bool complete = false;  // the whole body arrived
  bool stopped = false;   // onData returned false
  bool aborted = false;   // poll returned true
};

// `poll` runs about every 50ms while the request waits or reads; returning true
// abandons it. Refuses at once, with status 0, when the radio is not up.
FetchOutcome fetchStreaming(const std::string& url, const std::function<bool(const uint8_t*, size_t)>& onData,
                            const std::function<bool()>& poll);

}  // namespace rss

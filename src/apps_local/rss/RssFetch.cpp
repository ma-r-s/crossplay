#include "RssFetch.h"

#include <Arduino.h>
#include <Logging.h>
#include <ResumableFetch.h>
#include <WiFi.h>

#include "../../network/WifiPowerSaveGuard.h"

namespace rss {
namespace {

// Long enough for a slow blog on a weak signal, short enough that a dead host
// costs a refresh seconds rather than minutes.
constexpr uint32_t kTimeoutMs = 20000;
constexpr unsigned long kPollMs = 50;

}  // namespace

FetchOutcome fetchStreaming(const std::string& url, const std::function<bool(const uint8_t*, size_t)>& onData,
                            const std::function<bool()>& poll) {
  FetchOutcome outcome;
  // Entering the TLS stack with no radio panics the device rather than failing
  // (HttpDownloader.cpp says how Trivia found that out).
  if (WiFi.status() != WL_CONNECTED) {
    LOG_ERR("RSS", "no Wi-Fi; not fetching %s", url.c_str());
    return outcome;
  }
  WifiPowerSaveGuard psGuard;
  freeink::FetchSink sink;
  sink.write = onData;
  unsigned long lastPoll = 0;
  bool aborted = false;
  const auto shouldAbort = [&poll, &lastPoll, &aborted] {
    const unsigned long now = millis();
    if (!aborted && poll && now - lastPoll >= kPollMs) {
      lastPoll = now;
      aborted = poll();
    }
    return aborted;
  };
  const freeink::FetchResult result = freeink::fetchResumable(
      url, freeink::FetchOptions{},
      [](freeink::SecureHttpClient& http, bool) {
        http.setTimeout(kTimeoutMs);
        // As HttpDownloader does, for every fetch in this firmware.
        http.setInsecure();
        http.setUserAgent("CrossPlay-ESP32-" CROSSPOINT_VERSION);
      },
      sink, shouldAbort);
  outcome.status = result.status < 0 ? 0 : result.status;
  outcome.bytes = result.bytes;
  outcome.complete = result.complete && result.status >= 200 && result.status < 300;
  outcome.stopped = result.stopped;
  outcome.aborted = result.aborted || aborted;
  return outcome;
}

}  // namespace rss

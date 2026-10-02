#include "HttpDownloader.h"

#include <Arduino.h>
#include <Logging.h>
#include <ResumableFetch.h>
#include <WiFi.h>

#include <functional>
#include <string>

#include "DeviceReport.h"
#include "WifiPowerSaveGuard.h"

extern "C" void wolfSSL_Arduino_Serial_Print(const char* const msg) { LOG_DBG("WOLFSSL", "%s", msg); }

namespace {
// Per-socket-op timeout. Some OPDS download endpoints are slow to send headers
// (>15s) and chunked catalogs stall mid-body, so 15s killed them. 60s gives
// slow servers room.
constexpr int HTTP_TIMEOUT_MS = 60000;

// Written after every request, and read by the UI right after a failure.
int g_lastStatus = 0;
// Whether the last answer came from another origin than the request started at.
bool g_lastRedirected = false;

// How often the abort poll is allowed to pump input. SecureHttpClient calls the
// abort callback in a tight loop, so pumping on every call would spend the wait
// redrawing instead of waiting.
constexpr unsigned long ABORT_PUMP_INTERVAL_MS = 50;

// All HTTP(S) fetches go through wolfSSL (the firmware's only TLS stack: it
// speaks TLS 1.3 and reads large bodies reliably). Plain-http URLs still use a
// WiFiClient here, so this is safe for non-TLS targets too. A body cut short
// mid-transfer resumes with a Range request (see ResumableFetch.h).
HttpDownloader::DownloadError runGetSecure(const std::string& url, const std::string& username,
                                           const std::string& password,
                                           const std::vector<HttpDownloader::Header>& headers,
                                           const freeink::FetchSink& sink, const bool* cancelFlag = nullptr,
                                           size_t* bytesOut = nullptr, const bool downgradeRedirectsToHttp = false) {
  // Cleared per request: a transport failure returns before it is set, and a
  // stale 200 from the previous fetch would read as success.
  g_lastStatus = 0;
  g_lastRedirected = false;
  // No radio, no request. Entering the TLS stack with WiFi never started does
  // not fail, it PANICS: the socket layer takes a mutex that has not been
  // created and FreeRTOS asserts on the null handle (xQueueSemaphoreTake,
  // queue.c:1709). Trivia's pack download shipped that way and crashed the
  // device on the button. A caller that forgets the radio deserves an error it
  // can show, not a reboot -- and this is the one place every fetch passes
  // through, so the guard cannot be forgotten by the next app either.
  // Also correct while a match owns the radio: ESP-NOW leaves status
  // disconnected, so a fetch mid-game is refused rather than fighting for it.
  if (WiFi.status() != WL_CONNECTED) {
    LOG_ERR("HTTP", "no WiFi connection; refusing to fetch %s", url.c_str());
    return HttpDownloader::HTTP_ERROR;
  }
  WifiPowerSaveGuard psGuard;
  freeink::FetchOptions options;
  options.redirectToHttp = downgradeRedirectsToHttp;

  // Cancel used to be dead until the first body byte. SecureHttpClient polls
  // the abort callback while it waits for the status line and headers, and the
  // caller pumps input from its progress callback, so running progress from
  // the poll makes the button live during connect and during any server-side
  // work before the response starts (a catalog that converts a book on demand
  // on demand, which is seconds).
  size_t heldBytes = 0;
  size_t heldTotal = 0;
  unsigned long lastPumpMs = 0;
  freeink::FetchSink tracked = sink;
  if (sink.progress) {
    tracked.progress = [&sink, &heldBytes, &heldTotal](const size_t bytes, const size_t total) {
      heldBytes = bytes;
      heldTotal = total;
      sink.progress(bytes, total);
    };
  }
  const auto shouldAbort = [&sink, &heldBytes, &heldTotal, &lastPumpMs, cancelFlag] {
    const unsigned long now = millis();
    if (sink.progress && now - lastPumpMs >= ABORT_PUMP_INTERVAL_MS) {
      lastPumpMs = now;
      sink.progress(heldBytes, heldTotal);
    }
    return cancelFlag && *cancelFlag;
  };

  // Device report headers ride only on the starting origin: a redirect off one
  // of our hosts carries nothing onward, and the report counts as delivered
  // only when the answer came from that origin.
  bool answeredBySameOrigin = true;
  const freeink::FetchResult result = freeink::fetchResumable(
      url, options,
      [&](freeink::SecureHttpClient& http, const bool sameOrigin) {
        answeredBySameOrigin = sameOrigin;
        http.setTimeout(HTTP_TIMEOUT_MS);
        http.setInsecure();
        // setUserAgent replaces SecureHttpClient's built-in UA; addHeader would
        // append a second User-Agent header, which strict servers reject (aiohttp
        // answers 400 "Duplicate 'User-Agent' header found").
        http.setUserAgent("CrossPlay-ESP32-" CROSSPOINT_VERSION);
        // Credentials and caller headers stay with the starting origin; a
        // redirect elsewhere (or to plain http) gets neither.
        if (sameOrigin) {
          if (!username.empty() && !password.empty()) http.setBasicAuth(username, password);
          for (const auto& h : headers) http.addHeader(h.first, h.second);
          devreport::Header report[devreport::kHeaderCount];
          const int reportHeaders = devreport::headersFor(url.c_str(), report);
          for (int i = 0; i < reportHeaders; ++i) http.addHeader(report[i].name, report[i].value);
        }
        LOG_DBG("HTTP", "wolfSSL GET: %s (heap %u, max block %u)", url.c_str(), (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());
      },
      tracked, shouldAbort);
  if (bytesOut) *bytesOut = result.bytes;
  g_lastStatus = result.status < 0 ? 0 : result.status;
  g_lastRedirected = !answeredBySameOrigin;
  if (answeredBySameOrigin) devreport::delivered(url.c_str(), result.status);

  if (result.aborted) return HttpDownloader::ABORTED;
  if (result.stopped) return HttpDownloader::FILE_ERROR;
  if (result.status == 401 || result.status == 403) {
    LOG_ERR("HTTP", "wolfSSL request unauthorized: status %d: %s", result.status, url.c_str());
    return HttpDownloader::UNAUTHORIZED;
  }
  if (result.status < 200 || result.status >= 300) {
    LOG_ERR("HTTP", "wolfSSL request failed: status %d: %s", result.status, url.c_str());
    return HttpDownloader::HTTP_ERROR;
  }
  if (!result.complete) {
    LOG_ERR("HTTP", "wolfSSL incomplete: got %zu of %zu bytes", result.bytes, result.total);
    return HttpDownloader::HTTP_ERROR;
  }
  return HttpDownloader::OK;
}

}  // namespace

int HttpDownloader::lastStatus() { return g_lastStatus; }

bool HttpDownloader::lastAnswerRedirected() { return g_lastRedirected; }

bool HttpDownloader::fetchUrl(const std::string& url, std::string& outContent, const std::string& username,
                              const std::string& password) {
  outContent.clear();  // start clean; the sink appends, so don't carry prior content
  return fetchUrl(
      url,
      [&outContent](const uint8_t* data, size_t len) {
        outContent.append(reinterpret_cast<const char*>(data), len);
        return true;
      },
      username, password);
}

bool HttpDownloader::fetchUrl(const std::string& url, Stream& outContent, const std::string& username,
                              const std::string& password) {
  return fetchUrl(
      url, [&outContent](const uint8_t* data, size_t len) { return outContent.write(data, len) == len; }, username,
      password);
}

bool HttpDownloader::fetchUrl(const std::string& url, const DataCallback& onData, const std::string& username,
                              const std::string& password) {
  LOG_DBG("HTTP", "Fetching: %s", url.c_str());
  freeink::FetchSink sink;
  sink.write = onData;
  return runGetSecure(url, username, password, {}, sink) == OK;
}

HttpDownloader::DownloadError HttpDownloader::downloadToFile(const std::string& url, const std::string& destPath,
                                                             ProgressCallback progress, const bool* cancelFlag,
                                                             const std::string& username, const std::string& password,
                                                             const std::vector<Header>& headers,
                                                             bool downgradeRedirectsToHttp) {
  LOG_DBG("HTTP", "Downloading: %s -> %s", url.c_str(), destPath.c_str());

  // Stage in <dest>.part: a failed or cancelled download never replaces an
  // existing copy, and a partial file never sits under the real name.
  const std::string partPath = destPath + ".part";
  Storage.remove(partPath.c_str());
  HalFile file;
  if (!Storage.openFileForWrite("HTTP", partPath.c_str(), file)) {
    LOG_ERR("HTTP", "Failed to open file for writing");
    return FILE_ERROR;
  }

  freeink::FetchSink sink;
  sink.write = [&file](const uint8_t* data, size_t len) { return file.write(data, len) == len; };
  // Reopening for write truncates: the server restarted the body from byte 0.
  sink.rewind = [&file, &partPath] {
    file.close();
    return Storage.openFileForWrite("HTTP", partPath.c_str(), file);
  };
  sink.progress = progress;

  size_t downloaded = 0;
  const DownloadError result =
      runGetSecure(url, username, password, headers, sink, cancelFlag, &downloaded, downgradeRedirectsToHttp);
  // Close before any remove() on the same path; DESTRUCTOR_CLOSES_FILE would
  // otherwise close only after the remove. A failed rewind leaves no open handle.
  if (file.isOpen()) file.close();

  if (result != OK) {
    Storage.remove(partPath.c_str());
    return result;
  }
  if (downloaded == 0) {
    LOG_ERR("HTTP", "no data received");
    Storage.remove(partPath.c_str());
    return HTTP_ERROR;
  }
  if (!Storage.replaceFile(partPath.c_str(), destPath.c_str())) {
    LOG_ERR("HTTP", "Failed to move download into place: %s", destPath.c_str());
    Storage.remove(partPath.c_str());
    return FILE_ERROR;
  }
  LOG_DBG("HTTP", "Downloaded %zu bytes", downloaded);
  return OK;
}

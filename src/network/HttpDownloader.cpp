#include "HttpDownloader.h"

#include <Arduino.h>
#include <Logging.h>
#include <ResumableFetch.h>
#include <WiFi.h>

#include <algorithm>
#include <functional>
#include <string>
#include <utility>

#include "DeviceReport.h"
#include "WifiPowerSaveGuard.h"

extern "C" void wolfSSL_Arduino_Serial_Print(const char* const msg) { LOG_DBG("WOLFSSL", "%s", msg); }

namespace {
// Per-socket-op timeout. Some OPDS download endpoints are slow to send headers
// (>15s) and chunked catalogs stall mid-body, so 15s killed them. 60s gives
// slow servers room.
constexpr int HTTP_TIMEOUT_MS = 60000;

constexpr int MAX_REDIRECTS = 5;

// postForm response cap: token responses are small JSON; a misbehaving server
// must not balloon the heap.
constexpr size_t MAX_RESPONSE_BYTES = 4096;

// Written after every request, and read by the UI right after a failure.
int g_lastStatus = 0;
// Whether the last answer came from another origin than the request started at.
bool g_lastRedirected = false;

// ---- the one door every request in this file goes through -----------------
//
// fork-local seam. runGetSecure, fetchUrl(FetchOptions), postForm and
// postToFile each build a request of their own, and what CrossPlay adds to a
// request (the radio check, the device report, a cancel that works before the
// first byte) used to live only in runGetSecure, so upstream's later paths
// walked straight past it. It lives in these helpers instead, and
// host-tests/httpdoor fails any function here that opens a client or calls
// fetchResumable without passing openRequest() first.

// Starts a request; false, logged, when none may be made.
//
// No radio, no request. Entering the TLS stack with WiFi never started does
// not fail, it PANICS: the socket layer takes a mutex that has not been
// created and FreeRTOS asserts on the null handle (xQueueSemaphoreTake,
// queue.c:1709). Trivia's pack download shipped that way and crashed the
// device on the button. A caller that forgets the radio deserves an error it
// can show, not a reboot. Also correct while a match owns the radio: ESP-NOW
// leaves status disconnected, so a fetch mid-game is refused rather than
// fighting for it.
bool openRequest(const char* method, const std::string& url) {
  // Cleared per request: a transport failure returns before it is set, and a
  // stale 200 from the previous fetch would read as success.
  g_lastStatus = 0;
  g_lastRedirected = false;
  if (WiFi.status() == WL_CONNECTED) return true;
  LOG_ERR("HTTP", "no WiFi connection; refusing to %s %s", method, url.c_str());
  return false;
}

// Every hop of every request. Device report headers ride only on the starting
// origin (`url`): a redirect off one of our hosts carries nothing onward.
void applyCommonClientSetup(freeink::SecureHttpClient& http, const std::string& url, const bool sameOrigin) {
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setInsecure();
  // setUserAgent replaces SecureHttpClient's built-in UA; addHeader would
  // append a second User-Agent header, which strict servers reject (aiohttp
  // answers 400 "Duplicate 'User-Agent' header found").
  http.setUserAgent("CrossPlay-ESP32-" CROSSPOINT_VERSION);
  if (!sameOrigin) return;
  devreport::Header report[devreport::kHeaderCount];
  const int reportHeaders = devreport::headersFor(url.c_str(), report);
  for (int i = 0; i < reportHeaders; ++i) http.addHeader(report[i].name, report[i].value);
}

// Ends a request: the status lastStatus() reads (-1 or 0 for no answer), and
// the device report counts as delivered only when the answer came from the
// origin the request started at.
void closeRequest(const std::string& url, const int status, const bool sameOrigin) {
  g_lastStatus = status < 0 ? 0 : status;
  g_lastRedirected = !sameOrigin;
  if (sameOrigin) devreport::delivered(url.c_str(), status);
}

// How often the abort poll is allowed to pump input. SecureHttpClient calls the
// abort callback in a tight loop, so pumping on every call would spend the wait
// redrawing instead of waiting.
constexpr unsigned long ABORT_PUMP_INTERVAL_MS = 50;

// The abort callback for any request a caller can cancel. Cancel used to be
// dead until the first body byte. SecureHttpClient polls the abort callback
// while it waits for the status line and headers, and the caller pumps input
// from its progress callback, so running progress from the poll makes the
// button live during connect and during any server-side work before the
// response starts (a catalog that converts a book on demand, which is
// seconds). Report body progress through progressed() so the poll repeats
// the latest numbers rather than zero.
class AbortPoll {
 public:
  AbortPoll(HttpDownloader::ProgressCallback progress, const bool* cancelFlag)
      : progress_(std::move(progress)), cancelFlag_(cancelFlag) {}
  void progressed(const size_t bytes, const size_t total) {
    heldBytes_ = bytes;
    heldTotal_ = total;
    if (progress_) progress_(bytes, total);
  }
  bool operator()() {
    const unsigned long now = millis();
    if (progress_ && now - lastPumpMs_ >= ABORT_PUMP_INTERVAL_MS) {
      lastPumpMs_ = now;
      progress_(heldBytes_, heldTotal_);
    }
    return cancelFlag_ && *cancelFlag_;
  }
  bool reportsProgress() const { return static_cast<bool>(progress_); }

 private:
  HttpDownloader::ProgressCallback progress_;
  const bool* cancelFlag_;
  size_t heldBytes_ = 0;
  size_t heldTotal_ = 0;
  unsigned long lastPumpMs_ = 0;
};

// All HTTP(S) fetches go through wolfSSL (the firmware's only TLS stack: it
// speaks TLS 1.3 and reads large bodies reliably). Plain-http URLs still use a
// WiFiClient here, so this is safe for non-TLS targets too. A body cut short
// mid-transfer resumes with a Range request (see ResumableFetch.h).
HttpDownloader::DownloadError runGetSecure(const std::string& url, const std::string& username,
                                           const std::string& password,
                                           const std::vector<HttpDownloader::Header>& headers,
                                           const freeink::FetchSink& sink, const bool* cancelFlag = nullptr,
                                           size_t* bytesOut = nullptr, const bool downgradeRedirectsToHttp = false) {
  if (!openRequest("fetch", url)) return HttpDownloader::HTTP_ERROR;
  WifiPowerSaveGuard psGuard;
  freeink::FetchOptions options;
  options.redirectToHttp = downgradeRedirectsToHttp;

  AbortPoll abortPoll(sink.progress, cancelFlag);
  freeink::FetchSink tracked = sink;
  if (abortPoll.reportsProgress()) {
    tracked.progress = [&abortPoll](const size_t bytes, const size_t total) { abortPoll.progressed(bytes, total); };
  }

  bool answeredBySameOrigin = true;
  const freeink::FetchResult result = freeink::fetchResumable(
      url, options,
      [&](freeink::SecureHttpClient& http, const bool sameOrigin) {
        answeredBySameOrigin = sameOrigin;
        applyCommonClientSetup(http, url, sameOrigin);
        // Credentials and caller headers stay with the starting origin; a
        // redirect elsewhere (or to plain http) gets neither.
        if (sameOrigin) {
          if (!username.empty() && !password.empty()) http.setBasicAuth(username, password);
          for (const auto& h : headers) http.addHeader(h.first, h.second);
        }
        LOG_DBG("HTTP", "wolfSSL GET: %s (heap %u, max block %u)", url.c_str(), (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());
      },
      tracked, [&abortPoll] { return abortPoll(); });
  if (bytesOut) *bytesOut = result.bytes;
  // FetchResult::status is -1 when no response arrived (bad URL, DNS, connect, TLS).
  closeRequest(url, result.status, answeredBySameOrigin);

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
bool HttpDownloader::lastAnswerRedirected() { return g_lastRedirected; }

bool HttpDownloader::fetchUrl(const std::string& url, Stream& outContent, const std::string& username,
                              const std::string& password, const std::vector<Header>& headers) {
  return fetchUrl(
      url, [&outContent](const uint8_t* data, size_t len) { return outContent.write(data, len) == len; }, username,
      password, headers);
}

bool HttpDownloader::fetchUrl(const std::string& url, const DataCallback& onData, const std::string& username,
                              const std::string& password, const std::vector<Header>& headers) {
  LOG_DBG("HTTP", "Fetching: %s", url.c_str());
  freeink::FetchSink sink;
  sink.write = onData;
  return runGetSecure(url, username, password, headers, sink) == OK;
}

bool HttpDownloader::fetchUrl(const std::string& url, const DataCallback& onData, const FetchOptions& options) {
  // OPDS fetches need what fetchResumable withholds: the final HTTP status
  // and a 401 body (auth documents are served as 401 bodies). Direct GET with
  // a manual redirect loop; catalog feeds are small enough that Range resume
  // isn't worth carrying here (books go through downloadToFile, which has it).
  if (options.wwwAuthenticateOut) options.wwwAuthenticateOut->clear();
  if (!openRequest("fetch", url)) {
    if (options.statusOut) *options.statusOut = 0;
    return false;
  }
  WifiPowerSaveGuard psGuard;
  std::string current = url;
  const std::string startOrigin = freeink::fetchOrigin(url);
  int status = 0;
  bool sinkStopped = false;
  for (int hop = 0; hop <= MAX_REDIRECTS; ++hop) {
    freeink::SecureHttpClient http;
    if (!http.begin(current)) {
      LOG_ERR("HTTP", "bad URL: %s", current.c_str());
      status = 0;
      break;
    }
    // Credentials belong to the starting origin only: a redirect to another
    // origin (a CDN) must not receive them.
    const bool sameOrigin = freeink::fetchOrigin(current) == startOrigin;
    applyCommonClientSetup(http, url, sameOrigin);
    if (sameOrigin) {
      if (!options.bearer.empty()) {
        http.addHeader("Authorization", "Bearer " + options.bearer);
      } else if (!options.username.empty() && !options.password.empty()) {
        http.setBasicAuth(options.username, options.password);
      }
      for (const auto& h : options.headers) http.addHeader(h.first, h.second);
    }
    if (!options.accept.empty()) http.addHeader("Accept", options.accept);
    if (!options.acceptLanguage.empty()) http.addHeader("Accept-Language", options.acceptLanguage);

    LOG_DBG("HTTP", "wolfSSL GET: %s", current.c_str());
    status = http.GET([&](const uint8_t* data, size_t len) {
      const int st = http.getStatus();
      const bool wanted = (st >= 200 && st < 300) || (options.captureErrorBody && st == 401);
      if (!wanted) return true;  // redirect or error body: drain
      if (!onData(data, len)) {
        sinkStopped = true;
        return false;
      }
      return true;
    });
    if (options.wwwAuthenticateOut) *options.wwwAuthenticateOut = http.getCombinedHeader("www-authenticate");
    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
      const std::string location = http.getHeader("location");
      if (location.empty() || !freeink::SecureHttpClient::resolveUrl(current, location, current)) break;
      continue;
    }
    break;
  }
  closeRequest(url, status, freeink::fetchOrigin(current) == startOrigin);
  if (options.statusOut) *options.statusOut = g_lastStatus;
  return status >= 200 && status < 300 && !sinkStopped;
}

bool HttpDownloader::postForm(const std::string& url, const std::string& formBody, std::string& outResponse,
                              int* statusOut) {
  outResponse.clear();
  if (!openRequest("POST", url)) {
    if (statusOut) *statusOut = 0;
    return false;
  }
  WifiPowerSaveGuard psGuard;
  freeink::SecureHttpClient http;
  if (!http.begin(url)) {
    LOG_ERR("HTTP", "bad URL: %s", url.c_str());
    if (statusOut) *statusOut = 0;
    return false;
  }
  applyCommonClientSetup(http, url, /*sameOrigin=*/true);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  LOG_DBG("HTTP", "wolfSSL POST: %s", url.c_str());
  const int status = http.sendRequest("POST", reinterpret_cast<const uint8_t*>(formBody.data()), formBody.size(),
                                      [&outResponse](const uint8_t* data, size_t len) {
                                        const size_t room =
                                            MAX_RESPONSE_BYTES - std::min(outResponse.size(), MAX_RESPONSE_BYTES);
                                        outResponse.append(reinterpret_cast<const char*>(data), std::min(len, room));
                                        return true;
                                      });
  closeRequest(url, status, /*sameOrigin=*/true);
  if (statusOut) *statusOut = g_lastStatus;
  return status >= 200 && status < 300;
}

HttpDownloader::DownloadError HttpDownloader::postToFile(const std::string& url, const std::string& body,
                                                         const std::string& contentType, const std::string& destPath,
                                                         ProgressCallback progress, const bool* cancelFlag) {
  LOG_DBG("HTTP", "POST download: %s -> %s", url.c_str(), destPath.c_str());
  if (!openRequest("POST", url)) return HTTP_ERROR;
  WifiPowerSaveGuard psGuard;

  const std::string partPath = destPath + ".part";
  Storage.remove(partPath.c_str());
  HalFile file;
  if (!Storage.openFileForWrite("HTTP", partPath.c_str(), file)) {
    LOG_ERR("HTTP", "Failed to open file for writing");
    return FILE_ERROR;
  }

  freeink::SecureHttpClient http;
  if (!http.begin(url)) {
    LOG_ERR("HTTP", "bad URL: %s", url.c_str());
    return HTTP_ERROR;
  }
  applyCommonClientSetup(http, url, /*sameOrigin=*/true);
  http.addHeader("Content-Type", contentType);

  AbortPoll abortPoll(progress, cancelFlag);
  size_t downloaded = 0;
  bool writeFailed = false;
  const int status = http.sendRequest(
      "POST", reinterpret_cast<const uint8_t*>(body.data()), body.size(),
      [&](const uint8_t* data, size_t len) {
        const int st = http.getStatus();
        if (st < 200 || st >= 300) return true;  // drain the error body
        if (file.write(data, len) != len) {
          writeFailed = true;
          return false;
        }
        downloaded += len;
        if (http.hasContentLength()) abortPoll.progressed(downloaded, http.getContentLength());
        return true;
      },
      [&abortPoll] { return abortPoll(); });
  closeRequest(url, status, /*sameOrigin=*/true);
  // Close before any remove() on the same path; DESTRUCTOR_CLOSES_FILE would
  // otherwise close only after the remove.
  if (file.isOpen()) file.close();

  DownloadError result = OK;
  if (http.aborted()) {
    result = ABORTED;
  } else if (writeFailed) {
    result = FILE_ERROR;
  } else if (status < 200 || status >= 300) {
    LOG_ERR("HTTP", "POST failed: status %d: %s", status, url.c_str());
    result = HTTP_ERROR;
  } else if (downloaded == 0) {
    LOG_ERR("HTTP", "no data received");
    result = HTTP_ERROR;
  } else if (http.hasContentLength() && downloaded != http.getContentLength()) {
    LOG_ERR("HTTP", "POST response incomplete: %zu of %zu bytes", downloaded, (size_t)http.getContentLength());
    result = HTTP_ERROR;
  }
  if (result != OK) {
    Storage.remove(partPath.c_str());
    return result;
  }
  if (!Storage.replaceFile(partPath.c_str(), destPath.c_str())) {
    LOG_ERR("HTTP", "Failed to move download into place: %s", destPath.c_str());
    Storage.remove(partPath.c_str());
    return FILE_ERROR;
  }
  LOG_DBG("HTTP", "Downloaded %zu bytes", downloaded);
  return OK;
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

#include "BridgeHttp.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cctype>
#include <cstdio>
#include <cstring>

#if defined(FREEINK_NET_WOLFSSL)
#include <SecureHttpClient.h>

// One bundle for every bridge behind the same tunnel. See BridgeHttp.h.
#include "../study/StudySyncRoots.h"
#include "network/DeviceReport.h"
#else
#include <unistd.h>

#include <cstdlib>
#endif

namespace bridge {

namespace {

#if defined(FREEINK_NET_WOLFSSL)

// The verification roots. An SD bundle wins so a Cloudflare CA change is a
// file copy, not a reflash; the baked bundle is the fallback. Held in a static
// because SecureClient borrows the pointer for every connect -- and keyed by
// path, because two endpoints have two override files and a single static
// would hand the second one the first one's bytes.
const char* caRoots(const Endpoint& endpoint) {
  static std::string sdRoots;
  static std::string probedPath;
  if (probedPath != endpoint.rootsOverridePath) {
    probedPath = endpoint.rootsOverridePath;
    sdRoots.clear();
    HalFile file;
    if (Storage.openFileForRead(endpoint.tag, endpoint.rootsOverridePath, file)) {
      const size_t size = file.size();
      // A sanity floor: a truncated bundle fails the handshake with a generic
      // error, so refuse obviously-broken files here where the log can still
      // say why.
      if (size > 512 && size < 65536) {
        sdRoots.resize(size);
        if (file.read(reinterpret_cast<uint8_t*>(sdRoots.data()), size) == static_cast<int>(size) &&
            sdRoots.find("-----BEGIN CERTIFICATE-----") != std::string::npos) {
          LOG_INF(endpoint.tag, "using SD root bundle (%u bytes)", static_cast<unsigned>(size));
        } else {
          sdRoots.clear();
          LOG_ERR(endpoint.tag, "SD root bundle unreadable; using baked roots");
        }
      } else {
        LOG_ERR(endpoint.tag, "SD root bundle size %u rejected; using baked roots", static_cast<unsigned>(size));
      }
    }
  }
  return sdRoots.empty() ? study::kBridgeCaRoots : sdRoots.c_str();
}

// The firmware version, which every request to one of our hosts carries the
// way HttpDownloader's do; and the device report headers, when the toggle is
// on. Version alone names no device.
void identify(freeink::SecureHttpClient& http, const std::string& url) {
  http.setUserAgent("CrossPlay-ESP32-" CROSSPOINT_VERSION);
  devreport::Header report[devreport::kHeaderCount];
  const int n = devreport::headersFor(url.c_str(), report);
  for (int i = 0; i < n; ++i) http.addHeader(report[i].name, report[i].value);
}

// TLS wants ~35KB free with a 20KB block (the KOSync numbers, measured with
// the same wolfSSL build). Callers free what they can first; this is the last
// line of defense, not the plan.
bool insufficientHeap(const Endpoint& endpoint, std::string& message) {
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t maxBlock = ESP.getMaxAllocHeap();
  if (freeHeap < 35000 || maxBlock < 20000) {
    LOG_ERR(endpoint.tag, "heap too low for TLS: free=%u block=%u", freeHeap, maxBlock);
    message = "Not enough memory free to sync right now. Leave the app and open it again.";
    return true;
  }
  return false;
}

#endif

}  // namespace

std::string base(const Endpoint& endpoint) {
#if !defined(FREEINK_NET_WOLFSSL)
  if (endpoint.urlEnv != nullptr) {
    if (const char* env = std::getenv(endpoint.urlEnv)) return env;
  }
#endif
  return std::string("https://") + endpoint.host;
}

void Headers::add(const char* name, const std::string& value) {
  if (sendCount >= kMax) return;
  send[sendCount].name = name;
  send[sendCount].value = value;
  ++sendCount;
}

void Headers::collect(const char* name) {
  if (wantedCount >= kMax) return;
  wanted[wantedCount++] = name;
}

std::string Headers::value(const char* name) const {
  for (int i = 0; i < gotCount; ++i) {
    // Case-insensitively: HTTP header names are, and a service that answers
    // "etag" where this asked for "ETag" is within its rights. A case-sensitive
    // compare here would read as "the header was absent", which for
    // If-None-Match means re-downloading the same image forever with nothing
    // anywhere saying why.
    if (got[i].name.size() != std::strlen(name)) continue;
    size_t k = 0;
    for (; k < got[i].name.size(); ++k) {
      const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(got[i].name[k])));
      const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(name[k])));
      if (a != b) break;
    }
    if (k == got[i].name.size()) return got[i].value;
  }
  return std::string();
}

bool takeServerError(const std::string& response, std::string& message) {
  JsonDocument doc;
  if (deserializeJson(doc, response) == DeserializationError::Ok && doc["error"].is<const char*>()) {
    message = doc["error"].as<const char*>();
    return true;
  }
  return false;
}

#if defined(FREEINK_NET_WOLFSSL)

// Copy the headers the caller asked for out of a finished response. MUST run
// before http.end(), which is why it is a call at each site rather than a step
// tucked into the teardown: a header read after the client is closed comes back
// empty, and an empty ETag is not an error anywhere -- it just quietly means
// "download it again next time, and every time after that".
void harvest(freeink::SecureHttpClient& http, Headers* headers) {
  if (headers == nullptr) return;
  headers->gotCount = 0;
  for (int i = 0; i < headers->wantedCount; ++i) {
    const std::string v = http.getHeader(headers->wanted[i]);
    if (v.empty()) continue;
    headers->got[headers->gotCount].name = headers->wanted[i];
    headers->got[headers->gotCount].value = v;
    ++headers->gotCount;
  }
}

int request(const Endpoint& endpoint, const char* method, const std::string& path, const std::string& token,
            const uint8_t* body, const size_t bodyLen, std::string& response, std::string& message, Headers* headers) {
  if (insufficientHeap(endpoint, message)) return 0;
  freeink::SecureHttpClient http;
  http.setCACert(caRoots(endpoint));
  http.setTimeout(30000);
  http.setFollowRedirects(2);
  const std::string url = base(endpoint) + path;
  if (!http.begin(url)) {
    message = "The sync service address did not make sense. Update the firmware.";
    return 0;
  }
  if (!token.empty()) http.addHeader("Authorization", std::string("Bearer ") + token);
  if (headers != nullptr) {
    for (int i = 0; i < headers->sendCount; ++i) http.addHeader(headers->send[i].name, headers->send[i].value);
  }
  identify(http, url);
  LOG_INF(endpoint.tag, "%s %s (verified TLS)", method, path.c_str());
  const int status = body ? http.sendRequest(method, body, bodyLen) : http.sendRequest(method, std::string());
  devreport::delivered(url.c_str(), status);
  if (status <= 0) {
    LOG_ERR(endpoint.tag, "%s %s failed: %d", method, path.c_str(), status);
    message = "Could not reach the sync service. Check Wi-Fi and try again.";
    http.end();
#if defined(CROSSPOINT_DEV_SERIAL_BRIDGE)
    // Dev-build self-diagnosis, never a fallback: retry the same request
    // WITHOUT verification purely to bisect the failure, log the verdict, and
    // still fail. A release build never contains this branch.
    {
      freeink::SecureHttpClient probe;
      probe.setInsecure();
      probe.setTimeout(15000);
      int ps = -1;
      if (probe.begin(base(endpoint) + path)) ps = probe.sendRequest("GET", std::string());
      probe.end();
      if (ps > 0) {
        LOG_ERR(endpoint.tag, "DIAGNOSIS: insecure probe got HTTP %d -- certificate VERIFICATION is the failure", ps);
        message = "The bridge answered but its certificate was refused. This build logged the details.";
      } else {
        LOG_ERR(endpoint.tag, "DIAGNOSIS: insecure probe also failed (%d) -- network/DNS level, not certificates", ps);
      }
    }
#endif
    return 0;
  }
  response = http.getString();
  harvest(http, headers);
  http.end();
  return status;
}

int getToFile(const Endpoint& endpoint, const std::string& path, const std::string& token, const std::string& destPart,
              const size_t maxBytes, std::string& message, Headers* headers, size_t* received) {
  if (received != nullptr) *received = 0;
  if (insufficientHeap(endpoint, message)) return 0;
  freeink::SecureHttpClient http;
  http.setCACert(caRoots(endpoint));
  http.setTimeout(30000);
  http.setFollowRedirects(2);
  const std::string url = base(endpoint) + path;
  if (!http.begin(url)) {
    message = "The Live address did not make sense. Update the firmware.";
    return 0;
  }
  if (!token.empty()) http.addHeader("Authorization", std::string("Bearer ") + token);
  if (headers != nullptr) {
    for (int i = 0; i < headers->sendCount; ++i) http.addHeader(headers->send[i].name, headers->send[i].value);
  }
  identify(http, url);

  HalFile out;
  bool opened = false;
  bool writeFailed = false;
  size_t written = 0;
  const int status = http.GET([&](const uint8_t* data, const size_t len) {
    // The lazy open. A 304 and a 204 carry no body, so this lambda never runs
    // and the card is never touched -- no truncation, no write, no wear, on
    // precisely the wake that exists to cost nothing.
    if (!opened) {
      if (!Storage.openFileForWrite(endpoint.tag, destPart.c_str(), out)) {
        writeFailed = true;
        return false;
      }
      opened = true;
    }
    if (written + len > maxBytes) {
      // A body past the ceiling is stopped MID-STREAM rather than after: the
      // point of a ceiling is that the card never receives the overrun.
      LOG_ERR(endpoint.tag, "%s: body passed the %u-byte ceiling", path.c_str(), static_cast<unsigned>(maxBytes));
      writeFailed = true;
      return false;
    }
    if (out.write(data, len) != static_cast<int>(len)) {
      writeFailed = true;
      return false;
    }
    written += len;
    return true;
  });
  harvest(http, headers);
  http.end();
  if (opened) out.close();
  devreport::delivered(url.c_str(), status);

  if (writeFailed) {
    LOG_ERR(endpoint.tag, "could not write %s", destPart.c_str());
    message = "Could not write to the card.";
    Storage.remove(destPart.c_str());
    return 0;
  }
  if (status == 200 && written == 0) {
    LOG_ERR(endpoint.tag, "%s: a 200 with no body", path.c_str());
    message = "Live sent an image this reader could not use.";
    Storage.remove(destPart.c_str());
    return 0;
  }
  if (received != nullptr) *received = written;
  return status;
}

bool streamToFile(const Endpoint& endpoint, const std::string& path, const std::string& token,
                  const std::string& destPart, const size_t expectedSize, const char* incompleteMessage, bool* cancel,
                  std::string& message) {
  if (insufficientHeap(endpoint, message)) return false;
  HalFile out;
  if (!Storage.openFileForWrite(endpoint.tag, destPart.c_str(), out)) {
    message = "Could not write to the card.";
    return false;
  }
  freeink::SecureHttpClient http;
  http.setCACert(caRoots(endpoint));
  http.setTimeout(30000);
  http.setFollowRedirects(2);
  const std::string url = base(endpoint) + path;
  if (!http.begin(url)) {
    message = "The sync service address did not make sense.";
    return false;
  }
  http.addHeader("Authorization", std::string("Bearer ") + token);
  identify(http, url);
  size_t written = 0;
  const int status = http.GET(
      [&](const uint8_t* data, size_t len) {
        if (out.write(data, len) != static_cast<int>(len)) return false;
        written += len;
        return true;
      },
      [&]() { return cancel && *cancel; });
  http.end();
  out.close();
  devreport::delivered(url.c_str(), status);
  if (cancel && *cancel) {
    message = "Stopped.";
    return false;
  }
  if (status != 200 || written != expectedSize) {
    LOG_ERR(endpoint.tag, "download %s: status=%d written=%u expected=%u", path.c_str(), status,
            static_cast<unsigned>(written), static_cast<unsigned>(expectedSize));
    message = incompleteMessage;
    return false;
  }
  return true;
}

#else  // simulator: curl, because the HTTP stub cannot carry binary bodies.

// Pull the headers the caller asked for out of a curl -D dump.
//
// Parsed rather than grepped because a redirect (setFollowRedirects(2) on the
// device, -L nowhere here) and a 100-continue both put more than one block in
// the file, and the LAST block is the one that answered. Taking the first would
// read an intermediate response's headers as the real ones.
void harvestDump(const char* dumpPath, Headers* headers) {
  if (headers == nullptr) return;
  headers->gotCount = 0;
  FILE* f = std::fopen(dumpPath, "rb");
  if (f == nullptr) return;
  char line[1024];
  while (std::fgets(line, sizeof(line), f) != nullptr) {
    if (std::strncmp(line, "HTTP/", 5) == 0) {
      headers->gotCount = 0;  // a new response block supersedes the last
      continue;
    }
    const char* colon = std::strchr(line, ':');
    if (colon == nullptr) continue;
    std::string name(line, static_cast<size_t>(colon - line));
    std::string value(colon + 1);
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ')) value.pop_back();
    while (!value.empty() && value.front() == ' ') value.erase(value.begin());
    for (int i = 0; i < headers->wantedCount; ++i) {
      if (name.size() != std::strlen(headers->wanted[i])) continue;
      size_t k = 0;
      for (; k < name.size(); ++k) {
        if (std::tolower(static_cast<unsigned char>(name[k])) !=
            std::tolower(static_cast<unsigned char>(headers->wanted[i][k])))
          break;
      }
      if (k != name.size()) continue;
      if (headers->gotCount >= Headers::kMax) break;
      headers->got[headers->gotCount].name = headers->wanted[i];
      headers->got[headers->gotCount].value = value;
      ++headers->gotCount;
      break;
    }
  }
  std::fclose(f);
}

int request(const Endpoint& endpoint, const char* method, const std::string& path, const std::string& token,
            const uint8_t* body, const size_t bodyLen, std::string& response, std::string& message, Headers* headers) {
  char bodyPath[] = "/tmp/bridgehttp-body-XXXXXX";
  char outPath[] = "/tmp/bridgehttp-out-XXXXXX";
  char dumpPath[] = "/tmp/bridgehttp-hdr-XXXXXX";
  const int fdBody = mkstemp(bodyPath);
  const int fdOut = mkstemp(outPath);
  const int fdDump = mkstemp(dumpPath);
  if (fdBody < 0 || fdOut < 0 || fdDump < 0) {
    message = "sim: mkstemp failed";
    return 0;
  }
  close(fdDump);
  if (body && bodyLen) {
    FILE* f = fdopen(fdBody, "wb");
    fwrite(body, 1, bodyLen, f);
    fclose(f);
  } else {
    close(fdBody);
  }
  close(fdOut);
  std::string cmd = "curl -sS -m 60 -o '" + std::string(outPath) + "' -D '" + std::string(dumpPath) +
                    "' -w '%{http_code}' -X " + method;
  if (!token.empty()) cmd += " -H 'Authorization: Bearer " + token + "'";
  if (headers != nullptr) {
    for (int i = 0; i < headers->sendCount; ++i) {
      cmd += " -H '" + headers->send[i].name + ": " + headers->send[i].value + "'";
    }
  }
  if (body) cmd += " -H 'Content-Type: application/json' --data-binary @'" + std::string(bodyPath) + "'";
  if (!body && std::strcmp(method, "POST") == 0) cmd += " --data ''";
  cmd += " '" + base(endpoint) + path + "'";
  FILE* pipe = popen(cmd.c_str(), "r");
  char statusBuf[8] = {};
  if (pipe) {
    if (fgets(statusBuf, sizeof(statusBuf), pipe) == nullptr) statusBuf[0] = '\0';
    pclose(pipe);
  }
  const int status = atoi(statusBuf);
  response.clear();
  if (FILE* f = fopen(outPath, "rb")) {
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) response.append(buf, n);
    fclose(f);
  }
  harvestDump(dumpPath, headers);
  remove(bodyPath);
  remove(outPath);
  remove(dumpPath);
  if (status == 0) message = "Could not reach the sync service. Check Wi-Fi and try again.";
  return status;
}

int getToFile(const Endpoint& endpoint, const std::string& path, const std::string& token, const std::string& destPart,
              const size_t maxBytes, std::string& message, Headers* headers, size_t* received) {
  if (received != nullptr) *received = 0;
  std::string response;
  const int status = request(endpoint, "GET", path, token, nullptr, 0, response, message, headers);
  // The card is touched on a 200 and on nothing else, which is the same
  // contract the device path gets from its lazy open. Written the other way
  // round here because curl has already buffered the body by the time we know
  // the status, and a simulator that wrote an empty file on a 304 would make
  // the one behaviour this feature is built on untestable on a laptop.
  if (status != 200) return status;
  if (response.empty() || response.size() > maxBytes) {
    LOG_ERR(endpoint.tag, "%s: %u bytes, ceiling %u", path.c_str(), static_cast<unsigned>(response.size()),
            static_cast<unsigned>(maxBytes));
    message = "Live sent an image this reader could not use.";
    return 0;
  }
  HalFile out;
  if (!Storage.openFileForWrite(endpoint.tag, destPart.c_str(), out)) {
    message = "Could not write to the card.";
    return 0;
  }
  out.write(reinterpret_cast<const uint8_t*>(response.data()), response.size());
  out.close();
  if (received != nullptr) *received = response.size();
  return status;
}

bool streamToFile(const Endpoint& endpoint, const std::string& path, const std::string& token,
                  const std::string& destPart, const size_t expectedSize, const char* incompleteMessage, bool* cancel,
                  std::string& message) {
  (void)cancel;
  std::string response;
  const int status = request(endpoint, "GET", path, token, nullptr, 0, response, message);
  if (status != 200 || response.size() != expectedSize) {
    message = incompleteMessage;
    return false;
  }
  HalFile out;
  if (!Storage.openFileForWrite(endpoint.tag, destPart.c_str(), out)) {
    message = "Could not write to the card.";
    return false;
  }
  out.write(reinterpret_cast<const uint8_t*>(response.data()), response.size());
  return true;
}

#endif

}  // namespace bridge

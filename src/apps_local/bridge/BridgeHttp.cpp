#include "BridgeHttp.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

#if defined(FREEINK_NET_WOLFSSL)
#include <SecureHttpClient.h>

// One bundle for every bridge behind the same tunnel. See BridgeHttp.h.
#include "../study/StudySyncRoots.h"
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

bool takeServerError(const std::string& response, std::string& message) {
  JsonDocument doc;
  if (deserializeJson(doc, response) == DeserializationError::Ok && doc["error"].is<const char*>()) {
    message = doc["error"].as<const char*>();
    return true;
  }
  return false;
}

#if defined(FREEINK_NET_WOLFSSL)

int request(const Endpoint& endpoint, const char* method, const std::string& path, const std::string& token,
            const uint8_t* body, const size_t bodyLen, std::string& response, std::string& message) {
  if (insufficientHeap(endpoint, message)) return 0;
  freeink::SecureHttpClient http;
  http.setCACert(caRoots(endpoint));
  http.setTimeout(30000);
  http.setFollowRedirects(2);
  if (!http.begin(base(endpoint) + path)) {
    message = "The sync service address did not make sense. Update the firmware.";
    return 0;
  }
  if (!token.empty()) http.addHeader("Authorization", std::string("Bearer ") + token);
  LOG_INF(endpoint.tag, "%s %s (verified TLS)", method, path.c_str());
  const int status = body ? http.sendRequest(method, body, bodyLen) : http.sendRequest(method, std::string());
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
  http.end();
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
  if (!http.begin(base(endpoint) + path)) {
    message = "The sync service address did not make sense.";
    return false;
  }
  http.addHeader("Authorization", std::string("Bearer ") + token);
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

int request(const Endpoint& endpoint, const char* method, const std::string& path, const std::string& token,
            const uint8_t* body, const size_t bodyLen, std::string& response, std::string& message) {
  char bodyPath[] = "/tmp/bridgehttp-body-XXXXXX";
  char outPath[] = "/tmp/bridgehttp-out-XXXXXX";
  const int fdBody = mkstemp(bodyPath);
  const int fdOut = mkstemp(outPath);
  if (fdBody < 0 || fdOut < 0) {
    message = "sim: mkstemp failed";
    return 0;
  }
  if (body && bodyLen) {
    FILE* f = fdopen(fdBody, "wb");
    fwrite(body, 1, bodyLen, f);
    fclose(f);
  } else {
    close(fdBody);
  }
  close(fdOut);
  std::string cmd = "curl -sS -m 60 -o '" + std::string(outPath) + "' -w '%{http_code}' -X " + method;
  if (!token.empty()) cmd += " -H 'Authorization: Bearer " + token + "'";
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
  remove(bodyPath);
  remove(outPath);
  if (status == 0) message = "Could not reach the sync service. Check Wi-Fi and try again.";
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

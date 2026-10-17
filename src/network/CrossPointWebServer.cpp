#include "CrossPointWebServer.h"

#include <ArduinoJson.h>
#include <BoardConfig.h>
#include <BookKey.h>
#include <Crypto.h>
#include <DeviceSecret.h>
#include <FsHelpers.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalMemory.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <Logging.h>
#include <Memory.h>
#include <ResumableFetch.h>
#include <SecureHttpClient.h>
#include <Util.h>
#include <WiFi.h>
#include <WolfsslCrypto.h>
#include <base64.h>
#include <esp_efuse.h>
#include <esp_efuse_table.h>
#include <esp_ota_ops.h>
#include <wolfssl/wolfcrypt/aes.h>
#include <wolfssl/wolfcrypt/hash.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string_view>

#include "../apps_local/notes/NotesCore.h"
#include "ClippingStore.h"
#include "CrossPointSettings.h"
#include "DevInputCommands.h"
#include "DevMode.h"
#include "DevSerialBridge.h"
#include "FirmwareFlasher.h"
#include "FontInstaller.h"
#include "OpdsServerStore.h"
#include "ProtectedPaths.h"
#include "SdCardFontSystem.h"
#include "SettingsList.h"
#include "WebDAVHandler.h"
#include "WifiCredentialStore.h"
#include "WifiPowerSaveGuard.h"
#include "apps_local/wallpapers/WallpapersCore.h"
#include "html/FilesPageHtml.generated.h"
#include "html/FontsPageHtml.generated.h"
#include "html/HomePageHtml.generated.h"
#include "html/NotesPageHtml.generated.h"
#include "html/RunnerPageHtml.generated.h"
#include "html/SettingsPageHtml.generated.h"
#include "html/WallpaperPageHtml.generated.h"
#include "html/js/jszip_minJs.generated.h"
#include "html/js/wallconvertJs.generated.h"
#include "util/BookCacheUtils.h"
#include "util/DictionaryRegistry.h"
#include "util/PluginHttp.h"
#include "util/PluginLocations.h"
#include "util/TaskWatchdog.h"

namespace {
// Arduino's WebServer retains parsed request arguments until the next request.
// For JSON POSTs, that includes the complete "plain" body. Expose a narrowly
// scoped release operation so large request bodies do not remain resident
// between requests, and so outbound TLS can reuse that memory immediately.
class CrossPointHttpServer final : public WebServer {
 public:
  explicit CrossPointHttpServer(uint16_t port) : WebServer(port) {}

  void releaseRequestArguments() {
    if (_currentArgs) {
      delete[] _currentArgs;
      _currentArgs = nullptr;
    }
    _currentArgCount = 0;

    if (_postArgs) {
      delete[] _postArgs;
      _postArgs = nullptr;
    }
    _postArgsLen = 0;
  }
};

void releaseRequestArguments(WebServer* server) {
  static_cast<CrossPointHttpServer*>(server)->releaseRequestArguments();
}

// Folders/files to hide from the web interface file browser
// Note: Items starting with "." are automatically hidden
constexpr const char* HIDDEN_ITEMS[] = {"System Volume Information", "XTCache"};

// Formats the library index tracks (LibraryIndex isBookName): an upload of any
// of these must mark the index dirty so the next Library entry rebuilds it.
bool isLibraryBookFile(const String& filename) {
  return FsHelpers::checkFileExtension(filename, ".epub") || FsHelpers::checkFileExtension(filename, ".txt") ||
         FsHelpers::checkFileExtension(filename, ".md") || FsHelpers::checkFileExtension(filename, ".xtc");
}
constexpr uint16_t UDP_PORTS[] = {54982, 48123, 39001, 44044, 59678};
constexpr uint16_t LOCAL_UDP_PORT = 8134;

// Where Developer Mode uploads land. Fixed on purpose; see handleDevUploadData.
constexpr const char* kDevUploadPath = "/.crosspoint/devmode-firmware.bin";

// Static pointer for WebSocket callback (WebSocketsServer requires C-style callback)
CrossPointWebServer* wsInstance = nullptr;

// WebSocket upload state
HalFile wsUploadFile;
String wsUploadFileName;
String wsUploadPath;
size_t wsUploadSize = 0;
size_t wsUploadReceived = 0;
unsigned long wsUploadStartTime = 0;
bool wsUploadInProgress = false;
uint8_t wsUploadClientNum = 255;  // 255 = no active upload client
size_t wsLastProgressSent = 0;
String wsLastCompleteName;
size_t wsLastCompleteSize = 0;
unsigned long wsLastCompleteAt = 0;

String normalizeWebPath(const String& inputPath) {
  if (inputPath.isEmpty() || inputPath == "/") {
    return "/";
  }
  std::string normalized = FsHelpers::normalisePath(inputPath.c_str());
  String result = normalized.c_str();
  if (result.isEmpty()) {
    return "/";
  }
  if (!result.startsWith("/")) {
    result = "/" + result;
  }
  if (result.length() > 1 && result.endsWith("/")) {
    result = result.substring(0, result.length() - 1);
  }
  return result;
}

bool isProtectedItemName(const String& name) {
  if (name.startsWith(".")) {
    return true;
  }
  for (const auto* item : HIDDEN_ITEMS) {
    if (name.equals(item)) {
      return true;
    }
  }
  return false;
}

}  // namespace

// File listing page template - now using generated headers:
// - HomePageHtml (from html/HomePage.html)
// - FilesPageHeaderHtml (from html/FilesPageHeader.html)
// - FilesPageFooterHtml (from html/FilesPageFooter.html)
CrossPointWebServer::CrossPointWebServer(Surface surface) : surface(surface) {}

CrossPointWebServer::~CrossPointWebServer() { stop(); }

void CrossPointWebServer::begin() {
  if (running) {
    LOG_DBG("WEB", "Web server already running");
    return;
  }

  // Check if we have a valid network connection (either STA connected or AP mode)
  const wifi_mode_t wifiMode = WiFi.getMode();
  const bool isStaConnected = (wifiMode & WIFI_MODE_STA) && (WiFi.status() == WL_CONNECTED);
  const bool isInApMode = (wifiMode & WIFI_MODE_AP) && (WiFi.softAPgetStationNum() >= 0);  // AP is running

  if (!isStaConnected && !isInApMode) {
    LOG_DBG("WEB", "Cannot start webserver - no valid network (mode=%d, status=%d)", wifiMode, WiFi.status());
    return;
  }

  // Store AP mode flag for later use (e.g., in handleStatus)
  apMode = isInApMode;

  LOG_DBG("WEB", "[MEM] Free heap before begin: %d bytes", ESP.getFreeHeap());
  LOG_DBG("WEB", "Network mode: %s", apMode ? "AP" : "STA");

  LOG_DBG("WEB", "Creating web server on port %d...", port);
  server.reset(new CrossPointHttpServer(port));

  // Disable WiFi sleep to improve responsiveness and prevent 'unreachable' errors.
  // This is critical for reliable web server operation on ESP32.
  WiFi.setSleep(false);
  // Default varies by ESP32 core version. The activity's loss-recovery loop
  // relies on driver retries during transient disconnects.
  WiFi.setAutoReconnect(true);

  // Note: WebServer class doesn't have setNoDelay() in the standard ESP32 library.
  // We rely on disabling WiFi sleep for responsiveness.

  LOG_DBG("WEB", "[MEM] Free heap after WebServer allocation: %d bytes", ESP.getFreeHeap());

  if (!server) {
    LOG_ERR("WEB", "Failed to create WebServer!");
    return;
  }

  // Add Access-Control-Allow-* headers to every response so web-based clients
  // and PWAs on other origins can use the HTTP API. Preflight OPTIONS requests
  // are answered in handleNotFound().
  // NOT on the Developer Mode surface. This core's enableCORS sends
  // Access-Control-Allow-Origin/Methods/Headers: *, which lets any page the
  // user happens to visit both call these routes AND READ THE REPLIES. That
  // turns a six-digit code into something grindable from a drive-by tab
  // rather than from the LAN, and the reply to a successful guess hands over
  // the token. The reader's own web UI is a browser page and needs CORS; the
  // dev API's client is curl and does not.
  // Full only. WallpapersOnly serves its own page to its own origin, so CORS
  // would only widen who may read the replies, and DeveloperOnly deliberately
  // withholds it (see below).
  if (isFull()) server->enableCORS(true);

  // Setup routes
  LOG_DBG("WEB", "Setting up routes (%s)...",
          isFull() ? "full" : (isDev() ? "developer mode only" : "wallpapers only"));
  if (isFull()) {
    server->on("/", HTTP_GET, [this] { handleRoot(); });
    server->on("/files", HTTP_GET, [this] { handleFileList(); });
    server->on("/js/jszip.min.js", HTTP_GET, [this] { handleJszip(); });

    server->on("/api/status", HTTP_GET, [this] { handleStatus(); });
    server->on("/api/files", HTTP_GET, [this] { handleFileListData(); });
    server->on("/download", HTTP_GET, [this] { handleDownload(); });

    // Upload endpoint with special handling for multipart form data
    server->on("/upload", HTTP_POST, [this] { handleUploadPost(upload); }, [this] { handleUpload(upload); });

    // Create folder endpoint
    server->on("/mkdir", HTTP_POST, [this] { handleCreateFolder(); });

    // Rename file endpoint
    server->on("/rename", HTTP_POST, [this] { handleRename(); });

    // Move file endpoint
    server->on("/move", HTTP_POST, [this] { handleMove(); });

    // Delete file/folder endpoint
    server->on("/delete", HTTP_POST, [this] { handleDelete(); });

    // Settings endpoints
    server->on("/settings", HTTP_GET, [this] { handleSettingsPage(); });
    server->on("/api/settings", HTTP_GET, [this] { handleGetSettings(); });
    server->on("/api/settings", HTTP_POST, [this] { handlePostSettings(); });

    // Font management endpoints
    server->on("/fonts", HTTP_GET, [this] { handleFontsPage(); });
    server->on("/api/fonts", HTTP_GET, [this] { handleFontList(); });
    server->on("/api/fonts/upload", HTTP_POST, [this] { handleFontUpload(); }, [this] { handleFontUploadData(); });
    server->on("/api/fonts/delete", HTTP_POST, [this] { handleFontDelete(); });

    // OPDS server endpoints
    server->on("/api/opds", HTTP_GET, [this] { handleGetOpdsServers(); });
    server->on("/api/opds", HTTP_POST, [this] { handlePostOpdsServer(); });
    server->on("/api/opds/delete", HTTP_POST, [this] { handleDeleteOpdsServer(); });

    // Browser-side plugins (JS on the SD card) + their generic capabilities.
    server->on("/api/plugins", HTTP_GET, [this] { handlePluginList(); });
    server->on("/plugin", HTTP_GET, [this] { handlePluginFile(); });
    server->on("/plugins-run", HTTP_GET, [this] { handlePluginRunnerPage(); });
    server->on("/api/plugin-jobs", HTTP_POST, [this] { handlePluginJobSubmit(); });
    server->on("/api/plugin-jobs/claim", HTTP_GET, [this] { handlePluginJobClaim(); });
    server->on("/api/plugin-jobs/complete", HTTP_POST, [this] { handlePluginJobComplete(); });
    server->on("/api/plugin-jobs/status", HTTP_GET, [this] { handlePluginJobStatus(); });
    server->on("/api/relay", HTTP_POST, [this] { handleRelay(); });
    server->on("/api/crypto", HTTP_POST, [this] { handleCrypto(); });
    server->on("/api/fetch", HTTP_POST, [this] { handleFetch(); });
    server->on("/api/book-key", HTTP_POST, [this] { handleBookKey(); });
    server->on("/api/plugin-fs", HTTP_POST, [this] { handlePluginFs(); }, [this] { handlePluginFsUpload(); });

    // Wi-Fi credential endpoints
    server->on("/api/wifi", HTTP_GET, [this] { handleGetWifiNetworks(); });
    server->on("/api/wifi", HTTP_POST, [this] { handlePostWifiNetwork(); });
    server->on("/api/wifi/delete", HTTP_POST, [this] { handleDeleteWifiNetwork(); });
  }  // isFull()

  if (isWallpapers()) {
    server->on("/w", HTTP_GET, [this] { handleWallpaperPage(); });
    server->on("/js/wallconvert.js", HTTP_GET, [this] { handleWallpaperScript(); });
    // HTTP_PUT for the same structural reason /api/dev/upload is: this core
    // hands ONE callback to both the multipart and the raw paths with nothing
    // to tell them apart, and calling server->upload() in raw mode dereferences
    // a null _currentUpload and reboots. PUT makes canUpload() unreachable, so
    // only the raw path can ever fire.
    server->on("/w/upload", HTTP_PUT, [this] { handleWallpaperUpload(); }, [this] { handleWallpaperUploadData(); });
  }

  if (isNotes()) {
    server->on("/n", HTTP_GET, [this] { handleNotesPage(); });
    server->on("/n/text", HTTP_GET, [this] { handleNotesText(); });
    // PUT rather than POST for the same reason /w/upload is: this core hands
    // one callback to both the multipart and the raw paths with nothing to tell
    // them apart. A note is small enough to arrive as a plain body, so there is
    // no raw handler at all here -- server->arg("plain") is the whole read.
    server->on("/n/text", HTTP_PUT, [this] { handleNotesSave(); });
  }

  // The developer surface. Present for Full and DeveloperOnly and DELIBERATELY
  // NOT for WallpapersOnly or NotesOnly: these routes flash firmware, and the whole point of
  // that surface is that its address is printed in a QR code for anyone in the
  // room to scan. "Always present" was true when there were two surfaces; a
  // third one that quietly inherited a flashing API would be the worst kind of
  // default. /api/status carries no secrets and is how a script finds a device
  // before it has a token, so it rides along with them.
  if (isFull() || isDev()) {
    server->on("/api/status", HTTP_GET, [this] { handleStatus(); });
    server->on("/api/dev/pair", HTTP_POST, [this] { handleDevPair(); });
    server->on("/api/dev/flash", HTTP_POST, [this] { handleDevFlash(); });
    // HTTP_PUT, and that is load-bearing rather than taste. This core's
    // FunctionRequestHandler hands the SAME callback to both the upload path and
    // the raw path, with nothing passed in to tell them apart -- and calling
    // server->upload() while the server is in raw mode dereferences a null
    // _currentUpload and reboots the device. Registering as PUT makes canUpload()
    // structurally unreachable (it requires HTTP_POST), so only the raw path can
    // ever fire and there is one mode to write for instead of two to distinguish.
    server->on("/api/dev/upload", HTTP_PUT, [this] { handleDevUpload(); }, [this] { handleDevUploadData(); });
    server->on("/api/dev/disable", HTTP_POST, [this] { handleDevDisable(); });
    server->on("/api/dev/crash", HTTP_GET, [this] { handleDevCrash(); });
    server->on("/api/dev/log", HTTP_GET, [this] { handleDevLog(); });
#if CROSSPOINT_DEV_SERIAL_BRIDGE
    // Driving the device, over the transport that needs no cable. Gated on the
    // same flag as the injector itself, so a release build carries neither the
    // routes nor the per-frame input overlay they schedule onto.
    server->on("/api/dev/input", HTTP_POST, [this] { handleDevInput(); });
    server->on("/api/dev/screen", HTTP_GET, [this] { handleDevScreen(); });
    server->on("/api/dev/serial", HTTP_GET, [this] { handleDevSerial(); });
#endif

  }  // isFull() || isDev()

  server->onNotFound([this] { handleNotFound(); });
  LOG_DBG("WEB", "[MEM] Free heap after route setup: %d bytes", ESP.getFreeHeap());

  // Collect WebDAV headers and register handler
  // X-Dev-Token rides along here: Arduino's WebServer only retains headers it
  // was told to collect, so without this hasHeader() is always false and every
  // paired request looks unpaired. If-None-Match is collected so the
  // static-page handlers can answer conditional GETs with 304.
  const char* collectedHeaders[] = {"Depth",      "Destination", "Overwrite",     "If",
                                    "Lock-Token", "Timeout",     "If-None-Match", "X-Dev-Token"};
  server->collectHeaders(collectedHeaders, 8);
  if (isFull()) {
    server->addHandler(new WebDAVHandler());  // deleted by WebServer when the server is stopped
    LOG_DBG("WEB", "WebDAV handler initialized");
  }

  server->begin();

  if (isFull()) {
    // Fast binary uploads for the file manager. Dev mode uploads over plain
    // HTTP instead, which is one fewer listening port for the surface that
    // stays up the longest.
    LOG_DBG("WEB", "Starting WebSocket server on port %d...", wsPort);
    wsServer.reset(new WebSocketsServer(wsPort));
    wsInstance = const_cast<CrossPointWebServer*>(this);
    wsServer->begin();
    wsServer->onEvent(wsEventCallback);
    LOG_DBG("WEB", "WebSocket server started");
  }

  udpActive = udp.begin(LOCAL_UDP_PORT);
  LOG_DBG("WEB", "Discovery UDP %s on port %d", udpActive ? "enabled" : "failed", LOCAL_UDP_PORT);

  // Do not subscribe the serving task to the task watchdog. Arduino WebServer
  // permits five-second client and ACK waits, which can consume the entire
  // default watchdog window on a weak connection. The interrupt watchdog still
  // catches hard CPU lockups, matching the rest of the application lifecycle.

  running = true;

  LOG_DBG("WEB", "Web server started on port %d", port);
  // Show the correct IP based on network mode
  const String ipAddr = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  LOG_DBG("WEB", "Access at http://%s/", ipAddr.c_str());
  LOG_DBG("WEB", "WebSocket at ws://%s:%d/", ipAddr.c_str(), wsPort);
  LOG_DBG("WEB", "[MEM] Free heap after server.begin(): %d bytes", ESP.getFreeHeap());
}

void CrossPointWebServer::suspendTransferServices() {
  // Leave the WebSocket server alone mid-upload; killing it would abort the
  // transfer. The fetch just stalls that upload until it completes.
  if (wsServer && !wsUploadInProgress) {
    wsServer->close();
    wsServer.reset();
  }
  if (udpActive) udp.stop();
  LOG_DBG("WEB", "Transfer services suspended, heap %u, max block %u", (unsigned)ESP.getFreeHeap(),
          (unsigned)ESP.getMaxAllocHeap());
}

void CrossPointWebServer::resumeTransferServices() {
  if (!running) return;
  if (!wsServer) {
    auto* ws = new (std::nothrow) WebSocketsServer(wsPort);
    if (ws) {
      wsServer.reset(ws);
      wsServer->begin();
      wsServer->onEvent(wsEventCallback);
    } else {
      LOG_ERR("WEB", "OOM: WebSocket server restart");
    }
  }
  if (udpActive) udpActive = udp.begin(LOCAL_UDP_PORT);
  LOG_DBG("WEB", "Transfer services resumed, heap %u, max block %u", (unsigned)ESP.getFreeHeap(),
          (unsigned)ESP.getMaxAllocHeap());
}

bool CrossPointWebServer::dropUploadIfCancelled() const {
  if (!uploadCancelCheck || !uploadCancelCheck()) return false;
  // WebServer's next read of the body then fails and it raises UPLOAD_FILE_ABORTED.
  server->client().stop();
  return true;
}

void CrossPointWebServer::abortWsUpload(const char* tag) {
  // Explicit close() required: file-scope global persists beyond function scope
  wsUploadFile.close();
  String filePath = wsUploadPath;
  if (!filePath.endsWith("/")) filePath += "/";
  filePath += wsUploadFileName;
  if (Storage.remove(filePath.c_str())) {
    LOG_DBG(tag, "Deleted incomplete upload: %s", filePath.c_str());
  } else {
    LOG_DBG(tag, "Failed to delete incomplete upload: %s", filePath.c_str());
  }
  wsUploadInProgress = false;
  wsUploadClientNum = 255;
  wsLastProgressSent = 0;
}

void CrossPointWebServer::stop() {
  if (!running || !server) {
    LOG_DBG("WEB", "stop() called but already stopped (running=%d, server=%p)", running, server.get());
    return;
  }

  LOG_DBG("WEB", "STOP INITIATED - setting running=false first");
  running = false;  // Set this FIRST to prevent handleClient from using server

  LOG_DBG("WEB", "[MEM] Free heap before stop: %d bytes", ESP.getFreeHeap());

  // Close any in-progress WebSocket upload and remove partial file
  if (wsUploadInProgress && wsUploadFile) {
    abortWsUpload("WEB");
  }

  // Stop WebSocket server
  if (wsServer) {
    LOG_DBG("WEB", "Stopping WebSocket server...");
    wsServer->close();
    wsServer.reset();
    wsInstance = nullptr;
    LOG_DBG("WEB", "WebSocket server stopped");
  }

  if (udpActive) {
    udp.stop();
    udpActive = false;
  }

  // Brief delay to allow any in-flight handleClient() calls to complete
  delay(20);

  server->stop();
  LOG_DBG("WEB", "[MEM] Free heap after server->stop(): %d bytes", ESP.getFreeHeap());

  // Brief delay before deletion
  delay(10);

  server.reset();
  LOG_DBG("WEB", "Web server stopped and deleted");
  LOG_DBG("WEB", "[MEM] Free heap after delete server: %d bytes", ESP.getFreeHeap());

  // Note: Static upload variables (uploadFileName, uploadPath, uploadError) are declared
  // later in the file and will be cleared when they go out of scope or on next upload
  LOG_DBG("WEB", "[MEM] Free heap final: %d bytes", ESP.getFreeHeap());
}

void CrossPointWebServer::handleClient() {
  // Check running flag FIRST before accessing server
  if (!running) {
    return;
  }

  // Double-check server pointer is valid
  if (!server) {
    LOG_DBG("WEB", "WARNING: handleClient called with null server!");
    return;
  }

  // No heartbeat. It fired every 10 seconds and said only that a loop was
  // still looping -- and the RTC log ring is SIXTEEN lines, so it flushed the
  // ring every 160 seconds. That made /api/dev/log and /api/dev/crash useless
  // for anything that happened more than two and a half minutes ago: a real
  // diagnosis of a wedged cable was overwritten by a heartbeat while it was
  // still being read. A log line that only says "still running" is worth less
  // than the sixteenth of the ring it costs.

  server->handleClient();
  // WebServer otherwise keeps the last request's argument strings allocated
  // until another request arrives. They are no longer observable once its
  // handler returns, so release them now instead of retaining a JSON body.
  releaseRequestArguments(server.get());

  // Handle WebSocket events
  if (wsServer) {
    wsServer->loop();
  }

  // Respond to discovery broadcasts
  if (udpActive) {
    int packetSize = udp.parsePacket();
    if (packetSize > 0) {
      char buffer[16];
      int len = udp.read(buffer, sizeof(buffer) - 1);
      if (len > 0) {
        buffer[len] = '\0';
        if (strcmp(buffer, "hello") == 0) {
          String hostname = WiFi.getHostname();
          if (hostname.isEmpty()) {
            hostname = "crosspoint";
          }
          // A discovery handshake, not a label. Two things match this leading
          // token to recognise the device: upstream's crosspoint_reader Calibre
          // plugin, and the fork's OWN scripts_local/wifi-flash.sh. Renaming it
          // would break wireless flashing, which is the surprising half.
          String message = "crosspoint (on " + hostname + ");" + String(wsPort);
          udp.beginPacket(udp.remoteIP(), udp.remotePort());
          udp.write(reinterpret_cast<const uint8_t*>(message.c_str()), message.length());
          udp.endPacket();
        }
      }
    }
  }
}

CrossPointWebServer::WsUploadStatus CrossPointWebServer::getWsUploadStatus() const {
  WsUploadStatus status;
  status.inProgress = wsUploadInProgress;
  status.received = wsUploadReceived;
  status.total = wsUploadSize;
  status.filename = wsUploadFileName.c_str();
  status.lastCompleteName = wsLastCompleteName.c_str();
  status.lastCompleteSize = wsLastCompleteSize;
  status.lastCompleteAt = wsLastCompleteAt;
  return status;
}

static void sendStaticContent(WebServer* server, const char* data, size_t len, const char* etag,
                              const char* contentType) {
  // Content is baked into flash at build time, so the ETag is stable for the
  // lifetime of a firmware image. Honor If-None-Match with a 304 so browsers
  // reuse their cache instead of re-downloading on every navigation.
  if (server->header("If-None-Match") == etag) {
    server->sendHeader("ETag", etag);
    server->sendHeader("Cache-Control", "no-cache");
    server->send(304);
    return;
  }
  server->sendHeader("Content-Encoding", "gzip");
  server->sendHeader("ETag", etag);
  // no-cache: the browser may cache, but must revalidate (conditional GET)
  // before reuse — this is what unlocks 304 responses.
  server->sendHeader("Cache-Control", "no-cache");
  server->send_P(200, contentType, data, len);
}

void CrossPointWebServer::handleRoot() const {
  sendStaticContent(server.get(), HomePageHtml, sizeof(HomePageHtml), HomePageHtmlETag, "text/html");
  LOG_DBG("WEB", "Served root page");
}

void CrossPointWebServer::handleJszip() const {
  sendStaticContent(server.get(), jszip_minJs, jszip_minJsCompressedSize, jszip_minJsETag, "application/javascript");
  LOG_DBG("WEB", "Served jszip.min.js");
}

void CrossPointWebServer::handleNotFound() const {
  // CORS preflight: routes are registered per-method, so OPTIONS requests land
  // here. The Access-Control-Allow-* headers are added by enableCORS().
  if (server->method() == HTTP_OPTIONS) {
    server->send(204, "text/plain", "");
    return;
  }

  // in AP mode, redirect unmatched browser/captive-portal requests to "/" so the OS auto-opens the browser
  // API requests (/api/*) still return 404 so XHR errors surface correctly
  // see https://en.wikipedia.org/wiki/Captive_portal#Detection
  if (apMode && !server->uri().startsWith("/api/")) {
    server->sendHeader("Location", "/", true);
    server->send(302, "text/plain", "");
    return;
  }

  String message = "404 Not Found\n\n";
  message += "URI: " + server->uri() + "\n";
  server->send(404, "text/plain", message);
}

void CrossPointWebServer::handleStatus() const {
  // Get correct IP based on AP vs STA mode
  const String ipAddr = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();

  JsonDocument doc;
  doc["version"] = CROSSPOINT_VERSION;
  doc["ip"] = ipAddr;
  doc["mode"] = apMode ? "AP" : "STA";
  doc["rssi"] = apMode ? 0 : WiFi.RSSI();
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["uptime"] = millis() / 1000;
  // ?plugin=<name> adds a stable device ID for that plugin: sha256(secret ||
  // name). Not reversible to any hardware ID, and different per plugin.
  String plugin = server->arg("plugin");
  plugin.toLowerCase();  // FAT folder names ignore case
  uint8_t secret[32];
  if (!plugin.isEmpty() && plugin.length() <= 64 && deviceSecret(secret)) {
    uint8_t input[32 + 64];
    memcpy(input, secret, sizeof(secret));
    memcpy(input + sizeof(secret), plugin.c_str(), plugin.length());
    uint8_t hash[32];
    if (wc_Sha256Hash(input, sizeof(secret) + plugin.length(), hash) == 0) {
      char hex[65];
      for (size_t i = 0; i < sizeof(hash); i++) snprintf(hex + 2 * i, 3, "%02x", hash[i]);
      doc["deviceId"] = hex;
    }
  }
#if FREEINK_DEVICE_X4 || FREEINK_DEVICE_X3
  doc["device"] = gpio.deviceIsX3() ? "X3" : "X4";
#else
  doc["device"] = BoardConfig::ACTIVE.name;
#endif

  char snBuf[33] = {0};
  bool valid = false;
#if !CONFIG_IDF_TARGET_ESP32
  // Classic ESP32's efuse table has no USER_DATA block (C3/S3 only)
  if (esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA, snBuf, 256) == ESP_OK) {
    valid = snBuf[0] != '\0' && snBuf[0] != (char)0xFF;
    for (int i = 0; i < 32 && snBuf[i] != '\0'; i++) {
      if (!std::isprint(static_cast<unsigned char>(snBuf[i]))) {
        valid = false;
        break;
      }
    }
  }
#endif

  if (valid) {
    doc["serial"] = snBuf;
  } else {
    doc["serial"] = "Not found";
  }

  String response;
  serializeJson(doc, response);
  server->send(200, "application/json", response);
}

void CrossPointWebServer::scanFiles(const char* path, const std::function<void(FileInfo)>& callback) const {
  HalFile root = Storage.open(path);
  if (!root) {
    LOG_DBG("WEB", "Failed to open directory: %s", path);
    return;
  }

  if (!root.isDirectory()) {
    LOG_DBG("WEB", "Not a directory: %s", path);
    root.close();
    return;
  }

  LOG_DBG("WEB", "Scanning files in: %s", path);

  HalFile file = root.openNextFile();
  char name[500];
  while (file) {
    file.getName(name, sizeof(name));
    auto fileName = String(name);

    // Skip hidden items (starting with ".")
    bool shouldHide = !SETTINGS.showHiddenFiles && fileName.startsWith(".");

    // Check against explicitly hidden items list
    if (!shouldHide) {
      for (const auto* item : HIDDEN_ITEMS) {
        if (fileName.equals(item)) {
          shouldHide = true;
          break;
        }
      }
    }

    if (!shouldHide) {
      FileInfo info;
      info.name = fileName;
      info.isDirectory = file.isDirectory();

      if (info.isDirectory) {
        info.size = 0;
        info.isEpub = false;
      } else {
        info.size = file.size();
        info.isEpub = isEpubFile(info.name);
      }

      callback(info);
    }

    file.close();
    yield();                          // Yield to allow WiFi and other tasks to process during long scans
    resetTaskWatchdogIfSubscribed();  // Reset watchdog to prevent timeout on large directories
    file = root.openNextFile();
  }
  root.close();
}

bool CrossPointWebServer::isEpubFile(const String& filename) const { return FsHelpers::hasEpubExtension(filename); }

void CrossPointWebServer::handleFileList() const {
  sendStaticContent(server.get(), FilesPageHtml, sizeof(FilesPageHtml), FilesPageHtmlETag, "text/html");
}

void CrossPointWebServer::handleFileListData() const {
  // Get current path from query string (default to root)
  String currentPath = "/";
  if (server->hasArg("path")) {
    currentPath = normalizeWebPath(server->arg("path"));
  }

  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");
  char output[512];
  constexpr size_t outputSize = sizeof(output);
  bool seenFirst = false;
  JsonDocument doc;

  scanFiles(currentPath.c_str(), [this, &output, &doc, seenFirst](const FileInfo& info) mutable {
    doc.clear();
    doc["name"] = info.name;
    doc["size"] = info.size;
    doc["isDirectory"] = info.isDirectory;
    doc["isEpub"] = info.isEpub;

    const size_t written = serializeJson(doc, output, outputSize);
    if (written >= outputSize) {
      // JSON output truncated; skip this entry to avoid sending malformed JSON
      LOG_DBG("WEB", "Skipping file entry with oversized JSON for name: %s", info.name.c_str());
      return;
    }

    if (seenFirst) {
      server->sendContent(",");
    } else {
      seenFirst = true;
    }
    server->sendContent(output);
  });
  server->sendContent("]");
  // End of streamed response, empty chunk to signal client
  server->sendContent("");
  LOG_DBG("WEB", "Served file listing page for path: %s", currentPath.c_str());
}

void CrossPointWebServer::streamFileToClient(HalFile& file) const {
  NetworkClient client = server->client();
  static constexpr size_t CHUNK_SIZE = 4096;
  // Off the stack: the web-server task also runs TLS and SD from this stack.
  auto buffer = makeUniqueNoThrow<uint8_t[]>(CHUNK_SIZE);
  if (!buffer) {
    LOG_ERR("WEB", "OOM: %u byte stream buffer", (unsigned)CHUNK_SIZE);
    return;
  }

  bool ok = true;
  while (ok && file.available()) {
    const int result = file.read(buffer.get(), CHUNK_SIZE);
    if (result <= 0) break;
    const size_t bytesRead = static_cast<size_t>(result);
    size_t totalWritten = 0;
    while (totalWritten < bytesRead) {
      resetTaskWatchdogIfSubscribed();
      const size_t wrote = client.write(buffer.get() + totalWritten, bytesRead - totalWritten);
      if (wrote == 0) {
        ok = false;
        break;
      }
      totalWritten += wrote;
    }
  }
  client.clear();
}

void CrossPointWebServer::handleDownload() const {
  if (!server->hasArg("path")) {
    server->send(400, "text/plain", "Missing path");
    return;
  }

  String itemPath = normalizeWebPath(server->arg("path"));
  if (itemPath.isEmpty() || itemPath == "/") {
    server->send(400, "text/plain", "Invalid path");
    return;
  }

  const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);
  if (itemName.startsWith(".") || protectedpaths::isSensitivePath(itemPath.c_str())) {
    server->send(403, "text/plain", "Cannot access system files");
    return;
  }
  for (const auto* item : HIDDEN_ITEMS) {
    if (itemName.equals(item)) {
      server->send(403, "text/plain", "Cannot access protected items");
      return;
    }
  }

  if (!Storage.exists(itemPath.c_str())) {
    server->send(404, "text/plain", "Item not found");
    return;
  }

  HalFile file = Storage.open(itemPath.c_str());
  if (!file) {
    server->send(500, "text/plain", "Failed to open file");
    return;
  }
  if (file.isDirectory()) {
    file.close();
    server->send(400, "text/plain", "Path is a directory");
    return;
  }

  String contentType = "application/octet-stream";
  if (isEpubFile(itemPath)) {
    contentType = "application/epub+zip";
  }

  char nameBuf[128] = {0};
  String filename = "download";
  if (file.getName(nameBuf, sizeof(nameBuf))) {
    filename = nameBuf;
  }

  server->setContentLength(file.size());
  server->sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
  server->send(200, contentType.c_str(), "");
  streamFileToClient(file);
  file.close();
}

// Diagnostic counters for upload performance analysis
static unsigned long uploadStartTime = 0;
static unsigned long totalWriteTime = 0;
static size_t writeCount = 0;

static bool flushUploadBuffer(CrossPointWebServer::UploadState& state) {
  if (state.bufferPos > 0 && state.file) {
    resetTaskWatchdogIfSubscribed();  // Reset watchdog before potentially slow SD write
    const unsigned long writeStart = millis();
    const size_t written = state.file.write(state.buffer.get(), state.bufferPos);
    totalWriteTime += millis() - writeStart;
    writeCount++;
    resetTaskWatchdogIfSubscribed();  // Reset watchdog after SD write

    if (written != state.bufferPos) {
      LOG_DBG("WEB", "[UPLOAD] Buffer flush failed: expected %d, wrote %d", state.bufferPos, written);
      state.bufferPos = 0;
      return false;
    }
    state.bufferPos = 0;
  }
  return true;
}

// Drop a partially written upload so a truncated book never lands in the library.
// The file is new: uploads refuse to overwrite an existing name.
static void removeUploadedFile(const CrossPointWebServer::UploadState& state) {
  String filePath = state.path;
  if (!filePath.endsWith("/")) filePath += "/";
  filePath += state.fileName;
  Storage.remove(filePath.c_str());
}

void CrossPointWebServer::handleUpload(UploadState& state) const {
  static size_t lastLoggedSize = 0;

  // Reset watchdog at start of every upload callback - HTTP parsing can be slow
  resetTaskWatchdogIfSubscribed();

  // Safety check: ensure server is still valid
  if (!running || !server) {
    LOG_DBG("WEB", "[UPLOAD] ERROR: handleUpload called but server not running!");
    return;
  }

  const HTTPUpload& upload = server->upload();

  if (upload.status == UPLOAD_FILE_START) {
    // Reset watchdog - this is the critical 1% crash point
    resetTaskWatchdogIfSubscribed();

    state.fileName = upload.filename;
    state.size = 0;
    state.success = false;
    state.error = "";
    uploadStartTime = millis();
    lastLoggedSize = 0;
    state.bufferPos = 0;
    totalWriteTime = 0;
    writeCount = 0;
    state.buffer.reset();

    if (!FsHelpers::isSafePathComponent(state.fileName)) {
      state.error = "Invalid file name";
      LOG_DBG("WEB", "[UPLOAD] Rejected unsafe filename: %s", state.fileName.c_str());
      return;
    }

    // Get upload path from query parameter (defaults to root if not specified)
    // Note: We use query parameter instead of form data because multipart form
    // fields aren't available until after file upload completes
    if (server->hasArg("path")) {
      state.path = normalizeWebPath(server->arg("path"));
    } else {
      state.path = "/";
    }

    LOG_DBG("WEB", "[UPLOAD] START: %s to path: %s", state.fileName.c_str(), state.path.c_str());
    LOG_DBG("WEB", "[UPLOAD] Free heap: %d bytes", ESP.getFreeHeap());

    String filePath = state.path;
    if (!filePath.endsWith("/")) filePath += "/";
    filePath += state.fileName;
    if (protectedpaths::isSensitivePath(filePath.c_str())) {
      state.error = "Cannot write protected items";
      return;
    }

    // Check if file already exists - SD operations can be slow
    resetTaskWatchdogIfSubscribed();
    if (Storage.exists(filePath.c_str())) {
      state.error = "File already exists: " + state.fileName;
      LOG_DBG("WEB", "[UPLOAD] Collision: %s", filePath.c_str());
      return;
    }

    // The buffer spans upload callbacks; keep it off the task stack and release it at the end.
    state.buffer = makeUniqueNoThrow<uint8_t[]>(UploadState::UPLOAD_BUFFER_SIZE);
    if (!state.buffer) {
      state.error = tr(STR_MEMORY_ERROR);
      LOG_ERR("WEB", "OOM: upload buffer");
      return;
    }

    // Open file for writing - this can be slow due to FAT cluster allocation
    resetTaskWatchdogIfSubscribed();
    if (!Storage.openFileForWrite("WEB", filePath, state.file)) {
      state.error = "Failed to create file on SD card";
      LOG_DBG("WEB", "[UPLOAD] FAILED to create file: %s", filePath.c_str());
      state.buffer.reset();
      return;
    }
    resetTaskWatchdogIfSubscribed();

    LOG_DBG("WEB", "[UPLOAD] File created successfully: %s", filePath.c_str());
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (dropUploadIfCancelled()) return;
    if (state.file && state.error.isEmpty()) {
      // Buffer incoming data and flush when buffer is full
      // This reduces SD card write operations and improves throughput
      const uint8_t* data = upload.buf;
      size_t remaining = upload.currentSize;

      while (remaining > 0) {
        const size_t space = UploadState::UPLOAD_BUFFER_SIZE - state.bufferPos;
        const size_t toCopy = (remaining < space) ? remaining : space;

        memcpy(state.buffer.get() + state.bufferPos, data, toCopy);
        state.bufferPos += toCopy;
        data += toCopy;
        remaining -= toCopy;

        // Flush buffer when full
        if (state.bufferPos >= UploadState::UPLOAD_BUFFER_SIZE) {
          if (!flushUploadBuffer(state)) {
            state.error = "Failed to write to SD card - disk may be full";
            state.file.close();
            state.buffer.reset();
            removeUploadedFile(state);
            return;
          }
        }
      }

      state.size += upload.currentSize;

      // Log progress every 100KB
      if (state.size - lastLoggedSize >= 102400) {
        const unsigned long elapsed = millis() - uploadStartTime;
        const float kbps = (elapsed > 0) ? (state.size / 1024.0) / (elapsed / 1000.0) : 0;
        LOG_DBG("WEB", "[UPLOAD] %d bytes (%.1f KB), %.1f KB/s, %d writes", state.size, state.size / 1024.0, kbps,
                writeCount);
        lastLoggedSize = state.size;
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (!server->client().connected()) return;
    if (state.file) {
      // Flush any remaining buffered data
      if (!flushUploadBuffer(state)) {
        state.error = "Failed to write final data to SD card";
      }
      state.file.close();
      if (!state.error.isEmpty()) removeUploadedFile(state);

      if (state.error.isEmpty()) {
        state.success = true;
        const unsigned long elapsed = millis() - uploadStartTime;
        const float avgKbps = (elapsed > 0) ? (state.size / 1024.0) / (elapsed / 1000.0) : 0;
        const float writePercent = (elapsed > 0) ? (totalWriteTime * 100.0 / elapsed) : 0;
        LOG_DBG("WEB", "[UPLOAD] Complete: %s (%d bytes in %lu ms, avg %.1f KB/s)", state.fileName.c_str(), state.size,
                elapsed, avgKbps);
        LOG_DBG("WEB", "[UPLOAD] Diagnostics: %d writes, total write time: %lu ms (%.1f%%)", writeCount, totalWriteTime,
                writePercent);

        // Clear epub cache after uploading the file
        String filePath = state.path;
        if (!filePath.endsWith("/")) filePath += "/";
        filePath += state.fileName;
        clearBookCache(filePath.c_str());
        if (isLibraryBookFile(state.fileName)) library::markLibraryIndexDirty();
      }
    }
    state.buffer.reset();
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    state.bufferPos = 0;  // Discard buffered data
    state.buffer.reset();
    if (state.file) {
      state.file.close();
      removeUploadedFile(state);
    }
    state.error = "Upload aborted";
    LOG_DBG("WEB", "Upload aborted");
  }
}

void CrossPointWebServer::handleUploadPost(UploadState& state) const {
  if (state.success) {
    server->send(200, "text/plain", "File uploaded successfully: " + state.fileName);
  } else {
    const String error = state.error.isEmpty() ? "Unknown error during upload" : state.error;
    server->send(400, "text/plain", error);
  }
}

void CrossPointWebServer::handleCreateFolder() const {
  // Get folder name from form data
  if (!server->hasArg("name")) {
    server->send(400, "text/plain", "Missing folder name");
    return;
  }

  const String folderName = server->arg("name");

  // Validate folder name
  if (folderName.isEmpty()) {
    server->send(400, "text/plain", "Folder name cannot be empty");
    return;
  }
  if (!FsHelpers::isSafePathComponent(folderName)) {
    LOG_DBG("WEB", "Rejected unsafe folder name: %s", folderName.c_str());
    server->send(400, "text/plain", "Invalid folder name");
    return;
  }
  if (isProtectedItemName(folderName)) {
    LOG_DBG("WEB", "Rejected protected folder name: %s", folderName.c_str());
    server->send(403, "text/plain", "Cannot create protected item");
    return;
  }

  // Get parent path
  String parentPath = "/";
  if (server->hasArg("path")) {
    parentPath = normalizeWebPath(server->arg("path"));
  }

  // Build full folder path
  String folderPath = parentPath;
  if (!folderPath.endsWith("/")) folderPath += "/";
  folderPath += folderName;

  LOG_DBG("WEB", "Creating folder: %s", folderPath.c_str());

  // Check if already exists
  if (Storage.exists(folderPath.c_str())) {
    server->send(400, "text/plain", "Folder already exists");
    return;
  }

  // Create the folder
  if (Storage.mkdir(folderPath.c_str())) {
    LOG_DBG("WEB", "Folder created successfully: %s", folderPath.c_str());
    server->send(200, "text/plain", "Folder created: " + folderName);
  } else {
    LOG_DBG("WEB", "Failed to create folder: %s", folderPath.c_str());
    server->send(500, "text/plain", "Failed to create folder");
  }
}

void CrossPointWebServer::handleRename() const {
  if (!server->hasArg("path") || !server->hasArg("name")) {
    server->send(400, "text/plain", "Missing path or new name");
    return;
  }

  String itemPath = normalizeWebPath(server->arg("path"));
  String newName = server->arg("name");
  newName.trim();

  if (itemPath.isEmpty() || itemPath == "/") {
    server->send(400, "text/plain", "Invalid path");
    return;
  }
  if (newName.isEmpty()) {
    server->send(400, "text/plain", "New name cannot be empty");
    return;
  }
  if (newName.indexOf('/') >= 0 || newName.indexOf('\\') >= 0) {
    server->send(400, "text/plain", "Invalid file name");
    return;
  }
  if (isProtectedItemName(newName)) {
    server->send(403, "text/plain", "Cannot rename to protected name");
    return;
  }

  const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);
  if (isProtectedItemName(itemName)) {
    server->send(403, "text/plain", "Cannot rename protected item");
    return;
  }
  if (newName == itemName) {
    server->send(200, "text/plain", "Name unchanged");
    return;
  }

  if (!Storage.exists(itemPath.c_str())) {
    server->send(404, "text/plain", "Item not found");
    return;
  }

  HalFile file = Storage.open(itemPath.c_str());
  if (!file) {
    server->send(500, "text/plain", "Failed to open file");
    return;
  }
  if (file.isDirectory()) {
    file.close();
    server->send(400, "text/plain", "Only files can be renamed");
    return;
  }

  String parentPath = itemPath.substring(0, itemPath.lastIndexOf('/'));
  if (parentPath.isEmpty()) {
    parentPath = "/";
  }
  String newPath = parentPath;
  if (!newPath.endsWith("/")) {
    newPath += "/";
  }
  newPath += newName;
  if (protectedpaths::isSensitivePath(itemPath.c_str()) || protectedpaths::isSensitivePath(newPath.c_str())) {
    file.close();
    server->send(403, "text/plain", "Cannot move protected item");
    return;
  }

  if (Storage.exists(newPath.c_str())) {
    file.close();
    server->send(409, "text/plain", "Target already exists");
    return;
  }

  clearBookCache(itemPath.c_str());
  file.close();
  const bool success = ClippingStore::moveBook(itemPath.c_str(), newPath.c_str());

  if (success) {
    LOG_DBG("WEB", "Renamed file: %s -> %s", itemPath.c_str(), newPath.c_str());
    server->send(200, "text/plain", "Renamed successfully");
  } else {
    LOG_ERR("WEB", "Failed to rename file: %s -> %s", itemPath.c_str(), newPath.c_str());
    server->send(500, "text/plain", "Failed to rename file");
  }
}

void CrossPointWebServer::handleMove() const {
  if (!server->hasArg("path") || !server->hasArg("dest")) {
    server->send(400, "text/plain", "Missing path or destination");
    return;
  }

  String itemPath = normalizeWebPath(server->arg("path"));
  String destPath = normalizeWebPath(server->arg("dest"));

  if (itemPath.isEmpty() || itemPath == "/") {
    server->send(400, "text/plain", "Invalid path");
    return;
  }
  if (destPath.isEmpty()) {
    server->send(400, "text/plain", "Invalid destination");
    return;
  }

  const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);
  if (isProtectedItemName(itemName)) {
    server->send(403, "text/plain", "Cannot move protected item");
    return;
  }
  if (destPath != "/") {
    const String destName = destPath.substring(destPath.lastIndexOf('/') + 1);
    if (isProtectedItemName(destName)) {
      server->send(403, "text/plain", "Cannot move into protected folder");
      return;
    }
  }

  if (!Storage.exists(itemPath.c_str())) {
    server->send(404, "text/plain", "Item not found");
    return;
  }

  HalFile file = Storage.open(itemPath.c_str());
  if (!file) {
    server->send(500, "text/plain", "Failed to open file");
    return;
  }
  if (file.isDirectory()) {
    file.close();
    server->send(400, "text/plain", "Only files can be moved");
    return;
  }

  if (!Storage.exists(destPath.c_str())) {
    file.close();
    server->send(404, "text/plain", "Destination not found");
    return;
  }
  HalFile destDir = Storage.open(destPath.c_str());
  if (!destDir || !destDir.isDirectory()) {
    if (destDir) {
      destDir.close();
    }
    file.close();
    server->send(400, "text/plain", "Destination is not a folder");
    return;
  }
  destDir.close();

  String newPath = destPath;
  if (!newPath.endsWith("/")) {
    newPath += "/";
  }
  newPath += itemName;
  if (protectedpaths::isSensitivePath(itemPath.c_str()) || protectedpaths::isSensitivePath(newPath.c_str())) {
    file.close();
    server->send(403, "text/plain", "Cannot move protected item");
    return;
  }

  if (newPath == itemPath) {
    file.close();
    server->send(200, "text/plain", "Already in destination");
    return;
  }
  if (Storage.exists(newPath.c_str())) {
    file.close();
    server->send(409, "text/plain", "Target already exists");
    return;
  }

  clearBookCache(itemPath.c_str());
  file.close();
  const bool success = ClippingStore::moveBook(itemPath.c_str(), newPath.c_str());

  if (success) {
    LOG_DBG("WEB", "Moved file: %s -> %s", itemPath.c_str(), newPath.c_str());
    server->send(200, "text/plain", "Moved successfully");
  } else {
    LOG_ERR("WEB", "Failed to move file: %s -> %s", itemPath.c_str(), newPath.c_str());
    server->send(500, "text/plain", "Failed to move file");
  }
}

void CrossPointWebServer::handleDelete() const {
  // To ensure backwards compatibility, plain `path` is mapped
  // to a single element JSON array.
  bool hasPathArg = server->hasArg("path");
  bool hasPathsArg = server->hasArg("paths");
  // Check 'paths' or `path` argument is provided
  if (!(hasPathArg || hasPathsArg)) {
    server->send(400, "text/plain", "Missing `path` or `paths` argument");
    return;
  }
  if (hasPathArg && hasPathsArg) {
    server->send(400, "text/plain", "Provide either 'path' or 'paths', not both");
    return;
  }

  // Parse paths
  String pathsArg;
  JsonDocument doc;
  DeserializationError error = DeserializationError(DeserializationError::Code::Ok);
  if (hasPathsArg) {
    pathsArg = server->arg("paths");
    error = deserializeJson(doc, pathsArg);
  } else {
    pathsArg = server->arg("path");
    doc.add(pathsArg);
  }
  if (error) {
    server->send(400, "text/plain", "Invalid paths format");
    return;
  }

  auto paths = doc.as<JsonArray>();
  if (paths.isNull() || paths.size() == 0) {
    server->send(400, "text/plain", "No paths provided");
    return;
  }

  // Iterate over paths and delete each item
  bool allSuccess = true;
  String failedItems;

  for (const auto& p : paths) {
    auto itemPath = normalizeWebPath(p.as<String>());

    // Validate path
    if (itemPath.isEmpty() || itemPath == "/") {
      failedItems += itemPath + " (cannot delete root); ";
      allSuccess = false;
      continue;
    }

    // Security check: prevent deletion of protected items
    const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);

    // Hidden/system files and credential stores are protected
    if (itemName.startsWith(".") || protectedpaths::isSensitivePath(itemPath.c_str())) {
      failedItems += itemPath + " (hidden/system file); ";
      allSuccess = false;
      continue;
    }

    // Check against explicitly protected items
    bool isProtected = false;
    for (const auto* item : HIDDEN_ITEMS) {
      if (itemName.equals(item)) {
        isProtected = true;
        break;
      }
    }
    if (isProtected) {
      failedItems += itemPath + " (protected file); ";
      allSuccess = false;
      continue;
    }

    // Check if item exists
    if (!Storage.exists(itemPath.c_str())) {
      failedItems += itemPath + " (not found); ";
      allSuccess = false;
      continue;
    }

    // Decide whether it's a directory or file by opening it
    bool success = false;
    HalFile f = Storage.open(itemPath.c_str());
    if (f && f.isDirectory()) {
      // For folders, ensure empty before removing
      HalFile entry = f.openNextFile();
      if (entry) {
        entry.close();
        f.close();
        failedItems += itemPath + " (folder not empty); ";
        allSuccess = false;
        continue;
      }
      f.close();
      success = Storage.rmdir(itemPath.c_str());
    } else {
      // It's a file (or couldn't open as dir) — remove file
      if (f) f.close();
      success = removeBookFile(itemPath.c_str());
    }

    if (!success) {
      failedItems += itemPath + " (deletion failed); ";
      allSuccess = false;
    }
  }

  if (allSuccess) {
    server->send(200, "text/plain", "All items deleted successfully");
  } else {
    server->send(500, "text/plain", "Failed to delete some items: " + failedItems);
  }
}

void CrossPointWebServer::handleSettingsPage() const {
  sendStaticContent(server.get(), SettingsPageHtml, sizeof(SettingsPageHtml), SettingsPageHtmlETag, "text/html");
  LOG_DBG("WEB", "Served settings page");
}

void CrossPointWebServer::handleGetSettings() const {
  // Pass the SD font registry so the fontFamily setting's enumStringValues
  // includes SD-resident families — otherwise the web API only exposes the
  // three built-in fonts.
  std::vector<DictionaryEntry> dictionaries;
  DictionaryRegistry::discover(dictionaries);
  const auto& settings = getSettingsList(&sdFontSystem.registry(), &dictionaries);

  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");

  char output[512];
  constexpr size_t outputSize = sizeof(output);
  bool seenFirst = false;
  JsonDocument doc;

  for (const auto& s : settings) {
    if (!s.key) continue;  // Skip ACTION-only entries

    doc.clear();
    doc["key"] = s.key;
    doc["name"] = I18N.get(s.nameId);
    doc["category"] = I18N.get(s.category);

    switch (s.type) {
      case SettingType::TOGGLE: {
        doc["type"] = "toggle";
        if (s.valuePtr) {
          doc["value"] = static_cast<int>(SETTINGS.*(s.valuePtr));
        }
        break;
      }
      case SettingType::ENUM: {
        doc["type"] = "enum";
        if (s.valuePtr) {
          doc["value"] = static_cast<int>(SETTINGS.*(s.valuePtr));
        } else if (s.valueGetter) {
          doc["value"] = static_cast<int>(s.valueGetter());
        }
        JsonArray options = doc["options"].to<JsonArray>();
        if (!s.enumStringValues.empty()) {
          for (const auto& opt : s.enumStringValues) {
            options.add(opt);
          }
        } else {
          for (const auto& opt : s.enumLabels()) {
            options.add(I18N.get(opt));
          }
        }
        break;
      }
      case SettingType::VALUE: {
        doc["type"] = "value";
        if (s.valuePtr) {
          doc["value"] = static_cast<int>(SETTINGS.*(s.valuePtr));
        }
        doc["min"] = s.valueRange.min;
        doc["max"] = s.valueRange.max;
        doc["step"] = s.valueRange.step;
        break;
      }
      case SettingType::STRING: {
        doc["type"] = "string";
        if (s.obfuscated) {
          // Device-bound credentials may contain invalid text after an SD card move.
          doc["value"] = nullptr;
          doc["hasPassword"] =
              s.stringGetter
                  ? !s.stringGetter().empty()
                  : s.stringMaxLen > 0 && *(reinterpret_cast<const char*>(&SETTINGS) + s.stringOffset) != '\0';
        } else if (s.stringGetter) {
          doc["value"] = s.stringGetter();
        } else if (s.stringMaxLen > 0) {
          doc["value"] = reinterpret_cast<const char*>(&SETTINGS) + s.stringOffset;
        }
        break;
      }
      default:
        continue;
    }

    const size_t written = serializeJson(doc, output, outputSize);
    if (written >= outputSize) {
      LOG_DBG("WEB", "Skipping oversized setting JSON for: %s", s.key);
      continue;
    }

    if (seenFirst) {
      server->sendContent(",");
    } else {
      seenFirst = true;
    }
    server->sendContent(output);
    yield();                          // Yield to allow WiFi and other tasks to process during a slow send
    resetTaskWatchdogIfSubscribed();  // Reset watchdog: each sendContent() is a blocking network write
  }

  server->sendContent("]");
  server->sendContent("");
  LOG_DBG("WEB", "Served settings API");
}

void CrossPointWebServer::handlePostSettings() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }

  std::vector<DictionaryEntry> dictionaries;
  DictionaryRegistry::discover(dictionaries);
  const auto& settings = getSettingsList(&sdFontSystem.registry(), &dictionaries);
  int applied = 0;

  for (const auto& s : settings) {
    if (!s.key) continue;
    if (!doc[s.key].is<JsonVariant>()) continue;
    if (isLocalOnlySetting(s.key)) {
      // Not merely ignored -- said out loud, because a silent no-op on a
      // security-relevant write is indistinguishable from it having worked.
      LOG_ERR("WEB", "refused a network write to local-only setting '%s'", s.key);
      continue;
    }

    switch (s.type) {
      case SettingType::TOGGLE: {
        const int val = doc[s.key].as<int>() ? 1 : 0;
        if (s.valuePtr) {
          SETTINGS.*(s.valuePtr) = val;
        }
        applied++;
        break;
      }
      case SettingType::ENUM: {
        const int val = doc[s.key].as<int>();
        const int maxVal = s.enumStringValues.empty() ? static_cast<int>(s.enumLabels().size())
                                                      : static_cast<int>(s.enumStringValues.size());
        if (val >= 0 && val < maxVal) {
          if (s.valuePtr) {
            SETTINGS.*(s.valuePtr) = static_cast<uint8_t>(val);
          } else if (s.valueSetter) {
            s.valueSetter(static_cast<uint8_t>(val));
          }
          applied++;
        }
        break;
      }
      case SettingType::VALUE: {
        const int val = doc[s.key].as<int>();
        if (val >= s.valueRange.min && val <= s.valueRange.max) {
          if (s.valuePtr) {
            SETTINGS.*(s.valuePtr) = static_cast<uint8_t>(val);
          }
          applied++;
        }
        break;
      }
      case SettingType::STRING: {
        const std::string val = doc[s.key].as<std::string>();
        if (s.stringSetter) {
          s.stringSetter(val);
        } else if (s.stringMaxLen > 0) {
          char* ptr = reinterpret_cast<char*>(&SETTINGS) + s.stringOffset;
          strncpy(ptr, val.c_str(), s.stringMaxLen - 1);
          ptr[s.stringMaxLen - 1] = '\0';
        }
        applied++;
        break;
      }
      default:
        break;
    }
  }

  SETTINGS.saveToFile();

  LOG_DBG("WEB", "Applied %d setting(s)", applied);
  server->send(200, "text/plain", String("Applied ") + String(applied) + " setting(s)");
}

// ---- OPDS Server API ----

void CrossPointWebServer::handleGetOpdsServers() const {
  const auto& servers = OPDS_STORE.getServers();

  // Stream JSON array incrementally to avoid allocating the full response in memory
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");

  char output[512];
  constexpr size_t outputSize = sizeof(output);
  JsonDocument doc;

  for (size_t i = 0; i < servers.size(); i++) {
    doc.clear();
    doc["index"] = i;
    doc["name"] = servers[i].name;
    doc["url"] = servers[i].url;
    doc["username"] = servers[i].username;
    // Never expose passwords over the API — only indicate whether one is set
    doc["hasPassword"] = !servers[i].password.empty();

    const size_t written = serializeJson(doc, output, outputSize);
    if (written >= outputSize) continue;

    if (i > 0) server->sendContent(",");
    server->sendContent(output);
    yield();                          // Yield to allow WiFi and other tasks to process during a slow send
    resetTaskWatchdogIfSubscribed();  // Reset watchdog: each sendContent() is a blocking network write
  }

  server->sendContent("]");
  server->sendContent("");
  LOG_DBG("WEB", "Served OPDS servers API (%zu servers)", servers.size());
}

void CrossPointWebServer::handlePostOpdsServer() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }

  OpdsServer opdsServer;
  opdsServer.name = doc["name"] | std::string("");
  opdsServer.url = doc["url"] | std::string("");
  opdsServer.username = doc["username"] | std::string("");

  // The password field is optional in the JSON payload. When absent (vs. present but empty),
  // we preserve the existing password — the web UI omits it when the user hasn't changed it.
  bool hasPasswordField = doc["password"].is<const char*>() || doc["password"].is<std::string>();
  std::string password = doc["password"] | std::string("");

  if (doc["index"].is<int>()) {
    int idx = doc["index"].as<int>();
    if (idx < 0 || idx >= static_cast<int>(OPDS_STORE.getCount())) {
      server->send(400, "text/plain", "Invalid server index");
      return;
    }
    // Preserve existing password if not explicitly provided
    if (!hasPasswordField) {
      const auto* existing = OPDS_STORE.getServer(static_cast<size_t>(idx));
      if (existing) password = existing->password;
    }
    opdsServer.password = password;
    OPDS_STORE.updateServer(static_cast<size_t>(idx), opdsServer);
    LOG_DBG("WEB", "Updated OPDS server at index %d", idx);
  } else {
    opdsServer.password = password;
    if (!OPDS_STORE.addServer(opdsServer)) {
      server->send(400, "text/plain", "Cannot add server (limit reached)");
      return;
    }
    LOG_DBG("WEB", "Added new OPDS server: %s", opdsServer.name.c_str());
  }

  server->send(200, "text/plain", "OK");
}

// Uses POST (not HTTP DELETE) because ESP32 WebServer doesn't support DELETE with body.
void CrossPointWebServer::handleDeleteOpdsServer() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }

  if (!doc["index"].is<int>()) {
    server->send(400, "text/plain", "Missing index");
    return;
  }

  int idx = doc["index"].as<int>();
  if (idx < 0 || idx >= static_cast<int>(OPDS_STORE.getCount())) {
    server->send(400, "text/plain", "Invalid server index");
    return;
  }

  OPDS_STORE.removeServer(static_cast<size_t>(idx));
  LOG_DBG("WEB", "Deleted OPDS server at index %d", idx);
  server->send(200, "text/plain", "OK");
}

// ---- Wi-Fi Credentials API ----

void CrossPointWebServer::handleGetWifiNetworks() const {
  const auto credentials = WIFI_STORE.getCredentialSummaries();

  // Stream JSON array incrementally to avoid allocating the full response in memory
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");

  char output[320];
  constexpr size_t outputSize = sizeof(output);
  JsonDocument doc;

  for (size_t i = 0; i < credentials.size(); i++) {
    doc.clear();
    doc["index"] = i;
    doc["ssid"] = credentials[i].ssid;
    // Never expose Wi-Fi passwords over the API — only indicate whether one is set
    doc["hasPassword"] = credentials[i].hasPassword;
    doc["isLastConnected"] = credentials[i].isLastConnected;

    const size_t written = serializeJson(doc, output, outputSize);
    if (written >= outputSize) continue;

    if (i > 0) server->sendContent(",");
    server->sendContent(output);
    yield();                          // Yield to allow WiFi and other tasks to process during a slow send
    resetTaskWatchdogIfSubscribed();  // Reset watchdog: each sendContent() is a blocking network write
  }

  server->sendContent("]");
  server->sendContent("");
  LOG_DBG("WEB", "Served Wi-Fi credentials API (%zu network(s))", credentials.size());
}

void CrossPointWebServer::handlePostWifiNetwork() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }

  std::string ssid = doc["ssid"] | std::string("");
  if (ssid.empty()) {
    server->send(400, "text/plain", "SSID is required");
    return;
  }

  // The password field is optional in the JSON payload. When absent (vs. present but empty),
  // preserve the existing password for updates. Empty passwords are valid for open networks.
  bool hasPasswordField = doc["password"].is<const char*>() || doc["password"].is<std::string>();
  std::string password = doc["password"] | std::string("");

  if (doc["index"].is<int>()) {
    int idx = doc["index"].as<int>();
    if (idx < 0) {
      server->send(400, "text/plain", "Invalid network index");
      return;
    }
    const auto credential = WIFI_STORE.getCredentialAt(static_cast<size_t>(idx));
    if (!credential) {
      server->send(400, "text/plain", "Invalid network index");
      return;
    }

    const std::string oldSsid = credential->ssid;
    if (!hasPasswordField) {
      password = credential->password;
    }

    bool ok = true;
    if (oldSsid != ssid) {
      ok = WIFI_STORE.removeCredential(oldSsid) && WIFI_STORE.addCredential(ssid, password);
    } else {
      ok = WIFI_STORE.addCredential(ssid, password);
    }

    if (!ok) {
      server->send(400, "text/plain", "Failed to update Wi-Fi network");
      return;
    }

    LOG_DBG("WEB", "Updated Wi-Fi network at index %d (SSID: %s)", idx, ssid.c_str());
  } else {
    if (!WIFI_STORE.addCredential(ssid, password)) {
      server->send(400, "text/plain", "Cannot add network (limit reached)");
      return;
    }
    LOG_DBG("WEB", "Added Wi-Fi network: %s", ssid.c_str());
  }

  server->send(200, "text/plain", "OK");
}

// Uses POST (not HTTP DELETE) because ESP32 WebServer doesn't support DELETE with body.
void CrossPointWebServer::handleDeleteWifiNetwork() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }

  if (!doc["index"].is<int>()) {
    server->send(400, "text/plain", "Missing index");
    return;
  }

  int idx = doc["index"].as<int>();
  if (idx < 0) {
    server->send(400, "text/plain", "Invalid network index");
    return;
  }
  const auto ssid = WIFI_STORE.getSsidAt(static_cast<size_t>(idx));
  if (!ssid) {
    server->send(400, "text/plain", "Invalid network index");
    return;
  }

  if (!WIFI_STORE.removeCredential(*ssid)) {
    server->send(400, "text/plain", "Failed to delete Wi-Fi network");
    return;
  }

  LOG_DBG("WEB", "Deleted Wi-Fi network at index %d (SSID: %s)", idx, ssid->c_str());
  server->send(200, "text/plain", "OK");
}

// ---------------------------------------------------------------------------
// Browser-side plugins (JS on the SD card)
// ---------------------------------------------------------------------------

namespace {

// A path component is safe if it has no separators or parent refs.
bool safeComponent(const String& s) {
  return !s.isEmpty() && s.indexOf('/') < 0 && s.indexOf('\\') < 0 && s.indexOf("..") < 0;
}

const char* pluginContentType(const String& file) {
  if (file.endsWith(".js")) return "application/javascript";
  if (file.endsWith(".css")) return "text/css";
  if (file.endsWith(".html")) return "text/html";
  if (file.endsWith(".json")) return "application/json";
  if (file.endsWith(".svg")) return "image/svg+xml";
  return "application/octet-stream";
}

}  // namespace

bool CrossPointWebServer::readJsonBody(JsonDocument& out) const {
  if (!server->hasArg("plain")) {
    server->send(400, "application/json", "{\"error\":\"missing body\"}");
    return false;
  }
  if (deserializeJson(out, server->arg("plain")) != DeserializationError::Ok) {
    server->send(400, "application/json", "{\"error\":\"bad json\"}");
    return false;
  }
  return true;
}

void CrossPointWebServer::sendJson(const JsonDocument& doc) const {
  String out;
  if (!out.reserve(measureJson(doc))) {
    LOG_ERR("WEB", "OOM: JSON response");
    server->send(503, "application/json", "{\"error\":\"out of memory\"}");
    return;
  }
  serializeJson(doc, out);
  server->send(200, "application/json", out);
}

// GET /api/plugins -> [{ "name", "title", "mount" }, ...]. Only plugins with a
// plugin.js are listed (the page loads it); optional manifest.json supplies the
// title and mount point.
void CrossPointWebServer::handlePluginList() const {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();

  for (const auto& e : PluginLocations::scanPlugins()) {
    if (!e.hasPluginJs) continue;
    JsonObject obj = arr.add<JsonObject>();
    obj["name"] = e.name;
    obj["dir"] = e.dir;         // the plugin keeps its own files here
    obj["title"] = e.name;      // overridden by manifest below
    obj["mount"] = "settings";  // default mount point
    std::string manifest;
    if (e.hasManifest && Storage.readFileToString("WEB", e.dir + "/manifest.json", 64 * 1024, manifest)) {
      JsonDocument m;
      if (deserializeJson(m, manifest) == DeserializationError::Ok) {
        if (m["title"].is<const char*>()) obj["title"] = m["title"];
        if (m["mount"].is<const char*>()) obj["mount"] = m["mount"];
      }
    }
  }

  sendJson(doc);
}

// GET /plugin?name=<plugin>&file=<file> -> serve /.crosspoint/plugins/<plugin>/<file>
void CrossPointWebServer::handlePluginFile() const {
  const String name = server->arg("name");
  const String file = server->arg("file");
  if (!safeComponent(name) || !safeComponent(file)) {
    server->send(400, "text/plain", "bad plugin path");
    return;
  }
  const std::string pluginDir = PluginLocations::findPluginDir(name.c_str());
  if (pluginDir.empty()) {
    server->send(404, "text/plain", "not found");
    return;
  }
  const std::string path = pluginDir + "/" + file.c_str();
  HalFile f = Storage.open(path.c_str(), O_RDONLY);
  if (!f || !f.isOpen() || f.isDirectory()) {
    server->send(404, "text/plain", "not found");
    return;
  }

  server->setContentLength(f.size());
  server->send(200, pluginContentType(file), "");
  streamFileToClient(f);
}

// POST /api/relay {plugin, method, url, headers:{}, body}
//   -> 200 with the upstream body raw, its status in X-Relay-Status and its
//      headers in X-Relay-Headers ([[name, value], ...] JSON, duplicates kept)
// Lets a plugin make an outbound HTTP(S) call the browser can't (CORS): the
// device makes it via SecureNet. Sending the body raw avoids escaping it into
// JSON on the device; PluginHost.relay() rebuilds {status, headers, body}.
void CrossPointWebServer::handleRelay() {
  JsonDocument req;
  if (!readJsonBody(req)) return;
  const String plugin = req["plugin"] | "";
  const std::string url = req["url"] | "";
  const std::string method = req["method"] | "GET";
  if (!safeComponent(plugin) || url.empty()) {
    server->send(400, "application/json", "{\"error\":\"missing plugin/url\"}");
    return;
  }
  pluginhttp::Headers headers;
  pluginhttp::readHeaders(req["headers"], headers);
  const std::string body = req["body"] | "";
  // All values needed below now have independent storage. Drop both copies of
  // the inbound JSON before wolfSSL allocates its handshake working set.
  req.clear();
  req.shrinkToFit();
  releaseRequestArguments(server.get());

  suspendTransferServices();
  ScopedCleanup resumeServices{[this] { resumeTransferServices(); }};
  LOG_DBG("WEB", "Relay TLS start: heap %u, max block %u: %s", (unsigned)ESP.getFreeHeap(),
          (unsigned)ESP.getMaxAllocHeap(), url.c_str());

  // One bounded body buffer; larger payloads use /api/fetch. This task is
  // subscribed to the task WDT for the whole web-server session, so feed it
  // while a slow peer keeps the request waiting.
  static constexpr size_t RELAY_BODY_LIMIT = 32 * 1024;
  String respBody;
  pluginhttp::Headers respHeaders;
  const int status =
      pluginhttp::request(nullptr, url, method, body, headers, respBody, RELAY_BODY_LIMIT, &respHeaders, [] {
        resetTaskWatchdogIfSubscribed();
        return false;  // never aborts; only feeds
      });
  // Transport failure, a truncated body, or one over the cap / out of memory
  // (the reason is logged by pluginhttp).
  if (status < 0) {
    server->send(502, "application/json", "{\"error\":\"relay failed; large bodies need /api/fetch\"}");
    return;
  }

  JsonDocument headersDoc;
  JsonArray headerArray = headersDoc.to<JsonArray>();
  for (const auto& h : respHeaders) {
    JsonArray pair = headerArray.add<JsonArray>();
    pair.add(h.first);
    pair.add(h.second);
  }
  String headersJson;
  serializeJson(headersDoc, headersJson);
  server->sendHeader("X-Relay-Status", String(status));
  server->sendHeader("X-Relay-Headers", headersJson);
  server->send(200, "application/octet-stream", respBody);
}

namespace {}  // namespace

// POST /api/crypto {op, ...base64 fields...} -> {data|public|private|key|cert, ...}
// Generic wolfSSL primitives (hash, random, AES, RSA, PKCS#12) a plugin can use.
// Stateless; keys are base64 in the request/reply.
void CrossPointWebServer::handleCrypto() {
  using namespace freeink::content;
  JsonDocument req;
  if (!readJsonBody(req)) return;
  const std::string_view op = req["op"] | "";
  const auto sendOom = [this](const char* operation) {
    LOG_ERR("WEB", "OOM: crypto %s", operation);
    server->send(503, "application/json", "{\"error\":\"out of memory\"}");
  };
  struct Bytes {
    std::unique_ptr<uint8_t[]> data;
    size_t size = 0;
    // Never null, so an empty field is still a valid zero-length input.
    const uint8_t* ptr() const {
      static constexpr uint8_t EMPTY = 0;
      return data ? data.get() : &EMPTY;
    }
  };
  // Nothrow decode of a base64 field: a bundle-sized input under heap pressure
  // must fail the op, not abort() the device (-fno-exceptions). The cap keeps
  // a LAN client from posting a multi-megabyte value; crypto inputs (keys,
  // certs, PKCS#12 bundles) are a few KB.
  bool decodeOom = false;
  auto dec = [&](const char* field) {
    static constexpr size_t MAX_CRYPTO_FIELD = 64 * 1024;
    Bytes out;
    const char* v = req[field].as<const char*>();
    const size_t encodedLen = v ? strlen(v) : 0;
    if (encodedLen == 0 || encodedLen > MAX_CRYPTO_FIELD) return out;
    const size_t cap = encodedLen * 3 / 4 + 3;
    out.data = makeUniqueNoThrow<uint8_t[]>(cap);
    if (!out.data) decodeOom = true;
    const int32_t n = out.data ? base64Decode(v, encodedLen, out.data.get(), cap) : -1;
    out.size = n < 0 ? 0 : static_cast<size_t>(n);
    return out;
  };

  JsonDocument resp;
  bool encodeFailed = false;
  const auto setEncoded = [&](const char* name, const uint8_t* data, size_t size) {
    const uint8_t empty = 0;
    if (size == 0) data = &empty;
    const String encoded = base64::encode(data, size);
    if ((size && encoded.isEmpty()) || encoded == "-FAIL-") {
      encodeFailed = true;
      return;
    }
    resp[name] = encoded;
  };
  const auto setEncodedVector = [&](const char* name, const std::vector<uint8_t>& data) {
    setEncoded(name, data.data(), data.size());
  };

  WolfsslCrypto c;

  if (op == "random") {
    static constexpr int MAX_RANDOM_BYTES = 4096;  // generous for keys/salts/tokens; blocks a runaway allocation
    const int n = std::clamp(static_cast<int>(req["len"] | 16), 0, MAX_RANDOM_BYTES);
    auto out = makeUniqueNoThrow<uint8_t[]>(n);
    if (n && !out) {
      sendOom("random output");
      return;
    }
    if (n) c.randomBytes(out.get(), n);
    const uint8_t empty = 0;
    setEncoded("data", n ? out.get() : &empty, n);
  } else if (op == "sha1") {
    const Bytes d = dec("data");
    if (decodeOom) return sendOom("input");
    uint8_t h[20];
    c.sha1(d.ptr(), d.size, h);
    setEncoded("data", h, 20);
  } else if (op == "aesenc" || op == "aesdec") {
    const Bytes k = dec("key"), iv = dec("iv"), d = dec("data");
    if (decodeOom) return sendOom("input");
    if (k.size != 16 || iv.size != 16) {
      resp["error"] = "key/iv must be 16 bytes";
    } else if (op == "aesenc") {
      const size_t outSize = ((d.size / 16) + 1) * 16;
      auto out = makeUniqueNoThrow<uint8_t[]>(outSize);
      // Pad inside the output buffer; the SDK helper allocates a second copy.
      auto aes = makeUniqueNoThrow<Aes>();
      if (!out || !aes) return sendOom("AES output");
      memcpy(out.get(), d.ptr(), d.size);
      memset(out.get() + d.size, static_cast<int>(outSize - d.size), outSize - d.size);
      if (wc_AesSetKey(aes.get(), k.ptr(), 16, iv.ptr(), AES_ENCRYPTION) == 0 &&
          wc_AesCbcEncrypt(aes.get(), out.get(), out.get(), outSize) == 0)
        setEncoded("data", out.get(), outSize);
      else
        resp["error"] = "aesenc failed";
    } else if (d.size % 16 != 0) {
      resp["error"] = "data not block-aligned";
    } else {
      auto out = makeUniqueNoThrow<uint8_t[]>(d.size);
      if (d.size && !out) {
        return sendOom("AES output");
      }
      uint8_t empty = 0;
      if (c.aes128CbcDecrypt(k.ptr(), iv.ptr(), d.ptr(), d.size, d.size ? out.get() : &empty))
        setEncoded("data", d.size ? out.get() : &empty, d.size);
      else
        resp["error"] = "aesdec failed";
    }
  } else if (op == "sha256") {
    const Bytes d = dec("data");
    if (decodeOom) return sendOom("input");
    uint8_t h[32];
    c.sha256(d.ptr(), d.size, h);
    setEncoded("data", h, 32);
  } else if (op == "rsadec") {
    // Raw private-key operation with a caller-supplied PKCS#8 key; the caller
    // removes the padding.
    const Bytes priv = dec("private"), d = dec("data");
    if (decodeOom) return sendOom("input");
    static constexpr size_t MAX_MODULUS = 512;
    auto out = makeUniqueNoThrow<uint8_t[]>(MAX_MODULUS);
    if (!out) return sendOom("RSA output");
    const int32_t n = c.rsaPrivateRaw(priv.ptr(), priv.size, d.ptr(), d.size, out.get(), MAX_MODULUS);
    if (n > 0)
      setEncoded("data", out.get(), static_cast<size_t>(n));
    else
      resp["error"] = "rsadec failed";
  } else if (op == "keygen") {
    RsaKeyPairDer kp;
    if (c.rsaGenerate(&kp)) {
      setEncodedVector("public", kp.spki);
      setEncodedVector("private", kp.pkcs8);
    } else {
      resp["error"] = "keygen failed: " + c.lastError;
    }
  } else if (op == "pubencrypt") {
    const Bytes cert = dec("cert"), d = dec("data");
    if (decodeOom) return sendOom("input");
    auto out = makeUniqueNoThrow<uint8_t[]>(512);  // off the stack; RSA output up to 4096-bit
    if (!out) {
      return sendOom("RSA output");
    }
    size_t olen = 0;
    if (c.rsaPublicEncrypt(cert.ptr(), cert.size, d.ptr(), d.size, out.get(), 512, &olen))
      setEncoded("data", out.get(), olen);
    else
      resp["error"] = "pubencrypt failed: " + c.lastError + " (cert " + std::to_string(cert.size) + "B, data " +
                      std::to_string(d.size) + "B)";
  } else if (op == "sign") {
    const Bytes priv = dec("private"), h = dec("hash");
    if (decodeOom) return sendOom("input");
    uint8_t sig[128];
    if (h.size != 20)
      resp["error"] = "hash must be 20 bytes";
    else if (c.rsaPrivateSignRaw(priv.ptr(), priv.size, h.ptr(), sig))
      setEncoded("data", sig, 128);
    else
      resp["error"] = "sign failed";
  } else if (op == "pkcs12") {
    const Bytes p12 = dec("data");
    if (decodeOom) return sendOom("input");
    const std::string pw = req["password"] | "";
    // The decoded bundle no longer depends on the request document. Reclaim
    // its large base64 string before the KDF and certificate parsing begin.
    req.clear();
    std::vector<uint8_t> key, cert;
    if (p12.size == 0) {
      resp["error"] = "pkcs12 failed: bundle missing, invalid, or out of memory";
    } else if (c.pkcs12Extract(p12.ptr(), p12.size, pw, &key, &cert)) {
      setEncodedVector("key", key);
      setEncodedVector("cert", cert);
    } else {
      resp["error"] = "pkcs12 failed: " + c.lastError;
    }
  } else {
    resp["error"] = "unknown op";
  }

  if (encodeFailed || resp.overflowed()) {
    return sendOom("response");
  }
  sendJson(resp);
}

// POST /api/fetch {plugin, url, dest, headers?, offset?, maxBytes?}
//   -> {status, bytes, complete, total?}
// Device downloads a URL straight to SD, so a large body never passes through
// the browser.
void CrossPointWebServer::handleFetch() {
  JsonDocument req;
  if (!readJsonBody(req)) return;
  const std::string url = req["url"] | "";
  const std::string dest = req["dest"] | "";
  const size_t requestedOffset = req["offset"] | 0;
  size_t segmentLimit = req["maxBytes"] | 0;
  static constexpr size_t FETCH_MAX_SEGMENT_SIZE = 4 * 1024 * 1024;
  if (segmentLimit > FETCH_MAX_SEGMENT_SIZE) segmentLimit = FETCH_MAX_SEGMENT_SIZE;
  if (url.empty() || !protectedpaths::isPluginPath(dest)) {
    server->send(400, "application/json", "{\"error\":\"bad url/dest\"}");
    return;
  }

  pluginhttp::Headers requestHeaders;
  pluginhttp::readHeaders(req["headers"], requestHeaders);
  req.clear();
  req.shrinkToFit();
  releaseRequestArguments(server.get());

  // Stage in <dest>.part so an interrupted or abandoned download never sits
  // under the real name, and an existing dest survives until the new copy is complete.
  const std::string part = dest + ".part";
  HalFile file;
  if (requestedOffset == 0) {
    // Mirror handlePluginFs(): create missing parents so a plugin's first fetch
    // into a fresh subfolder (e.g. /.crosspoint/plugins/<name>/) doesn't fail
    // before anything has a chance to create it.
    const size_t lastSlash = dest.rfind('/');
    if (lastSlash != std::string::npos && lastSlash > 0) {
      Storage.ensureDirectoryExists(dest.substr(0, lastSlash).c_str());
    }
    Storage.remove(part.c_str());
    if (!Storage.openFileForWrite("PLG", part, file)) {
      server->send(500, "application/json", "{\"error\":\"cannot create file\"}");
      return;
    }
  } else {
    file = Storage.open(part.c_str(), O_RDWR | O_AT_END);
    const size_t existingSize = file ? file.size() : 0;
    if (!file || existingSize != requestedOffset) {
      if (file) file.close();
      char msg[96];
      snprintf(msg, sizeof(msg), "{\"error\":\"offset mismatch\",\"bytes\":%u}", (unsigned)existingSize);
      server->send(409, "application/json", msg);
      return;
    }
  }

  // Resume and Range-restart handling live in fetchResumable (ResumableFetch.h).
  suspendTransferServices();
  ScopedCleanup resumeServices{[this] { resumeTransferServices(); }};
  WifiPowerSaveGuard psGuard;

  size_t written = requestedOffset;
  size_t nextHeapLog = written;
  bool sdFull = false;
  bool segmentBoundary = false;
  bool rangeUnsupported = false;
  const unsigned long fetchStartedAt = millis();
  unsigned long lastBrowserHeartbeat = fetchStartedAt;
  bool browserResponseStarted = false;

  // A phone may discard an HTTP response that sends no bytes for several
  // minutes even while the device is actively downloading upstream. Start a
  // chunked JSON response only once the operation becomes long-running, then
  // send JSON whitespace to keep that browser-facing connection active.
  const auto keepBrowserAlive = [this, &browserResponseStarted, &lastBrowserHeartbeat]() {
    const unsigned long now = millis();
    if (now - lastBrowserHeartbeat < 5000) return;
    lastBrowserHeartbeat = now;
    if (!server->client().connected()) return;
    if (!browserResponseStarted) {
      server->setContentLength(CONTENT_LENGTH_UNKNOWN);
      server->send(200, "application/json", "");
      browserResponseStarted = true;
    }
    server->sendContent(" \n", 2);
  };
  const auto sendFetchResult = [this, &browserResponseStarted](int code, const String& payload) {
    if (!browserResponseStarted) {
      server->send(code, "application/json", payload);
      return;
    }
    if (server->client().connected()) {
      server->sendContent(payload);
      server->sendContent("", 0);
    }
  };

  freeink::FetchOptions options;
  options.startOffset = requestedOffset;
  freeink::FetchSink sink;
  sink.write = [&](const uint8_t* data, size_t len) {
    resetTaskWatchdogIfSubscribed();
    const size_t writeLen = segmentLimit > 0 ? std::min(len, requestedOffset + segmentLimit - written) : len;
    if (file.write(data, writeLen) != writeLen) {
      sdFull = true;
      return false;
    }
    written += writeLen;
    // Heap trajectory during the transfer: a steady value rules RAM out of a
    // mid-body failure; a falling one implicates it.
    if (written >= nextHeapLog) {
      LOG_DBG("WEB", "Fetch %u bytes, heap %u", (unsigned)written, (unsigned)ESP.getFreeHeap());
      nextHeapLog = written + 1024 * 1024;
    }
    keepBrowserAlive();
    // A bounded segment stops here; the next browser request resumes from
    // `written` with Range.
    if (segmentLimit > 0 && written - requestedOffset >= segmentLimit) {
      segmentBoundary = true;
      return false;
    }
    return true;
  };
  sink.rewind = [&] {
    // Range ignored: the body restarts from byte 0, which only a transfer
    // that has not yet reported progress to the browser can follow.
    if (requestedOffset > 0) {
      rangeUnsupported = true;
      return false;
    }
    file.close();
    written = 0;
    return Storage.openFileForWrite("PLG", part, file);
  };
  const freeink::FetchResult result = freeink::fetchResumable(
      url, options,
      [&](freeink::SecureHttpClient& http, const bool sameOrigin) {
        http.setUserAgent("CrossPlay");
        // The SecureNet transport ships no CA bundle, so peer verification always
        // fails (wolfSSL -188); skip it like HttpDownloader does. Traffic stays
        // TLS-encrypted, just unauthenticated — matching the prior library-lending flow.
        http.setInsecure();
        // Some delivery servers assemble books on the fly and can stall mid-body
        // while packaging; the default 15s no-data timeout truncates those downloads.
        http.setTimeout(60000);
        // The plugin's headers (typically its Authorization) stay with the
        // starting origin; a redirect to another server gets none of them.
        if (sameOrigin) {
          for (const auto& header : requestHeaders) http.addHeader(header.first, header.second);
        }
      },
      sink,
      // The write callback only runs when bytes arrive; with the 60s no-data
      // timeout a server stall would starve this task's WDT subscription.
      // shouldAbort is polled in every wait loop.
      [&] {
        resetTaskWatchdogIfSubscribed();
        keepBrowserAlive();
        return false;  // never aborts; only feeds
      });
  if (file.isOpen()) {
    file.flush();
    file.close();
  }
  const int status = result.status;
  const size_t totalExpected = result.total;
  bool complete = result.complete || (segmentBoundary && totalExpected > 0 && written >= totalExpected);

  const bool ok2xx = status >= 200 && status < 300;
  // A bounded segment ended mid-body: the browser requests the next one, so
  // the .part stays and nothing is installed yet.
  const bool midSegment = segmentBoundary && !complete && ok2xx;

  if (!complete && ok2xx && !midSegment) {
    Storage.remove(part.c_str());
    char msg[96];
    const char* error = sdFull ? "sd write failed" : rangeUnsupported ? "range unsupported" : "download truncated";
    // complete:false matters once the heartbeat has committed HTTP 200 chunked:
    // it is the only signal fetchToSd()'s resume loop still sees on this path
    // (it then detects zero progress and throws instead of returning success).
    snprintf(msg, sizeof(msg), "{\"error\":\"%s\",\"bytes\":%u,\"complete\":false}", error, (unsigned)written);
    LOG_ERR("WEB", "Fetch failed after %u bytes in %lu ms: %s", (unsigned)written, millis() - fetchStartedAt,
            url.c_str());
    sendFetchResult(502, msg);
    return;
  }

  JsonDocument resp;
  if (!ok2xx) {
    Storage.remove(part.c_str());
    resp["error"] = status < 0 ? "transport failure" : "http status";
  } else if (complete && !Storage.replaceFile(part.c_str(), dest.c_str())) {
    Storage.remove(part.c_str());
    complete = false;
    resp["error"] = "sd write failed";
  }
  resp["status"] = status;
  resp["bytes"] = written;
  resp["complete"] = complete;
  if (totalExpected > 0) resp["total"] = totalExpected;
  String out;
  serializeJson(resp, out);
  const bool browserConnected = server->client().connected();
  LOG_INF("WEB", "Fetch %s: %u bytes in %lu ms, browser %s: %s",
          midSegment ? "segment done"
          : complete ? "complete"
                     : "failed",
          (unsigned)written, millis() - fetchStartedAt, browserConnected ? "connected" : "disconnected", url.c_str());
  sendFetchResult(200, out);
}

// POST /api/plugin-fs?plugin=<name>&path=<path> with the file contents as a
// multipart file part. A plugin writes a small file to SD. Multipart, not a raw
// body: WebServer turns a plain body into a NUL-terminated String (truncating
// binary data) and buffers all of it first, while file parts stream in chunks.
void CrossPointWebServer::handlePluginFsUpload() {
  static constexpr size_t MAX_PLUGIN_FILE = 256 * 1024;
  auto& st = pluginFsUpload;
  const HTTPUpload& part = server->upload();
  const auto fail = [&st](const int status, const char* error) {
    if (st.file.isOpen()) st.file.close();  // explicit: remove follows on the same path
    if (!st.tmp.empty()) Storage.remove(st.tmp.c_str());
    st.errorStatus = status;
    st.error = error;
  };

  switch (part.status) {
    case UPLOAD_FILE_START: {
      if (st.file.isOpen()) st.file.close();
      st.path = server->arg("path").c_str();
      st.tmp.clear();
      st.bytes = 0;
      st.started = true;
      st.errorStatus = 0;
      st.error = nullptr;
      const String plugin = server->arg("plugin");
      if (!safeComponent(plugin) || !protectedpaths::isPluginPath(st.path)) {
        LOG_ERR("WEB", "Rejected plugin file write: plugin='%s' path='%s'", plugin.c_str(), st.path.c_str());
        fail(400, "bad path");
        return;
      }
      // ensureDirectoryExists() creates missing parents along the way, so this
      // covers any depth under /.crosspoint/plugins/<name>/... in one call.
      const size_t lastSlash = st.path.rfind('/');
      if (lastSlash != std::string::npos && lastSlash > 0) {
        Storage.ensureDirectoryExists(st.path.substr(0, lastSlash).c_str());
      }
      st.tmp = st.path + ".tmp";
      Storage.remove(st.tmp.c_str());
      if (!Storage.openFileForWrite("PLG", st.tmp, st.file)) fail(500, "cannot write");
      return;
    }
    case UPLOAD_FILE_WRITE:
      if (st.errorStatus) return;
      if (part.currentSize > MAX_PLUGIN_FILE - st.bytes) {
        fail(413, "too large");
        return;
      }
      resetTaskWatchdogIfSubscribed();
      if (st.file.write(part.buf, part.currentSize) != part.currentSize) {
        fail(500, "sd write failed");
        return;
      }
      st.bytes += part.currentSize;
      return;
    case UPLOAD_FILE_END:
      if (st.errorStatus) return;
      st.file.close();
      // An empty body must not replace existing credentials with nothing.
      if (st.bytes == 0) {
        fail(400, "empty body");
      } else if (!Storage.replaceFile(st.tmp.c_str(), st.path.c_str())) {
        fail(500, "sd write failed");
      }
      return;
    case UPLOAD_FILE_ABORTED:
      fail(400, "upload aborted");
      return;
  }
}

void CrossPointWebServer::handlePluginFs() {
  auto& st = pluginFsUpload;
  if (!st.started) {
    server->send(400, "application/json", "{\"error\":\"missing file part\"}");
  } else if (st.errorStatus) {
    char msg[64];
    snprintf(msg, sizeof(msg), "{\"error\":\"%s\"}", st.error);
    server->send(st.errorStatus, "application/json", msg);
  } else {
    JsonDocument resp;
    resp["ok"] = true;
    resp["bytes"] = st.bytes;
    sendJson(resp);
  }
  st.started = false;
}

// POST /api/book-key {path, key (base64, 16 bytes), expires?} -> {ok}
// Stores a protected book's content key, wrapped to this device, as
// "<path>.key" for the reader to open the book with.
void CrossPointWebServer::handleBookKey() {
  JsonDocument req;
  if (!readJsonBody(req)) return;
  const std::string path = req["path"] | "";
  const char* keyB64 = req["key"] | "";
  const int64_t expires = req["expires"] | static_cast<int64_t>(0);
  uint8_t key[bookkey::KEY_LEN];
  const int32_t n = freeink::content::base64Decode(keyB64, strlen(keyB64), key, sizeof(key));
  if (!protectedpaths::isPluginPath(path) || n != static_cast<int32_t>(sizeof(key)) || expires < 0) {
    server->send(400, "application/json", "{\"error\":\"bad path/key\"}");
    return;
  }
  if (!bookkey::write(path, key, expires)) {
    server->send(500, "application/json", "{\"error\":\"cannot store key\"}");
    return;
  }
  server->send(200, "application/json", "{\"ok\":true}");
}

void CrossPointWebServer::handlePluginRunnerPage() const {
  sendStaticContent(server.get(), RunnerPageHtml, sizeof(RunnerPageHtml), RunnerPageHtmlETag, "text/html");
  LOG_DBG("WEB", "Served plugin runner page");
}

CrossPointWebServer::PluginJob* CrossPointWebServer::allocPluginJob() {
  PluginJob* best = nullptr;
  for (auto& job : pluginJobs) {
    if (job.state == JOB_EMPTY) return &job;
    const bool finished = job.state == JOB_DONE || job.state == JOB_ERROR;
    if (finished && (!best || job.updatedAt < best->updatedAt)) best = &job;
  }
  return best;
}

// POST /api/plugin-jobs {plugin, action, args?} -> {id}
void CrossPointWebServer::handlePluginJobSubmit() {
  JsonDocument req;
  if (!readJsonBody(req)) return;
  const String plugin = req["plugin"] | "";
  const String action = req["action"] | "";
  // The claim response embeds action without JSON escaping.
  const auto identifierSafe = [](const String& s) {
    for (const char c : s) {
      if (!isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-' && c != '.') return false;
    }
    return !s.isEmpty();
  };
  if (!safeComponent(plugin) || !identifierSafe(action) || plugin.length() >= sizeof(PluginJob::plugin) ||
      action.length() >= sizeof(PluginJob::action) ||
      (!req["args"].isNull() && measureJson(req["args"]) >= sizeof(PluginJob::args))) {
    server->send(400, "application/json", "{\"error\":\"bad plugin/action/args\"}");
    return;
  }
  PluginJob* job = allocPluginJob();
  if (!job) {
    server->send(503, "application/json", "{\"error\":\"job queue full\"}");
    return;
  }
  *job = PluginJob{};
  job->id = nextPluginJobId++;
  job->state = JOB_PENDING;
  job->updatedAt = millis();
  snprintf(job->plugin, sizeof(job->plugin), "%s", plugin.c_str());
  snprintf(job->action, sizeof(job->action), "%s", action.c_str());
  if (!req["args"].isNull()) serializeJson(req["args"], job->args, sizeof(job->args));
  LOG_INF("WEB", "Plugin job %u queued: %s/%s", (unsigned)job->id, job->plugin, job->action);
  char msg[48];
  snprintf(msg, sizeof(msg), "{\"id\":%u}", (unsigned)job->id);
  server->send(200, "application/json", msg);
}

// GET /api/plugin-jobs/claim?plugin=<name> -> {id, action, args} or {id:0}
void CrossPointWebServer::handlePluginJobClaim() {
  const String plugin = server->arg("plugin");
  const uint32_t now = millis();
  for (auto& job : pluginJobs) {
    if (job.state == JOB_RUNNING && now - job.updatedAt > PLUGIN_JOB_LEASE_MS) {
      job.state = JOB_PENDING;
      job.updatedAt = now;
      LOG_INF("WEB", "Plugin job %u lease expired; requeued", (unsigned)job.id);
    }
    if (job.state != JOB_PENDING || plugin != job.plugin) continue;
    job.state = JOB_RUNNING;
    job.claim = nextPluginJobClaim++;
    job.updatedAt = now;
    const std::string msg = "{\"id\":" + std::to_string(job.id) + ",\"claim\":" + std::to_string(job.claim) +
                            ",\"action\":\"" + job.action + "\",\"args\":" + (job.args[0] ? job.args : "{}") + "}";
    server->send(200, "application/json", msg.c_str());
    return;
  }
  server->send(200, "application/json", "{\"id\":0}");
}

// POST /api/plugin-jobs/complete {id, claim, ok, result?} -> {ok}
// 409 when `claim` is stale: the lease expired and another runner re-claimed
// the job, so this late result must not overwrite that runner's.
void CrossPointWebServer::handlePluginJobComplete() {
  JsonDocument req;
  if (!readJsonBody(req)) return;
  const uint32_t id = req["id"] | 0;
  const uint32_t claim = req["claim"] | 0;
  for (auto& job : pluginJobs) {
    if (job.id != id) continue;
    if (job.state == JOB_DONE || job.state == JOB_ERROR) {
      server->send(200, "application/json", "{\"ok\":true}");
      return;
    }
    if (job.state != JOB_RUNNING) break;
    if (job.claim != claim) {
      LOG_INF("WEB", "Plugin job %u: stale completion ignored", (unsigned)id);
      server->send(409, "application/json", "{\"error\":\"stale claim\"}");
      return;
    }
    job.state = (req["ok"] | false) ? JOB_DONE : JOB_ERROR;
    job.updatedAt = millis();
    job.result[0] = '\0';
    if (!req["result"].isNull()) {
      if (measureJson(req["result"]) >= sizeof(job.result)) {
        strcpy(job.result, "{\"error\":\"result too large\"}");
      } else {
        serializeJson(req["result"], job.result, sizeof(job.result));
      }
    }
    LOG_INF("WEB", "Plugin job %u %s", (unsigned)id, job.state == JOB_DONE ? "done" : "failed");
    server->send(200, "application/json", "{\"ok\":true}");
    return;
  }
  server->send(404, "application/json", "{\"error\":\"no such running job\"}");
}

// GET /api/plugin-jobs/status?id=<n> -> {id, state, result}
void CrossPointWebServer::handlePluginJobStatus() {
  const uint32_t id = strtoul(server->arg("id").c_str(), nullptr, 10);
  static constexpr const char* STATE_NAMES[] = {"empty", "pending", "running", "done", "error"};
  for (auto& job : pluginJobs) {
    if (job.id != id || job.state == JOB_EMPTY) continue;
    const std::string msg = "{\"id\":" + std::to_string(id) + ",\"state\":\"" + STATE_NAMES[job.state] +
                            "\",\"result\":" + (job.result[0] ? job.result : "null") + "}";
    server->send(200, "application/json", msg.c_str());
    return;
  }
  // Unknown: never existed, or its slot was recycled after completion.
  char msg[64];
  snprintf(msg, sizeof(msg), "{\"id\":%u,\"state\":\"unknown\",\"result\":null}", (unsigned)id);
  server->send(200, "application/json", msg);
}

// WebSocket callback trampoline
void CrossPointWebServer::wsEventCallback(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (wsInstance) {
    wsInstance->onWebSocketEvent(num, type, payload, length);
  }
}

// WebSocket event handler for fast binary uploads
// Protocol:
//   1. Client sends TEXT message: "START:<filename>:<size>:<path>"
//   2. Client sends BINARY messages with file data chunks
//   3. Server sends TEXT "PROGRESS:<received>:<total>" after each chunk
//   4. Server sends TEXT "DONE" or "ERROR:<message>" when complete
void CrossPointWebServer::onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      LOG_DBG("WS", "Client %u disconnected", num);
      // Only clean up if this is the client that owns the active upload.
      // A new client may have already started a fresh upload before this
      // DISCONNECTED event fires (race condition on quick cancel + retry).
      if (num == wsUploadClientNum && wsUploadInProgress && wsUploadFile) {
        abortWsUpload("WS");
      }
      break;

    case WStype_CONNECTED: {
      LOG_DBG("WS", "Client %u connected", num);
      break;
    }

    case WStype_TEXT: {
      // Parse control messages
      String msg = String((char*)payload);
      LOG_DBG("WS", "Text from client %u: %s", num, msg.c_str());

      if (msg.startsWith("START:")) {
        // Reject any START while an upload is already active to prevent
        // leaking the open wsUploadFile handle (owning client re-START included)
        if (wsUploadInProgress) {
          wsServer->sendTXT(num, "ERROR:Upload already in progress");
          break;
        }

        // Parse: START:<filename>:<size>:<path>
        int firstColon = msg.indexOf(':', 6);
        int secondColon = msg.indexOf(':', firstColon + 1);

        if (firstColon > 0 && secondColon > 0) {
          wsUploadFileName = msg.substring(6, firstColon);
          if (!FsHelpers::isSafePathComponent(wsUploadFileName)) {
            LOG_DBG("WS", "START rejected: invalid filename '%s'", wsUploadFileName.c_str());
            wsServer->sendTXT(num, "ERROR:Invalid file name");
            return;
          }
          String sizeToken = msg.substring(firstColon + 1, secondColon);
          bool sizeValid = sizeToken.length() > 0;
          int digitStart = (sizeValid && sizeToken[0] == '+') ? 1 : 0;
          if (digitStart > 0 && sizeToken.length() < 2) sizeValid = false;
          for (int i = digitStart; i < (int)sizeToken.length() && sizeValid; i++) {
            if (!isdigit((unsigned char)sizeToken[i])) sizeValid = false;
          }
          if (!sizeValid) {
            LOG_DBG("WS", "START rejected: invalid size token '%s'", sizeToken.c_str());
            wsServer->sendTXT(num, "ERROR:Invalid START format");
            return;
          }
          wsUploadSize = sizeToken.toInt();
          wsUploadPath = normalizeWebPath(msg.substring(secondColon + 1));
          wsUploadReceived = 0;
          wsLastProgressSent = 0;
          wsUploadStartTime = millis();

          String filePath = wsUploadPath;
          if (!filePath.endsWith("/")) filePath += "/";
          filePath += wsUploadFileName;
          if (protectedpaths::isSensitivePath(filePath.c_str())) {
            wsServer->sendTXT(num, "ERROR:Cannot write protected items");
            return;
          }

          resetTaskWatchdogIfSubscribed();
          if (Storage.exists(filePath.c_str())) {
            LOG_DBG("WS", "Upload collision: %s", filePath.c_str());
            wsServer->sendTXT(num, "ERROR:File already exists: " + wsUploadFileName);
            return;
          }

          LOG_DBG("WS", "Starting upload: %s (%d bytes) to %s", wsUploadFileName.c_str(), wsUploadSize,
                  filePath.c_str());

          // Open file for writing
          resetTaskWatchdogIfSubscribed();
          if (!Storage.openFileForWrite("WS", filePath, wsUploadFile)) {
            wsServer->sendTXT(num, "ERROR:Failed to create file");
            wsUploadInProgress = false;
            wsUploadClientNum = 255;
            return;
          }
          resetTaskWatchdogIfSubscribed();

          // Zero-byte upload: complete immediately without waiting for BIN frames
          if (wsUploadSize == 0) {
            // Explicit close() required: file-scope global persists beyond function scope
            wsUploadFile.close();
            wsLastCompleteName = wsUploadFileName;
            wsLastCompleteSize = 0;
            wsLastCompleteAt = millis();
            LOG_DBG("WS", "Zero-byte upload complete: %s", filePath.c_str());
            clearBookCache(filePath.c_str());
            if (isLibraryBookFile(wsUploadFileName)) library::markLibraryIndexDirty();
            wsServer->sendTXT(num, "DONE");
            wsLastProgressSent = 0;
            break;
          }

          wsUploadClientNum = num;
          wsUploadInProgress = true;
          wsServer->sendTXT(num, "READY");
        } else {
          wsServer->sendTXT(num, "ERROR:Invalid START format");
        }
      }
      break;
    }

    case WStype_BIN: {
      if (!wsUploadInProgress || !wsUploadFile || num != wsUploadClientNum) {
        wsServer->sendTXT(num, "ERROR:No upload in progress");
        return;
      }

      // Write binary data directly to file
      size_t remaining = wsUploadSize - wsUploadReceived;
      if (length > remaining) {
        abortWsUpload("WS");
        wsServer->sendTXT(num, "ERROR:Upload overflow");
        return;
      }
      resetTaskWatchdogIfSubscribed();
      size_t written = wsUploadFile.write(payload, length);
      resetTaskWatchdogIfSubscribed();

      if (written != length) {
        abortWsUpload("WS");
        wsServer->sendTXT(num, "ERROR:Write failed - disk full?");
        return;
      }

      wsUploadReceived += written;

      // Send progress update (every 64KB or at end)
      if (wsUploadReceived - wsLastProgressSent >= 65536 || wsUploadReceived >= wsUploadSize) {
        String progress = "PROGRESS:" + String(wsUploadReceived) + ":" + String(wsUploadSize);
        wsServer->sendTXT(num, progress);
        wsLastProgressSent = wsUploadReceived;
      }

      // Check if upload complete
      if (wsUploadReceived >= wsUploadSize) {
        // Explicit close() required: file-scope global persists beyond function scope
        wsUploadFile.close();
        wsUploadInProgress = false;
        wsUploadClientNum = 255;

        wsLastCompleteName = wsUploadFileName;
        wsLastCompleteSize = wsUploadSize;
        wsLastCompleteAt = millis();

        unsigned long elapsed = millis() - wsUploadStartTime;
        float kbps = (elapsed > 0) ? (wsUploadSize / 1024.0) / (elapsed / 1000.0) : 0;

        LOG_DBG("WS", "Upload complete: %s (%d bytes in %lu ms, %.1f KB/s)", wsUploadFileName.c_str(), wsUploadSize,
                elapsed, kbps);

        // Clear epub cache after uploading the file
        String filePath = wsUploadPath;
        if (!filePath.endsWith("/")) filePath += "/";
        filePath += wsUploadFileName;
        clearBookCache(filePath.c_str());
        if (isLibraryBookFile(wsUploadFileName)) library::markLibraryIndexDirty();

        wsServer->sendTXT(num, "DONE");
        wsLastProgressSent = 0;
      }
      break;
    }

    default:
      break;
  }
}

// --- Font management handlers ---

void CrossPointWebServer::handleFontsPage() const {
  sendStaticContent(server.get(), FontsPageHtml, sizeof(FontsPageHtml), FontsPageHtmlETag, "text/html");
  LOG_DBG("WEB", "Served fonts page");
}

void CrossPointWebServer::handleFontList() const {
  // Pick up any uploads/deletes that happened since the last reader load.
  const_cast<SdCardFontSystem&>(sdFontSystem).refreshIfDirty();
  const auto& families = sdFontSystem.registry().getFamilies();

  JsonDocument doc;
  JsonArray arr = doc["families"].to<JsonArray>();
  doc["maxFamilies"] = SdCardFontRegistry::MAX_SD_FAMILIES;
  // Vector (.ttf/.otf) fonts need PSRAM to stay resident; DRAM-only boards
  // only take pre-rendered .cpfont uploads.
  doc["vectorFonts"] = HalMemory::getPsramHeap().totalBytes > 0;

  for (const auto& family : families) {
    JsonObject fObj = arr.add<JsonObject>();
    fObj["name"] = family.name;

    JsonArray sizes = fObj["sizes"].to<JsonArray>();
    for (uint8_t s : family.availableSizes()) {
      sizes.add(s);
    }

    JsonArray files = fObj["files"].to<JsonArray>();
    for (const auto& file : family.files) {
      JsonObject fileObj = files.add<JsonObject>();
      // Extract filename from full path
      const char* name = strrchr(file.path.c_str(), '/');
      fileObj["name"] = name ? name + 1 : file.path.c_str();

      // Stat the file for size
      HalFile f;
      if (Storage.openFileForRead("WEB", file.path.c_str(), f)) {
        fileObj["size"] = static_cast<unsigned long>(f.size());
        f.close();
      } else {
        fileObj["size"] = 0;
      }
    }
  }

  sendJson(doc);
}

void CrossPointWebServer::handleFontUploadData() {
  HTTPUpload& upload = server->upload();

  switch (upload.status) {
    case UPLOAD_FILE_START: {
      resetTaskWatchdogIfSubscribed();
      String family = server->arg("family");
      fontUpload.file = HalFile();
      fontUpload.familyName.clear();
      fontUpload.filePath.clear();
      fontUpload.valid = false;
      fontUpload.magicChecked = false;
      fontUpload.isVector = false;
      fontUpload.bytesWritten = 0;
      fontUpload.bufferPos = 0;
      fontUpload.buffer.reset();

      if (!FontInstaller::isValidFamilyName(family.c_str())) {
        LOG_ERR("WEB", "Invalid font family name: %s", family.c_str());
        break;
      }

      String filename = upload.filename;
      filename.replace(' ', '_');
      // Validate filename: rejects path traversal (../, /, \) and enforces
      // a .cpfont/.ttf/.otf basename of alphanumeric + hyphen + underscore.
      // Without this an attacker could supply
      // "../../.crosspoint/settings.json" as a "filename" and have it written
      // outside the fonts directory. Vector fonts need PSRAM to stay
      // resident, so DRAM-only boards only accept .cpfont.
      const bool psramCapable = HalMemory::getPsramHeap().totalBytes > 0;
      fontUpload.isVector = psramCapable && FontInstaller::isValidVectorFontFilename(filename.c_str());
      if (!fontUpload.isVector && !FontInstaller::isValidCpfontFilename(filename.c_str())) {
        LOG_ERR("WEB", "Invalid font filename: %s", filename.c_str());
        break;
      }

      fontUpload.familyName = family.c_str();

      // Create a temporary FontInstaller for directory creation
      FontInstaller installer(sdFontSystem.registry());
      if (!installer.ensureFamilyDir(family.c_str())) {
        LOG_ERR("WEB", "Failed to create font family dir");
        break;
      }

      fontUpload.buffer = makeUniqueNoThrow<uint8_t[]>(FontUploadState::BUFFER_SIZE);
      if (!fontUpload.buffer) {
        LOG_ERR("WEB", "OOM: font upload buffer");
        break;
      }

      char path[128];
      FontInstaller::buildFontPath(family.c_str(), filename.c_str(), path, sizeof(path));
      fontUpload.filePath = path;

      if (!Storage.openFileForWrite("WEB", path, fontUpload.file)) {
        LOG_ERR("WEB", "Failed to open font file for write: %s", path);
        fontUpload.buffer.reset();
        break;
      }

      fontUpload.valid = true;
      LOG_DBG("WEB", "Font upload started: %s -> %s", filename.c_str(), path);
      break;
    }

    case UPLOAD_FILE_WRITE: {
      if (dropUploadIfCancelled() || !fontUpload.valid) break;
      resetTaskWatchdogIfSubscribed();

      // Validate magic bytes on first chunk only
      if (!fontUpload.magicChecked && upload.currentSize >= 8) {
        bool magicOk;
        if (fontUpload.isVector) {
          // sfnt versions: 0x00010000 (TrueType), "OTTO" (CFF), "true" (Apple)
          static constexpr uint8_t kTtfMagic[4] = {0x00, 0x01, 0x00, 0x00};
          magicOk = memcmp(upload.buf, kTtfMagic, 4) == 0 || memcmp(upload.buf, "OTTO", 4) == 0 ||
                    memcmp(upload.buf, "true", 4) == 0;
        } else {
          magicOk = memcmp(upload.buf, "CPFONT\0\0", 8) == 0;
        }
        if (!magicOk) {
          LOG_ERR("WEB", "Invalid font magic bytes");
          fontUpload.valid = false;
          break;
        }
        fontUpload.magicChecked = true;
      }

      // Buffer writes for efficiency
      size_t remaining = upload.currentSize;
      const uint8_t* src = upload.buf;
      while (remaining > 0) {
        size_t space = FontUploadState::BUFFER_SIZE - fontUpload.bufferPos;
        size_t chunk = (remaining < space) ? remaining : space;
        memcpy(fontUpload.buffer.get() + fontUpload.bufferPos, src, chunk);
        fontUpload.bufferPos += chunk;
        src += chunk;
        remaining -= chunk;

        if (fontUpload.bufferPos >= FontUploadState::BUFFER_SIZE) {
          if (fontUpload.file.write(fontUpload.buffer.get(), fontUpload.bufferPos) != fontUpload.bufferPos) {
            LOG_ERR("WEB", "Font upload write failed: %s", fontUpload.filePath.c_str());
            fontUpload.valid = false;
            break;
          }
          fontUpload.bytesWritten += fontUpload.bufferPos;
          fontUpload.bufferPos = 0;
          resetTaskWatchdogIfSubscribed();
        }
      }
      break;
    }

    case UPLOAD_FILE_END: {
      // Flush remaining buffer
      if (fontUpload.valid && fontUpload.bufferPos > 0) {
        if (fontUpload.file.write(fontUpload.buffer.get(), fontUpload.bufferPos) != fontUpload.bufferPos) {
          LOG_ERR("WEB", "Font upload write failed: %s", fontUpload.filePath.c_str());
          fontUpload.valid = false;
        }
        fontUpload.bytesWritten += fontUpload.bufferPos;
        fontUpload.bufferPos = 0;
      }
      if (fontUpload.file.isOpen()) {
        fontUpload.file.close();
      }
      fontUpload.bufferPos = 0;
      fontUpload.buffer.reset();

      if (!fontUpload.valid && !fontUpload.filePath.empty()) {
        Storage.remove(fontUpload.filePath.c_str());
      }

      LOG_DBG("WEB", "Font upload end: valid=%d, %zu bytes", fontUpload.valid, fontUpload.bytesWritten);
      break;
    }

    case UPLOAD_FILE_ABORTED: {
      fontUpload.bufferPos = 0;
      fontUpload.buffer.reset();
      if (fontUpload.file) {
        fontUpload.file.close();
      }
      if (!fontUpload.filePath.empty()) {
        Storage.remove(fontUpload.filePath.c_str());
      }
      fontUpload.valid = false;
      LOG_DBG("WEB", "Font upload aborted");
      break;
    }
  }
}

void CrossPointWebServer::handleFontUpload() {
  if (fontUpload.valid) {
    sdFontSystem.markRegistryDirty();
    server->send(200, "application/json", "{\"ok\":true}");
    LOG_DBG("WEB", "Font upload complete: %s", fontUpload.filePath.c_str());
  } else {
    server->send(400, "application/json", "{\"error\":\"Invalid font file\"}");
  }
}

void CrossPointWebServer::handleFontDelete() {
  String body = server->arg("plain");
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);

  if (err || !doc["family"].is<const char*>()) {
    server->send(400, "application/json", "{\"error\":\"Invalid request\"}");
    return;
  }

  const char* familyName = doc["family"];
  FontInstaller installer(sdFontSystem.registry());
  auto result = installer.deleteFamily(familyName);

  if (result == FontInstaller::Error::OK) {
    sdFontSystem.markRegistryDirty();
    server->send(200, "application/json", "{\"ok\":true}");
    LOG_DBG("WEB", "Deleted font family: %s", familyName);
  } else {
    server->send(500, "application/json", "{\"error\":\"Delete failed\"}");
    LOG_ERR("WEB", "Failed to delete font family: %s", familyName);
  }
}

// Settings a network client must never be able to change, however
// authenticated the surface feels.
//
// The settings list drives the menu, the on-disk JSON AND the web API from one
// entry, which is exactly why devMode had to be excluded by hand: adding the
// toggle gave it a web setter for free, and that setter lives on the reader's
// UNAUTHENTICATED web UI. Anyone on the network, while the transfer screen was
// open, could have switched Developer Mode on permanently and across reboots --
// using the temporary surface to enable the persistent one. Turning this device
// into a development device is a decision made at the device.
bool CrossPointWebServer::isLocalOnlySetting(const char* key) { return key && strcmp(key, "devMode") == 0; }

// Every /api/dev/ route except pairing goes through here first.
//
// Two conditions, and the order matters for what a stranger can learn: dev mode
// off is reported as 404, indistinguishable from a build that has no such
// route, so scanning a network tells you nothing about which devices could be
// turned into targets. Only once dev mode is on does a wrong token get a 401.
bool CrossPointWebServer::devTokenOk() const {
  if (!devmode::status().enabled) return false;
  std::string token;  // header only; see devAuthorised()
  if (server->hasHeader("X-Dev-Token")) token = server->header("X-Dev-Token").c_str();
  return devmode::tokenValid(token);
}

bool CrossPointWebServer::devAuthorised() {
  const auto st = devmode::status();
  if (!st.enabled) {
    server->send(404, "text/plain", "not found\n");
    return false;
  }
  // Header only. Accepting ?token= made every follow-up call a CORS-simple
  // request that a page could issue without a preflight; requiring a custom
  // header means a browser must preflight, and with CORS off on this surface
  // the preflight fails. curl is unaffected.
  std::string token;
  if (server->hasHeader("X-Dev-Token")) token = server->header("X-Dev-Token").c_str();
  if (!devmode::tokenValid(token)) {
    // Deliberately not logged. This is reachable by anyone on the network, and
    // logPrintf writes every level into a 16-line RTC ring -- so an
    // unauthenticated client could erase the pre-panic tail that /api/dev/crash
    // exists to deliver, sixteen requests in, from a page the owner merely
    // visited. The 401 tells the caller everything the log would have.
    server->send(401, "text/plain", "pair first: POST /api/dev/pair with the code on the device\n");
    return false;
  }
  return true;
}

// Exchange the six digits shown on the device for a token.
//
// Rate-limited and self-rotating; see devmode::pair().
//
// An earlier version argued no limit was needed because "the window is a person
// standing at the device". That was wrong twice over on this branch: dev mode
// keeps the device awake indefinitely, so the window is however long the toggle
// is on, and CORS made the endpoint reachable from any page the user visits
// rather than only from the LAN.
void CrossPointWebServer::handleDevPair() {
  const auto st = devmode::status();
  if (!st.enabled) {
    server->send(404, "text/plain", "not found\n");
    return;
  }
  std::string code;
  if (server->hasArg("code")) code = server->arg("code").c_str();
  // BEFORE pair(), not after. pair() mutates the window on every evaluated
  // guess, so asking afterwards always finds one open and every refusal became
  // a 429 -- telling someone who had simply mistyped that "the code you have is
  // fine, wait 30s", which is the exact inversion of the advice this was added
  // to fix. Read it first: non-zero here means the attempt was never looked at.
  const unsigned long retry = devmode::pairRetryInMs();
  const std::string token = devmode::pair(code);
  if (token.empty()) {
    // Distinguish "wrong" from "closed". They are the same empty return, and
    // saying "wrong code" to someone holding the RIGHT code -- because a flood,
    // or their own earlier typos, closed the window -- sends them to re-read six
    // digits that were already correct.
    if (retry > 0) {
      server->send(429, "text/plain",
                   "not checked: pairing is rate-limited for another " + String(retry) +
                       "ms. The code on the screen has NOT changed -- wait and send the same one.\n");
    } else {
      server->send(401, "text/plain", "wrong code\n");
    }
    return;
  }
  JsonDocument doc;
  doc["token"] = token;
  doc["device"] = BoardConfig::ACTIVE.name;
  doc["version"] = CROSSPOINT_VERSION;
  String out;
  serializeJson(doc, out);
  server->send(200, "application/json", out);
}

// Switch Developer Mode off, from the computer.
//
// This exists because the obvious way to close a device does not work and the
// script used to claim it did. Flashing a release image does NOT turn Developer
// Mode off: the setting is a field in /.crosspoint/settings.json ON THE SD CARD,
// and flashing replaces the firmware, not the card. Worse, every build now
// carries these routes -- that is the whole point of it being a runtime setting
// -- so a device flashed "back to a release" kept joining Wi-Fi and kept
// answering /api/dev/flash while its owner believed it had been closed.
//
// Applied immediately: saveToFile() persists it, and devmode::update() sees the
// setting change on the next loop and tears the server and the radio down.
void CrossPointWebServer::handleDevDisable() {
  if (!devAuthorised()) return;
  SETTINGS.devMode = 0;
  SETTINGS.saveToFile();
  LOG_INF("DEVMODE", "switched off by a paired client");
  server->send(200, "text/plain", "OK developer mode off\n");
}

// What the device remembers about the last time it died.
//
// Nothing here is captured for this endpoint; all of it already existed and was
// only reachable by standing in front of the device. HalSystem keeps the panic
// message and a stack backtrace in RTC_NOINIT memory, and Logging keeps the
// last lines in a second RTC_NOINIT ring with a magic word guarding against
// cold-boot garbage. Both survive the reset that a panic causes, which is the
// whole reason they are in RTC memory rather than the heap.
//
// The log tail matters more than the backtrace most of the time: a backtrace
// says where it died, the preceding lines say what it was doing.
//
// READ-ONLY. Fetching a crash report must not destroy it -- two people
// debugging the same device would otherwise race, and the first curl would win
// and the second would see a healthy device. Clearing stays where it was: the
// on-device crash screen, when a human dismisses it.
void CrossPointWebServer::handleDevCrash() {
  if (!devAuthorised()) return;

  JsonDocument doc;
  const bool corrupt = sanitizeLogHead();
  doc["panicked"] = HalSystem::isRebootFromPanic();
  doc["panic"] = HalSystem::getPanicInfo(true);
  // A corrupt ring is reported rather than hidden: an empty "logs" that means
  // "nothing was logged" and one that means "RTC memory was garbage" are
  // different findings, and guessing between them wastes a debugging session.
  doc["logsValid"] = !corrupt;
  doc["logs"] = corrupt ? "" : getLastLogs();
  doc["uptime"] = millis() / 1000;
  doc["version"] = CROSSPOINT_VERSION;

  String out;
  serializeJson(doc, out);
  server->send(200, "application/json", out);
}

// The same ring, for a device that has not crashed. Sixteen lines is short; it
// is what fits in RTC memory beside the panic buffers, and it is deliberately
// the same buffer as the crash report so there is one thing to reason about
// rather than two that can disagree.
void CrossPointWebServer::handleDevLog() {
  if (!devAuthorised()) return;
  if (sanitizeLogHead()) {
    server->send(200, "text/plain", "");
    return;
  }
  server->send(200, "text/plain", getLastLogs().c_str());
}

#if CROSSPOINT_DEV_SERIAL_BRIDGE
// One input command per request, in the same words the serial bridge takes:
// TAP x y [holdMs] / LONG x y / SWIPE x0 y0 x1 y1 [ms] / BTN NAME [holdMs].
//
// The body is the command, not a JSON envelope. It is what a person types into
// curl and what drive.py already speaks, and a second encoding of four verbs
// would be two things to keep in step for no reader's benefit.
void CrossPointWebServer::handleDevInput() {
  if (!devAuthorised()) return;
  // The content type is load-bearing, and there is no recovering from the wrong
  // one. This core only keeps a body whole under arg("plain") when it is NOT
  // form-encoded; for application/x-www-form-urlencoded it runs the body
  // through _parseArguments, which bails at the first field with no '=' and
  // leaves args() == 0. An input command never contains '=', so a body sent the
  // way `curl -d` and Python's urllib send it by default is not merely
  // misplaced, it is gone. Say so in the error rather than guessing.
  const String body = server->arg("plain");
  if (body.isEmpty()) {
    server->send(400, "text/plain",
                 "ERR body must be one command, e.g. TAP 400 240\n"
                 "    send it as Content-Type: text/plain -- a form-encoded body is\n"
                 "    parsed away by the HTTP core before this handler sees it:\n"
                 "      curl -H 'Content-Type: text/plain' --data-binary 'TAP 400 240' ...\n");
    return;
  }
  // Trim, so a trailing newline from `curl --data-binary @file` is not an
  // argument. The injector's vocabulary is line-oriented; the transport is not.
  String line = body;
  line.trim();
  char reply[96];
  const bool okay = devinput::runCommand(line.c_str(), reply, sizeof(reply));
  // Refusals only, and not at DBG either: addToLogRingBuffer() is called for
  // EVERY level (Logging.cpp:75), and these routes exist only in the envs that
  // set LOG_LEVEL=2 -- so demoting an accepted tap to LOG_DBG changes the tag
  // and nothing else. The RTC ring is 16 lines and it is the only diagnostic
  // channel a Wi-Fi driver has; sixteen accepted taps would still erase whatever
  // the driver was trying to read. The OK is already in the response the caller
  // just read, so it carries nothing the caller does not have. A refusal does.
  if (!okay) LOG_INF("WEB", "dev input refused: %s -> %s", line.c_str(), reply);
  // 409 rather than 400 for "busy": the command was well formed and will work
  // when the previous event finishes, which is a retry, not a fix.
  const int code = okay ? 200 : (strncmp(reply, "ERR busy", 8) == 0 ? 409 : 400);
  server->send(code, "text/plain", String(reply) + "\n");
}

// The serial transport's TX counters, over Wi-Fi.
//
// The whole point: when the cable wedges, CMD:CDCSTAT is unreachable by
// definition, and the RTC log ring is sixteen lines. This is the only way to
// ask a device with a dead cable what its cable did.
void CrossPointWebServer::handleDevSerial() {
  if (!devAuthorised()) return;
  // 256: eight uint32 counters at their widest already ran to 145 characters,
  // so 128 truncated silently -- snprintf is safe, but the line it produced was
  // not the line it claimed to be.
  char line[256];
  devbridge::txStatsLine(line, sizeof(line));
  server->send(200, "text/plain", String(line) + "\n");
}

// The framebuffer as it stands, 1bpp, row-major, MSB leftmost -- the same bytes
// the serial bridge streams, so one host-side decoder serves both.
//
// Unlike the serial path this arrives whole rather than truncated, because TCP
// has no equivalent of the CDC ring that drops what will not fit. It is still
// chunked on the way out, for the reason given at the write loop below.
void CrossPointWebServer::handleDevScreen() {
  if (!devAuthorised()) return;
  const uint8_t* fb = display.getFrameBuffer();
  if (fb == nullptr) {
    server->send(503, "text/plain", "ERR no framebuffer\n");
    return;
  }
  const size_t size = display.getBufferSize();
  // Width and height are not in the payload, so say them where a host decoder
  // can read them. Queued before send(), which is what emits the header block.
  //
  // From the DISPLAY, not from BoardConfig: the host multiplies these to check
  // the length it got, so they have to be the geometry that produced the byte
  // count. The two agree on both device envs today, but the SDK has boards
  // whose silicon frame and framebuffer frame differ, and there the mismatch
  // would reject every screenshot as short.
  server->sendHeader("X-Panel-Width", String(display.getDisplayWidth()));
  server->sendHeader("X-Panel-Height", String(display.getDisplayHeight()));
  server->setContentLength(size);
  server->send(200, "application/octet-stream", "");

  // Chunked, watchdog-fed, and stopping on a dead peer -- the same shape as
  // handleDownload above, and for the same reason. NetworkClient::write resets
  // its retry budget on every partial send, so one 48KB call against a peer
  // that trickles can hold this loop indefinitely; handleClient() runs on the
  // main loop, so that is the whole UI frozen, not a slow download.
  NetworkClient client = server->client();
  size_t sent = 0;
  while (sent < size) {
    resetTaskWatchdogIfSubscribed();
    const size_t wrote = client.write(fb + sent, size - sent > 4096 ? 4096 : size - sent);
    if (wrote == 0) break;  // peer gone; the short body is the honest answer
    sent += wrote;
  }
  client.clear();
  if (sent < size) LOG_ERR("WEB", "screen: client took %u of %u bytes", sent, size);
}
#endif  // CROSSPOINT_DEV_SERIAL_BRIDGE

// Upload straight to the card, token-gated, so Developer Mode never has to
// expose the file manager to move a firmware image across.
//
// Streams in chunks like every other upload path here; the body is 6MB and will
// not fit anywhere else. The destination is fixed rather than caller-chosen: an
// authenticated endpoint that writes an arbitrary path is a worse primitive
// than one that writes the only path this feature needs.
// ---------------------------------------------------------------------------
// The Wallpapers surface: one page, its script, and one upload.
// ---------------------------------------------------------------------------

// One screen of text a person typed, with room to spare. A page left open on a
// laptop can paste a book, and this is the only door a client can push bytes
// through.
constexpr size_t kNotesMaxBytes = 16 * 1024;

void CrossPointWebServer::handleNotesPage() const {
  sendStaticContent(server.get(), NotesPageHtml, sizeof(NotesPageHtml), NotesPageHtmlETag, "text/html");
}

void CrossPointWebServer::handleNotesText() {
  if (notesPath.empty()) {
    server->send(503, "text/plain", "No note is open on the reader.");
    return;
  }
  // The name rides in a header rather than in the body, so the body is the note
  // and nothing else: a page that has to strip a prelude off the text it shows
  // is one bug away from saving the prelude back into the file.
  String encoded;
  for (const char c : notesName) {
    if (static_cast<unsigned char>(c) < 0x80 && (isalnum(c) || c == '-' || c == '_' || c == '.' || c == ' ')) {
      encoded += c;
    } else {
      char hex[4];
      std::snprintf(hex, sizeof(hex), "%%%02X", static_cast<unsigned char>(c));
      encoded += hex;
    }
  }
  server->sendHeader("X-Note-Name", encoded);
  // The page draws tick boxes, and writes them back, only for a list. It used
  // to do both for everything, so saving a NOTE from a phone turned its
  // paragraphs into a list.
  server->sendHeader("X-Note-Kind", notesIsList ? "list" : "note");
  server->sendHeader("Cache-Control", "no-store");
  server->send(200, "text/plain; charset=utf-8", Storage.readFile(notesPath.c_str()));
}

void CrossPointWebServer::handleNotesSave() {
  if (notesPath.empty()) {
    server->send(503, "text/plain", "No note is open on the reader.");
    return;
  }
  const String raw = server->arg("plain");
  // A note is one screen of text somebody typed. The cap is here rather than in
  // the app because this is the only door a client can push bytes through, and
  // a page left open on a laptop can paste a book.
  if (raw.length() > kNotesMaxBytes) {
    server->send(413, "text/plain", "That is too long for a note.");
    return;
  }

  // EVERY LINE BECOMES AN ITEM. Without this, a person typing "Milk" on their
  // phone got a line the reader could not tick, could not delete and drew at
  // half the size of a real one -- and the only way to avoid it was to type
  // "- [ ] " by hand, nine keyboard taps before the first letter on an iOS
  // keyboard, thirty for a shopping list. The page now teaches no syntax
  // because there is none to get wrong.
  std::string body(raw.c_str(), raw.length());
  // Only a LIST gets markers written for it. A note is words somebody kept, and
  // turning every line of it into a tick box would be the old two-kinds mistake
  // wearing the opposite face. The kind is the reader's, not read off the file:
  // an empty note is an empty file, which reads as a list.
  if (notesIsList) notes::coerceToList(body);

  // Beside itself, then renamed. Opening the real path truncates it first, so a
  // connection dropped mid-write would leave a note that parses as empty --
  // which reads exactly like a note somebody deleted.
  const std::string part = notesPath + ".part";
  {
    HalFile file;
    if (!Storage.openFileForWrite("NOTES", part.c_str(), file)) {
      server->send(500, "text/plain", "The card would not take it.");
      return;
    }
    if (!body.empty() && file.write(body.data(), body.size()) != static_cast<int>(body.size())) {
      Storage.remove(part.c_str());
      server->send(500, "text/plain", "The card would not take it.");
      return;
    }
  }
  Storage.remove(notesPath.c_str());
  if (!Storage.rename(part.c_str(), notesPath.c_str())) {
    Storage.remove(part.c_str());
    server->send(500, "text/plain", "The card would not take it.");
    return;
  }
  notesChanged = true;
  server->send(200, "text/plain", "saved");
}

void CrossPointWebServer::handleWallpaperPage() const {
  // sendStaticContent since upstream #2560: the same generated page, now
  // with its ETag so a browser's conditional GET gets a 304.
  sendStaticContent(server.get(), WallpaperPageHtml, sizeof(WallpaperPageHtml), WallpaperPageHtmlETag, "text/html");
}

void CrossPointWebServer::handleWallpaperScript() const {
  server->sendHeader("Content-Encoding", "gzip");
  server->send_P(200, "application/javascript", wallconvertJs, sizeof(wallconvertJs));
}

namespace {
// The device names the file, never the client. That is not tidiness: it deletes
// the traversal question and the collision question outright rather than
// answering them. handleUpload thirty lines away takes upload.filename into a
// path with no check at all (card #353), and the font handler beside it does
// check -- so this fork has already proved it can go either way on the same
// afternoon. There is nothing to validate if nothing is accepted.
//
// The original name is no loss: the picker captions a user's wallpaper with its
// file stem, and a phone hands over "IMG_0001".
std::string nextWallpaperPath() {
  for (int i = 1; i <= wallpapers::kMaxUploadSlot; ++i) {
    // The SHAPE comes from wallpapers::uploadFileName, which is also what the
    // picker's harness picks a plausible arrival by and what
    // host-tests/wallpapers builds its corpus from. Spelled here as well, it
    // would go stale the first time one of the three moved.
    const std::string path = std::string(wallpapers::kLibraryDir) + "/" + wallpapers::uploadFileName(i);
    if (!Storage.exists(path.c_str())) return path;
  }
  return std::string();
}
}  // namespace

void CrossPointWebServer::handleWallpaperUploadData() {
#ifdef SIMULATOR
  // The simulator's WebServer shim has no raw() body API. A PLATFORM gate, not
  // a feature gate, exactly as /api/dev/upload carries.
  return;
#else
  HTTPRaw& raw = server->raw();

  if (raw.status == RAW_START) {
    // Everything is decided HERE, because by RAW_END the whole body is already
    // on the card -- but nothing is ANSWERED here: sending a response while the
    // body is still being parsed corrupts the server.
    wallUpload.accepted = false;
    wallUpload.ok = false;
    wallUpload.written = 0;
    wallUpload.refusal = nullptr;

    if (!isWallpapers()) {
      wallUpload.refusal = "not this surface";
      return;
    }

    // The free-space precondition the WebDAV path never had. Three outcomes,
    // and Unknown REFUSES: freeBytes() returns false when the cluster walk
    // failed, which is an abnormal card and never "the card is empty".
    // Refusing costs a retry; proceeding costs the one user whose card was
    // already in trouble -- and the floor exists to protect Study's review log,
    // not this app.
    uint64_t freeNow = 0;
    const bool queryOk = Storage.freeBytes(freeNow);
    if (wallpapers::roomFor(queryOk, freeNow, wallpapers::kCardFloorBytes) != wallpapers::Room::Ok) {
      wallUpload.refusal = "The reader is low on space. Nothing was written.";
      return;
    }

    if (!Storage.exists(wallpapers::kLibraryDir) && !Storage.mkdir(wallpapers::kLibraryDir)) {
      wallUpload.refusal = "The reader could not open its wallpapers folder.";
      return;
    }

    wallUpload.final = nextWallpaperPath();
    if (wallUpload.final.empty()) {
      wallUpload.refusal = "The reader has too many wallpapers already.";
      return;
    }
    wallUpload.target = wallUpload.final + ".part";
    Storage.remove(wallUpload.target.c_str());
    if (!Storage.openFileForWrite("WALL", wallUpload.target, wallUpload.file)) {
      wallUpload.refusal = "The reader could not write to its card.";
      return;
    }
    wallUpload.accepted = true;
    LOG_INF("WALL", "receiving wallpaper -> %s", wallUpload.target.c_str());
    return;
  }

  if (!wallUpload.accepted) return;

  if (raw.status == RAW_WRITE) {
    // Bounded before a byte is written, not after: kWallpaperFileBytes is the
    // only size the panel accepts, so a longer body is a wrong file or a
    // hostile one and there is no reason to spend the card on it.
    if (wallUpload.written + raw.currentSize > wallpapers::kWallpaperFileBytes) {
      wallUpload.refusal = "That is not a reader wallpaper.";
      wallUpload.accepted = false;
      wallUpload.file.close();
      Storage.remove(wallUpload.target.c_str());
      return;
    }
    if (wallUpload.file.write(raw.buf, raw.currentSize) == raw.currentSize) {
      wallUpload.written += raw.currentSize;
    } else {
      wallUpload.refusal = "The card stopped accepting the file.";
      wallUpload.accepted = false;
      wallUpload.file.close();
      Storage.remove(wallUpload.target.c_str());
    }
    return;
  }

  if (raw.status == RAW_END) {
    wallUpload.file.close();
    // A short body is a dropped connection. The .part is removed rather than
    // left behind: sweepPartFiles() only matches .part, which is why this path
    // writes one instead of the .davtmp WebDAV would have left invisible to
    // both the sweep and the picker.
    if (wallUpload.written != wallpapers::kWallpaperFileBytes) {
      Storage.remove(wallUpload.target.c_str());
      wallUpload.refusal = "The picture did not arrive completely.";
      return;
    }
    Storage.remove(wallUpload.final.c_str());
    if (!Storage.rename(wallUpload.target.c_str(), wallUpload.final.c_str())) {
      Storage.remove(wallUpload.target.c_str());
      wallUpload.refusal = "The card refused to name the file.";
      return;
    }
    wallUpload.ok = true;
    LOG_INF("WALL", "wallpaper saved: %s", wallUpload.final.c_str());
  }
#endif
}

void CrossPointWebServer::handleWallpaperUpload() {
  // Read ONCE and clear, the lesson /api/dev/upload paid for: ok is only ever
  // set by the raw callback, so a PUT that never reaches it -- no body, or a
  // body the core drops -- would otherwise inherit the PREVIOUS request's
  // success and be answered 204. That is a success reported for an upload that
  // did not happen, and the phone would say "it is on your reader".
  const bool uploaded = wallUpload.ok;
  const char* refusal = wallUpload.refusal;
  wallUpload.ok = false;
  wallUpload.refusal = nullptr;

  if (uploaded) {
    server->send(204, "text/plain", "");
    return;
  }
  server->send(refusal != nullptr ? 507 : 400, "text/plain",
               refusal != nullptr ? refusal : "The reader did not receive a picture.");
}

void CrossPointWebServer::handleDevUploadData() {
#ifdef SIMULATOR
  // The simulator's WebServer shim has no raw() body API, and the emulator has
  // neither a radio to reach nor flash to write. A PLATFORM gate, not a feature
  // gate: Developer Mode still ships in every device build including releases,
  // which is the whole point of it not being a build flag.
  return;
#else
  HTTPRaw& raw = server->raw();

  if (raw.status == RAW_START) {
    // Decide authorisation here, because by RAW_END the whole body would
    // already be on the card -- but do NOT answer here. Sending a response
    // while the body is still being parsed corrupts the server.
    devUpload.authorised = devTokenOk();
    devUpload.written = 0;
    devUpload.ok = false;
    if (!devUpload.authorised) return;
    // NOT a watchdog guard for the unauthorised case, whatever it looks like:
    // the body is drained in RAW_WRITE chunks that this return never reaches,
    // and resetTaskWatchdogIfSubscribed() is a no-op across this whole
    // firmware anyway -- nothing subscribes loopTask to the TWDT. The real
    // exposure is in Known limits: an unauthenticated client can hold loop()
    // by trickling a body, which is a freeze, not a reset.
    resetTaskWatchdogIfSubscribed();
    Storage.remove(kDevUploadPath);
    if (!Storage.openFileForWrite("DEVMODE", kDevUploadPath, devUpload.file)) {
      LOG_ERR("DEVMODE", "cannot open %s for write", kDevUploadPath);
      return;
    }
    LOG_INF("DEVMODE", "receiving firmware -> %s", kDevUploadPath);
    return;
  }

  if (!devUpload.authorised) return;

  if (raw.status == RAW_WRITE) {
    resetTaskWatchdogIfSubscribed();
    if (devUpload.file && devUpload.file.write(raw.buf, raw.currentSize) == raw.currentSize) {
      devUpload.written += raw.currentSize;
    } else {
      LOG_ERR("DEVMODE", "write failed at %u bytes", static_cast<unsigned>(devUpload.written));
      devUpload.file.close();
    }
    return;
  }

  if (raw.status == RAW_END) {
    if (devUpload.file) {
      devUpload.file.close();
      devUpload.ok = devUpload.written > 0;
      LOG_INF("DEVMODE", "received %u bytes", static_cast<unsigned>(devUpload.written));
    }
    return;
  }

  if (raw.status == RAW_ABORTED) {
    LOG_ERR("DEVMODE", "upload aborted at %u bytes", static_cast<unsigned>(devUpload.written));
    devUpload.file.close();
    devUpload.ok = false;
  }
#endif
}

void CrossPointWebServer::handleDevUpload() {
  // A request with no multipart body never reaches the data callback at all, so
  // devUpload.authorised is still false from the previous request. Re-test here
  // rather than trust it, and let devAuthorised() send the refusal.
  if (!devAuthorised()) {
    devUpload.authorised = false;
    devUpload.ok = false;  // same reason as the consume below
    return;
  }
  // Read ONCE and clear. ok is only ever set by the raw callback, so a PUT that
  // never reaches it -- no body, or a body the core drops -- would otherwise
  // inherit the previous request's success and be answered 200 with the
  // previous path and size. That is a success reported for an upload that did
  // not happen, and wifi-flash.sh would go on to ask the device to flash it.
  const bool uploaded = devUpload.ok;
  devUpload.ok = false;
  if (!uploaded) {
    server->send(500, "text/plain", "upload failed\n");
    return;
  }
  JsonDocument doc;
  doc["path"] = kDevUploadPath;
  doc["size"] = devUpload.written;
  String out;
  serializeJson(doc, out);
  server->send(200, "application/json", out);
}

// Flash an image already sitting on the SD card, so `scripts_local/wifi-flash.sh`
// is two ordinary requests: POST /upload to put firmware.bin on the card (the
// same route that uploads books, no size cap), then this to install it.
//
// Deliberately NOT an upload endpoint of its own. Streaming straight into the
// OTA partition would save one SD write, but it would also mean a second,
// separately-written path into esp_ota_write that does not share
// validateImageFile's magic/segment/checksum/SHA/chip/board checks with the SD
// and OTA paths. One flasher, three callers.
//
// Synchronous on purpose: the response IS the result. The flash blocks the
// server task for a minute or so, which also blocks the UI, and that is the
// right trade for a dev-build-only tool -- the alternative is a 202 and a
// caller that has to guess. Point curl at --max-time 300.
void CrossPointWebServer::handleDevFlash() {
  if (!devAuthorised()) return;
  // Defaults to whatever /api/dev/upload just wrote, so the ordinary flow is
  // upload-then-flash with no path bookkeeping in the caller.
  const String path = server->hasArg("path") ? server->arg("path") : String(kDevUploadPath);

  const esp_partition_t* dest = esp_ota_get_next_update_partition(nullptr);
  if (!dest) {
    LOG_ERR("DEVFLASH", "no OTA partition");
    server->send(500, "text/plain", "no OTA partition\n");
    return;
  }

  LOG_INF("DEVFLASH", "validating %s against '%s' (%u bytes)", path.c_str(), dest->label,
          static_cast<unsigned>(dest->size));
  const auto vr = firmware_flash::validateImageFile(path.c_str(), dest->size);
  if (vr != firmware_flash::Result::OK) {
    // The name is the diagnosis: TOO_LARGE means this device's table predates
    // the repartition, WRONG_BOARD means a sticky image on an x4pro, and
    // OPEN_FAIL means the upload never landed.
    LOG_ERR("DEVFLASH", "validate %s: %s", path.c_str(), firmware_flash::resultName(vr));
    server->send(422, "text/plain", String(firmware_flash::resultName(vr)) + "\n");
    return;
  }

  LOG_INF("DEVFLASH", "flashing %s -> %s", path.c_str(), dest->label);
  const auto fr = firmware_flash::flashFromSdPath(path.c_str(), nullptr, nullptr, true);
  if (fr != firmware_flash::Result::OK) {
    LOG_ERR("DEVFLASH", "flash failed: %s", firmware_flash::resultName(fr));
    server->send(500, "text/plain", String(firmware_flash::resultName(fr)) + "\n");
    return;
  }

  // Answer before rebooting, or the caller cannot tell success from a device
  // that fell off the network. send() hands the body to lwIP but does not wait
  // for the peer's ACK, so give the socket a moment to drain before the reset.
  LOG_INF("DEVFLASH", "flashed, restarting");
  server->send(200, "text/plain", "OK flashed, restarting\n");
#ifndef SIMULATOR
  server->client().flush();  // the simulator's NetworkClient shim has no flush()
#endif
  delay(250);
  ESP.restart();
}

#include "CrossPointWebServer.h"

#include <ArduinoJson.h>
#include <BoardConfig.h>
#include <FsHelpers.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_efuse.h>
#include <esp_efuse_table.h>
#include <esp_ota_ops.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "CrossPointSettings.h"
#include "DevInputCommands.h"
#include "DevMode.h"
#include "DevSerialBridge.h"
#include "FirmwareFlasher.h"
#include "FontInstaller.h"
#include "OpdsServerStore.h"
#include "SdCardFontSystem.h"
#include "SettingsList.h"
#include "WebDAVHandler.h"
#include "WifiCredentialStore.h"
#include "apps_local/wallpapers/WallpapersCore.h"
#include "html/FilesPageHtml.generated.h"
#include "html/FontsPageHtml.generated.h"
#include "html/HomePageHtml.generated.h"
#include "html/SettingsPageHtml.generated.h"
#include "html/WallpaperPageHtml.generated.h"
#include "html/js/jszip_minJs.generated.h"
#include "html/js/wallconvertJs.generated.h"
#include "util/BookCacheUtils.h"
#include "util/TaskWatchdog.h"

namespace {
// Folders/files to hide from the web interface file browser
// Note: Items starting with "." are automatically hidden
constexpr const char* HIDDEN_ITEMS[] = {"System Volume Information", "XTCache"};
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
  server.reset(new WebServer(port));

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

  // The developer surface. Present for Full and DeveloperOnly and DELIBERATELY
  // NOT for WallpapersOnly: these routes flash firmware, and the whole point of
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
  // paired request looks unpaired.
  const char* davHeaders[] = {"Depth", "Destination", "Overwrite", "If", "Lock-Token", "Timeout", "X-Dev-Token"};
  server->collectHeaders(davHeaders, 7);
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

static void sendHtmlContent(WebServer* server, const char* data, size_t len) {
  server->sendHeader("Content-Encoding", "gzip");
  server->send_P(200, "text/html", data, len);
}

void CrossPointWebServer::handleRoot() const {
  sendHtmlContent(server.get(), HomePageHtml, sizeof(HomePageHtml));
  LOG_DBG("WEB", "Served root page");
}

void CrossPointWebServer::handleJszip() const {
  server->sendHeader("Content-Encoding", "gzip");
  server->send_P(200, "application/javascript", jszip_minJs, jszip_minJsCompressedSize);
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
  sendHtmlContent(server.get(), FilesPageHtml, sizeof(FilesPageHtml));
}

void CrossPointWebServer::handleFileListData() const {
  // Get current path from query string (default to root)
  String currentPath = "/";
  if (server->hasArg("path")) {
    currentPath = server->arg("path");
    // Ensure path starts with /
    if (!currentPath.startsWith("/")) {
      currentPath = "/" + currentPath;
    }
    // Remove trailing slash unless it's root
    if (currentPath.length() > 1 && currentPath.endsWith("/")) {
      currentPath = currentPath.substring(0, currentPath.length() - 1);
    }
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

void CrossPointWebServer::handleDownload() const {
  if (!server->hasArg("path")) {
    server->send(400, "text/plain", "Missing path");
    return;
  }

  String itemPath = server->arg("path");
  if (itemPath.isEmpty() || itemPath == "/") {
    server->send(400, "text/plain", "Invalid path");
    return;
  }
  if (!itemPath.startsWith("/")) {
    itemPath = "/" + itemPath;
  }

  const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);
  if (itemName.startsWith(".")) {
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

  NetworkClient client = server->client();
  const size_t chunkSize = 4096;
  uint8_t buffer[chunkSize];

  bool downloadOk = true;
  while (downloadOk && file.available()) {
    int result = file.read(buffer, chunkSize);
    if (result <= 0) break;
    size_t bytesRead = static_cast<size_t>(result);
    size_t totalWritten = 0;
    while (totalWritten < bytesRead) {
      resetTaskWatchdogIfSubscribed();
      size_t wrote = client.write(buffer + totalWritten, bytesRead - totalWritten);
      if (wrote == 0) {
        downloadOk = false;
        break;
      }
      totalWritten += wrote;
    }
  }
  client.clear();
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
    const size_t written = state.file.write(state.buffer.data(), state.bufferPos);
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

    // Get upload path from query parameter (defaults to root if not specified)
    // Note: We use query parameter instead of form data because multipart form
    // fields aren't available until after file upload completes
    if (server->hasArg("path")) {
      state.path = server->arg("path");
      // Ensure path starts with /
      if (!state.path.startsWith("/")) {
        state.path = "/" + state.path;
      }
      // Remove trailing slash unless it's root
      if (state.path.length() > 1 && state.path.endsWith("/")) {
        state.path = state.path.substring(0, state.path.length() - 1);
      }
    } else {
      state.path = "/";
    }

    LOG_DBG("WEB", "[UPLOAD] START: %s to path: %s", state.fileName.c_str(), state.path.c_str());
    LOG_DBG("WEB", "[UPLOAD] Free heap: %d bytes", ESP.getFreeHeap());

    String filePath = state.path;
    if (!filePath.endsWith("/")) filePath += "/";
    filePath += state.fileName;

    // Check if file already exists - SD operations can be slow
    resetTaskWatchdogIfSubscribed();
    if (Storage.exists(filePath.c_str())) {
      state.error = "File already exists: " + state.fileName;
      LOG_DBG("WEB", "[UPLOAD] Collision: %s", filePath.c_str());
      return;
    }

    // Open file for writing - this can be slow due to FAT cluster allocation
    resetTaskWatchdogIfSubscribed();
    if (!Storage.openFileForWrite("WEB", filePath, state.file)) {
      state.error = "Failed to create file on SD card";
      LOG_DBG("WEB", "[UPLOAD] FAILED to create file: %s", filePath.c_str());
      return;
    }
    resetTaskWatchdogIfSubscribed();

    LOG_DBG("WEB", "[UPLOAD] File created successfully: %s", filePath.c_str());
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (state.file && state.error.isEmpty()) {
      // Buffer incoming data and flush when buffer is full
      // This reduces SD card write operations and improves throughput
      const uint8_t* data = upload.buf;
      size_t remaining = upload.currentSize;

      while (remaining > 0) {
        const size_t space = UploadState::UPLOAD_BUFFER_SIZE - state.bufferPos;
        const size_t toCopy = (remaining < space) ? remaining : space;

        memcpy(state.buffer.data() + state.bufferPos, data, toCopy);
        state.bufferPos += toCopy;
        data += toCopy;
        remaining -= toCopy;

        // Flush buffer when full
        if (state.bufferPos >= UploadState::UPLOAD_BUFFER_SIZE) {
          if (!flushUploadBuffer(state)) {
            state.error = "Failed to write to SD card - disk may be full";
            state.file.close();
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
    if (state.file) {
      // Flush any remaining buffered data
      if (!flushUploadBuffer(state)) {
        state.error = "Failed to write final data to SD card";
      }
      state.file.close();

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
      }
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    state.bufferPos = 0;  // Discard buffered data
    if (state.file) {
      state.file.close();
      // Try to delete the incomplete file
      String filePath = state.path;
      if (!filePath.endsWith("/")) filePath += "/";
      filePath += state.fileName;
      Storage.remove(filePath.c_str());
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

  // Get parent path
  String parentPath = "/";
  if (server->hasArg("path")) {
    parentPath = server->arg("path");
    if (!parentPath.startsWith("/")) {
      parentPath = "/" + parentPath;
    }
    if (parentPath.length() > 1 && parentPath.endsWith("/")) {
      parentPath = parentPath.substring(0, parentPath.length() - 1);
    }
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

  if (Storage.exists(newPath.c_str())) {
    file.close();
    server->send(409, "text/plain", "Target already exists");
    return;
  }

  clearBookCache(itemPath.c_str());
  const bool success = file.rename(newPath.c_str());
  file.close();

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
  const bool success = file.rename(newPath.c_str());
  file.close();

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
    auto itemPath = p.as<String>();

    // Validate path
    if (itemPath.isEmpty() || itemPath == "/") {
      failedItems += itemPath + " (cannot delete root); ";
      allSuccess = false;
      continue;
    }

    // Ensure path starts with /
    if (!itemPath.startsWith("/")) {
      itemPath = "/" + itemPath;
    }

    // Security check: prevent deletion of protected items
    const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);

    // Hidden/system files are protected
    if (itemName.startsWith(".")) {
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
      success = Storage.remove(itemPath.c_str());
      clearBookCache(itemPath.c_str());
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
  sendHtmlContent(server.get(), SettingsPageHtml, sizeof(SettingsPageHtml));
  LOG_DBG("WEB", "Served settings page");
}

void CrossPointWebServer::handleGetSettings() const {
  // Pass the SD font registry so the fontFamily setting's enumStringValues
  // includes SD-resident families — otherwise the web API only exposes the
  // three built-in fonts.
  const auto& settings = getSettingsList(&sdFontSystem.registry());

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
          for (const auto& opt : s.enumValues) {
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
        if (s.stringGetter) {
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

  const auto& settings = getSettingsList(&sdFontSystem.registry());
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
        const int maxVal = s.enumStringValues.empty() ? static_cast<int>(s.enumValues.size())
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
          wsUploadPath = msg.substring(secondColon + 1);
          wsUploadReceived = 0;
          wsLastProgressSent = 0;
          wsUploadStartTime = millis();

          // Ensure path is valid
          if (!wsUploadPath.startsWith("/")) wsUploadPath = "/" + wsUploadPath;
          if (wsUploadPath.length() > 1 && wsUploadPath.endsWith("/")) {
            wsUploadPath = wsUploadPath.substring(0, wsUploadPath.length() - 1);
          }

          String filePath = wsUploadPath;
          if (!filePath.endsWith("/")) filePath += "/";
          filePath += wsUploadFileName;

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
  sendHtmlContent(server.get(), FontsPageHtml, sizeof(FontsPageHtml));
  LOG_DBG("WEB", "Served fonts page");
}

void CrossPointWebServer::handleFontList() const {
  // Pick up any uploads/deletes that happened since the last reader load.
  const_cast<SdCardFontSystem&>(sdFontSystem).refreshIfDirty();
  const auto& families = sdFontSystem.registry().getFamilies();

  JsonDocument doc;
  JsonArray arr = doc["families"].to<JsonArray>();
  doc["maxFamilies"] = SdCardFontRegistry::MAX_SD_FAMILIES;

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

  String json;
  serializeJson(doc, json);
  server->send(200, "application/json", json);
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
      fontUpload.bytesWritten = 0;
      fontUpload.bufferPos = 0;

      if (!FontInstaller::isValidFamilyName(family.c_str())) {
        LOG_ERR("WEB", "Invalid font family name: %s", family.c_str());
        break;
      }

      String filename = upload.filename;
      filename.replace(' ', '_');
      // Validate filename: rejects path traversal (../, /, \) and enforces
      // a .cpfont basename of alphanumeric + hyphen + underscore. Without
      // this an attacker could supply "../../.crosspoint/settings.json" as
      // a "filename" and have it written outside the fonts directory.
      if (!FontInstaller::isValidCpfontFilename(filename.c_str())) {
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

      char path[128];
      FontInstaller::buildFontPath(family.c_str(), filename.c_str(), path, sizeof(path));
      fontUpload.filePath = path;

      if (!Storage.openFileForWrite("WEB", path, fontUpload.file)) {
        LOG_ERR("WEB", "Failed to open font file for write: %s", path);
        break;
      }

      fontUpload.valid = true;
      LOG_DBG("WEB", "Font upload started: %s -> %s", filename.c_str(), path);
      break;
    }

    case UPLOAD_FILE_WRITE: {
      if (!fontUpload.valid) break;
      resetTaskWatchdogIfSubscribed();

      // Validate magic bytes on first chunk only
      if (!fontUpload.magicChecked && upload.currentSize >= 8) {
        if (memcmp(upload.buf, "CPFONT\0\0", 8) != 0) {
          LOG_ERR("WEB", "Invalid .cpfont magic bytes");
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
        memcpy(fontUpload.buffer.data() + fontUpload.bufferPos, src, chunk);
        fontUpload.bufferPos += chunk;
        src += chunk;
        remaining -= chunk;

        if (fontUpload.bufferPos >= FontUploadState::BUFFER_SIZE) {
          fontUpload.file.write(fontUpload.buffer.data(), fontUpload.bufferPos);
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
        fontUpload.file.write(fontUpload.buffer.data(), fontUpload.bufferPos);
        fontUpload.bytesWritten += fontUpload.bufferPos;
        fontUpload.bufferPos = 0;
      }
      if (fontUpload.file.isOpen()) {
        fontUpload.file.close();
      }

      if (!fontUpload.valid && !fontUpload.filePath.empty()) {
        Storage.remove(fontUpload.filePath.c_str());
      }

      LOG_DBG("WEB", "Font upload end: valid=%d, %zu bytes", fontUpload.valid, fontUpload.bytesWritten);
      break;
    }

    case UPLOAD_FILE_ABORTED: {
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
    server->send(400, "application/json", "{\"error\":\"Invalid .cpfont file\"}");
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

void CrossPointWebServer::handleWallpaperPage() const {
  sendHtmlContent(server.get(), WallpaperPageHtml, sizeof(WallpaperPageHtml));
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
  for (int i = 1; i <= 9999; ++i) {
    char name[48];
    std::snprintf(name, sizeof(name), "%s/w%04d.bmp", wallpapers::kLibraryDir, i);
    if (!Storage.exists(name)) return std::string(name);
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

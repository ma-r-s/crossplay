#pragma once

#include <HalStorage.h>
#include <NetworkUdp.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

#include <memory>
#include <string>
#include <vector>

// Structure to hold file information
struct FileInfo {
  String name;
  size_t size;
  bool isEpub;
  bool isDirectory;
};

class CrossPointWebServer {
 public:
  struct WsUploadStatus {
    bool inProgress = false;
    size_t received = 0;
    size_t total = 0;
    std::string filename;
    std::string lastCompleteName;
    size_t lastCompleteSize = 0;
    unsigned long lastCompleteAt = 0;
  };

  // Used by POST upload handler
  struct UploadState {
    HalFile file;
    String fileName;
    String path = "/";
    size_t size = 0;
    bool success = false;
    String error = "";

    // Upload write buffer - batches small writes into larger SD card operations
    // 4KB is a good balance: large enough to reduce syscall overhead, small enough
    // to keep individual write times short and avoid watchdog issues
    static constexpr size_t UPLOAD_BUFFER_SIZE = 4096;  // 4KB buffer
    std::vector<uint8_t> buffer;
    size_t bufferPos = 0;

    UploadState() { buffer.resize(UPLOAD_BUFFER_SIZE); }
  } upload;

  // WHICH SURFACE this server exposes. Three, not a boolean, because the reason
  // the old flag existed applies again with a different answer.
  //
  // The reader's web UI is unauthenticated by design -- a deliberate, temporary
  // thing you open from a screen and close again. DeveloperOnly exists because
  // dev mode keeps its server up for as long as the toggle is on, and serving
  // the file manager persistently would quietly turn "I left dev mode on" into
  // "anyone on this network can browse my card".
  //
  // WallpapersOnly exists for the same reason one step further: the Wallpapers
  // app puts an address in a QR CODE and invites you to scan it, which is the
  // opposite of deliberate-and-temporary. Reusing Full there would have meant
  // /files, /download, /delete, /api/settings, /api/wifi (the saved network
  // list) and WebDAV over the whole card, all reachable from a code printed on
  // a screen. It serves one page and takes one upload.
  enum class Surface : uint8_t {
    Full,            // the reader's web UI: file manager, settings, WebDAV, WebSocket
    DeveloperOnly,   // /api/dev/* and nothing else
    WallpapersOnly,  // GET /w, its script, and PUT /w/upload. No dev routes either.
  };

  explicit CrossPointWebServer(Surface surface = Surface::Full);
  ~CrossPointWebServer();

  // Start the web server (call after WiFi is connected)
  void begin();

  // Stop the web server
  void stop();

  // Call this periodically to handle client requests
  void handleClient();

  // Check if server is running
  bool isRunning() const { return running; }

  WsUploadStatus getWsUploadStatus() const;

  // Get the port number
  uint16_t getPort() const { return port; }

 private:
  std::unique_ptr<WebServer> server = nullptr;
  std::unique_ptr<WebSocketsServer> wsServer = nullptr;
  bool running = false;
  const Surface surface = Surface::Full;
  bool isFull() const { return surface == Surface::Full; }
  bool isDev() const { return surface == Surface::DeveloperOnly; }
  bool isWallpapers() const { return surface == Surface::WallpapersOnly; }

  // The wallpaper upload, streamed straight to the card. Separate from
  // UploadState because it shares nothing with the multipart path: no
  // filename from the client, no directory from a query string.
  struct WallUpload {
    HalFile file;
    std::string target;  // the .part being written
    std::string final;   // where it is renamed on success
    size_t written = 0;
    bool accepted = false;  // the precondition passed and the file opened
    bool ok = false;        // the body arrived complete and the rename worked
    const char* refusal = nullptr;
  } wallUpload;
  bool apMode = false;  // true when running in AP mode, false for STA mode
  uint16_t port = 80;
  uint16_t wsPort = 81;  // WebSocket port
  NetworkUDP udp;
  bool udpActive = false;

  // WebSocket upload state
  void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
  static void wsEventCallback(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
  void abortWsUpload(const char* tag);

  // File scanning
  void scanFiles(const char* path, const std::function<void(FileInfo)>& callback) const;
  String formatFileSize(size_t bytes) const;
  bool isEpubFile(const String& filename) const;

  // Request handlers
  void handleRoot() const;
  void handleJszip() const;
  void handleNotFound() const;
  void handleStatus() const;
  void handleFileList() const;
  void handleFileListData() const;
  void handleDownload() const;
  void handleUpload(UploadState& state) const;
  void handleUploadPost(UploadState& state) const;
  void handleCreateFolder() const;
  void handleRename() const;
  void handleMove() const;
  void handleDelete() const;

  // Settings handlers
  void handleSettingsPage() const;
  void handleGetSettings() const;
  void handlePostSettings();

  // Font management handlers
  void handleFontsPage() const;
  void handleFontList() const;
  void handleFontUpload();
  void handleFontUploadData();
  void handleFontDelete();

  // Font upload state
  struct FontUploadState {
    HalFile file;
    std::string familyName;
    std::string filePath;
    bool valid = false;
    bool magicChecked = false;
    size_t bytesWritten = 0;
    static constexpr size_t BUFFER_SIZE = 4096;
    std::vector<uint8_t> buffer;
    size_t bufferPos = 0;

    FontUploadState() { buffer.resize(BUFFER_SIZE); }
  } fontUpload;

  // OPDS server handlers
  void handleGetOpdsServers() const;
  void handlePostOpdsServer();
  void handleDeleteOpdsServer();

  // Developer Mode endpoints. Present in every build, refused unless the
  // setting is on AND the caller carries a token from a successful pair.
  void handleDevPair();

  // The Wallpapers surface.
  void handleWallpaperPage() const;
  void handleWallpaperScript() const;
  void handleWallpaperUpload();      // the reply, after the body
  void handleWallpaperUploadData();  // the raw body, streamed
  void handleDevFlash();
  void handleDevUpload();      // POST completion
  void handleDevUploadData();  // streaming body
  void handleDevDisable();
  void handleDevCrash();
  void handleDevLog();
#if CROSSPOINT_DEV_SERIAL_BRIDGE
  void handleDevInput();
  void handleDevScreen();
  void handleDevSerial();
#endif
  struct DevUploadState {
    HalFile file;
    size_t written = 0;
    bool ok = false;
    bool authorised = false;  // decided at UPLOAD_FILE_START, answered at the end
  } devUpload;
  // Shared gate for every /api/dev/ route except pairing. Sends the refusal
  // itself and returns false, so each handler is one line of guard.
  // True for settings that must never be writable over the network, whatever
  // the surface. See the definition for why devMode is one.
  static bool isLocalOnlySetting(const char* key);
  bool devAuthorised();
  // Same test as devAuthorised() but SILENT. The upload data callback runs
  // while the request body is still being parsed, and calling server->send()
  // there corrupts the server and reboots the device -- a remote reset any
  // unpaired caller could trigger. So the callback decides with this and the
  // completion handler is the only thing that answers.
  bool devTokenOk() const;

  // Wi-Fi credential handlers
  void handleGetWifiNetworks() const;
  void handlePostWifiNetwork();
  void handleDeleteWifiNetwork();
};

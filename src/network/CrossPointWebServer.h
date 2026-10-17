#pragma once

#include <ArduinoJson.h>
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
    std::unique_ptr<uint8_t[]> buffer;
    size_t bufferPos = 0;
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
    // NotesOnly exists for the same reason one step further again: the Notes
    // app puts an address in a QR code and invites a phone to scan it, and what
    // is behind that code is ONE note -- the one the reader has open -- not the
    // card. It serves one page and reads and writes one file, whose path the
    // app sets before begin() and the client can never name. There is nothing
    // to validate because nothing is accepted.
    NotesOnly,
  };

  explicit CrossPointWebServer(Surface surface = Surface::Full);

  // The one file the NotesOnly surface reads and writes, and the name to show
  // on the page. Set before begin(); the client never names either, which
  // deletes the traversal question rather than answering it.
  // `isList` is the KIND, from the reader, which is the only side that knows it
  // for an empty note: a note made with + NOTE and a list made with + LIST are
  // both an empty file until their first line.
  void setNotesFile(const std::string& path, const std::string& displayName, const bool isList) {
    notesPath = path;
    notesName = displayName;
    notesIsList = isList;
  }
  // True once a client has saved, so the app knows to re-read the file rather
  // than polling the card. Cleared by the reader of it.
  bool takeNotesChanged() {
    const bool changed = notesChanged;
    notesChanged = false;
    return changed;
  }
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

  // An HTTP upload holds handleClient() until its body is read, so this is polled on each chunk.
  // Returning true drops the client: the upload aborts and its partial file is removed.
  void setUploadCancelCheck(std::function<bool()> check) { uploadCancelCheck = std::move(check); }

  // Get the port number
  uint16_t getPort() const { return port; }

 private:
  std::unique_ptr<WebServer> server = nullptr;
  std::unique_ptr<WebSocketsServer> wsServer = nullptr;
  bool running = false;
  const Surface surface = Surface::Full;
  std::string notesPath;
  std::string notesName;
  bool notesIsList = true;
  bool notesChanged = false;
  bool isFull() const { return surface == Surface::Full; }
  bool isDev() const { return surface == Surface::DeveloperOnly; }
  bool isWallpapers() const { return surface == Surface::WallpapersOnly; }
  bool isNotes() const { return surface == Surface::NotesOnly; }

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
  std::function<bool()> uploadCancelCheck;
  bool dropUploadIfCancelled() const;

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
  // Streams an already-open file to the client in 4KB chunks, feeding the
  // watchdog per write and aborting cleanly (rather than looping past a dead
  // connection) if a write stalls. Caller sets headers/content-length and
  // closes the file.
  void streamFileToClient(HalFile& file) const;
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
    bool isVector = false;  // .ttf/.otf upload (PSRAM boards only) vs .cpfont
    size_t bytesWritten = 0;
    static constexpr size_t BUFFER_SIZE = 4096;
    std::unique_ptr<uint8_t[]> buffer;
    size_t bufferPos = 0;
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
  void handleNotesPage() const;
  void handleNotesText();
  void handleNotesSave();
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

  // Missing or malformed JSON sends a 400 response and returns false.
  bool readJsonBody(JsonDocument& out) const;
  void sendJson(const JsonDocument& doc) const;
  void handlePluginList() const;  // GET  /api/plugins   -> discovered plugins
  void handlePluginFile() const;  // GET  /plugin?name&file -> serve SD file
  void handleRelay();             // POST /api/relay     -> device makes an HTTP(S) call
  void handleCrypto();            // POST /api/crypto    -> generic crypto primitive (base64 I/O)
  void handleFetch();             // POST /api/fetch     -> device downloads a URL to SD
  void handleBookKey();           // POST /api/book-key  -> store a protected book's wrapped content key
  void handlePluginFs();          // POST /api/plugin-fs -> plugin writes a small file to SD
  void handlePluginFsUpload();    // its multipart file part, streamed to <path>.tmp

  // One /api/plugin-fs write in flight: chunks land in `tmp`, which replaces
  // `path` only after a complete, non-empty body.
  struct PluginFsUploadState {
    HalFile file;
    std::string path, tmp;
    size_t bytes = 0;
    bool started = false;
    int errorStatus = 0;  // non-zero: HTTP status to answer with
    const char* error = nullptr;
  } pluginFsUpload;

  // SD-plugin job queue. External systems (a companion app, a script) enqueue
  // {plugin, action, args}; any open page hosting the plugin (File Manager,
  // Settings, or the headless /plugins-run page) claims and executes it, then
  // posts the result. The firmware only stores small JSON blobs — plugin logic
  // never runs on-device. Fixed pool inside this (heap-allocated, web-session
  // lifetime) object: no allocation per job, oldest finished slot recycled.
  struct PluginJob {
    uint32_t id = 0;         // 0 = empty slot
    uint32_t claim = 0;      // current claim; a completion must echo it
    uint32_t updatedAt = 0;  // millis() of last state change
    uint8_t state = 0;
    char plugin[24] = {0};
    char action[24] = {0};
    char args[192] = {0};    // Serialized JSON value
    char result[192] = {0};  // Serialized JSON value from the executor
  };
  static constexpr uint8_t JOB_EMPTY = 0;
  static constexpr uint8_t JOB_PENDING = 1;
  static constexpr uint8_t JOB_RUNNING = 2;
  static constexpr uint8_t JOB_DONE = 3;
  static constexpr uint8_t JOB_ERROR = 4;
  static constexpr size_t MAX_PLUGIN_JOBS = 6;
  static constexpr uint32_t PLUGIN_JOB_LEASE_MS = 10UL * 60 * 1000;
  PluginJob pluginJobs[MAX_PLUGIN_JOBS];
  uint32_t nextPluginJobId = 1;
  uint32_t nextPluginJobClaim = 1;
  PluginJob* allocPluginJob();
  void handlePluginRunnerPage() const;  // GET /plugins-run -> headless executor page
  void handlePluginJobSubmit();         // POST /api/plugin-jobs          -> {id}
  void handlePluginJobClaim();          // GET  /api/plugin-jobs/claim    -> next pending job for a plugin
  void handlePluginJobComplete();       // POST /api/plugin-jobs/complete -> executor posts the outcome
  void handlePluginJobStatus();         // GET  /api/plugin-jobs/status   -> external caller polls

  // An outbound transfer blocks the serving task for its whole duration, so
  // the WebSocket server and discovery UDP cannot answer anyone until it
  // finishes; their buffers are worth more as TLS headroom (4.4KB heap floor
  // measured with them resident during a large transfer).
  void suspendTransferServices();
  void resumeTransferServices();
};

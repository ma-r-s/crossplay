#pragma once

#include <Print.h>
#include <common/FsApiConstants.h>  // for oflag_t
#include <freertos/semphr.h>

#include <memory>
#include <string>
#include <vector>

class HalFile;

class HalStorage {
 public:
  HalStorage();
  bool begin();
  bool ready() const;
  std::vector<String> listFiles(const char* path = "/", int maxFiles = 200);
  // Read the entire file at `path` into a String. Returns empty string on failure.
  String readFile(const char* path);
  // Low-memory helpers:
  // Stream the file contents to a `Print` (e.g. `Serial`, or any `Print`-derived object).
  // Returns true on success, false on failure.
  bool readFileToStream(const char* path, Print& out, size_t chunkSize = 256);
  // Read up to `bufferSize-1` bytes into `buffer`, null-terminating it. Returns bytes read.
  size_t readFileToBuffer(const char* path, char* buffer, size_t bufferSize, size_t maxBytes = 0);
  // Write a string to `path` on the SD card. Overwrites existing file.
  // Returns true on success.
  bool writeFile(const char* path, const String& content);
  // Ensure a directory exists, creating it if necessary. Returns true on success.
  bool ensureDirectoryExists(const char* path);

  HalFile open(const char* path, const oflag_t oflag = O_RDONLY);
  bool mkdir(const char* path, const bool pFlag = true);
  bool exists(const char* path);

  // Free bytes on the card. Returns false, leaving `out` untouched, when the
  // volume could not answer -- which is NOT the same as "no space".
  //
  // Every app on this fork that writes to the card has written blind, because
  // nothing above SDCardManager surfaced this. Trivia declined to ship a
  // 6.21MB download onto the one card holding a live Anki collection for
  // exactly that reason. Ask before a large write, and treat false as unknown
  // rather than as room: deriving it from sdTotalBytes() - sdUsedBytes()
  // reports a FAILED query as an almost-empty card.
  //
  // Costs a FAT walk at most once per 20s (SDCardManager caches it); on FAT32
  // without a valid FSInfo that walk is seconds, so ask once before a write
  // rather than per chunk.
  bool freeBytes(uint64_t& out);
  bool remove(const char* path);
  bool rename(const char* oldPath, const char* newPath);
  bool rmdir(const char* path);

  bool openFileForRead(const char* moduleName, const char* path, HalFile& file);
  bool openFileForRead(const char* moduleName, const std::string& path, HalFile& file);
  bool openFileForRead(const char* moduleName, const String& path, HalFile& file);
  bool openFileForWrite(const char* moduleName, const char* path, HalFile& file);
  bool openFileForWrite(const char* moduleName, const std::string& path, HalFile& file);
  bool openFileForWrite(const char* moduleName, const String& path, HalFile& file);
  // Open positioned at the end, creating the file if it is not there.
  // openFileForWrite() carries O_TRUNC, so it cannot be used to add to a file
  // that already has something in it. Added for the xkcd pack, which grows by
  // a few records when the device fetches the comics published since the pack
  // was built; the alternative was copying a 90MB file to add 30KB.
  bool openFileForAppend(const char* moduleName, const char* path, HalFile& file);
  // Read-write in place: no O_TRUNC and no append, so a seek followed by a
  // write actually lands where the seek asked. Needed to patch a header in a
  // file you are not rewriting -- openFileForWrite would destroy it, and
  // openFileForAppend sends every write to the end on hosts where it maps to
  // O_APPEND, which silently made the header land at EOF.
  bool openFileForUpdate(const char* moduleName, const char* path, HalFile& file);
  bool removeDir(const char* path);

  static HalStorage& getInstance() { return instance; }

  class StorageLock;  // private class, used internally

 private:
  static HalStorage instance;

  bool initialized = false;
  SemaphoreHandle_t storageMutex = nullptr;
};

#define Storage HalStorage::getInstance()

class HalFile : public Print {
  friend class HalStorage;
  class Impl;
  std::unique_ptr<Impl> impl;
  explicit HalFile(std::unique_ptr<Impl> impl);

 public:
  HalFile();
  ~HalFile();
  HalFile(HalFile&&);
  HalFile& operator=(HalFile&&);
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;

  void flush();  // no-op on a never-assigned handle
  size_t getName(char* name, size_t len);
  size_t size();
  size_t fileSize();
  uint64_t fileSize64();
  bool seek(size_t pos);
  bool seek64(uint64_t pos);
  bool seekCur(int64_t offset);
  bool seekSet(size_t offset);
  int available() const;
  size_t position() const;
  int read(void* buf, size_t count);
  int read();  // read a single byte
  size_t write(const uint8_t* buf, size_t count) override;
  size_t write(const void* buf, size_t count);
  size_t write(uint8_t b) override;
  bool rename(const char* newPath);
  bool isDirectory() const;
  void rewindDirectory();
  bool close();  // returns false on a never-assigned handle
  HalFile openNextFile();
  bool isOpen() const;
  operator bool() const;
};

// Downstream code must use Storage instead of SdMan
#ifdef SdMan
#undef SdMan
#endif

#pragma once
// A card that behaves like the device's, for StudyBridgeFile.
//
// The one property that matters is the one the simulator gets wrong: SdFat's
// open(path, O_CREAT) fails when the parent folder does not exist, and the
// simulator's HalStorage creates every missing folder on the way. So a write
// into a folder nothing made passes in the simulator and fails on every real
// reader. This fake refuses it, the way the card does.
//
// Paths are mapped under a temporary directory the test chooses.
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <string>

class HalFile {
 public:
  HalFile() = default;
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;
  ~HalFile() { close(); }
  size_t write(const uint8_t* buf, size_t count) { return out ? std::fwrite(buf, 1, count, out) : 0; }
  void close() {
    if (out) std::fclose(out);
    out = nullptr;
  }
  FILE* out = nullptr;
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }
  std::string root;

  std::string real(const char* path) const { return root + path; }

  bool exists(const char* path) {
    struct stat st;
    return ::stat(real(path).c_str(), &st) == 0;
  }
  bool ensureDirectoryExists(const char* path) {
    struct stat st;
    if (::stat(real(path).c_str(), &st) == 0) return S_ISDIR(st.st_mode);
    return ::mkdir(real(path).c_str(), 0777) == 0;
  }
  // O_RDWR | O_CREAT | O_TRUNC on SdFat: no parent, no file.
  bool openFileForWrite(const char*, const char* path, HalFile& file) {
    file.close();
    const std::string full = real(path);
    const std::string parent = full.substr(0, full.find_last_of('/'));
    struct stat st;
    if (::stat(parent.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) return false;
    file.out = std::fopen(full.c_str(), "wb");
    return file.out != nullptr;
  }
  bool remove(const char* path) { return ::unlink(real(path).c_str()) == 0; }
  bool rename(const char* from, const char* to) { return ::rename(real(from).c_str(), real(to).c_str()) == 0; }
};

#define Storage HalStorage::getInstance()

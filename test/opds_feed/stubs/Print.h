#pragma once

// Host-test stub for Arduino's Print. OpdsParser derives from it so feeds can
// be streamed in from HttpDownloader; on the host the test just calls write().
#include <cstddef>
#include <cstdint>

class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t* buffer, size_t size) = 0;
  virtual void flush() {}
};

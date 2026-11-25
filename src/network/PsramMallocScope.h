#pragma once

#if (FREEINK_DEVICE_PICCO || FREEINK_DEVICE_X4PRO) && !defined(SIMULATOR)
#include <esp_heap_caps.h>
#include <sdkconfig.h>

#include <atomic>

// While alive, malloc sends blocks above 16 bytes to PSRAM instead of only those above
// CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL. Internal DRAM is down to a few KB once Wi-Fi and TLS are up, and many small
// allocations (feed strings, network buffers, text layout) would otherwise exhaust it and stall the connection.
// The limit is process-wide, so scopes nest: the outermost one lowers it and the last one to end restores it.
class PsramMallocScope {
 public:
  PsramMallocScope() {
    if (depth().fetch_add(1) == 0) heap_caps_malloc_extmem_enable(16);
  }
  ~PsramMallocScope() {
    if (depth().fetch_sub(1) == 1) heap_caps_malloc_extmem_enable(CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL);
  }
  PsramMallocScope(const PsramMallocScope&) = delete;
  PsramMallocScope& operator=(const PsramMallocScope&) = delete;

 private:
  static std::atomic<int>& depth() {
    static std::atomic<int> count{0};
    return count;
  }
};
#else
class PsramMallocScope {
 public:
  PsramMallocScope() = default;
  PsramMallocScope(const PsramMallocScope&) = delete;
  PsramMallocScope& operator=(const PsramMallocScope&) = delete;
};
#endif

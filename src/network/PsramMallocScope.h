#pragma once

#if (FREEINK_DEVICE_PICCO || FREEINK_DEVICE_X4PRO) && !defined(SIMULATOR)
#include <esp_heap_caps.h>
#include <sdkconfig.h>

// While alive, malloc sends blocks of 16 bytes and up to PSRAM instead of only those above
// CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL. Internal DRAM is down to a few KB once Wi-Fi and TLS are up, and a feed's
// many small strings or a bulk transfer's network buffers would otherwise exhaust it and stall the connection.
// The limit is process-wide, so keep the scope to one network operation.
class PsramMallocScope {
 public:
  PsramMallocScope() { heap_caps_malloc_extmem_enable(16); }
  ~PsramMallocScope() { heap_caps_malloc_extmem_enable(CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL); }
  PsramMallocScope(const PsramMallocScope&) = delete;
  PsramMallocScope& operator=(const PsramMallocScope&) = delete;
};
#else
class PsramMallocScope {
 public:
  PsramMallocScope() = default;
  PsramMallocScope(const PsramMallocScope&) = delete;
  PsramMallocScope& operator=(const PsramMallocScope&) = delete;
};
#endif

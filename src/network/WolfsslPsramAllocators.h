#pragma once

#include <Logging.h>
#include <esp_heap_caps.h>
#include <wolfssl/wolfcrypt/memory.h>

#include <cstdlib>

// Puts wolfSSL's heap in PSRAM, using internal DRAM only when PSRAM is full.
// malloc already sends blocks above 4 KB to PSRAM, but a TLS handshake also makes
// many smaller ones (bignum temporaries are about 2 KB each), and those would
// otherwise take the internal DRAM the Wi-Fi driver allocates from.
namespace wolfsslpsram {
inline void* allocate(size_t size) {
  return heap_caps_malloc_prefer(size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

// heap_caps blocks and malloc blocks free the same way, so anything allocated
// before registration is still released correctly.
inline void release(void* ptr) { free(ptr); }

inline void* reallocate(void* ptr, size_t size) {
  return heap_caps_realloc_prefer(ptr, size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
}  // namespace wolfsslpsram

inline void useWolfsslPsramAllocators() {
  if (wolfSSL_SetAllocators(wolfsslpsram::allocate, wolfsslpsram::release, wolfsslpsram::reallocate) != 0) {
    LOG_ERR("TLS", "Could not route wolfSSL allocations to PSRAM");
  }
}

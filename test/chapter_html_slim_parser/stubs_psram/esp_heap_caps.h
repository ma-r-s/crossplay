#pragma once

#include <cstddef>

// The current malloc spill threshold, as PsramMallocScope last set it.
inline size_t extmemThreshold = 4096;
inline void heap_caps_malloc_extmem_enable(const size_t limit) { extmemThreshold = limit; }

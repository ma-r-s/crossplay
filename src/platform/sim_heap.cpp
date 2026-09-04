// A device-shaped heap for the simulator.
//
// The simulator's ESPMock answers 1024*1024 to every heap question, forever.
// Three things follow, and all of them hid real behaviour:
//
//   * the number is wrong. An X4 Pro reports 327680 total and around 275000
//     free, so the simulator claims roughly three times the room there is.
//   * the number never moves, so a leak, or a screen that allocates more each
//     time it opens, looks identical to one that does not.
//   * two guards in this firmware are written as `if (getMaxAllocHeap() < n)`
//     and therefore have never once been taken outside a device:
//     DictZip.cpp:187 and Dictionary.cpp:326.
//
// This counts what the firmware actually allocates and answers from the
// device's budget instead. It is a model, not an emulator: it tracks the
// program's own C++ allocations, so the numbers move with the firmware's
// behaviour rather than matching an ESP32's allocator byte for byte. That is
// the part worth having, because the questions being asked of it are "is this
// growing" and "is there room for one more buffer".
//
// Not compiled into firmware: the simulator env is the only one that builds
// this directory, and the whole file is behind SIMULATOR anyway.

#ifdef SIMULATOR

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>

namespace {

// The X4 Pro's internal heap, from the x4pro build's own report: "RAM: 19.8%
// (used 64948 bytes from 327680 bytes)". Static allocation is already spent
// before main(), so the budget below is what is left for the heap, matching the
// ~275000 free that a real device logs at rest.
constexpr size_t kDeviceHeapTotal = 327680;
constexpr size_t kDeviceStaticUse = 64948;
constexpr size_t kDeviceHeapBudget = kDeviceHeapTotal - kDeviceStaticUse;

// Live bytes handed out to the firmware. Relaxed ordering throughout: these are
// counters read for reporting, and a torn read costs an inaccurate log line
// rather than a wrong decision.
std::atomic<size_t> gLive{0};
std::atomic<size_t> gPeak{0};

// Where the firmware's own consumption is measured from.
//
// operator new sees every allocation this process makes, and most of them are
// not the firmware's: SDL, the window, the host-side framebuffer, the C++
// runtime. Counting those against a 262KB budget spends it before main() has
// drawn anything, and the first version of this file duly reported "Free: 0"
// forever -- which would have made both getMaxAllocHeap() guards fire always
// and switched off dictionary lookups in the simulator entirely.
//
// So the floor is whatever was live the first time the firmware asked a heap
// question, and what is reported afterwards is growth from there. That answers
// the questions actually being asked of it ("is this leaking", "has this screen
// got more expensive", "is there room for one more buffer") without pretending
// to know how many bytes an ESP32's allocator would have spent on the same
// work. It is not the device's absolute number and is not offered as one.
std::atomic<size_t> gFloor{0};
std::atomic<bool> gFloorSet{false};

size_t sinceFloor() {
  size_t floor = gFloor.load(std::memory_order_relaxed);
  if (!gFloorSet.load(std::memory_order_relaxed)) {
    floor = gLive.load(std::memory_order_relaxed);
    gFloor.store(floor, std::memory_order_relaxed);
    // The peak has to start again from here. It has been climbing since process
    // start, most of that SDL and the runtime, so subtracting the floor from it
    // afterwards would be differencing two unrelated epochs and reporting the
    // answer as a high-water mark.
    gPeak.store(floor, std::memory_order_relaxed);
    gFloorSet.store(true, std::memory_order_relaxed);
  }
  const size_t live = gLive.load(std::memory_order_relaxed);
  return live > floor ? live - floor : 0;
}

void took(const size_t n) {
  const size_t now = gLive.fetch_add(n, std::memory_order_relaxed) + n;
  size_t peak = gPeak.load(std::memory_order_relaxed);
  while (now > peak && !gPeak.compare_exchange_weak(peak, now, std::memory_order_relaxed)) {
  }
}

void gave(const size_t n) { gLive.fetch_sub(n, std::memory_order_relaxed); }

// Every block carries its own size. The alternative is malloc_size(), which is
// libc-specific, and this has to build the same way on macOS and on the Linux
// runner that CI uses.
struct Header {
  size_t bytes;
};

constexpr size_t kHeaderBytes = sizeof(Header) < alignof(std::max_align_t) ? alignof(std::max_align_t) : sizeof(Header);

}  // namespace

// The firmware's own allocations, and only those: this replaces the global
// operator new for the simulator binary, so anything the standard library
// allocates on its own behalf is counted too. That is the honest reading of
// "what has this program taken", which is the question the guards ask.
void* operator new(const size_t size) {
  void* block = std::malloc(size + kHeaderBytes);
  if (block == nullptr) {
    // The simulator is not the place to model an ESP32 running out; a host that
    // cannot serve the request has a problem of its own. -fno-exceptions is a
    // firmware flag, but throwing here would still be wrong: the firmware never
    // catches, so the useful behaviour is to stop where it happened.
    std::abort();
  }
  static_cast<Header*>(block)->bytes = size;
  took(size);
  return static_cast<char*>(block) + kHeaderBytes;
}

void* operator new(const size_t size, const std::nothrow_t&) noexcept {
  void* block = std::malloc(size + kHeaderBytes);
  if (block == nullptr) return nullptr;
  static_cast<Header*>(block)->bytes = size;
  took(size);
  return static_cast<char*>(block) + kHeaderBytes;
}

void* operator new[](const size_t size) { return operator new(size); }
void* operator new[](const size_t size, const std::nothrow_t& tag) noexcept { return operator new(size, tag); }

void operator delete(void* p) noexcept {
  if (p == nullptr) return;
  auto* block = static_cast<char*>(p) - kHeaderBytes;
  gave(reinterpret_cast<Header*>(block)->bytes);
  std::free(block);
}

void operator delete(void* p, size_t) noexcept { operator delete(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { operator delete(p); }
void operator delete[](void* p) noexcept { operator delete(p); }
void operator delete[](void* p, size_t) noexcept { operator delete(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { operator delete(p); }

// What ESPMock answers with. Declared in the simulator's Arduino.h by
// scripts_local/sim_catchup.py, which patches the fetched copy because a
// library's own include path beats the project's -I.
extern "C" {

uint32_t crossplay_sim_heap_size() { return static_cast<uint32_t>(kDeviceHeapBudget); }

uint32_t crossplay_sim_free_heap() {
  const size_t used = sinceFloor();
  return used >= kDeviceHeapBudget ? 0u : static_cast<uint32_t>(kDeviceHeapBudget - used);
}

// The worst free since the floor. READ THIS ONE WITH SUSPICION.
//
// Steady state models well: opening RecentBooks moves getFreeHeap() by ~3KB,
// which is a device-plausible number. Transients do not. A screen that decodes
// an image allocates a host-resolution buffer with no device counterpart, and
// one of those alone exceeds the whole 262KB budget, so this reports 0 on a
// screen that costs the device almost nothing.
//
// Left truthful rather than massaged. Reporting the current free instead would
// read as "no dip ever happened", which is a worse lie than an obviously
// alarming number, and clamping it would hide the one thing it does say
// reliably: that a large transient occurred. Use it to compare runs of the same
// screen, never as a device figure.
uint32_t crossplay_sim_min_free_heap() {
  const size_t floor = gFloor.load(std::memory_order_relaxed);
  const size_t peak = gPeak.load(std::memory_order_relaxed);
  const size_t worst = peak > floor ? peak - floor : 0;
  return worst >= kDeviceHeapBudget ? 0u : static_cast<uint32_t>(kDeviceHeapBudget - worst);
}

// The largest single block, which on a device is smaller than the free total
// because the heap fragments. Modelling that properly needs the allocator's own
// free list; this reports the free total, so it is an optimistic bound. Said
// plainly because the two guards that read it are deciding whether one more
// buffer fits, and an optimistic answer makes them fire later here than on a
// device, never earlier.
uint32_t crossplay_sim_max_alloc_heap() { return crossplay_sim_free_heap(); }

}  // extern "C"

#endif  // SIMULATOR

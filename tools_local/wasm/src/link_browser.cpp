// PLAY NEARBY, between two browser tabs' worth of firmware.
//
// On the device the radio is ESP-NOW; on Mario's Mac it is UDP on loopback,
// which is how two simulator windows find each other. A browser has neither.
// Two instances of the module are two WebAssembly memories, and the only thing
// that can carry bytes between them is the JavaScript that owns them both.
//
// So this implements linkplay::Radio against a router in site/assets/
// emulator.js. Nothing here is device code and nothing here is reachable from
// a firmware build: LinkRadio.cpp routes to this file only under
// __EMSCRIPTEN__, and this file is only compiled by tools_local/wasm/build.py.
//
// The shape is deliberately the same as the loopback one, because host election
// depends on it. Each instance claims a slot, the slot becomes the low half of
// its address, and "lower address wins" therefore means "whoever started
// first" -- the same deterministic answer the simulator gives, and the same
// shape the device gets from MACs.
//
// Threads: the firmware runs on a Web Worker and the router lives on the main
// thread. Sends proxy across with MAIN_THREAD_EM_ASM, which blocks the worker
// for the length of one JavaScript call -- at a few packets a second that is
// free, and it removes any question about how long a buffer stays alive.
// Deliveries come the other way: the page calls crossplay_link_deliver on the
// main thread, which writes into the receive ring the worker drains. The ring
// was built for exactly that (an ESP-NOW callback producing, a task consuming)
// and its head and tail are already atomics over shared memory.

#include <emscripten/em_asm.h>
#include <emscripten/emscripten.h>

#include <cstring>

#include "../../../src/apps_local/link/LinkRadio.h"

// Logging belongs to the firmware and reaches Arduino.h; this file is only ever
// compiled inside the simulator build, so it is always available.
#include <Logging.h>

namespace linkplay {
namespace {

// Same range and slot count as the loopback transport, so the two behave
// identically and a bug in one reproduces in the other.
constexpr uint16_t kBasePort = 45700;
constexpr int kSlots = 8;

// Only one match runs at a time per instance, so one slot is enough -- the same
// reasoning, and the same shape, as the ESP-NOW branch's activeRadio.
Radio* activeRadio = nullptr;

// Which of the page's devices this module is. Set once at boot by
// crossplay_link_bind(); everything below passes it back so the router can tell
// two instances apart.
//
// It is a number we carry rather than a lookup through `Module`, because a
// MAIN_THREAD_EM_ASM snippet runs proxied on the main thread and `Module` did
// not resolve to this instance there -- every claim came back -1 and PLAY
// NEARBY reported "the radio would not start".
int instanceId = -1;

// Staging for a packet on its way in. The page writes here and then calls
// deliver, which saves it having to allocate inside our heap.
uint8_t inbox[kMaxPacketBytes];

Address addressForPort(const uint16_t port) {
  Address address;
  address.bytes[0] = 0x02;  // locally administered, same as a soft MAC
  address.bytes[4] = static_cast<uint8_t>(port >> 8);
  address.bytes[5] = static_cast<uint8_t>(port & 0xFF);
  return address;
}

uint16_t portForAddress(const Address& address) {
  return static_cast<uint16_t>((static_cast<uint16_t>(address.bytes[4]) << 8) | address.bytes[5]);
}

}  // namespace

bool Radio::begin() {
  end();

  // The page hands out slots and refuses to hand out the same one twice, which
  // is what a failed bind() does for the loopback transport.
  const int slot = MAIN_THREAD_EM_ASM_INT(
      {
        var radio = globalThis.crossplayRadio;
        return radio ? radio.claim($0) : -1;
      },
      instanceId);
  if (slot < 0 || slot >= kSlots) {
    LOG_ERR("LINK", "no free slot in the page's router");
    return false;
  }

  socket_ = slot;  // reused as the slot; end() clears it the same way
  local_ = addressForPort(static_cast<uint16_t>(kBasePort + slot));
  started_ = true;
  LOG_INF("LINK", "radio up in the browser as slot %d", slot);
  activeRadio = this;
  return true;
}

void Radio::end() {
  if (started_) {
    const int slot = socket_;
    MAIN_THREAD_EM_ASM(
        {
          var radio = globalThis.crossplayRadio;
          if (radio) radio.release($0);
        },
        slot);
  }
  if (activeRadio == this) activeRadio = nullptr;
  socket_ = -1;
  started_ = false;
  head_.store(0, std::memory_order_relaxed);
  tail_.store(0, std::memory_order_relaxed);
  overflowed_.store(false, std::memory_order_relaxed);
}

bool Radio::send(const Address& to, const uint8_t* data, const size_t length) {
  if (!started_ || data == nullptr || length == 0 || length > kMaxPacketBytes) return false;

  // -1 for a broadcast, so the router does the "everyone but me" fan-out the
  // loopback transport does with a loop over the port range.
  const int destination = (to == kBroadcast) ? -1 : static_cast<int>(portForAddress(to)) - kBasePort;
  MAIN_THREAD_EM_ASM(
      {
        var radio = globalThis.crossplayRadio;
        // HEAPU8 here is this module's own heap, which is exactly the one the
        // packet is sitting in.
        if (radio) radio.send($0, $1, HEAPU8.subarray($2, $2 + $3));
      },
      socket_, destination, reinterpret_cast<uintptr_t>(data), static_cast<int>(length));
  return true;
}

bool Radio::receive(Address& from, uint8_t* buffer, const size_t capacity, size_t& length) {
  if (!started_ || buffer == nullptr) return false;
  while (true) {
    const uint8_t head = head_.load(std::memory_order_relaxed);
    if (head == tail_.load(std::memory_order_acquire)) return false;

    const Datagram& slot = ring_[head];
    const bool fits = slot.length <= capacity;
    if (fits) {
      from = slot.from;
      memcpy(buffer, slot.data, slot.length);
      length = slot.length;
    }
    head_.store(static_cast<uint8_t>((head + 1) % kQueueDepth), std::memory_order_release);
    // Too big for the caller is dropped rather than truncated, and the drain
    // moves on rather than stalling behind it.
    if (fits) return true;
  }
}

}  // namespace linkplay

extern "C" {

// Called once by the page after boot, before anything can ask for a slot.
EMSCRIPTEN_KEEPALIVE void crossplay_link_bind(const int id) { linkplay::instanceId = id; }

// Where the page writes a packet before announcing it. One buffer is enough:
// deliver runs on the main thread, and the main thread is one thread.
EMSCRIPTEN_KEEPALIVE uintptr_t crossplay_link_inbox() {
  return reinterpret_cast<uintptr_t>(linkplay::inbox);
}
EMSCRIPTEN_KEEPALIVE int crossplay_link_inbox_size() { return static_cast<int>(linkplay::kMaxPacketBytes); }

// fromSlot identifies the sender the way the socket did on the desktop: taken
// from the router rather than from the payload, so an instance cannot claim to
// be another one by writing an address into the packet.
EMSCRIPTEN_KEEPALIVE void crossplay_link_deliver(const int fromSlot, const int length) {
  if (linkplay::activeRadio == nullptr) return;
  if (fromSlot < 0 || fromSlot >= linkplay::kSlots) return;
  const linkplay::Address from = linkplay::addressForPort(static_cast<uint16_t>(linkplay::kBasePort + fromSlot));
  linkplay::activeRadio->enqueueFromCallback(from.bytes, linkplay::inbox, length);
}

}  // extern "C"

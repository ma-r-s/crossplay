#pragma once

// The hostile link, shared by every test in this directory.
//
// A Medium that drops, delays, duplicates and reorders under a seeded PRNG, and
// a Transport that speaks through it. Extracted so the protocol tests and the
// LinkPlay tests hold their sessions to the same bad radio rather than each
// inventing a kinder one.
//
// Deliberately not a mock: it carries real encoded packets, counts every copy it
// carries (a packet storm is invisible to correctness assertions and shows up
// only as traffic), and expands a broadcast per registered receiver so a three
// device test is not structurally deaf.

#include <cstdint>
#include <cstring>
#include <vector>

#include "../../src/apps_local/link/LinkTransport.h"

namespace linktest {

using namespace linkplay;

uint32_t nextRandom(uint32_t& state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

Address addressOf(const uint8_t last) { return Address{{0x02, 0, 0, 0, 0, last}}; }

// ---------------------------------------------------------------------------
// A link that misbehaves on purpose.
// ---------------------------------------------------------------------------

struct Medium {
  struct InFlight {
    Address from;
    Address to;
    uint8_t data[kMaxPacketBytes] = {};
    size_t length = 0;
    uint32_t deliverAtMs = 0;
    bool taken = false;
  };

  std::vector<InFlight> queue;
  // Every device on the air. A broadcast is expanded into one copy per
  // receiver, because that is what a broadcast is: the first version kept a
  // single copy with a `taken` flag, so whichever device ticked first consumed
  // it and the rest never heard it at all. With two devices that is invisible.
  // With three it made the third structurally deaf, and the test asserting it
  // stayed unpaired passed for that reason rather than the intended one.
  std::vector<Address> receivers;
  uint32_t nowMs = 0;
  uint32_t random = 1;
  // Every copy this medium has carried, duplicates included. A protocol that
  // answers a packet with a packet the peer answers back can be perfectly
  // correct on every state assertion and still saturate a real radio, so the
  // amount of traffic is itself something the tests get to assert on.
  long carried = 0;

  void join(const Address& address) { receivers.push_back(address); }

  // Percentages. Loss is applied per copy, so a duplicated packet can have one
  // copy dropped and the other survive, which is exactly the sort of asymmetry
  // that hides ordering bugs.
  int lossPercent = 0;
  int duplicatePercent = 0;
  int maxJitterMs = 0;
  // Packets from this address are dropped entirely, to model one-way range.
  Address blackholeSource = {};
  bool hasBlackhole = false;

  void submit(const Address& from, const Address& to, const uint8_t* data, const size_t length) {
    if (to == kBroadcast) {
      // Each copy takes its own loss and jitter roll, which is also more
      // faithful than one shared verdict: a real broadcast is received or
      // missed per device, not all-or-nothing.
      for (const Address& receiver : receivers) {
        if (receiver == from) continue;
        submit(from, receiver, data, length);
      }
      return;
    }
    int copies = 1;
    if (duplicatePercent > 0 && static_cast<int>(nextRandom(random) % 100) < duplicatePercent) copies = 2;
    for (int i = 0; i < copies; ++i) {
      if (lossPercent > 0 && static_cast<int>(nextRandom(random) % 100) < lossPercent) continue;
      InFlight packet;
      packet.from = from;
      packet.to = to;
      memcpy(packet.data, data, length);
      packet.length = length;
      packet.deliverAtMs = nowMs + (maxJitterMs > 0 ? nextRandom(random) % static_cast<uint32_t>(maxJitterMs + 1) : 0);
      queue.push_back(packet);
      carried++;
    }
  }

  bool take(const Address& receiver, Address& from, uint8_t* buffer, const size_t capacity, size_t& length) {
    for (auto& packet : queue) {
      if (packet.taken) continue;
      if (packet.deliverAtMs > nowMs) continue;
      if (!(packet.to == receiver)) continue;
      packet.taken = true;
      if (packet.length > capacity) continue;  // transport drops, never truncates
      from = packet.from;
      memcpy(buffer, packet.data, packet.length);
      length = packet.length;
      return true;
    }
    return false;
  }

  void collect() {
    std::vector<InFlight> kept;
    for (const auto& packet : queue)
      if (!packet.taken) kept.push_back(packet);
    queue.swap(kept);
  }
};

class FakeTransport final : public Transport {
 public:
  FakeTransport(Medium& medium, const Address& address) : medium_(medium), address_(address) { medium_.join(address_); }

  bool send(const Address& to, const uint8_t* data, const size_t length) override {
    if (medium_.hasBlackhole && medium_.blackholeSource == address_) return true;  // radio says fine, nothing lands
    medium_.submit(address_, to, data, length);
    return true;
  }

  bool receive(Address& from, uint8_t* buffer, const size_t capacity, size_t& length) override {
    return medium_.take(address_, from, buffer, capacity, length);
  }

  const Address& localAddress() const override { return address_; }

 private:
  Medium& medium_;
  Address address_;
};

}  // namespace linktest

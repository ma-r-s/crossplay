#pragma once

// The Transport a match actually runs on.
//
// One class, two implementations, chosen at compile time:
//
//   device     ESP-NOW. Connectionless, no AP, no router, no DHCP, which is
//              what makes "tap MULTIPLAYER and you are playing" possible at
//              all.
//   simulator  UDP on localhost. Two simulator instances on one Mac find each
//              other and play, so the whole feature is exercisable before the
//              hardware arrives.
//
// The session cannot tell them apart, and neither can the game above it.

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "LinkTransport.h"

namespace linkplay {

// Peers must share a channel or they never hear each other, and sweeping all
// thirteen makes discovery slow and unreliable. So it is fixed, baked in, and
// never surfaced. The cost is more interference in a crowded room, which shows
// up as a retransmit rather than a failure.
constexpr uint8_t kChannel = 1;

// How long the radio stays awake per power-save interval while a match is up.
// The panel takes ~500ms to repaint, so latency this small is invisible, and
// the receiver spends most of its time asleep instead of listening flat out.
//
// UNMEASURED. CONFIG_ESP_WIFI_STA_DISCONNECTED_PM_ENABLE=y in sdkconfig.x4pro,
// so esp_now_set_wake_window() applies even though we never associate to an AP,
// but what it actually costs in mA is a bench measurement nobody has taken. If
// a match turns out to drain the cell, this is the number to move first.
constexpr uint16_t kWakeWindowMs = 50;

class Radio final : public Transport {
 public:
  Radio() = default;
  ~Radio() override;
  Radio(const Radio&) = delete;
  Radio& operator=(const Radio&) = delete;

  // Takes over the radio. On device this puts WiFi into station mode on
  // kChannel, so it cannot coexist with a WiFi connection; the caller owns
  // that decision.
  bool begin();
  void end();
  bool started() const { return started_; }

  // True if a packet was dropped because the receive queue was full. Only a
  // diagnostic: the session retransmits, so a drop costs latency, not state.
  bool overflowed() const { return overflowed_.load(std::memory_order_relaxed); }

  bool send(const Address& to, const uint8_t* data, size_t length) override;
  bool receive(Address& from, uint8_t* buffer, size_t capacity, size_t& length) override;
  const Address& localAddress() const override { return local_; }

  // Called only by the platform receive callback.
  void enqueueFromCallback(const uint8_t* sourceMac, const uint8_t* data, int length);

 private:
  // Six is comfortably more than a turn-based game generates between loop
  // passes, and the whole ring is 1.3KB: sized for our 204-byte packets rather
  // than inherited from a file-transfer buffer.
  static constexpr size_t kQueueDepth = 6;

  struct Datagram {
    Address from;
    uint16_t length = 0;
    uint8_t data[kMaxPacketBytes] = {};
  };

  // A single-producer, single-consumer ring. The producer is the ESP-NOW
  // receive callback (serialised by the WiFi task) and the consumer is the main
  // loop, so the two indices need no lock: each is written by exactly one side.
  // That keeps FreeRTOS out of this header and heap out of the radio entirely.
  Datagram ring_[kQueueDepth] = {};
  std::atomic<uint8_t> head_{0};  // consumer owns
  std::atomic<uint8_t> tail_{0};  // producer owns

  Address local_ = {};
  bool started_ = false;
  std::atomic<bool> overflowed_{false};

  int socket_ = -1;  // simulator only
};

}  // namespace linkplay

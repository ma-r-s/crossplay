#pragma once

// What LinkSession needs from a radio, and nothing more.
//
// Three implementations are planned and the session cannot tell them apart:
//
//   LinkEspNow    the real one, on the device
//   LinkLoopback  a local socket, so two simulators on one Mac can play
//   FakeLink      host-tests/link/, a link that drops, delays, duplicates and
//                 reorders on purpose
//
// The last one is the reason this interface exists at all. "It works itself out
// all the edge cases" is only true if the edge cases can be produced on demand,
// and a real radio will not produce them when asked.

#include <cstddef>
#include <cstdint>

#include "LinkProtocol.h"

namespace linkplay {

class Transport {
 public:
  virtual ~Transport() = default;

  // Sends one packet. `to` may be kBroadcast. Returns false if the packet could
  // not be handed to the radio at all; the session treats that as a normal loss
  // rather than an error, because it retransmits anyway.
  virtual bool send(const Address& to, const uint8_t* data, size_t length) = 0;

  // Takes one packet from the receive queue. Returns false when empty, so the
  // session drains with a while loop. Packets longer than `capacity` are
  // dropped by the transport rather than truncated: a truncated packet would
  // fail decode() anyway, and half a game state is worse than none.
  virtual bool receive(Address& from, uint8_t* buffer, size_t capacity, size_t& length) = 0;

  // This device's address. Host election compares it against the peer's, so it
  // has to be stable for the lifetime of a match and unique on the channel.
  virtual const Address& localAddress() const = 0;
};

}  // namespace linkplay

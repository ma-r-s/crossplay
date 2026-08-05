#pragma once

// The wire format for local multiplayer ("link play").
//
// Freestanding: no Arduino, no radio, no allocation, so host-tests/link/ can
// play whole matches on a laptop against a deliberately hostile link.
//
// Packets are deliberately tiny. The header is 12 bytes and the payload caps at
// 192, so the largest packet is 204 bytes -- under ESP-NOW v1's 250-byte limit.
// That matters: it means the protocol works whether or not the build has
// ESP-NOW v2, and we never have to care which one shipped.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace linkplay {

// A radio address. Six bytes because that is what ESP-NOW peers are, but the
// session only ever compares them, so a loopback transport can invent its own.
struct Address {
  uint8_t bytes[6] = {};

  bool operator==(const Address& other) const { return memcmp(bytes, other.bytes, sizeof(bytes)) == 0; }
  bool operator!=(const Address& other) const { return !(*this == other); }
  bool isZero() const {
    for (const uint8_t b : bytes)
      if (b != 0) return false;
    return true;
  }
  // Host election is "lower address wins", so the ordering has to be total and
  // agreed by both sides. memcmp over the same six bytes is both.
  bool operator<(const Address& other) const { return memcmp(bytes, other.bytes, sizeof(bytes)) < 0; }
};

constexpr Address kBroadcast = {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};

constexpr uint8_t kProtocolVersion = 1;
constexpr size_t kHeaderBytes = 12;
constexpr size_t kMaxPayloadBytes = 192;
constexpr size_t kMaxPacketBytes = kHeaderBytes + kMaxPayloadBytes;
// The name a device shows to others. Sized for the widest triple the name
// lists can roll (apps_local/player/PlayerName.h), and still short enough to
// sit in a header without truncation. LinkActivity.cpp static_asserts the two
// against each other, because a name that overflows here is not a compile error
// -- it is a face that comes out wrong on somebody else's device.
//
// This grew from 15 when a name went from two words to three. Nothing else had
// to change: the name rides as a variable-length payload with its length in the
// header, not as a fixed field, so an older build receiving a longer name
// truncates it rather than misreading the packet. That is why there is no
// kProtocolVersion bump here.
constexpr size_t kMaxNameBytes = 20;
// A note is deliberately one byte. Everything that has needed one so far is an
// intent ("play again", "leaving"), and a wider channel would invite games to
// route game state through it, which is what the turn-taking State is for.
constexpr size_t kMaxSayBytes = 1;

enum class PacketType : uint8_t {
  // Broadcast "I am here and I want to play <gameId>". Discovery is this and
  // nothing else: no lobby, no server, no pairing step the user can see.
  Hello = 1,
  // Unicast "I pick you". A match exists once both sides have sent one to each
  // other, which is what stops three devices from pairing inconsistently.
  Join = 2,
  // Unicast, carries the whole shared game state rather than a move. See
  // LinkSession.h for why that choice removes a category of bug.
  State = 3,
  // Unicast acknowledgement, and doubles as the heartbeat: a side with nothing
  // to say re-acks the last state it saw, which is how silence gets detected.
  Ack = 4,
  // Unicast "I am leaving". Sent on app exit and on lock so the other device
  // can say so immediately rather than waiting out the silence timeout.
  Bye = 5,
  // Unicast, and the only thing either side may send at any moment: a short
  // note that does not touch the turn. A rematch is a conversation ("shall
  // we?" / "yes") happening exactly when the game has ended and there is no
  // turn left to carry it, which is why State could not do this job.
  Say = 6,
  // Acknowledges a Say. Its own type rather than a flag on Ack, because the two
  // run on separate sequences and an Ack that could mean either would have to
  // say which.
  SayAck = 7,
};

enum class ByeReason : uint8_t {
  Unknown = 0,
  LeftApp = 1,
};

struct PacketView {
  PacketType type = PacketType::Hello;
  uint16_t gameId = 0;
  uint16_t sequence = 0;
  // The sender's coin toss for this search. See Session::isHost().
  uint8_t toss = 0;
  const uint8_t* payload = nullptr;
  uint8_t payloadLength = 0;
};

// Returns the encoded length, or 0 if the arguments do not fit. `toss` rides in
// every packet rather than only in Hello, because a match can be sealed by an
// Ack or a State just as easily as by a Join, and both sides have to reach the
// same answer from whichever packet happened to arrive.
size_t encode(uint8_t* output, size_t capacity, PacketType type, uint16_t gameId, uint16_t sequence, uint8_t toss,
              const uint8_t* payload, uint8_t payloadLength);

// Rejects anything that is not ours: wrong magic, wrong version, a length that
// disagrees with the header, or an unknown type. A malformed packet is dropped
// rather than diagnosed, because on a shared channel most of them will simply
// be somebody else's traffic.
bool decode(const uint8_t* data, size_t length, PacketView& output);

}  // namespace linkplay

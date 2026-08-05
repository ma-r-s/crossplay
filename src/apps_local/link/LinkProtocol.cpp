#include "LinkProtocol.h"

namespace linkplay {
namespace {

constexpr uint8_t kMagic[4] = {'X', 'L', 'N', 'K'};

// Offsets into the header. Little-endian, byte at a time: RISC-V faults on a
// raw reinterpret_cast of a uint8_t* to a wider type (CLAUDE.md golden rule 6),
// and a packet buffer is never guaranteed to be aligned.
constexpr size_t kOffVersion = 4;
constexpr size_t kOffType = 5;
constexpr size_t kOffGameId = 6;
constexpr size_t kOffSequence = 8;
constexpr size_t kOffPayloadLength = 10;
constexpr size_t kOffToss = 11;

void writeU16(uint8_t* data, const uint16_t value) {
  data[0] = static_cast<uint8_t>(value);
  data[1] = static_cast<uint8_t>(value >> 8);
}

uint16_t readU16(const uint8_t* data) {
  return static_cast<uint16_t>(static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8));
}

bool knownType(const uint8_t raw) {
  return raw >= static_cast<uint8_t>(PacketType::Hello) && raw <= static_cast<uint8_t>(PacketType::SayAck);
}

}  // namespace

size_t encode(uint8_t* output, const size_t capacity, const PacketType type, const uint16_t gameId,
              const uint16_t sequence, const uint8_t toss, const uint8_t* payload, const uint8_t payloadLength) {
  if (output == nullptr) return 0;
  if (payloadLength > kMaxPayloadBytes) return 0;
  if (payloadLength > 0 && payload == nullptr) return 0;
  const size_t total = kHeaderBytes + payloadLength;
  if (capacity < total) return 0;

  memcpy(output, kMagic, sizeof(kMagic));
  output[kOffVersion] = kProtocolVersion;
  output[kOffType] = static_cast<uint8_t>(type);
  writeU16(output + kOffGameId, gameId);
  writeU16(output + kOffSequence, sequence);
  output[kOffPayloadLength] = payloadLength;
  output[kOffToss] = toss;
  if (payloadLength > 0) memcpy(output + kHeaderBytes, payload, payloadLength);
  return total;
}

bool decode(const uint8_t* data, const size_t length, PacketView& output) {
  if (data == nullptr) return false;
  if (length < kHeaderBytes || length > kMaxPacketBytes) return false;
  if (memcmp(data, kMagic, sizeof(kMagic)) != 0) return false;
  if (data[kOffVersion] != kProtocolVersion) return false;
  if (!knownType(data[kOffType])) return false;

  const uint8_t payloadLength = data[kOffPayloadLength];
  if (payloadLength > kMaxPayloadBytes) return false;
  if (length != kHeaderBytes + payloadLength) return false;

  output.type = static_cast<PacketType>(data[kOffType]);
  output.gameId = readU16(data + kOffGameId);
  output.sequence = readU16(data + kOffSequence);
  output.toss = data[kOffToss];
  output.payload = payloadLength > 0 ? data + kHeaderBytes : nullptr;
  output.payloadLength = payloadLength;
  return true;
}

}  // namespace linkplay

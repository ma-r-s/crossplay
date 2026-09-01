#include "WavelengthSave.h"

namespace wavelength {
namespace {

// Little-endian by hand rather than by memcpy'ing a struct. The v1 file WAS a
// struct copy, and it only round-trips because this fork builds one endianness
// and that struct happened to need no padding; writing the bytes out means the
// format is a decision rather than a coincidence, and it is what lets the host
// test read a byte offset and say what it means.
void put16(uint8_t*& p, const uint16_t v) {
  *p++ = static_cast<uint8_t>(v & 0xFF);
  *p++ = static_cast<uint8_t>((v >> 8) & 0xFF);
}

void put32(uint8_t*& p, const uint32_t v) {
  put16(p, static_cast<uint16_t>(v & 0xFFFF));
  put16(p, static_cast<uint16_t>((v >> 16) & 0xFFFF));
}

uint16_t take16(const uint8_t*& p) {
  const uint16_t lo = *p++;
  const uint16_t hi = *p++;
  return static_cast<uint16_t>(lo | (hi << 8));
}

uint32_t take32(const uint8_t*& p) {
  const uint32_t lo = take16(p);
  const uint32_t hi = take16(p);
  return lo | (hi << 16);
}

enum : uint8_t {
  kFlagSessionStarted = 1u << 0,
  kFlagHasPeeked = 1u << 1,
  kFlagPractice = 1u << 2,
  kFlagCallWasRight = 1u << 3,
  kFlagAbandonedRound = 1u << 4,
};

}  // namespace

void Saved::clearRound() {
  screen = 0;
  resumeScreen = 0;
  spectrum = -1;
  choice[0] = -1;
  choice[1] = -1;
  dealt = 0;
  target = 0;
  guess = 0;
  lastPoints = 0;
  hasPeeked = false;
  practiceRound = true;
  callWasRight = false;
  abandonedRound = false;
}

size_t pack(const Saved& in, uint8_t* out, const size_t cap) {
  if (cap < kSaveBytes) return 0;
  uint8_t* p = out;
  *p++ = kSaveVersion;

  put16(p, in.record.rounds);
  put16(p, in.record.points);
  for (int i = 0; i < kBucketCount; ++i) put16(p, in.record.buckets[i]);
  put16(p, in.record.bestRoundTenths);
  for (int i = 0; i < kSeenWords; ++i) put32(p, in.seen[i]);

  put16(p, static_cast<uint16_t>(in.session.round));
  put16(p, static_cast<uint16_t>(in.session.total));
  put16(p, static_cast<uint16_t>(in.session.scoredRounds));
  put16(p, in.abandoned);

  uint8_t flags = 0;
  if (in.sessionStarted) flags |= kFlagSessionStarted;
  if (in.hasPeeked) flags |= kFlagHasPeeked;
  if (in.practiceRound) flags |= kFlagPractice;
  if (in.callWasRight) flags |= kFlagCallWasRight;
  if (in.abandonedRound) flags |= kFlagAbandonedRound;
  *p++ = flags;
  *p++ = in.screen;
  *p++ = in.resumeScreen;
  *p++ = in.dealt;
  put16(p, static_cast<uint16_t>(in.spectrum));
  put16(p, static_cast<uint16_t>(in.choice[0]));
  put16(p, static_cast<uint16_t>(in.choice[1]));
  *p++ = in.target;
  *p++ = in.guess;
  *p++ = in.lastPoints;
  return static_cast<size_t>(p - out);
}

bool unpack(const uint8_t* in, const size_t len, Saved& out) {
  if (in == nullptr || len < kLegacyBytes) return false;
  const uint8_t version = in[0];
  if (version != kSaveVersionLegacy && version != kSaveVersion) return false;

  out = Saved{};
  const uint8_t* p = in + 1;
  out.record.rounds = take16(p);
  out.record.points = take16(p);
  for (int i = 0; i < kBucketCount; ++i) out.record.buckets[i] = take16(p);
  out.record.bestRoundTenths = take16(p);
  for (int i = 0; i < kSeenWords; ++i) out.seen[i] = take32(p);

  // A version 1 card, or a version 2 card that was truncated before the
  // session block. The record is still good, so keep it and start the evening
  // fresh rather than throwing away a year of rounds over a missing tail.
  if (version == kSaveVersionLegacy || len < kSaveBytes) return true;

  out.session.round = take16(p);
  out.session.total = take16(p);
  out.session.scoredRounds = take16(p);
  out.abandoned = take16(p);

  const uint8_t flags = *p++;
  out.sessionStarted = (flags & kFlagSessionStarted) != 0;
  out.hasPeeked = (flags & kFlagHasPeeked) != 0;
  out.practiceRound = (flags & kFlagPractice) != 0;
  out.callWasRight = (flags & kFlagCallWasRight) != 0;
  out.abandonedRound = (flags & kFlagAbandonedRound) != 0;
  out.screen = *p++;
  out.resumeScreen = *p++;
  out.dealt = *p++;
  out.spectrum = static_cast<int16_t>(take16(p));
  out.choice[0] = static_cast<int16_t>(take16(p));
  out.choice[1] = static_cast<int16_t>(take16(p));
  out.target = *p++;
  out.guess = *p++;
  out.lastPoints = *p++;

  // A session cannot have started before its first round, and a slot that is
  // set at all has to be one the strip has. Either means the tail is not what
  // it claims, so keep the record and drop the round.
  //
  // Zero is legal for both, and means "not dealt yet": the pass and pick
  // screens are a resumable position with no number and no guess in them. What
  // a given screen actually requires is checked by the activity, which is the
  // layer that knows what a screen number means.
  if (out.session.round < 1) out.session.round = 1;
  const bool slotsAreSane = out.target <= kSlots && out.guess <= kSlots;
  if (!slotsAreSane) out.clearRound();
  return true;
}

}  // namespace wavelength

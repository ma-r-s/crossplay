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

  put32(p, in.bootId);
  put32(p, in.savedAt);
  return static_cast<size_t>(p - out);
}

bool unpack(const uint8_t* in, const size_t len, Saved& out) {
  if (in == nullptr || len < kLegacyBytes) return false;
  const uint8_t version = in[0];
  if (version != kSaveVersionLegacy && version != kSaveVersionSession && version != kSaveVersion) return false;

  out = Saved{};
  const uint8_t* p = in + 1;
  out.record.rounds = take16(p);
  out.record.points = take16(p);
  for (int i = 0; i < kBucketCount; ++i) out.record.buckets[i] = take16(p);
  out.record.bestRoundTenths = take16(p);
  for (int i = 0; i < kSeenWords; ++i) out.seen[i] = take32(p);

  // A version 1 card, or a later card that was truncated before the session
  // block. The record is still good, so keep it and start the evening fresh
  // rather than throwing away a year of rounds over a missing tail.
  //
  // Measured against the SESSION block's length, not against the whole file.
  // The two were the same number until v3 appended to the tail, and comparing
  // against the whole file would have made every v2 card ever written look
  // truncated -- silently dropping the session it was carrying, which is the
  // loss this file exists to prevent, committed by its own upgrade.
  if (version == kSaveVersionLegacy || len < kSessionBytes) return true;

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

  // The boot and the clock stamp are v3's tail. A v2 card stops here and keeps
  // both at zero, which resumeFor() reads as "this file cannot tell me which
  // run of the chip wrote it" -- so the table is asked. That is the right
  // answer for a card written by a build that never recorded it: the one thing
  // certain about it is that it was written before this build was installed.
  if (version >= kSaveVersion && len >= kSaveBytes) {
    out.bootId = take32(p);
    out.savedAt = take32(p);
  }

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

Resume resumeFor(const Saved& saved, const uint32_t bootId) {
  // Is there an evening in this file at all? Four things say yes, and the
  // screen is only one of them. A save sitting on the front door with round 7
  // and 23 points behind it has no round in flight and is EXACTLY the reported
  // bug one screen further out: the menu's own button says PLAY ROUND 7, and a
  // new table taps it and adds to a score it did not earn. What goes stale is
  // the session, not just the round.
  const bool carryable =
      saved.sessionStarted || saved.screen != 0 || saved.session.round > 1 || saved.session.total > 0;
  if (!carryable) return Resume::Nothing;

  // Zero on the CARD is "cannot know", never a match: it is what every v1 and
  // v2 card carries, and without this one guard a caller that also had no boot
  // id would match all of them at once. Guarding the caller's side as well
  // would read as symmetry and could not change an answer -- zero fails the
  // equality below like any other mismatch -- so it is not written.
  if (saved.bootId == 0) return Resume::Ask;
  return saved.bootId == bootId ? Resume::Carry : Resume::Ask;
}

int minutesSince(const uint32_t savedAt, const uint32_t now) {
  if (savedAt < kClockFloor || now < kClockFloor) return -1;
  if (now < savedAt) return -1;  // the clock moved backwards; an NTP sync does that
  return static_cast<int>((now - savedAt) / 60u);
}

}  // namespace wavelength

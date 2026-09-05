#include "TriviaCore.h"

namespace trivia {
namespace {

uint16_t readU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t readU32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

bool sameText(const char* a, const char* b) {
  if (a == nullptr || b == nullptr) return false;
  while (*a != '\0' && *b != '\0') {
    char ca = *a, cb = *b;
    if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
    if (ca != cb) return false;
    ++a;
    ++b;
  }
  return *a == '\0' && *b == '\0';
}

}  // namespace

bool Pack::open(ByteSource& source) {
  source_ = nullptr;
  count_ = 0;

  uint8_t header[16] = {};
  if (!source.read(0, header, sizeof(header))) return false;
  for (int i = 0; i < 8; ++i) {
    if (header[i] != kMagic[i]) return false;
  }
  if (readU16(header + 8) != kFormatVersion) return false;

  const uint32_t count = readU32(header + 12);
  if (count == 0) return false;

  // The index is (count + 1) entries; the file must be at least that big or a
  // read past the end would return whatever the card had there.
  const uint32_t indexBytes = (count + 1) * 4u;
  if (source.size() < sizeof(header) + indexBytes) return false;

  // The sentinel is the total size of the record blob. A card pulled mid-write
  // otherwise opens as a complete pack whose tail records are whatever bytes
  // were there before, which reads as corrupt questions rather than as a
  // missing pack. Rejecting whole means the app offers to fetch one instead.
  uint8_t sentinel[4] = {};
  if (!source.read(sizeof(header) + count * 4u, sentinel, sizeof(sentinel))) return false;
  const uint32_t blobBytes = readU32(sentinel);
  if (static_cast<uint64_t>(sizeof(header)) + indexBytes + blobBytes > source.size()) return false;

  source_ = &source;
  count_ = count;
  indexAt_ = sizeof(header);
  base_ = sizeof(header) + indexBytes;
  return true;
}

bool Pack::read(const uint32_t index, Question& out) const {
  if (source_ == nullptr || index >= count_) return false;

  uint8_t pair[8] = {};
  if (!source_->read(indexAt_ + index * 4u, pair, sizeof(pair))) return false;
  const uint32_t start = readU32(pair);
  const uint32_t end = readU32(pair + 4);
  if (end <= start) return false;

  const uint32_t length = end - start;
  if (length > kMaxRecordBytes) return false;  // reject, never truncate

  uint8_t buffer[kMaxRecordBytes];
  if (!source_->read(base_ + start, buffer, length)) return false;
  if (length < 5) return false;

  out.difficulty_ = buffer[0];
  out.year_ = readU16(buffer + 1);
  out.altCount_ = buffer[3];
  out.wrongCount_ = buffer[4];
  if (out.altCount_ > kMaxAlternates || out.wrongCount_ > kMaxDistractors) return false;

  const int fields = 2 + out.altCount_ + out.wrongCount_;
  uint32_t in = 5;
  uint32_t outAt = 0;
  for (int i = 0; i < fields; ++i) {
    if (in + 2 > length) return false;
    const uint16_t len = readU16(buffer + in);
    in += 2;
    if (in + len > length) return false;
    if (outAt + len + 1 > kMaxRecordBytes) return false;
    out.offset_[i] = static_cast<uint16_t>(outAt);
    for (uint16_t b = 0; b < len; ++b) out.bytes_[outAt + b] = static_cast<char>(buffer[in + b]);
    out.bytes_[outAt + len] = '\0';
    outAt += len + 1u;
    in += len;
  }
  out.fieldCount_ = fields;
  return true;
}

// One chunked pass when the state file is opened, answering both counts. A byte
// at a time would be one read per question through the storage mutex; a
// 256-byte window is a 256th of that, and the buffer is small enough for the
// stack. No question count written down here: the pack is as big as the rating
// run that built it.
void PackState::scanCounts() {
  seenCount_ = 0;
  flaggedCount_ = 0;
  if (source_ == nullptr) return;
  uint8_t window[256];
  for (uint32_t at = 0; at < count_;) {
    const uint32_t want = (count_ - at) < sizeof(window) ? (count_ - at) : sizeof(window);
    if (!source_->read(at, window, want)) break;
    for (uint32_t i = 0; i < want; ++i) {
      if ((window[i] & kSeen) != 0) ++seenCount_;
      if ((window[i] & kFlagged) != 0) ++flaggedCount_;
    }
    at += want;
  }
}

bool PackState::open(WritableByteSource& source, const uint32_t count) {
  source_ = nullptr;
  count_ = 0;
  if (count == 0) return false;
  // The state file is one byte per pack index, so its length and the pack's
  // count are the same fact written twice. ANY disagreement means a pack was
  // replaced under it; the caller rewrites rather than reading bytes that
  // describe questions no longer at those indices.
  //
  // `!=`, not `<`. A LONGER file used to be accepted, on the reasoning that
  // every index still had a byte -- but a stale FLAGGED byte landing on an
  // arbitrary question hides it from every draw, with nothing on screen and no
  // way for a player to clear it. That is what a pack SHRINKING does, which is
  // what a rated pack does to the 50,000 it replaces.
  if (source.size() != count) return false;
  source_ = &source;
  count_ = count;
  // Counted here, not lazily: without this the totals start at zero every boot
  // and report only the current session, which renders perfectly plausibly.
  scanCounts();
  return true;
}

uint8_t PackState::flags(const uint32_t index) const {
  if (source_ == nullptr || index >= count_) return 0;
  uint8_t byte = 0;
  if (!source_->read(index, &byte, 1)) return 0;
  return byte;
}

bool PackState::setFlag(const uint32_t index, const uint8_t bit) {
  if (source_ == nullptr || index >= count_) return false;
  uint8_t byte = flags(index);
  const uint8_t updated = static_cast<uint8_t>(byte | bit);
  if (updated == byte) return true;  // already set, no write
  if (!source_->write(index, &updated, 1)) return false;
  if (!source_->flush()) return false;
  // Kept in step here rather than rescanned, and only when the bit is NEW --
  // the early return above means this cannot double-count a reflag.
  if ((bit & kSeen) != 0 && (byte & kSeen) == 0) ++seenCount_;
  if ((bit & kFlagged) != 0 && (byte & kFlagged) == 0) ++flaggedCount_;
  return true;
}

uint32_t Rng::next() {
  // xorshift32. Small, no multiply, and identical on host and device, which is
  // what lets a test assert a sequence.
  state_ ^= state_ << 13;
  state_ ^= state_ >> 17;
  state_ ^= state_ << 5;
  return state_;
}

uint32_t Rng::below(const uint32_t bound) { return bound == 0 ? 0 : next() % bound; }

void Chooser::begin(const Pack& pack, PackState& state, Rng& rng) {
  pack_ = &pack;
  state_ = &state;
  rng_ = &rng;
}

bool Chooser::next(uint32_t& indexOut, const bool requireChoice, const int difficulty) {
  if (pack_ == nullptr || state_ == nullptr || pack_->count() == 0) return false;
  const uint32_t count = pack_->count();

  // A FRESH random start on every call, not a cursor walked forward from the
  // last pick. Walking forward made consecutive questions ADJACENT RECORDS, and
  // the pack is built from a Jeopardy archive ordered by game and then category
  // -- so a round served twelve "this country" clues whose answer was Italy
  // every single time, with the same China/Japan/India/Spain distractors. A
  // player who knew the first answer got the other eleven free; one who did not
  // scored chance. Either way the round was decided by question one.
  //
  // The forward scan stays, because it is how the filters below are satisfied:
  // it steps over flagged, seen, unreadable and wrong-difficulty records to the
  // next usable one. What changed is only where it starts.
  const uint32_t start = rng_->below(count);

  // Two passes: prefer unseen, then allow seen. Without the second pass the app
  // simply stops once the pack is exhausted, which is a dead end rather than an
  // ending -- and at five questions a night that is years away, but a device
  // handed round a bar reaches it faster than anyone expects.
  for (int pass = 0; pass < 2; ++pass) {
    for (uint32_t step = 0; step < count; ++step) {
      const uint32_t i = (start + step) % count;
      const uint8_t flags = state_->flags(i);
      if ((flags & kFlagged) != 0) continue;  // a player rejected it
      if (pass == 0 && (flags & kSeen) != 0) continue;
      Question q;
      if (!pack_->read(i, q)) continue;  // torn record, skip
      if (requireChoice && !q.playableAsChoice()) continue;
      if (difficulty != 0 && q.difficulty() != difficulty) continue;
      indexOut = i;
      return true;
    }
  }
  return false;
}

bool buildChoices(const Question& question, Rng& rng, Choices& out) {
  if (!question.playableAsChoice()) return false;

  // Draw three of the stored distractors by partial Fisher-Yates over an index
  // list, so a replay of the same question gives different wrong answers.
  int pool[kMaxDistractors];
  const int n = question.distractorCount();
  for (int i = 0; i < n; ++i) pool[i] = i;
  for (int i = 0; i < kOptions - 1; ++i) {
    const int j = i + static_cast<int>(rng.below(static_cast<uint32_t>(n - i)));
    const int t = pool[i];
    pool[i] = pool[j];
    pool[j] = t;
  }

  out.option[0] = question.answer();
  for (int i = 0; i < kOptions - 1; ++i) out.option[i + 1] = question.distractor(pool[i]);

  // Shuffle all four. The stored order always has the answer first, so drawing
  // without this would put it in slot one every time -- the position leak the
  // pack's own source corpora have, measured at 28% on A/B.
  for (int i = kOptions - 1; i > 0; --i) {
    const int j = static_cast<int>(rng.below(static_cast<uint32_t>(i + 1)));
    const char* t = out.option[i];
    out.option[i] = out.option[j];
    out.option[j] = t;
  }
  for (int i = 0; i < kOptions; ++i) {
    if (sameText(out.option[i], question.answer())) out.correct = i;
  }
  return true;
}

bool answerMatches(const Question& question, const char* given) {
  if (sameText(question.answer(), given)) return true;
  for (int i = 0; i < question.alternateCount(); ++i) {
    if (sameText(question.alternate(i), given)) return true;
  }
  return false;
}

}  // namespace trivia

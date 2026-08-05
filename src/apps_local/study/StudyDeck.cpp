#include "StudyDeck.h"

#include <cstring>

namespace study {

namespace {

constexpr uint8_t kDeckMagic[8] = {'X', 'S', 'T', 'U', 'D', 'Y', 'D', 0};
constexpr uint8_t kMetaMagic[8] = {'X', 'S', 'T', 'U', 'D', 'Y', 'M', 0};
constexpr uint16_t kFormatVersion = 1;

// Everything on disk is little-endian, and the ESP32-S3 is too, but a raw
// reinterpret_cast on a byte buffer is an unaligned load -- which faults on
// RISC-V and is undefined everywhere. memcpy compiles to the same instruction
// when the alignment happens to be right and stays correct when it is not.
uint16_t readU16(const uint8_t* p) {
  uint16_t v;
  std::memcpy(&v, p, sizeof(v));
  return v;
}

uint32_t readU32(const uint8_t* p) {
  uint32_t v;
  std::memcpy(&v, p, sizeof(v));
  return v;
}

int64_t readI64(const uint8_t* p) {
  int64_t v;
  std::memcpy(&v, p, sizeof(v));
  return v;
}

float readF32(const uint8_t* p) {
  float v;
  std::memcpy(&v, p, sizeof(v));
  return v;
}

constexpr int64_t kSecondsPerDay = 86400;

}  // namespace

bool DeckMeta::hasParams() const {
  for (int i = 0; i < kNumParams; ++i) {
    if (params[i] != 0.0f) return true;
  }
  return false;
}

Memory CardState::memory() const {
  Memory m;
  m.stability = stability;
  m.difficulty = difficulty;
  // A card with no stability has never been through FSRS, whatever its state
  // byte claims. Trusting the byte alone would let a corrupt record feed
  // zeroes into pow() and produce an infinite interval.
  m.learned = stability > 0.0f && difficulty > 0.0f;
  return m;
}

void CardState::setMemory(const Memory& m) {
  stability = m.stability;
  difficulty = m.difficulty;
}

const char* Note::field(const Field f) const {
  const int i = static_cast<int>(f);
  if (i < 0 || i >= kFieldCount) return "";
  return bytes_ + offsets_[i];
}

uint16_t Note::length(const Field f) const {
  const int i = static_cast<int>(f);
  if (i < 0 || i >= kFieldCount) return 0;
  return lengths_[i];
}

bool StudyDeck::openMeta(ByteSource& meta) {
  // magic(8) + version(2) + reserved(2) + params(76) + f32 + 3*i32 + i64 + 2
  uint8_t header[8 + 2 + 2 + kNumParams * 4 + 4 + 12 + 8 + 2];
  if (!meta.read(0, header, sizeof(header))) return false;
  if (std::memcmp(header, kMetaMagic, sizeof(kMetaMagic)) != 0) return false;
  if (readU16(header + 8) != kFormatVersion) return false;

  const uint8_t* p = header + 12;
  for (int i = 0; i < kNumParams; ++i) {
    meta_.params[i] = readF32(p + i * 4);
  }
  p += kNumParams * 4;
  meta_.desiredRetention = readF32(p);
  meta_.maximumInterval = static_cast<int32_t>(readU32(p + 4));
  meta_.newPerDay = static_cast<int32_t>(readU32(p + 8));
  meta_.reviewsPerDay = static_cast<int32_t>(readU32(p + 12));
  p += 16;
  meta_.collectionCreated = readI64(p);
  meta_.rolloverHour = p[8];
  const uint8_t nameLength = p[9];

  const uint32_t nameOffset = static_cast<uint32_t>(sizeof(header));
  const uint32_t copyLength = nameLength < sizeof(meta_.name) - 1 ? nameLength : sizeof(meta_.name) - 1;
  std::memset(meta_.name, 0, sizeof(meta_.name));
  if (copyLength > 0 && !meta.read(nameOffset, meta_.name, copyLength)) return false;
  meta_.name[copyLength] = '\0';

  // A retention target outside this band is a corrupt file, not a preference.
  // Left unchecked it produces intervals of zero or of centuries.
  if (!(meta_.desiredRetention > 0.5f && meta_.desiredRetention < 0.999f)) {
    meta_.desiredRetention = 0.9f;
  }
  if (meta_.maximumInterval < 1) meta_.maximumInterval = 36500;
  if (meta_.rolloverHour > 23) meta_.rolloverHour = 4;
  return true;
}

bool StudyDeck::openDeck(ByteSource& deck) {
  uint8_t header[16];
  if (!deck.read(0, header, sizeof(header))) return false;
  if (std::memcmp(header, kDeckMagic, sizeof(kDeckMagic)) != 0) return false;
  if (readU16(header + 8) != kFormatVersion) return false;

  fieldCount_ = header[10];
  if (fieldCount_ != kFieldCount) return false;
  const uint32_t count = readU32(header + 12);
  // The index is (count + 1) uint32s and has to fit the file. This is the one
  // sanity check that catches a truncated copy, which is the realistic failure
  // for a file the user drags onto an SD card.
  if (count == 0 || count > 4000000u) return false;
  const uint64_t indexBytes = (static_cast<uint64_t>(count) + 1) * 4;
  if (16 + indexBytes > deck.size()) return false;

  noteCount_ = static_cast<int>(count);
  indexBase_ = 16;
  return true;
}

bool StudyDeck::loadNote(ByteSource& deck, const int index, Note& out) const {
  if (index < 0 || index >= noteCount_) return false;

  // Two offsets, read as one 8-byte pull: the record is what lies between them.
  // The sentinel the converter appends is what makes this a subtraction rather
  // than a scan.
  uint8_t bounds[8];
  if (!deck.read(indexBase_ + static_cast<uint32_t>(index) * 4, bounds, sizeof(bounds))) return false;
  const uint32_t start = readU32(bounds);
  const uint32_t end = readU32(bounds + 4);
  if (end <= start || end > deck.size()) return false;

  const uint32_t recordLength = end - start;
  // Reject rather than truncate: a clipped UTF-8 sequence draws as garbage and
  // gets blamed on the font.
  if (recordLength + kFieldCount > kMaxNoteBytes) return false;

  uint8_t record[kMaxNoteBytes];
  if (!deck.read(start, record, recordLength)) return false;

  uint32_t in = 0;
  uint16_t outPos = 0;
  for (int f = 0; f < kFieldCount; ++f) {
    if (in + 2 > recordLength) return false;
    uint16_t length = readU16(record + in);
    in += 2;
    if (in + length > recordLength) return false;

    // The sentence field always ends in two bytes that are the emphasis span
    // rather than text -- (0, 0) when Anki had no <b>. Unconditional is what
    // makes this unambiguous: see the note in anki_to_deck.py.
    if (f == static_cast<int>(Field::Sentence)) {
      if (length < 2) return false;
      out.emphasisOffset_ = record[in + length - 2];
      out.emphasisLength_ = record[in + length - 1];
      length -= 2;
    }

    out.offsets_[f] = outPos;
    out.lengths_[f] = length;
    std::memcpy(out.bytes_ + outPos, record + in, length);
    outPos = static_cast<uint16_t>(outPos + length);
    out.bytes_[outPos++] = '\0';
    // Advance past the field as it was written, span bytes included.
    in += (f == static_cast<int>(Field::Sentence)) ? length + 2u : length;
  }
  return true;
}

bool StudyDeck::loadCard(ByteSource& cards, const int index, CardState& out) const {
  if (index < 0 || index >= noteCount_) return false;
  uint8_t record[kCardRecordSize];
  if (!cards.read(static_cast<uint32_t>(index) * kCardRecordSize, record, sizeof(record))) return false;

  out.ankiCardId = readI64(record);
  out.stability = readF32(record + 8);
  out.difficulty = readF32(record + 12);
  out.dueDay = static_cast<int32_t>(readU32(record + 16));
  out.lastReviewDay = static_cast<int32_t>(readU32(record + 20));
  out.reps = readU16(record + 24);
  out.lapses = readU16(record + 26);
  out.state = record[28];
  out.flags = record[29];
  return true;
}

bool StudyDeck::storeCard(WritableByteSource& cards, const int index, const CardState& in) const {
  if (index < 0 || index >= noteCount_) return false;
  uint8_t record[kCardRecordSize] = {};
  std::memcpy(record, &in.ankiCardId, 8);
  std::memcpy(record + 8, &in.stability, 4);
  std::memcpy(record + 12, &in.difficulty, 4);
  std::memcpy(record + 16, &in.dueDay, 4);
  std::memcpy(record + 20, &in.lastReviewDay, 4);
  std::memcpy(record + 24, &in.reps, 2);
  std::memcpy(record + 26, &in.lapses, 2);
  record[28] = in.state;
  record[29] = in.flags;
  return cards.write(static_cast<uint32_t>(index) * kCardRecordSize, record, sizeof(record));
}

int dayNumber(const DeckMeta& meta, const int64_t nowEpochSeconds) {
  // meta.collectionCreated is already the rollover-adjusted instant of day
  // zero, exactly as Anki stores it, so the rollover hour needs no further
  // arithmetic here -- it is carried for display and for re-deriving the
  // boundary when a deck is rebuilt.
  const int64_t elapsed = nowEpochSeconds - meta.collectionCreated;
  if (elapsed < 0) return 0;
  return static_cast<int>(elapsed / kSecondsPerDay);
}

}  // namespace study

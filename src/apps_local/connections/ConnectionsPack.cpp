#include "ConnectionsPack.h"

#include <cstring>

namespace connections {

namespace {

// Little-endian by hand rather than memcpy of a struct: the pack is written on
// one machine and read on another (the importer runs in host tests too), and
// RISC-V faults on unaligned wide reads anyway.
void putU16(uint8_t* p, const uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>(v >> 8);
}

void putU32(uint8_t* p, const uint32_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
  p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
  p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

uint16_t getU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

uint32_t getU32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

// Length-prefixed, single byte, because nothing here approaches 255: the
// longest word in the whole archive is 28 characters and the longest group name
// is 71.
uint32_t stringSize(const char* s) { return 1u + static_cast<uint32_t>(std::strlen(s)); }

}  // namespace

uint32_t encodedSize(const Puzzle& puzzle) {
  uint32_t size = 2 + 4;  // id, date
  for (int g = 0; g < kGroups; ++g) {
    size += 1;  // level
    size += stringSize(puzzle.groups[g].name);
    for (int m = 0; m < kMembers; ++m) size += stringSize(puzzle.groups[g].members[m]);
  }
  return size;
}

// --- reading ---------------------------------------------------------------

bool PackReader::open(const ReadFn read, void* indexCtx, void* dataCtx, const uint32_t indexSize) {
  read_ = nullptr;
  count_ = 0;
  if (read == nullptr || indexSize < kIndexHeaderSize) return false;

  uint8_t header[kIndexHeaderSize];
  if (!read(indexCtx, 0, header, kIndexHeaderSize)) return false;
  if (getU32(header) != kPackMagic) return false;
  if (getU16(header + 4) != kPackVersion) return false;

  const uint32_t body = indexSize - kIndexHeaderSize;
  // A partial entry means the import was interrupted. Refuse the pack rather
  // than serve a puzzle whose offset was never written.
  if (body % kIndexEntrySize != 0 || body == 0) return false;

  read_ = read;
  indexCtx_ = indexCtx;
  dataCtx_ = dataCtx;
  count_ = static_cast<int>(body / kIndexEntrySize);
  return true;
}

bool PackReader::dateAt(const int index, uint32_t& outDate) const {
  if (read_ == nullptr || index < 0 || index >= count_) return false;
  uint8_t entry[kIndexEntrySize];
  const uint32_t offset = kIndexHeaderSize + static_cast<uint32_t>(index) * kIndexEntrySize;
  if (!read_(indexCtx_, offset, entry, kIndexEntrySize)) return false;
  outDate = getU32(entry);
  return true;
}

int PackReader::indexOfDate(const uint32_t date) const {
  int lo = 0;
  int hi = count_ - 1;
  while (lo <= hi) {
    const int mid = lo + (hi - lo) / 2;
    uint32_t midDate = 0;
    if (!dateAt(mid, midDate)) return -1;
    if (midDate == date) return mid;
    if (midDate < date) {
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return -1;
}

int PackReader::indexOnOrBefore(const uint32_t date) const {
  int lo = 0;
  int hi = count_ - 1;
  int best = -1;
  while (lo <= hi) {
    const int mid = lo + (hi - lo) / 2;
    uint32_t midDate = 0;
    if (!dateAt(mid, midDate)) return -1;
    if (midDate <= date) {
      best = mid;
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return best;
}

bool PackReader::readPuzzle(const int index, Puzzle& out) const {
  if (read_ == nullptr || index < 0 || index >= count_) return false;

  uint8_t entry[kIndexEntrySize];
  const uint32_t indexOffset = kIndexHeaderSize + static_cast<uint32_t>(index) * kIndexEntrySize;
  if (!read_(indexCtx_, indexOffset, entry, kIndexEntrySize)) return false;
  uint32_t at = getU32(entry + 4);

  uint8_t head[6];
  if (!read_(dataCtx_, at, head, sizeof(head))) return false;
  at += sizeof(head);
  out = Puzzle{};
  out.id = getU16(head);
  out.date = getU32(head + 2);

  // The date is stored in both files. They are written together, so a mismatch
  // means the pack is corrupt in a way that would otherwise deal the wrong
  // puzzle for a date, which is worse than refusing to deal one.
  if (out.date != getU32(entry)) return false;

  for (int g = 0; g < kGroups; ++g) {
    uint8_t level = 0;
    if (!read_(dataCtx_, at, &level, 1)) return false;
    ++at;
    out.groups[g].level = level;

    uint8_t len = 0;
    if (!read_(dataCtx_, at, &len, 1)) return false;
    ++at;
    if (len > kMaxGroupLen) return false;
    if (len > 0 && !read_(dataCtx_, at, out.groups[g].name, len)) return false;
    out.groups[g].name[len] = '\0';
    at += len;

    for (int m = 0; m < kMembers; ++m) {
      if (!read_(dataCtx_, at, &len, 1)) return false;
      ++at;
      if (len > kMaxWordLen) return false;
      if (len > 0 && !read_(dataCtx_, at, out.groups[g].members[m], len)) return false;
      out.groups[g].members[m][len] = '\0';
      at += len;
    }
  }
  return true;
}

// --- writing ---------------------------------------------------------------

bool PackWriter::begin(const WriteFn write, void* indexCtx, void* dataCtx) {
  write_ = write;
  indexCtx_ = indexCtx;
  dataCtx_ = dataCtx;
  dataOffset_ = 0;
  lastDate_ = 0;
  written_ = 0;
  if (write == nullptr) return false;

  uint8_t header[kIndexHeaderSize] = {};
  putU32(header, kPackMagic);
  putU16(header + 4, kPackVersion);
  return write_(indexCtx_, header, kIndexHeaderSize);
}

bool PackWriter::add(const Puzzle& puzzle) {
  if (write_ == nullptr) return false;
  // Strictly ascending: the reader binary-searches this table, and an
  // out-of-order entry would make the search silently wrong rather than fail.
  if (written_ > 0 && puzzle.date <= lastDate_) return false;
  if (!isPlayable(puzzle)) return false;

  uint8_t entry[kIndexEntrySize];
  putU32(entry, puzzle.date);
  putU32(entry + 4, dataOffset_);
  if (!write_(indexCtx_, entry, kIndexEntrySize)) return false;

  uint8_t head[6];
  putU16(head, puzzle.id);
  putU32(head + 2, puzzle.date);
  if (!write_(dataCtx_, head, sizeof(head))) return false;
  dataOffset_ += sizeof(head);

  for (int g = 0; g < kGroups; ++g) {
    const uint8_t level = puzzle.groups[g].level;
    if (!write_(dataCtx_, &level, 1)) return false;
    ++dataOffset_;

    const uint8_t nameLen = static_cast<uint8_t>(std::strlen(puzzle.groups[g].name));
    if (!write_(dataCtx_, &nameLen, 1)) return false;
    ++dataOffset_;
    if (nameLen > 0 && !write_(dataCtx_, puzzle.groups[g].name, nameLen)) return false;
    dataOffset_ += nameLen;

    for (int m = 0; m < kMembers; ++m) {
      const uint8_t len = static_cast<uint8_t>(std::strlen(puzzle.groups[g].members[m]));
      if (!write_(dataCtx_, &len, 1)) return false;
      ++dataOffset_;
      if (len > 0 && !write_(dataCtx_, puzzle.groups[g].members[m], len)) return false;
      dataOffset_ += len;
    }
  }

  lastDate_ = puzzle.date;
  ++written_;
  return true;
}

}  // namespace connections

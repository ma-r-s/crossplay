#pragma once

// The on-card puzzle pack: how 1143 puzzles live on the SD card and how one is
// read back without holding the rest in RAM.
//
// Two append-only files rather than one:
//
//   connections.idx   magic, version, then one 8-byte entry per puzzle
//                     (date, offset-into-dat), ascending by date
//   connections.dat   the puzzle records themselves, back to back
//
// The split is what makes writing cheap. A single file would need either the
// whole index buffered in RAM until the end (32KB, during the one moment when
// TLS is also competing for heap) or a seek back over every record. Two files
// need neither: both are written straight through, and the puzzle count falls
// out of the index file's size, so there is no count field to backfill.
//
// Reading is a binary search over the index by seek, so finding today's puzzle
// costs about eleven 8-byte reads and no allocation at all.
//
// Freestanding: I/O arrives as two function pointers, so the device passes
// HalFile and a host test passes a byte vector. See host-tests/connections/.

#include <cstdint>

#include "ConnectionsCore.h"

namespace connections {

// "CXP1". Bumped if the record layout changes, which per the cache-format rule
// means the old pack is discarded and re-imported rather than misread.
constexpr uint32_t kPackMagic = 0x31505843;
constexpr uint16_t kPackVersion = 1;
constexpr uint32_t kIndexHeaderSize = 8;
constexpr uint32_t kIndexEntrySize = 8;

// Random-access reads over the index and data files. Returns false on a short
// or failed read; a truncated pack must fail loudly rather than deal a puzzle
// with half its words.
using ReadFn = bool (*)(void* ctx, uint32_t offset, void* dst, uint32_t len);

class PackReader {
 public:
  // `indexSize` is the byte size of the index file, which is where the puzzle
  // count comes from.
  bool open(ReadFn read, void* indexCtx, void* dataCtx, uint32_t indexSize);

  bool isOpen() const { return count_ > 0; }
  int count() const { return count_; }

  bool dateAt(int index, uint32_t& outDate) const;

  // Exact match, or -1. YYYYMMDD compares as an integer, so the index is
  // ascending by construction and a binary search is valid.
  int indexOfDate(uint32_t date) const;

  // The newest puzzle no later than `date`. This is what "today's puzzle"
  // actually wants: the archive can lag by a day, and showing yesterday's is
  // right where showing nothing is not.
  int indexOnOrBefore(uint32_t date) const;

  bool readPuzzle(int index, Puzzle& out) const;

 private:
  ReadFn read_ = nullptr;
  void* indexCtx_ = nullptr;
  void* dataCtx_ = nullptr;
  int count_ = 0;
};

// Appends to the two files in order. Sequential only: no seeks, so this works
// on a plain append-only sink.
using WriteFn = bool (*)(void* ctx, const void* src, uint32_t len);

class PackWriter {
 public:
  bool begin(WriteFn write, void* indexCtx, void* dataCtx);
  // Puzzles must arrive in ascending date order, which is how the source
  // archive is published. Rejects one that is not, rather than writing an index
  // the binary search would then silently misread.
  bool add(const Puzzle& puzzle);
  int written() const { return written_; }
  uint32_t lastDate() const { return lastDate_; }

 private:
  WriteFn write_ = nullptr;
  void* indexCtx_ = nullptr;
  void* dataCtx_ = nullptr;
  uint32_t dataOffset_ = 0;
  uint32_t lastDate_ = 0;
  int written_ = 0;
};

// Encoded size of a puzzle record. Exposed for tests and for sizing estimates.
uint32_t encodedSize(const Puzzle& puzzle);

}  // namespace connections

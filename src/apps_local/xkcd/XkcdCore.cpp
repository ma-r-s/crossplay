#include "XkcdCore.h"

namespace xkcd {
namespace {

// Explicit little-endian reads. The ESP32-C3 faults on unaligned multi-byte
// loads, and a 32-byte record read into a buffer is not guaranteed to be
// 4-byte aligned, so casting the buffer to a wider pointer is not an option
// here even though it would compile and pass on the host. See CLAUDE.md.
uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

uint32_t rd32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

void wr16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

void wr32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
  p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
  p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

// Copy the `which`'th NUL-terminated string starting at `offset`. Reads in
// blocks rather than a byte at a time: a title is 20-odd bytes and an SD read
// costs the same whether it is 1 byte or 128, so per-byte reads would make
// drawing a list of ten rows ten times slower than it needs to be.
int readString(ByteSource& text, uint32_t offset, int which, char* out, int cap) {
  if (!out || cap <= 0) return 0;
  out[0] = '\0';
  const uint32_t end = text.size();
  if (offset >= end) return 0;

  uint8_t buf[128];
  int seen = 0;   // how many terminators we have passed
  int wrote = 0;  // bytes placed in out
  uint32_t pos = offset;

  while (pos < end) {
    uint32_t want = sizeof(buf);
    if (want > end - pos) want = end - pos;
    if (!text.read(pos, buf, want)) break;

    for (uint32_t i = 0; i < want; ++i) {
      const char ch = static_cast<char>(buf[i]);
      if (ch == '\0') {
        if (seen == which) {
          out[wrote < cap ? wrote : cap - 1] = '\0';
          return wrote;
        }
        ++seen;
        continue;
      }
      if (seen == which && wrote < cap - 1) out[wrote++] = ch;
    }
    pos += want;
  }

  out[wrote < cap ? wrote : cap - 1] = '\0';
  return wrote;
}

}  // namespace

Comic decodeRecord(const uint8_t* rec) {
  Comic c;
  if (!rec) return c;
  c.num = rd16(rec + 0);
  c.width = rd16(rec + 2);
  c.height = rd16(rec + 4);
  c.stride = rd16(rec + 6);
  c.year = rd16(rec + 8);
  c.month = rec[10];
  c.day = rec[11];
  c.imageOffset = rd32(rec + 12);
  c.textOffset = rd32(rec + 16);
  // bytes 20..31 are padding, reserved so a new field does not move the others
  return c;
}

void encodeRecord(const Comic& c, uint8_t* rec) {
  if (!rec) return;
  for (int i = 0; i < static_cast<int>(kIndexRecordBytes); ++i) rec[i] = 0;
  wr16(rec + 0, c.num);
  wr16(rec + 2, c.width);
  wr16(rec + 4, c.height);
  wr16(rec + 6, c.stride);
  wr16(rec + 8, c.year);
  rec[10] = c.month;
  rec[11] = c.day;
  wr32(rec + 12, c.imageOffset);
  wr32(rec + 16, c.textOffset);
}

bool Archive::open(ByteSource& index) {
  count_ = 0;
  maxNum_ = 0;
  size_ = index.size();
  if (size_ < kIndexHeaderBytes) return false;

  uint8_t head[kIndexHeaderBytes];
  if (!index.read(0, head, kIndexHeaderBytes)) return false;
  if (rd32(head) != kMagic) return false;
  if (rd16(head + 4) != kFormatVersion) return false;

  const uint32_t count = rd32(head + 8);
  const uint32_t maxNum = rd32(head + 12);

  // The header's own count has to agree with the file it is in. A truncated
  // copy (a card pulled mid-write) otherwise reads as a full archive whose
  // last records are whatever was on the card before, which draws as garbage
  // rather than as a missing comic.
  const uint32_t available = (size_ - kIndexHeaderBytes) / kIndexRecordBytes;
  if (count == 0 || count > available) return false;

  count_ = static_cast<int>(count);
  maxNum_ = static_cast<uint16_t>(maxNum);
  return true;
}

bool Archive::at(ByteSource& index, int i, Comic& out) const {
  if (i < 0 || i >= count_) return false;
  uint8_t rec[kIndexRecordBytes];
  const uint32_t offset = kIndexHeaderBytes + static_cast<uint32_t>(i) * kIndexRecordBytes;
  if (!index.read(offset, rec, kIndexRecordBytes)) return false;
  out = decodeRecord(rec);
  return out.valid();
}

int Archive::lowerBound(ByteSource& index, uint16_t num) const {
  int lo = 0;
  int hi = count_;
  while (lo < hi) {
    const int mid = lo + (hi - lo) / 2;
    Comic c;
    if (!at(index, mid, c)) return -1;
    if (c.num < num) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

int Archive::find(ByteSource& index, uint16_t num) const {
  const int i = lowerBound(index, num);
  if (i < 0 || i >= count_) return -1;
  Comic c;
  if (!at(index, i, c)) return -1;
  return c.num == num ? i : -1;
}

int readTitle(ByteSource& text, const Comic& c, char* out, int cap) {
  return readString(text, c.textOffset, 0, out, cap);
}

int readAlt(ByteSource& text, const Comic& c, char* out, int cap) {
  return readString(text, c.textOffset, 1, out, cap);
}

int maxScroll(const Comic& c, int viewportH) {
  if (viewportH <= 0) return 0;
  const int over = static_cast<int>(c.height) - viewportH;
  return over > 0 ? over : 0;
}

Placement place(const Comic& c, int screenW, int viewportH, int scrollY) {
  Placement p;
  p.originX = (screenW - static_cast<int>(c.width)) / 2;
  if (p.originX < 0) p.originX = 0;

  const int maxS = maxScroll(c, viewportH);
  p.pans = maxS > 0;

  if (!p.pans) {
    // Fits: centred. A strip sits in the middle of the page like a comic on
    // newsprint, which is what it is.
    //
    // This was briefly anchored to the top, with the alt text filling the band
    // underneath, on the reasoning that dead space is a defect. That was a
    // decision nobody asked for: the alt text is a joke you choose to read,
    // and putting it on the page turns every comic into a comic plus an
    // explanation. The button is the way to it. What is left is one comic,
    // centred, on paper.
    p.scrollY = 0;
    p.visibleH = c.height;
    p.originY = (viewportH - static_cast<int>(c.height)) / 2;
    if (p.originY < 0) p.originY = 0;
    return p;
  }

  p.scrollY = scrollY < 0 ? 0 : (scrollY > maxS ? maxS : scrollY);
  p.originY = 0;
  p.visibleH = viewportH;
  return p;
}

void gapWindowFor(const Comic& c, int viewportH, int scrollY, bool down, int& firstRow, int& rowCount) {
  const int step = viewportH / 2 > 0 ? viewportH / 2 : 1;
  const int tol = viewportH * kSnapToleranceNum / kSnapToleranceDen;
  const int target = down ? scrollY + step : scrollY - step;

  // A landing row L is `kGutterPad` above the first inked row after a gap run,
  // so to judge every L in [target-tol, target+tol] we need the rows from the
  // start of the earliest qualifying run to the latest first-inked row.
  int lo = target - tol + kGutterPad - kMinGutterRows;
  int hi = target + tol + kGutterPad;

  if (lo < 0) lo = 0;
  if (hi > static_cast<int>(c.height) - 1) hi = static_cast<int>(c.height) - 1;
  if (hi < lo) {
    firstRow = 0;
    rowCount = 0;
    return;
  }
  firstRow = lo;
  rowCount = hi - lo + 1;
}

namespace {

// The best landing row within tolerance of `target`, or `target` itself when
// the window shows no gap worth stopping at.
int snapToGap(const Comic& c, int viewportH, int target, const GapWindow& w) {
  if (!w.isGap || w.rowCount <= 0) return target;

  const int tol = viewportH * kSnapToleranceNum / kSnapToleranceDen;
  int best = target;
  // Deliberately "nothing chosen yet" rather than `tol + 1`. Seeding the
  // distance with the tolerance makes the explicit `d <= tol` test below
  // redundant -- a mutation removing it changed nothing, because the seed was
  // silently enforcing the same bound. The bound belongs in the test that
  // states it, not in an initialiser that happens to imply it.
  int bestDist = -1;
  int gapRun = 0;

  for (int i = 0; i < w.rowCount; ++i) {
    const int y = w.firstRow + i;
    if (y < 0 || y >= static_cast<int>(c.height)) {
      gapRun = 0;
      continue;
    }
    if (w.isGap[i]) {
      ++gapRun;
      continue;
    }
    // `y` is the first inked row after `gapRun` gap rows. A run long enough to
    // be a real gap offers a landing a little above the art that follows it.
    if (gapRun >= kMinGutterRows) {
      int landing = y - kGutterPad;
      if (landing < 0) landing = 0;
      const int d = landing > target ? landing - target : target - landing;
      if (d <= tol && (bestDist < 0 || d < bestDist)) {
        bestDist = d;
        best = landing;
      }
    }
    gapRun = 0;
  }
  return best;
}

}  // namespace

int scrollDown(const Comic& c, int viewportH, int scrollY, const GapWindow& window) {
  const int maxS = maxScroll(c, viewportH);
  if (scrollY < 0) scrollY = 0;
  if (scrollY >= maxS) return maxS;

  const int step = viewportH / 2 > 0 ? viewportH / 2 : 1;
  const int target = scrollY + step;
  // The last step lands exactly on the end rather than somewhere near it, so
  // the bottom of the comic is always flush with the bottom of the screen.
  // Snapping here would stop short and demand one more tap to close a gap the
  // reader can already see.
  if (target >= maxS) return maxS;

  int next = snapToGap(c, viewportH, target, window);
  if (next > maxS) next = maxS;
  if (next < 0) next = 0;
  return next;
}

int scrollUp(const Comic& c, int viewportH, int scrollY, const GapWindow& window) {
  const int maxS = maxScroll(c, viewportH);
  if (scrollY > maxS) scrollY = maxS;
  if (scrollY <= 0) return 0;

  const int step = viewportH / 2 > 0 ? viewportH / 2 : 1;
  const int target = scrollY - step;
  if (target <= 0) return 0;

  int prev = snapToGap(c, viewportH, target, window);
  if (prev < 0) prev = 0;
  if (prev > maxS) prev = maxS;
  return prev;
}

int scrollPermille(const Comic& c, int viewportH, int scrollY) {
  const int maxS = maxScroll(c, viewportH);
  if (maxS <= 0) return 1000;
  if (scrollY <= 0) return 0;
  if (scrollY >= maxS) return 1000;
  return scrollY * 1000 / maxS;
}

int rowInk(const uint8_t* row, int width) {
  if (!row || width <= 0) return 0;
  // A nibble table rather than a loop over bits: this runs over every row the
  // device converts and every row a step looks at, and there is no popcount
  // intrinsic to rely on across both the host tool and the RISC-V target.
  static constexpr uint8_t kBitsIn[16] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};
  int ink = 0;
  const int fullBytes = width / 8;
  for (int b = 0; b < fullBytes; ++b) {
    ink += kBitsIn[row[b] & 0x0F] + kBitsIn[row[b] >> 4];
  }
  // The last byte is partial whenever the width is not a multiple of eight.
  // Its padding bits are not part of the image and must be masked off: the
  // packer leaves them zero, but a pack built by anything else may not, and a
  // stray padding bit would hide gutters across the whole comic.
  const int rest = width % 8;
  if (rest != 0) {
    const uint8_t masked = static_cast<uint8_t>(row[fullBytes] & (0xFF << (8 - rest)));
    ink += kBitsIn[masked & 0x0F] + kBitsIn[masked >> 4];
  }
  return ink;
}

bool rowIsGap(const uint8_t* row, int width) {
  if (!row || width <= 0) return true;
  int budget = width * kGapInkPercent / 100;
  if (budget < kGapInkFloor) budget = kGapInkFloor;
  return rowInk(row, width) <= budget;
}

char foldChar(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool titleMatches(const char* title, const char* needle) {
  if (!title || !needle) return false;
  if (needle[0] == '\0') return true;

  for (int i = 0; title[i] != '\0'; ++i) {
    int j = 0;
    while (needle[j] != '\0' && title[i + j] != '\0' && foldChar(title[i + j]) == foldChar(needle[j])) ++j;
    if (needle[j] == '\0') return true;
  }
  return false;
}

}  // namespace xkcd

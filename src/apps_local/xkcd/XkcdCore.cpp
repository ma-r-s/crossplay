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
  c.flags = rec[20];
  // byte 21 is reserved
  c.closerWidth = rd16(rec + 22);
  c.closerHeight = rd16(rec + 24);
  c.closerStride = rd16(rec + 26);
  c.closerOffset = rd32(rec + 28);
  // bytes 32..39 are padding, reserved so a new field does not move the others
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
  rec[20] = c.flags;
  wr16(rec + 22, c.closerWidth);
  wr16(rec + 24, c.closerHeight);
  wr16(rec + 26, c.closerStride);
  wr32(rec + 28, c.closerOffset);
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

Rendition renditionFor(const Comic& c, Lens lens) {
  Rendition r;
  const bool closer = lens == Lens::Closer && c.hasCloser();
  r.width = closer ? c.closerWidth : c.width;
  r.height = closer ? c.closerHeight : c.height;
  r.stride = closer ? c.closerStride : c.stride;
  r.offset = closer ? c.closerOffset : c.imageOffset;
  return r;
}

int maxScroll(const Rendition& r, int viewportH) {
  if (viewportH <= 0) return 0;
  const int over = static_cast<int>(r.height) - viewportH;
  return over > 0 ? over : 0;
}

int columnsIn(const Rendition& r, int viewportW) {
  if (viewportW <= 0) return 1;
  const int over = static_cast<int>(r.width) - viewportW;
  if (over <= 0) return 1;
  // Derived from the step the columns actually advance by, so this and place()
  // cannot disagree. Computing it from the viewport width instead was the bug
  // a column-guarantee test found immediately: at three columns or more the
  // last one revealed a sliver, because the columns were spaced 480 apart
  // while the width was laid out on a 432 grid.
  return 1 + (over + kColumnStep - 1) / kColumnStep;
}

Placement place(const Rendition& r, int viewportW, int viewportH, const Position& at) {
  Placement p;
  if (!r.valid() || viewportW <= 0 || viewportH <= 0) return p;

  const int cols = columnsIn(r, viewportW);
  const int column = at.column < 0 ? 0 : (at.column >= cols ? cols - 1 : at.column);

  // Columns advance by kColumnStep, not by a whole viewport, so consecutive
  // columns overlap by kColumnOverlap and a word split at the seam stays
  // readable on both sides. The last is clamped flush to the right edge, which
  // for a width on the column grid is exactly where it already lands.
  p.scrollX = column * kColumnStep;
  const int overX = static_cast<int>(r.width) - viewportW;
  if (p.scrollX > overX) p.scrollX = overX < 0 ? 0 : overX;

  p.visibleW = static_cast<int>(r.width) - p.scrollX;
  if (p.visibleW > viewportW) p.visibleW = viewportW;

  // Centred horizontally when the image fits across, flush left when it does
  // not. No branch on the column count is needed: an image with more than one
  // column is by definition wider than the viewport, so this is negative and
  // the clamp takes it to zero. A guard for that case was written first and a
  // mutation test showed it could never change the answer.
  p.originX = (viewportW - static_cast<int>(r.width)) / 2;
  if (p.originX < 0) p.originX = 0;

  const int maxS = maxScroll(r, viewportH);
  p.pans = maxS > 0 || cols > 1;

  if (maxS <= 0) {
    // Fits down the page: centred. A strip sits in the middle of the page like
    // a comic on newsprint, which is what it is.
    //
    // This was briefly anchored to the top, with the alt text filling the band
    // underneath, on the reasoning that dead space is a defect. That was a
    // decision nobody asked for: the alt text is a joke you choose to read,
    // and putting it on the page turns every comic into a comic plus an
    // explanation. The bar is the way to it.
    p.scrollY = 0;
    p.visibleH = r.height;
    p.originY = (viewportH - static_cast<int>(r.height)) / 2;
    if (p.originY < 0) p.originY = 0;
    return p;
  }

  p.scrollY = at.scrollY < 0 ? 0 : (at.scrollY > maxS ? maxS : at.scrollY);
  p.originY = 0;
  p.visibleH = viewportH;
  return p;
}

// --- Walking a rendition in reading order --------------------------------
//
// Left to right across the current band, then down to the next band and back
// to the left. The page view has one column so the horizontal half never
// fires and this is purely vertical; the closer view has two, and this is what
// makes it read 1, 2, 3, 4.
//
// The previous version went *down* a column and then back to the top of the
// next one, which read a multi-panel comic 1, 4, 7, 2, 5, 8. On e-ink there is
// no animation to show that the view moved sideways rather than somewhere
// arbitrary, so that came across as jumping at random. Reading order is not a
// refinement here, it is the difference between navigable and not.

bool canStepBack(const Rendition& r, int viewportW, int viewportH, const Position& at) {
  (void)r;
  (void)viewportW;
  (void)viewportH;
  return at.column > 0 || at.scrollY > 0;
}

bool canStepForward(const Rendition& r, int viewportW, int viewportH, const Position& at) {
  if (at.column + 1 < columnsIn(r, viewportW)) return true;
  return at.scrollY < maxScroll(r, viewportH);
}

Position stepForward(const Rendition& r, int viewportW, int viewportH, const Position& at, const GapWindow& window) {
  Position next = at;

  // Across first. Only when the band is exhausted does the reader drop down,
  // and dropping down returns to the left-hand column.
  if (at.column + 1 < columnsIn(r, viewportW)) {
    next.column = at.column + 1;
    return next;
  }
  if (at.scrollY < maxScroll(r, viewportH)) {
    next.column = 0;
    next.scrollY = scrollDown(r, viewportH, at.scrollY, window);
  }
  return next;
}

Position stepBack(const Rendition& r, int viewportW, int viewportH, const Position& at, const GapWindow& window) {
  Position prev = at;

  if (at.column > 0) {
    prev.column = at.column - 1;
    return prev;
  }
  if (at.scrollY > 0) {
    // Back up a band and land on its *last* column, which is where the reader
    // was when they left it. Going back to column zero instead would make a
    // step back and a step forward disagree.
    prev.scrollY = scrollUp(r, viewportH, at.scrollY, window);
    const int cols = columnsIn(r, viewportW);
    prev.column = cols > 0 ? cols - 1 : 0;
  }
  return prev;
}

void gapWindowFor(const Rendition& r, int viewportH, int scrollY, bool down, int& firstRow, int& rowCount) {
  const int step = viewportH / 2 > 0 ? viewportH / 2 : 1;
  const int tol = viewportH * kSnapToleranceNum / kSnapToleranceDen;
  const int target = down ? scrollY + step : scrollY - step;

  // A landing row L is `kGutterPad` above the first inked row after a gap run,
  // so to judge every L in [target-tol, target+tol] we need the rows from the
  // start of the earliest qualifying run to the latest first-inked row.
  int lo = target - tol + kGutterPad - kMinGutterRows;
  int hi = target + tol + kGutterPad;

  if (lo < 0) lo = 0;
  if (hi > static_cast<int>(r.height) - 1) hi = static_cast<int>(r.height) - 1;
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
int snapToGap(const Rendition& r, int viewportH, int target, const GapWindow& w) {
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
    if (y < 0 || y >= static_cast<int>(r.height)) {
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

int scrollDown(const Rendition& r, int viewportH, int scrollY, const GapWindow& window) {
  const int maxS = maxScroll(r, viewportH);
  if (scrollY < 0) scrollY = 0;
  if (scrollY >= maxS) return maxS;

  const int step = viewportH / 2 > 0 ? viewportH / 2 : 1;
  const int target = scrollY + step;
  // The last step lands exactly on the end rather than somewhere near it, so
  // the bottom of the comic is always flush with the bottom of the screen.
  // Snapping here would stop short and demand one more tap to close a gap the
  // reader can already see.
  if (target >= maxS) return maxS;

  int next = snapToGap(r, viewportH, target, window);
  if (next > maxS) next = maxS;
  if (next < 0) next = 0;
  return next;
}

int scrollUp(const Rendition& r, int viewportH, int scrollY, const GapWindow& window) {
  const int maxS = maxScroll(r, viewportH);
  if (scrollY > maxS) scrollY = maxS;
  if (scrollY <= 0) return 0;

  const int step = viewportH / 2 > 0 ? viewportH / 2 : 1;
  const int target = scrollY - step;
  if (target <= 0) return 0;

  int prev = snapToGap(r, viewportH, target, window);
  if (prev < 0) prev = 0;
  if (prev > maxS) prev = maxS;
  return prev;
}

Position mapAcross(const Comic& c, int viewportW, int viewportH, const Position& at, Lens to) {
  Position out;
  out.lens = to;
  if (to == Lens::Closer && !c.hasCloser()) {
    out.lens = Lens::Page;
    return at;
  }

  const Rendition from = renditionFor(c, at.lens);
  const Rendition dest = renditionFor(c, out.lens);
  if (!from.valid() || !dest.valid() || from.height == 0) return out;

  // The two renditions are the same artwork at different scales, so the row
  // under the top of the screen maps by the ratio of their heights. Entering
  // the closer view starts at the left of that band, which is where reading
  // order begins; leaving it keeps the row and has nowhere else to be.
  const int fromMax = maxScroll(from, viewportH);
  const int y = at.scrollY < 0 ? 0 : (at.scrollY > fromMax ? fromMax : at.scrollY);
  const int scaled =
      static_cast<int>(static_cast<int64_t>(y) * static_cast<int64_t>(dest.height) / static_cast<int64_t>(from.height));

  const int destMax = maxScroll(dest, viewportH);
  out.scrollY = scaled < 0 ? 0 : (scaled > destMax ? destMax : scaled);
  out.column = 0;
  (void)viewportW;
  return out;
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

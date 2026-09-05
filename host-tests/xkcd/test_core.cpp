// Freestanding tests for XkcdCore: the pack reader, gap detection, and the pan
// math.
//
// The pan math is fuzzed rather than spot-checked, because the failure it is
// written to prevent is not a wrong number, it is a *stuck reader* -- a step
// that lands back where it started makes a tap do nothing, and no individual
// example would ever look wrong. The properties below are what "the reader can
// always get to the bottom, and every tap moves" means.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../src/apps_local/xkcd/XkcdCore.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond, ...)                               \
  do {                                                 \
    ++checks;                                          \
    if (!(cond)) {                                     \
      ++failures;                                      \
      std::printf("FAIL %s:%d  ", __FILE__, __LINE__); \
      std::printf(__VA_ARGS__);                        \
      std::printf("\n");                               \
    }                                                  \
  } while (0)

// A ByteSource over a std::vector, which is what the device's HalStorage-backed
// one has to behave like.
class MemSource final : public xkcd::ByteSource {
 public:
  explicit MemSource(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {}
  bool read(uint32_t offset, void* dst, uint32_t length) override {
    if (offset > bytes_.size() || bytes_.size() - offset < length) return false;
    std::memcpy(dst, bytes_.data() + offset, length);
    return true;
  }
  uint32_t size() const override { return static_cast<uint32_t>(bytes_.size()); }

 private:
  std::vector<uint8_t> bytes_;
};

static void put32(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(x & 0xFF);
  v.push_back((x >> 8) & 0xFF);
  v.push_back((x >> 16) & 0xFF);
  v.push_back((x >> 24) & 0xFF);
}
static void put16(std::vector<uint8_t>& v, uint16_t x) {
  v.push_back(x & 0xFF);
  v.push_back((x >> 8) & 0xFF);
}

struct Pack {
  std::vector<uint8_t> index;
  std::vector<uint8_t> text;
};

static Pack buildPack(const std::vector<std::pair<uint16_t, std::string>>& comics) {
  Pack p;
  put32(p.index, xkcd::kMagic);
  put16(p.index, xkcd::kFormatVersion);
  put16(p.index, 0);
  put32(p.index, static_cast<uint32_t>(comics.size()));
  put32(p.index, comics.empty() ? 0 : comics.back().first);

  for (const auto& [num, title] : comics) {
    xkcd::Comic c;
    c.num = num;
    // A page rendition is fitted to the panel width, and a wider one is not a
    // legal record -- Archive::at rejects it. Building the fixture at 740
    // wide, as this did before the rework, now correctly fails to load.
    c.width = xkcd::kPanelWidth;
    c.height = 400;
    c.stride = (xkcd::kPanelWidth + 7) / 8;
    // Every other comic carries a closer view, so the archive path is
    // exercised with and without one.
    if (num % 2 == 1) {
      c.overviewWidth = xkcd::kPanelWidth;
      c.overviewHeight = 800;
      c.overviewStride = (xkcd::kPanelWidth + 7) / 8;
      c.overviewOffset = 12345;
    }
    c.year = 2020;
    c.month = 1;
    c.day = 1;
    c.textOffset = static_cast<uint32_t>(p.text.size());
    uint8_t rec[xkcd::kIndexRecordBytes];
    xkcd::encodeRecord(c, rec);
    p.index.insert(p.index.end(), rec, rec + sizeof(rec));

    for (char ch : title) p.text.push_back(static_cast<uint8_t>(ch));
    p.text.push_back(0);
    const std::string alt = "alt for " + title;
    for (char ch : alt) p.text.push_back(static_cast<uint8_t>(ch));
    p.text.push_back(0);
  }
  return p;
}

// ---------------------------------------------------------------- fixtures

// '#' is a row of lettering, '.' is a gap. `framed` adds a vertical border
// stroke down both edges of every row, which is what 20 of 30 sampled comics
// actually look like and what broke the first version of this detector.
struct FakeImage {
  std::vector<uint8_t> bits;
  int width = 0, height = 0, stride = 0;
};

// `verticals` is how many 2px vertical strokes run the full height: a frame is
// 2 (left and right), a framed table with two column dividers is 4. Every one
// of them puts ink in an otherwise empty row, which is what broke the first
// version of the detector.
static FakeImage makeImage(const std::string& rowSpec, int width = 740, int verticals = 0) {
  FakeImage img;
  img.width = width;
  img.height = static_cast<int>(rowSpec.size());
  img.stride = (width + 7) / 8;
  img.bits.assign(static_cast<size_t>(img.stride) * img.height, 0);

  auto ink = [&](int y, int x) {
    if (x >= 0 && x < width) img.bits[static_cast<size_t>(y) * img.stride + x / 8] |= 0x80 >> (x % 8);
  };

  for (int y = 0; y < img.height; ++y) {
    for (int v = 0; v < verticals; ++v) {
      // Spread them across the width: edges first, then interior dividers.
      const int x = (v == 0) ? 0 : (v == 1 ? width - 2 : (width * (v - 1)) / (verticals - 1));
      ink(y, x);
      ink(y, x + 1);
    }
    if (rowSpec[y] != '#') continue;
    // Enough ink to be unmistakably a line of lettering: a quarter of the row.
    for (int x = width / 4; x < width / 2; ++x) ink(y, x);
  }
  return img;
}

// A plain frame, which is what most comics have.
static FakeImage makeFramed(const std::string& rowSpec, int width = 740) { return makeImage(rowSpec, width, 2); }

// Placement and stepping work on a Rendition rather than a Comic, so neither
// has to branch on which of the two images is on screen.
static xkcd::Rendition rendFor(const FakeImage& img) {
  xkcd::Rendition r;
  r.width = static_cast<uint16_t>(img.width);
  r.height = static_cast<uint16_t>(img.height);
  r.stride = static_cast<uint16_t>(img.stride);
  return r;
}

static xkcd::Position at(int column, int scrollY, xkcd::Lens lens = xkcd::Lens::Art) {
  xkcd::Position p;
  p.lens = lens;
  p.column = column;
  p.scrollY = scrollY;
  return p;
}

// Exactly what the activity does: ask which rows the step needs, read that
// range, hand it straight back. Deriving the window anywhere else would be the
// same defect as hit-testing that recomputes its own geometry.
// Flags live in the caller, exactly as they will on the device.
static std::vector<uint8_t> gapFlags;

static xkcd::GapWindow windowFor(const xkcd::Rendition& c, const FakeImage& img, int viewportH, int row) {
  int firstRow = 0, rowCount = 0;
  xkcd::gapWindowFor(c, viewportH, row, firstRow, rowCount);
  xkcd::GapWindow w;
  if (rowCount <= 0) return w;
  CHECK(firstRow >= 0 && firstRow + rowCount <= img.height, "gapWindowFor asked for rows [%d,%d) outside 0..%d",
        firstRow, firstRow + rowCount, img.height);
  if (firstRow < 0 || firstRow + rowCount > img.height) return w;

  gapFlags.assign(static_cast<size_t>(rowCount), 0);
  for (int i = 0; i < rowCount; ++i) {
    const uint8_t* row = img.bits.data() + static_cast<size_t>(firstRow + i) * img.stride;
    gapFlags[i] = xkcd::rowIsGap(row, img.width) ? 1 : 0;
  }
  w.isGap = gapFlags.data();
  w.firstRow = firstRow;
  w.rowCount = rowCount;
  return w;
}

// ---------------------------------------------------------------- records

static void testRecordRoundTrip() {
  xkcd::Comic in;
  in.num = 1732;
  in.width = 740;
  in.height = 6370;
  in.stride = 93;
  in.year = 2016;
  in.month = 9;
  in.day = 12;
  in.imageOffset = 0xDEADBEEF;
  in.textOffset = 0x01020304;

  uint8_t rec[xkcd::kIndexRecordBytes];
  xkcd::encodeRecord(in, rec);
  const xkcd::Comic out = xkcd::decodeRecord(rec);

  CHECK(out.num == in.num, "num %u != %u", out.num, in.num);
  CHECK(out.width == in.width, "width");
  CHECK(out.height == in.height, "height");
  CHECK(out.stride == in.stride, "stride");
  CHECK(out.year == in.year, "year");
  CHECK(out.month == in.month, "month");
  CHECK(out.day == in.day, "day");
  CHECK(out.imageOffset == in.imageOffset, "imageOffset %u", out.imageOffset);
  CHECK(out.textOffset == in.textOffset, "textOffset");
  const xkcd::Rendition back = xkcd::renditionFor(out, xkcd::Lens::Art);
  CHECK(back.bytes() == 93u * 6370u, "page bytes %u", back.bytes());
  CHECK(out.overviewWidth == in.overviewWidth && out.overviewHeight == in.overviewHeight, "closer dimensions");
  CHECK(out.overviewStride == in.overviewStride && out.overviewOffset == in.overviewOffset, "closer stride and offset");

  // The record must stay 40 bytes: the whole point of a fixed width is that a
  // lookup is a seek. If this ever changes, kFormatVersion has to change too.
  CHECK(xkcd::kIndexRecordBytes == 40, "record size drifted");
}

// ---------------------------------------------------------------- archive

static void testArchive() {
  const Pack p = buildPack({{1, "Barrel - Part 1"}, {2, "Petit Trees"}, {403, "Convincing"}, {405, "Journal 3"}});
  MemSource index(p.index);
  MemSource text(p.text);

  xkcd::Archive a;
  CHECK(a.open(index), "archive should open");
  CHECK(a.count() == 4, "count %d", a.count());
  CHECK(a.maxNum() == 405, "maxNum %u", a.maxNum());

  CHECK(a.find(index, 1) == 0, "find 1");
  CHECK(a.find(index, 403) == 2, "find 403");
  CHECK(a.find(index, 405) == 3, "find 405");

  // 404 is not a comic, and never will be. Looking it up must miss cleanly
  // rather than land on a neighbour.
  CHECK(a.find(index, 404) == -1, "404 must not be found");
  CHECK(a.lowerBound(index, 404) == 3, "lowerBound(404) should be the next one");
  CHECK(a.find(index, 9999) == -1, "find past the end");
  CHECK(a.lowerBound(index, 9999) == 4, "lowerBound past the end is count");
  CHECK(a.lowerBound(index, 0) == 0, "lowerBound before the start");

  xkcd::Comic c;
  CHECK(a.at(index, 2, c) && c.num == 403, "at(2)");
  CHECK(!a.at(index, 4, c), "at past the end must fail");
  CHECK(!a.at(index, -1, c), "at negative must fail");

  char buf[64];
  CHECK(a.at(index, 0, c), "at(0)");
  xkcd::readTitle(text, c, buf, sizeof(buf));
  CHECK(std::strcmp(buf, "Barrel - Part 1") == 0, "title '%s'", buf);
  xkcd::readAlt(text, c, buf, sizeof(buf));
  CHECK(std::strcmp(buf, "alt for Barrel - Part 1") == 0, "alt '%s'", buf);

  CHECK(a.at(index, 3, c), "at(3)");
  xkcd::readTitle(text, c, buf, sizeof(buf));
  CHECK(std::strcmp(buf, "Journal 3") == 0, "last title '%s'", buf);

  // Truncation must not overrun, and must still terminate.
  char tiny[6];
  CHECK(a.at(index, 0, c), "at(0) again");
  const int n = xkcd::readTitle(text, c, tiny, sizeof(tiny));
  CHECK(n == 5, "truncated length %d", n);
  CHECK(tiny[5] == '\0', "truncated title must be terminated");
  CHECK(std::strcmp(tiny, "Barre") == 0, "truncated title '%s'", tiny);
}

static void testRejectsBadPacks() {
  xkcd::Archive a;

  std::vector<uint8_t> empty;
  MemSource s0(empty);
  CHECK(!a.open(s0), "empty file must be rejected");

  Pack p = buildPack({{1, "One"}, {2, "Two"}});
  {
    auto bad = p.index;
    bad[0] ^= 0xFF;
    MemSource s(bad);
    CHECK(!a.open(s), "wrong magic must be rejected");
  }
  {
    auto bad = p.index;
    bad[4] = 99;
    MemSource s(bad);
    CHECK(!a.open(s), "wrong version must be rejected");
  }
  // A count larger than the file holds -- a card pulled mid-write. This is the
  // one that matters: without the check it reads as a full archive whose tail
  // is whatever bytes were there before.
  {
    auto bad = p.index;
    bad[8] = 200;
    MemSource s(bad);
    CHECK(!a.open(s), "over-long count must be rejected");
  }
  {
    auto bad = p.index;
    bad.resize(bad.size() - 8);
    MemSource s(bad);
    CHECK(!a.open(s), "truncated index must be rejected");
  }
}

// ------------------------------------------------------------- placement

// A page rendition, as the builder guarantees they arrive: never wider than
// the panel, so the page view has no horizontal axis at all.
static xkcd::Rendition pageRend(int w, int h) {
  xkcd::Rendition r;
  r.width = static_cast<uint16_t>(w);
  r.height = static_cast<uint16_t>(h);
  r.stride = static_cast<uint16_t>((w + 7) / 8);
  return r;
}

static void testPlacement() {
  const int vw = 480, vh = 756;

  const xkcd::Rendition strip = pageRend(480, 180);  // a short wide strip
  xkcd::Placement p = xkcd::place(strip, vw, vh, at(0, 0));
  CHECK(!p.pans, "a 180px strip must not pan on a 756px viewport");
  CHECK(p.originX == 0, "originX %d (a full-width page has no side margin)", p.originX);
  CHECK(p.originY == (756 - 180) / 2, "originY %d (centred vertically)", p.originY);
  CHECK(p.visibleH == 180, "visibleH %d", p.visibleH);
  CHECK(xkcd::maxScroll(strip, vh) == 0, "maxScroll of a fitting comic is 0");

  // A narrow comic the upscale cap could not stretch to the full width is
  // centred, which is the only case that leaves a side margin.
  const xkcd::Rendition narrow = pageRend(318, 200);
  CHECK(xkcd::place(narrow, vw, vh, at(0, 0)).originX == (480 - 318) / 2, "a narrow page is centred");

  const xkcd::Rendition tall = pageRend(480, 1200);
  CHECK(xkcd::maxScroll(tall, vh) == 444, "maxScroll %d", xkcd::maxScroll(tall, vh));
  p = xkcd::place(tall, vw, vh, at(0, 300));
  CHECK(p.pans, "a 1200px comic must pan");
  CHECK(p.originY == 0, "a panning comic is pinned to the top");
  CHECK(p.scrollY == 300, "scrollY %d", p.scrollY);
  CHECK(p.visibleH == 756, "visibleH %d", p.visibleH);

  // Out-of-range scroll must be clamped, not wrapped or trusted.
  CHECK(xkcd::place(tall, vw, vh, at(0, 99999)).scrollY == 444, "over-scroll clamps to maxScroll");
  CHECK(xkcd::place(tall, vw, vh, at(0, -50)).scrollY == 0, "negative scroll clamps to 0");

  // **The page view has one column, and that is a property of the format.**
  // A record whose page rendition is wider than the panel is rejected by
  // valid(), so the reader can never be handed one -- which is what makes
  // "the page view never pans sideways" a guarantee rather than a hope.
  xkcd::Comic bad;
  bad.num = 1;
  bad.width = static_cast<uint16_t>(xkcd::kPanelWidth + 1);
  bad.height = 400;
  bad.stride = 61;
  CHECK(!bad.valid(), "a page rendition wider than the panel must be rejected outright");

  xkcd::Comic ok = bad;
  ok.width = xkcd::kPanelWidth;
  ok.stride = 60;
  CHECK(ok.valid(), "a page rendition exactly the panel's width is fine");
  CHECK(xkcd::columnsIn(xkcd::renditionFor(ok, xkcd::Lens::Art), vw) == 1,
        "a page rendition is always exactly one column");

  // A corrupt record must still not place the image at a negative origin and
  // blit off the framebuffer.
  CHECK(xkcd::place(pageRend(900, 100), vw, vh, at(0, 0)).originX == 0,
        "an over-wide image must not get a negative originX");
}

// ----------------------------------------------------------- the two views

// A comic as the builder now writes one: the artwork at the size its lettering
// needs (which may pan), plus a whole-comic overview that always fits a screen.
static xkcd::Comic withOverview(int pw, int ph, int cw, int ch) {
  xkcd::Comic c;
  c.num = 3266;
  c.width = static_cast<uint16_t>(pw);
  c.height = static_cast<uint16_t>(ph);
  c.stride = static_cast<uint16_t>((pw + 7) / 8);
  c.imageOffset = 1000;
  c.overviewWidth = static_cast<uint16_t>(cw);
  c.overviewHeight = static_cast<uint16_t>(ch);
  c.overviewStride = static_cast<uint16_t>((cw + 7) / 8);
  c.overviewOffset = 5000;
  return c;
}

static void testOverview() {
  const int vw = 480, vh = 756;
  // #3266-shaped: 740x731 at source, stored as 912x901 artwork that pans, with
  // a 480x474 overview of the whole thing behind OK.
  const xkcd::Comic c = withOverview(912, 901, 480, 474);
  CHECK(c.valid(), "artwork that pans, plus an overview, is a valid record");
  CHECK(c.hasOverview(), "and it says so");

  const xkcd::Rendition page = xkcd::renditionFor(c, xkcd::Lens::Art);
  const xkcd::Rendition close = xkcd::renditionFor(c, xkcd::Lens::Whole);
  CHECK(page.offset == 1000 && page.width == 912, "the artwork is selected by lens");
  CHECK(close.offset == 5000 && close.width == 480, "and so is the overview");

  // An overview wider than the panel is not an overview.
  xkcd::Comic bad = c;
  bad.overviewWidth = 912;
  bad.overviewStride = (912 + 7) / 8;
  CHECK(!bad.valid(), "an overview wider than the panel must be rejected");

  // A comic with no closer view falls back to the page rather than reading a
  // zero-length image at offset zero.
  xkcd::Comic plain = c;
  plain.overviewWidth = 0;
  CHECK(!plain.hasOverview(), "overviewWidth 0 is the sentinel for 'no closer view'");
  CHECK(xkcd::renditionFor(plain, xkcd::Lens::Whole).offset == 1000,
        "asking for an overview that does not exist gives the artwork");

  CHECK(xkcd::columnsIn(close, vw) == 1, "an overview is always one column");
  CHECK(xkcd::columnsIn(page, vw) == 2, "this artwork is two");

  // **The anti-sliver guarantee, stated as a test.** Column one is pulled back
  // flush with the right edge, so the new artwork it reveals is width - 480.
  // The builder's kMshowingWholeWidth is what keeps that at half a screen or more,
  // and this is the property that must never regress: the defect this whole
  // rework exists to fix was a second column revealing one pixel.
  const xkcd::Placement c0 = xkcd::place(page, vw, vh, at(0, 0));
  const xkcd::Placement c1 = xkcd::place(page, vw, vh, at(1, 0));
  CHECK(c0.scrollX == 0, "column 0 starts at the left edge");
  CHECK(c1.scrollX + c1.visibleW == 912, "column 1 ends flush with the artwork");
  CHECK(c1.scrollX == xkcd::kColumnStep, "column 1 reveals exactly one step of new art, got %d", c1.scrollX);

  // **The guarantee, at every column count, not just at two.** A closer view
  // is kColumnStep * N + kColumnOverlap wide, so each column advances by a
  // full kColumnStep and the last ends flush. Checked across the whole legal
  // range because the zoom is chosen from the comic's own lettering now, so N
  // is whatever readability asked for.
  for (int cols = 2; cols <= xkcd::kMaxColumns; ++cols) {
    const int width = xkcd::kColumnStep * cols + xkcd::kColumnOverlap;
    const xkcd::Rendition r = pageRend(width, 900);
    CHECK(xkcd::columnsIn(r, vw) == cols, "%dpx should be %d columns, got %d", width, cols, xkcd::columnsIn(r, vw));
    int last = 0;
    for (int i = 0; i < cols; ++i) {
      const xkcd::Placement p = xkcd::place(r, vw, vh, at(i, 0));
      if (i > 0) {
        CHECK(p.scrollX - last == xkcd::kColumnStep, "column %d of %d revealed %d px, not %d", i, cols,
              p.scrollX - last, xkcd::kColumnStep);
      }
      last = p.scrollX;
      if (i == cols - 1) {
        CHECK(p.scrollX + p.visibleW == width, "the last of %d columns must end flush, got %d", cols,
              p.scrollX + p.visibleW);
      }
    }
  }

  // And a width that is not a whole number of steps is not a legal record, so
  // the reader can never be handed one whose last column reveals a sliver.
  xkcd::Comic ragged = c;
  ragged.overviewWidth = 913;
  ragged.overviewStride = (913 + 7) / 8;
  CHECK(!ragged.valid(), "a closer width off the column grid must be rejected");

  // Out-of-range columns clamp rather than reading off the end of the image.
  CHECK(xkcd::place(page, vw, vh, at(9, 0)).scrollX == 912 - 480, "over-column clamps");
  CHECK(xkcd::place(page, vw, vh, at(-3, 0)).scrollX == 0, "negative column clamps");
}

// **Reading order.** Across the band, then down and back to the left. The
// previous version went down a column and back to the top of the next, which
// read a multi-panel comic 1, 4, 7, 2, 5, 8 -- and on e-ink, with no animation
// to show the view moving sideways, that is what "it jumps to random parts"
// actually was.
static void testReadingOrder() {
  const int vw = 480, vh = 756;
  const xkcd::GapWindow none;

  // Two columns, two bands: 912x1100 gives maxScroll 344.
  const xkcd::Rendition r = pageRend(912, 1100);
  CHECK(xkcd::columnsIn(r, vw) == 2, "two columns");
  CHECK(xkcd::maxScroll(r, vh) == 344, "maxScroll %d", xkcd::maxScroll(r, vh));

  xkcd::Position p = at(0, 0);
  CHECK(xkcd::canStepForward(r, vw, vh, p), "there is somewhere to go");
  p = xkcd::stepForward(r, vw, vh, p, none);
  CHECK(p.column == 1 && p.scrollY == 0, "1: across first, got col %d y %d", p.column, p.scrollY);
  p = xkcd::stepForward(r, vw, vh, p, none);
  CHECK(p.column == 0 && p.scrollY == 344, "2: then down AND back to the left, got col %d y %d", p.column, p.scrollY);
  p = xkcd::stepForward(r, vw, vh, p, none);
  CHECK(p.column == 1 && p.scrollY == 344, "3: across again, got col %d y %d", p.column, p.scrollY);
  CHECK(!xkcd::canStepForward(r, vw, vh, p), "bottom right is the end of the comic");
  const xkcd::Position end = xkcd::stepForward(r, vw, vh, p, none);
  CHECK(end.column == 1 && end.scrollY == 344, "the end must not wrap");

  // And back again, in the same order reversed, with no position skipped.
  p = xkcd::stepBack(r, vw, vh, p, none);
  CHECK(p.column == 0 && p.scrollY == 344, "back 1, got col %d y %d", p.column, p.scrollY);
  p = xkcd::stepBack(r, vw, vh, p, none);
  CHECK(p.column == 1 && p.scrollY == 0, "back 2 lands on the RIGHT of the band above, got col %d y %d", p.column,
        p.scrollY);
  p = xkcd::stepBack(r, vw, vh, p, none);
  CHECK(p.column == 0 && p.scrollY == 0, "back 3, got col %d y %d", p.column, p.scrollY);
  CHECK(!xkcd::canStepBack(r, vw, vh, p), "the top left is the start");
  CHECK(xkcd::stepBack(r, vw, vh, p, none).column == 0, "the start must not wrap");

  // Forward then back is the identity when nothing snaps. With a gap window it
  // can drift by up to the snap tolerance, which is tested in testPanProperties;
  // what must never happen is landing somewhere unreachable.
  xkcd::Position q = at(0, 0);
  for (int i = 0; i < 3; ++i) {
    const xkcd::Position fwd = xkcd::stepForward(r, vw, vh, q, none);
    const xkcd::Position rt = xkcd::stepBack(r, vw, vh, fwd, none);
    CHECK(rt.column == q.column && rt.scrollY == q.scrollY, "round trip from col %d y %d landed col %d y %d", q.column,
          q.scrollY, rt.column, rt.scrollY);
    q = fwd;
  }

  // A one-column rendition -- every page view -- must never produce a
  // horizontal step. This is the page view's whole promise.
  const xkcd::Rendition one = pageRend(480, 2000);
  xkcd::Position s = at(0, 0);
  for (int i = 0; i < 12 && xkcd::canStepForward(one, vw, vh, s); ++i) {
    s = xkcd::stepForward(one, vw, vh, s, none);
    CHECK(s.column == 0, "a page view must never leave column 0, got %d", s.column);
  }
}

// Switching views must not lose your place.
static void testMapAcross() {
  const int vw = 480, vh = 756;
  const xkcd::Comic c = withOverview(480, 1000, 912, 1900);

  const xkcd::Position mid = at(0, 244, xkcd::Lens::Art);  // the bottom of the page view
  const xkcd::Position into = xkcd::mapAcross(c, vw, vh, mid, xkcd::Lens::Whole);
  CHECK(into.lens == xkcd::Lens::Whole, "we are in the closer view");
  CHECK(into.column == 0, "and at the left of the band, where reading order starts");
  CHECK(into.scrollY > 244, "the same row is further down a taller rendition, got %d", into.scrollY);
  CHECK(into.scrollY <= xkcd::maxScroll(xkcd::renditionFor(c, xkcd::Lens::Whole), vh), "and still in range");

  const xkcd::Position back = xkcd::mapAcross(c, vw, vh, into, xkcd::Lens::Art);
  CHECK(back.lens == xkcd::Lens::Art, "and back out again");
  CHECK(back.scrollY >= 240 && back.scrollY <= 244, "landing within a few rows of where we left, got %d", back.scrollY);

  // Asking for a closer view that does not exist must leave the reader where
  // they are rather than in a lens with no image behind it.
  xkcd::Comic plain = c;
  plain.overviewWidth = 0;
  const xkcd::Position stay = xkcd::mapAcross(plain, vw, vh, mid, xkcd::Lens::Whole);
  CHECK(stay.lens == xkcd::Lens::Art && stay.scrollY == 244, "no closer view means nothing moves");
}

static void testGapDetection() {
  // A framed row with no lettering is a gap; a row of lettering is not. This
  // is the distinction the whole step rule rests on.
  {
    const FakeImage img = makeFramed("#.", 740);
    CHECK(!xkcd::rowIsGap(img.bits.data(), 740), "a lettered row is not a gap");
    CHECK(xkcd::rowIsGap(img.bits.data() + img.stride, 740), "a framed empty row is a gap");
    CHECK(xkcd::rowInk(img.bits.data() + img.stride, 740) == 4, "a frame costs 4 ink pixels, got %d",
          xkcd::rowInk(img.bits.data() + img.stride, 740));
  }
  // Narrow comics: 2% of 210 is four pixels, which a border alone costs. The
  // floor is what keeps these detectable at all.
  {
    const FakeImage img = makeFramed(".", 210);
    CHECK(xkcd::rowIsGap(img.bits.data(), 210), "a narrow framed empty row must still be a gap");
  }
  // The case the floor exists for, and the one a percentage alone gets wrong.
  // Structural ink is a count of *vertical strokes* and does not scale with
  // width: a 210px framed table with two column dividers costs 8 ink pixels in
  // an empty row, but 3% of 210 is only 6. Without the floor this row reads as
  // lettering and the comic loses every gap it has -- which is exactly how
  // #1093 came out with its steps slicing through the table.
  {
    const FakeImage img = makeImage(".", 210, /*verticals=*/4);
    const int ink = xkcd::rowInk(img.bits.data(), 210);
    CHECK(ink == 8, "fixture assumption: 4 verticals cost 8 ink pixels, got %d", ink);
    CHECK(ink > 210 * xkcd::kGapInkPercent / 100, "fixture must exceed the percentage budget to test the floor");
    CHECK(xkcd::rowIsGap(img.bits.data(), 210), "the floor must keep a narrow divided table's gaps detectable");
  }
  // Padding bits beyond the width must never count as ink. A width of 740
  // leaves four spare bits in the last byte; counting them would make every
  // row look inked and the comic would have no gaps at all.
  {
    FakeImage img = makeImage(".", 740);
    img.bits[img.stride - 1] |= 0x0F;
    CHECK(xkcd::rowInk(img.bits.data(), 740) == 0, "padding bits must be masked off, got %d",
          xkcd::rowInk(img.bits.data(), 740));
    CHECK(xkcd::rowIsGap(img.bits.data(), 740), "padding bits must not hide a gap");
  }
  CHECK(xkcd::rowIsGap(nullptr, 8), "a null row counts as a gap");
  CHECK(xkcd::rowInk(nullptr, 8) == 0, "a null row has no ink");

  // Full-byte and partial-byte ink both counted.
  {
    FakeImage img = makeImage(".", 16);
    img.bits[0] = 0xFF;
    img.bits[1] = 0xF0;
    CHECK(xkcd::rowInk(img.bits.data(), 16) == 12, "rowInk %d", xkcd::rowInk(img.bits.data(), 16));
  }
}

// -------------------------------------------------------------- pan math

static void testSnapping() {
  const int vp = 756;

  // A comic exactly the viewport is one panel and never moves.
  {
    const FakeImage img = makeImage(std::string(vp, '#'), 480, 0);
    const xkcd::Rendition c = rendFor(img);
    CHECK(xkcd::rowsIn(c, vp) == 1, "a comic that fits is one panel, got %d", xkcd::rowsIn(c, vp));
    CHECK(xkcd::evenTargetY(c, vp, 0) == 0, "and it starts at the top");
  }

  // **The travel is divided evenly and the last panel is exactly flush.** This
  // is the property Mario named: a fixed stride leaves the final step as
  // whatever is left over, which reads as going all the way back to the left
  // and dropping two pixels.
  for (int extra : {1, 5, 100, 379, 380, 760, 1000, 4000}) {
    const int h = vp + extra;
    const FakeImage img = makeImage(std::string(h, '#'), 480, 0);
    const xkcd::Rendition c = rendFor(img);
    const int rows = xkcd::rowsIn(c, vp);
    const int travel = xkcd::maxScroll(c, vp);
    CHECK(rows >= 2, "a comic %dpx over the viewport needs more than one panel", extra);
    CHECK(xkcd::evenTargetY(c, vp, 0) == 0, "panel 0 is the top");
    CHECK(xkcd::evenTargetY(c, vp, rows - 1) == travel, "the last panel is flush with the bottom, got %d of %d",
          xkcd::evenTargetY(c, vp, rows - 1), travel);

    // Every gap between consecutive panels is the same, to within the rounding
    // of an integer division. No step is a remainder.
    int smallest = 1 << 30, largest = 0;
    for (int i = 1; i < rows; ++i) {
      const int d = xkcd::evenTargetY(c, vp, i) - xkcd::evenTargetY(c, vp, i - 1);
      CHECK(d > 0, "panel %d of %d did not move (h=%d)", i, rows, h);
      if (d < smallest) smallest = d;
      if (d > largest) largest = d;
    }
    CHECK(largest - smallest <= 1, "steps for h=%d ranged %d..%d; they must be equal", h, smallest, largest);
    CHECK(largest <= vp / 2 + 1, "a step of %d is more than half a screen (h=%d)", largest, h);
  }

  // Snapping still pulls an interior panel onto a gutter, and never past the
  // tolerance.
  {
    std::string spec;
    for (int i = 0; i < 8; ++i) spec += std::string(300, '#') + std::string(20, '.');
    const FakeImage img = makeImage(spec, 480, 2);
    const xkcd::Rendition c = rendFor(img);
    const int rows = xkcd::rowsIn(c, vp);
    const int tol = vp * xkcd::kSnapToleranceNum / xkcd::kSnapToleranceDen;
    for (int i = 1; i + 1 < rows; ++i) {
      const int want = xkcd::evenTargetY(c, vp, i);
      const int got = xkcd::scrollYFor(c, vp, i, windowFor(c, img, vp, i));
      const int moved = got > want ? got - want : want - got;
      CHECK(moved <= tol, "panel %d snapped %dpx, tolerance is %d", i, moved, tol);
    }
    CHECK(xkcd::scrollYFor(c, vp, 0, xkcd::GapWindow{}) == 0, "the first panel is never pulled off the top");
    CHECK(xkcd::scrollYFor(c, vp, rows - 1, xkcd::GapWindow{}) == xkcd::maxScroll(c, vp),
          "nor the last off the bottom");
  }
}

// **Reading order for a comic stored on its side.** Its left-to-right became
// the stored image's top-to-bottom, so walking the stored image as if it were
// upright begins at what the reader sees as the bottom of the strip.
static void testSidewaysOrder() {
  const int vw = 480, vh = 756;
  const xkcd::GapWindow none;

  xkcd::Rendition up = pageRend(xkcd::kColumnStep * 3 + xkcd::kColumnOverlap, 400);
  xkcd::Rendition side = up;
  side.sideways = true;

  CHECK(xkcd::startOf(up, vw).column == 0, "an upright comic starts at the left");
  CHECK(xkcd::startOf(side, vw).column == 2, "a sideways comic starts at the stored right, got %d",
        xkcd::startOf(side, vw).column);

  // Upright: left to right.
  xkcd::Position p = xkcd::startOf(up, vw);
  p = xkcd::stepForward(up, vw, vh, p, none);
  CHECK(p.column == 1, "upright steps rightwards, got %d", p.column);

  // Sideways: the comic's next panel is the stored image's next column to the
  // LEFT, because the whole frame is turned a quarter turn.
  xkcd::Position q = xkcd::startOf(side, vw);
  q = xkcd::stepForward(side, vw, vh, q, none);
  CHECK(q.column == 1, "sideways steps leftwards, got %d", q.column);
  q = xkcd::stepForward(side, vw, vh, q, none);
  CHECK(q.column == 0, "and on to the stored left edge, got %d", q.column);
  CHECK(!xkcd::canStepForward(side, vw, vh, q), "which is the end of a single-band sideways comic");

  // And back, exactly.
  q = xkcd::stepBack(side, vw, vh, q, none);
  CHECK(q.column == 1, "back one, got %d", q.column);
  q = xkcd::stepBack(side, vw, vh, q, none);
  CHECK(q.column == 2, "back to the start, got %d", q.column);
  CHECK(!xkcd::canStepBack(side, vw, vh, q), "the start of a sideways comic is its stored right");

  // Dropping a band returns to the band's own starting side, not to column 0.
  xkcd::Rendition tall = side;
  tall.height = 1600;
  xkcd::Position t = xkcd::startOf(tall, vw);
  for (int i = 0; i < 3; ++i) t = xkcd::stepForward(tall, vw, vh, t, none);
  CHECK(t.row == 1 && t.column == 2, "a sideways band drop returns to the stored right, got col %d row %d", t.column,
        t.row);
}

struct Rng {
  uint32_t s;
  uint32_t next() {
    s = s * 1664525u + 1013904223u;
    return s >> 8;
  }
  int range(int lo, int hi) { return lo + static_cast<int>(next() % static_cast<uint32_t>(hi - lo + 1)); }
};

static void testPanProperties() {
  Rng rng{12345};
  // **The reader's real viewport, not a convenient one.** This said 480, and
  // that single number hid a shipping defect for the whole life of the app:
  // the Activity capped its gap-flag buffer at 256 rows, which is enough at
  // 480 and not enough at 756, so on the device the window was always refused
  // and *no step ever snapped to a gap*. A test that runs at a size the
  // product never uses is not a test.
  const int viewportH = 756;

  for (int trial = 0; trial < 400; ++trial) {
    const int height = rng.range(60, 3000);
    const int width = rng.range(120, 480);
    const bool framed = (trial % 3) == 0;

    std::string spec;
    while (static_cast<int>(spec.size()) < height) {
      spec += std::string(rng.range(5, 120), '#');
      spec += std::string(rng.range(1, 20), '.');
    }
    spec.resize(height);

    const FakeImage img = makeImage(spec, width, framed ? 2 : 0);
    const xkcd::Rendition c = rendFor(img);
    const int rows = xkcd::rowsIn(c, viewportH);
    const int travel = xkcd::maxScroll(c, viewportH);

    // 1. Panels are in range and strictly increasing.
    int last = -1;
    for (int i = 0; i < rows; ++i) {
      const int y = xkcd::scrollYFor(c, viewportH, i, windowFor(c, img, viewportH, i));
      CHECK(y >= 0 && y <= travel, "panel %d at %d, outside [0,%d] (h=%d)", i, y, travel, height);
      CHECK(y > last, "panel %d did not advance past %d (h=%d)", i, last, height);
      last = y;
    }
    // 2. The walk ends exactly at the bottom, never short and never over.
    CHECK(last == travel, "the last panel landed at %d, not %d (h=%d)", last, travel, height);

    // 3. Forward then back is the identity. It is now, because the position is
    //    a panel index and the offset is a pure function of it; when the offset
    //    was the state, each snap re-derived itself and the reader drifted.
    xkcd::Position p = xkcd::startOf(c, width);
    for (int i = 0; i + 1 < rows; ++i) {
      const xkcd::Position fwd = xkcd::stepForward(c, width, viewportH, p, windowFor(c, img, viewportH, p.row + 1));
      const xkcd::Position rt = xkcd::stepBack(c, width, viewportH, fwd, windowFor(c, img, viewportH, p.row));
      CHECK(rt.row == p.row && rt.column == p.column && rt.scrollY == p.scrollY,
            "round trip from row %d landed row %d (h=%d)", p.row, rt.row, height);
      p = fwd;
    }
  }
}

// -------------------------------------------------------------- coverage

// **Nothing may be unreachable.** Walking a comic from the top in reading
// order has to put every row on screen at some point: a traversal that skips
// artwork is worse than one that is merely awkward, and it is the failure the
// column walk would have had if a column had ever been taller than a band.
static void testCoverage() {
  const int vw = 480, vh = 756;
  const xkcd::GapWindow none;

  const int widths[] = {480, 720, 912};
  const int heights[] = {200, 756, 757, 1100, 2400, 9707};
  for (int w : widths) {
    for (int h : heights) {
      const xkcd::Rendition r = pageRend(w, h);
      std::vector<uint8_t> seenRow(static_cast<size_t>(h), 0);
      std::vector<uint8_t> seenCol(static_cast<size_t>(w), 0);

      xkcd::Position p = at(0, 0);
      int guard = 0;
      for (;;) {
        const xkcd::Placement pl = xkcd::place(r, vw, vh, p);
        for (int y = 0; y < pl.visibleH; ++y) {
          const size_t row = static_cast<size_t>(pl.scrollY + y);
          if (row < seenRow.size()) seenRow[row] = 1;
        }
        for (int x = 0; x < pl.visibleW; ++x) {
          const size_t col = static_cast<size_t>(pl.scrollX + x);
          if (col < seenCol.size()) seenCol[col] = 1;
        }
        if (!xkcd::canStepForward(r, vw, vh, p)) break;
        p = xkcd::stepForward(r, vw, vh, p, none);
        if (++guard > 4000) break;
      }
      CHECK(guard <= 4000, "walking %dx%d did not terminate", w, h);

      size_t missedRow = seenRow.size(), missedCol = seenCol.size();
      for (size_t i = 0; i < seenRow.size(); ++i)
        if (!seenRow[i]) {
          missedRow = i;
          break;
        }
      for (size_t i = 0; i < seenCol.size(); ++i)
        if (!seenCol[i]) {
          missedCol = i;
          break;
        }
      CHECK(missedRow == seenRow.size(), "%dx%d never showed row %zu", w, h, missedRow);
      CHECK(missedCol == seenCol.size(), "%dx%d never showed column %zu", w, h, missedCol);
    }
  }
}

// The window a step asks for has to fit the buffer the Activity actually
// declares for it. This is the assertion whose absence let gap snapping be
// dead on the device: it is cheap, and it fails loudly the moment either the
// viewport or the tolerance changes.
static void testGapWindowFitsTheDevice() {
  constexpr int kDeviceViewportH = 800 - 44;
  const int budget = xkcd::gapRowsFor(kDeviceViewportH);
  CHECK(budget > 256, "the old hardcoded 256 really was too small for this viewport (budget %d)", budget);

  const xkcd::Rendition tall = pageRend(480, 20000);
  const int rows = xkcd::rowsIn(tall, kDeviceViewportH);
  for (int row = 0; row < rows; ++row) {
    int firstRow = 0, rowCount = 0;
    xkcd::gapWindowFor(tall, kDeviceViewportH, row, firstRow, rowCount);
    CHECK(rowCount <= budget, "gapWindowFor asked for %d rows at panel %d, budget is %d", rowCount, row, budget);
    CHECK(firstRow >= 0 && firstRow + rowCount <= 20000, "window [%d,%d) is outside the image", firstRow,
          firstRow + rowCount);
  }
}

// ---------------------------------------------------------------- search

static void testSearch() {
  CHECK(xkcd::titleMatches("Is It Worth the Time?", "worth"), "case-insensitive substring");
  CHECK(xkcd::titleMatches("Is It Worth the Time?", "Worth"), "exact case");
  CHECK(xkcd::titleMatches("Is It Worth the Time?", "IS IT"), "leading, upper");
  CHECK(xkcd::titleMatches("Is It Worth the Time?", "time?"), "trailing with punctuation");
  CHECK(xkcd::titleMatches("Sandwich", ""), "an empty needle matches everything");
  CHECK(!xkcd::titleMatches("Sandwich", "sandwiches"), "a needle longer than the title must not match");
  CHECK(!xkcd::titleMatches("Sandwich", "xyz"), "no match");
  CHECK(!xkcd::titleMatches(nullptr, "a"), "null title");
  CHECK(!xkcd::titleMatches("a", nullptr), "null needle");
  // The overrun this is written against: matching must not read past the
  // title's terminator when the needle runs off the end.
  CHECK(!xkcd::titleMatches("ab", "abc"), "needle running off the end must not match");
}

int main() {
  testRecordRoundTrip();
  testArchive();
  testRejectsBadPacks();
  testPlacement();
  testOverview();
  testReadingOrder();
  testMapAcross();
  testGapDetection();
  testSnapping();
  testSidewaysOrder();
  testPanProperties();
  testSearch();
  testCoverage();
  testGapWindowFitsTheDevice();

  std::printf("%s  xkcd core: %d checks, %d failed\n", failures ? "FAIL" : "ok  ", checks, failures);
  return failures ? 1 : 0;
}

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
    c.width = 740;
    c.height = 400;
    c.stride = (740 + 7) / 8;
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

static xkcd::Comic comicFor(const FakeImage& img, uint16_t num = 1) {
  xkcd::Comic c;
  c.num = num;
  c.width = static_cast<uint16_t>(img.width);
  c.height = static_cast<uint16_t>(img.height);
  c.stride = static_cast<uint16_t>(img.stride);
  return c;
}

// Exactly what the activity does: ask which rows the step needs, read that
// range, hand it straight back. Deriving the window anywhere else would be the
// same defect as hit-testing that recomputes its own geometry.
// Flags live in the caller, exactly as they will on the device.
static std::vector<uint8_t> gapFlags;

static xkcd::GapWindow windowFor(const xkcd::Comic& c, const FakeImage& img, int viewportH, int scrollY, bool down) {
  int firstRow = 0, rowCount = 0;
  xkcd::gapWindowFor(c, viewportH, scrollY, down, firstRow, rowCount);
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
  CHECK(out.imageBytes() == 93u * 6370u, "imageBytes %u", out.imageBytes());

  // The record must stay 32 bytes: the whole point of a fixed width is that a
  // lookup is a seek. If this ever changes, kFormatVersion has to change too.
  CHECK(xkcd::kIndexRecordBytes == 32, "record size drifted");
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

static void testPlacement() {
  xkcd::Comic wide;  // a short wide strip: the 37% case
  wide.num = 1;
  wide.width = 740;
  wide.height = 180;
  wide.stride = 93;

  xkcd::Placement p = xkcd::place(wide, 800, 480, xkcd::Position{0, 0});
  CHECK(!p.pans, "a 180px strip must not pan on a 480px viewport");
  CHECK(p.originX == 30, "originX %d (centred horizontally in 800)", p.originX);
  CHECK(p.originY == 150, "originY %d (centred vertically in 480)", p.originY);
  CHECK(p.visibleH == 180, "visibleH %d", p.visibleH);
  CHECK(xkcd::maxScroll(wide, 480) == 0, "maxScroll of a fitting comic is 0");
  CHECK(xkcd::scrollPermille(wide, 800, 480, xkcd::Position{0, 0}) == 1000, "a comic that fits is wholly shown");

  xkcd::Comic tall;
  tall.num = 2;
  tall.width = 740;
  tall.height = 1200;
  tall.stride = 93;

  CHECK(xkcd::maxScroll(tall, 480) == 720, "maxScroll %d", xkcd::maxScroll(tall, 480));
  p = xkcd::place(tall, 800, 480, xkcd::Position{0, 300});
  CHECK(p.pans, "a 1200px comic must pan");
  CHECK(p.originY == 0, "a panning comic is pinned to the top");
  CHECK(p.scrollY == 300, "scrollY %d", p.scrollY);
  CHECK(p.visibleH == 480, "visibleH %d", p.visibleH);

  // Out-of-range scroll must be clamped, not wrapped or trusted.
  CHECK(xkcd::place(tall, 800, 480, xkcd::Position{0, 99999}).scrollY == 720, "over-scroll clamps to maxScroll");
  CHECK(xkcd::place(tall, 800, 480, xkcd::Position{0, -50}).scrollY == 0, "negative scroll clamps to 0");

  CHECK(xkcd::scrollPermille(tall, 800, 480, xkcd::Position{0, 0}) == 0, "top is 0");
  CHECK(xkcd::scrollPermille(tall, 800, 480, xkcd::Position{0, 720}) == 1000, "bottom is 1000");
  CHECK(xkcd::scrollPermille(tall, 800, 480, xkcd::Position{0, 360}) == 500, "halfway is 500");

  // Nothing in the archive is wider than the panel, but a corrupt record must
  // not place the image at a negative origin and blit off the buffer.
  xkcd::Comic over;
  over.num = 3;
  over.width = 900;
  over.height = 100;
  over.stride = 113;
  CHECK(xkcd::place(over, 800, 480, xkcd::Position{0, 0}).originX == 0, "an over-wide image must not get a negative originX");
}

// ---------------------------------------------------------------- columns

static void testColumns() {
  // A comic wider than the viewport is read a column at a time. This is the
  // answer to comics that are big in *both* axes: fitting them to the width is
  // the shrink that makes the lettering unreadable, so they are kept large and
  // walked across instead.
  xkcd::Comic big;  // #3266-shaped: near square, fine detail, kept at full size
  big.num = 3266;
  big.width = 740;
  big.height = 731;
  big.stride = 93;
  const int vw = 480, vh = 756;

  CHECK(xkcd::columnCount(big, vw) == 2, "740 across a 480 panel is 2 columns, got %d",
        xkcd::columnCount(big, vw));
  CHECK(xkcd::columnCount(big, 800) == 1, "the same comic is one column on a wide panel");

  // Column 0 shows the left slice from x=0; column 1 is pulled back so it ends
  // flush with the artwork rather than running into blank space.
  xkcd::Placement p0 = xkcd::place(big, vw, vh, xkcd::Position{0, 0});
  CHECK(p0.scrollX == 0, "first column starts at 0, got %d", p0.scrollX);
  CHECK(p0.visibleW == 480, "first column is a full viewport wide, got %d", p0.visibleW);
  CHECK(p0.originX == 0, "a columned comic is not centred, or it would shift between panes");

  xkcd::Placement p1 = xkcd::place(big, vw, vh, xkcd::Position{1, 0});
  CHECK(p1.scrollX == 740 - 480, "last column ends flush with the artwork, got %d", p1.scrollX);
  CHECK(p1.scrollX + p1.visibleW == 740, "the last column must reach the right edge");

  // Out-of-range columns clamp rather than reading off the end of the image.
  CHECK(xkcd::place(big, vw, vh, xkcd::Position{9, 0}).scrollX == 260, "over-column clamps");
  CHECK(xkcd::place(big, vw, vh, xkcd::Position{-3, 0}).scrollX == 0, "negative column clamps");

  // **The walk.** One control covers both axes: down the column, then on to
  // the top of the next. 731 rows in a 756 viewport means each column is a
  // single screen, so one step forward moves to column 1.
  const xkcd::GapWindow none;
  CHECK(xkcd::maxScroll(big, vh) == 0, "each column of this comic fits vertically");
  xkcd::Position at{0, 0};
  CHECK(xkcd::canStepForward(big, vw, vh, at), "there is a second column to reach");
  at = xkcd::stepForward(big, vw, vh, at, none);
  CHECK(at.column == 1 && at.scrollY == 0, "stepping off column 0 lands on column 1, got col %d y %d",
        at.column, at.scrollY);
  CHECK(!xkcd::canStepForward(big, vw, vh, at), "the last column is the end of the comic");
  CHECK(xkcd::stepForward(big, vw, vh, at, none).column == 1, "the end must not wrap");

  // And back again, symmetrically.
  CHECK(xkcd::canStepBack(at), "column 1 can go back");
  at = xkcd::stepBack(big, vw, vh, at, none);
  CHECK(at.column == 0, "stepping back returns to column 0, got %d", at.column);
  CHECK(!xkcd::canStepBack(at), "the start of the comic is the start");

  // Stepping back into a *tall* column lands at its bottom, not its top --
  // otherwise going back would skip everything the column held.
  xkcd::Comic tallWide;
  tallWide.num = 1;
  tallWide.width = 740;
  tallWide.height = 2000;
  tallWide.stride = 93;
  const int maxS = xkcd::maxScroll(tallWide, vh);
  CHECK(maxS > 0, "fixture must actually scroll");
  xkcd::Position second{1, 0};
  const xkcd::Position back = xkcd::stepBack(tallWide, vw, vh, second, none);
  CHECK(back.column == 0 && back.scrollY == maxS, "back into a tall column lands at its bottom, got y %d",
        back.scrollY);

  // The rail spans the whole comic, not the current column: it must not snap
  // backwards when a column break is crossed.
  CHECK(xkcd::scrollPermille(big, vw, vh, xkcd::Position{0, 0}) == 0, "rail starts empty");
  CHECK(xkcd::scrollPermille(big, vw, vh, xkcd::Position{1, 0}) == 1000, "rail is full at the last pane");
  CHECK(xkcd::scrollPermille(tallWide, vw, vh, xkcd::Position{0, maxS}) < 1000,
        "the bottom of the first column is not the end of a two-column comic");
}

// ------------------------------------------------------------ gap reading

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
  const int vp = 480;
  const int tol = vp * xkcd::kSnapToleranceNum / xkcd::kSnapToleranceDen;  // 96

  // No gaps anywhere: the step is exactly half a viewport.
  {
    const FakeImage img = makeImage(std::string(1400, '#'));
    const xkcd::Comic c = comicFor(img);
    CHECK(xkcd::scrollDown(c, vp, 0, windowFor(c, img, vp, 0, true)) == 240, "plain step should be 240");
  }
  // A gap whose art resumes at row 266 offers a landing at 266-6 = 260, which
  // is 20 from the target: within tolerance, so the step is pulled onto it.
  {
    const FakeImage img =
        makeFramed(std::string(250, '#') + std::string(16, '.') + std::string(1134, '#'), 740);
    const xkcd::Comic c = comicFor(img);
    CHECK(xkcd::scrollDown(c, vp, 0, windowFor(c, img, vp, 0, true)) == 260, "a near gap should be snapped to, got %d",
          xkcd::scrollDown(c, vp, 0, windowFor(c, img, vp, 0, true)));
  }
  // A gap well past tolerance does not pull the step.
  {
    const FakeImage img = makeImage(std::string(600, '#') + std::string(16, '.') + std::string(784, '#'));
    const xkcd::Comic c = comicFor(img);
    CHECK(xkcd::scrollDown(c, vp, 0, windowFor(c, img, vp, 0, true)) == 240, "a distant gap must not pull the step");
  }
  // A gap too short to be a gutter is the space between two lines of
  // lettering, and stopping there would put half a sentence at the top.
  {
    const FakeImage img = makeImage(std::string(250, '#') + std::string(2, '.') + std::string(1148, '#'));
    const xkcd::Comic c = comicFor(img);
    CHECK(xkcd::scrollDown(c, vp, 0, windowFor(c, img, vp, 0, true)) == 240, "a 2-row gap must not be a landing");
  }
  // The nearest of several gaps wins.
  {
    const FakeImage img = makeImage(std::string(200, '#') + std::string(8, '.') + std::string(42, '#') +
                                    std::string(8, '.') + std::string(1142, '#'));
    const xkcd::Comic c = comicFor(img);
    // Landings at 208-6=202 and 258-6=252; the target is 240, so 252 wins.
    CHECK(xkcd::scrollDown(c, vp, 0, windowFor(c, img, vp, 0, true)) == 252, "the nearest landing should win, got %d",
          xkcd::scrollDown(c, vp, 0, windowFor(c, img, vp, 0, true)));
  }
  // **The window has to reach back far enough to see a run that starts before
  // the target.** A gap of exactly kMinGutterRows ending just after the target
  // is a legitimate landing, but only if the whole run is visible: a window
  // that began at the target would see two of its rows, judge the run too
  // short, and step blindly through the art instead.
  {
    // Art to 236, gap 236..241 (6 rows), art resumes at 242 -> landing 236,
    // which is 4 from the target of 240 and well inside tolerance.
    const FakeImage img =
        makeFramed(std::string(236, '#') + std::string(6, '.') + std::string(1158, '#'), 740);
    const xkcd::Comic c = comicFor(img);
    CHECK(xkcd::scrollDown(c, vp, 0, windowFor(c, img, vp, 0, true)) == 236,
          "a gap run beginning before the target must still be seen whole, got %d",
          xkcd::scrollDown(c, vp, 0, windowFor(c, img, vp, 0, true)));
  }

  // A null window is legal and means "no artwork available" -- the step must
  // fall back to a plain half-screen rather than refusing to move.
  {
    const FakeImage img = makeImage(std::string(1400, '#'));
    const xkcd::Comic c = comicFor(img);
    CHECK(xkcd::scrollDown(c, vp, 0, xkcd::GapWindow{}) == 240, "a missing window must fall back to a plain step");
    CHECK(xkcd::scrollUp(c, vp, 480, xkcd::GapWindow{}) == 240, "and the same upward");
  }

  // The case that discriminates the flush landing, which the property tests
  // cannot see because reachability holds either way: the plain step lands
  // *exactly* on the end and there is a gap within tolerance just short of it.
  // The end must win -- snapping would stop high, show a sliver of the last
  // panel, and demand one more tap to close a gap the reader can already see.
  {
    // height 1400 -> maxScroll 920. Start at 680 so target == 920 exactly.
    const FakeImage img =
        makeFramed(std::string(900, '#') + std::string(16, '.') + std::string(484, '#'), 740);
    const xkcd::Comic c = comicFor(img);
    const int maxS = xkcd::maxScroll(c, vp);
    CHECK(maxS == 920, "fixture assumption: maxScroll is %d", maxS);
    CHECK(xkcd::scrollDown(c, vp, maxS - vp / 2, windowFor(c, img, vp, maxS - vp / 2, true)) == maxS,
          "a step landing exactly on the end must not be pulled short by a gap");
  }
  // The same at the top.
  {
    const FakeImage img =
        makeFramed(std::string(10, '#') + std::string(16, '.') + std::string(1374, '#'), 740);
    const xkcd::Comic c = comicFor(img);
    CHECK(xkcd::scrollUp(c, vp, vp / 2, windowFor(c, img, vp, vp / 2, false)) == 0,
          "a step landing exactly on the top must not be pulled short by a gap");
  }
  CHECK(xkcd::kSnapToleranceNum * 2 < xkcd::kSnapToleranceDen,
        "tolerance must stay under half a viewport or steps can stop progressing");
  CHECK(vp / 2 - tol > 0, "a snapped step must still advance by %d px", vp / 2 - tol);
}

// A small deterministic LCG. Deterministic so a failure is reproducible; the
// point is coverage of shapes, not randomness.
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
  const int viewportH = 480;

  for (int trial = 0; trial < 600; ++trial) {
    // Heights spanning the real archive: fits, just over, and the long tail.
    const int height = rng.range(60, 3000);
    const int width = rng.range(120, 780);
    const bool framed = (trial % 3) == 0;

    // A plausible comic: bands of art separated by gaps of varying height,
    // including gaps too short to be landings.
    std::string spec;
    while (static_cast<int>(spec.size()) < height) {
      spec += std::string(rng.range(5, 120), '#');
      spec += std::string(rng.range(1, 20), '.');
    }
    spec.resize(height);

    const FakeImage img = makeImage(spec, width, framed ? 2 : 0);
    const xkcd::Comic c = comicFor(img);
    const int maxS = xkcd::maxScroll(c, viewportH);

    // 1. Every step stays in range.
    // 2. Every step from anywhere but the bottom moves strictly forward.
    int s = 0;
    int guard = 0;
    while (s < maxS && guard < 2000) {
      const int next = xkcd::scrollDown(c, viewportH, s, windowFor(c, img, viewportH, s, true));
      CHECK(next >= 0 && next <= maxS, "scrollDown out of range: %d not in [0,%d] h=%d", next, maxS, height);
      CHECK(next > s, "scrollDown did not progress from %d (h=%d w=%d)", s, height, width);
      s = next;
      ++guard;
    }
    // 3. Panning down always arrives exactly at the end, in bounded time.
    CHECK(guard < 2000, "scrollDown did not terminate (h=%d)", height);
    CHECK(s == maxS, "scrollDown ended at %d, not maxScroll %d", s, maxS);

    // 4. And symmetrically upward, back to exactly the top.
    guard = 0;
    while (s > 0 && guard < 2000) {
      const int prev = xkcd::scrollUp(c, viewportH, s, windowFor(c, img, viewportH, s, false));
      CHECK(prev >= 0 && prev <= maxS, "scrollUp out of range: %d", prev);
      CHECK(prev < s, "scrollUp did not progress from %d (h=%d)", s, height);
      s = prev;
      ++guard;
    }
    CHECK(guard < 2000, "scrollUp did not terminate");
    CHECK(s == 0, "scrollUp ended at %d, not 0", s);

    // 5. Clamping at the ends is idempotent, so a tap at the bottom is a
    //    no-op rather than a wrap.
    CHECK(xkcd::scrollDown(c, viewportH, maxS, windowFor(c, img, viewportH, maxS, true)) == maxS,
          "down at the bottom must stay");
    CHECK(xkcd::scrollUp(c, viewportH, 0, windowFor(c, img, viewportH, 0, false)) == 0, "up at the top must stay");

    // 6. The rail agrees with the ends.
    CHECK(xkcd::scrollPermille(c, width, viewportH, xkcd::Position{0, 0}) == (maxS > 0 ? 0 : 1000),
          "rail at the top");
    CHECK(xkcd::scrollPermille(c, width, viewportH, xkcd::Position{0, maxS}) == 1000, "rail at the bottom");
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
  testColumns();
  testGapDetection();
  testSnapping();
  testPanProperties();
  testSearch();

  std::printf("%s  xkcd core: %d checks, %d failures\n", failures ? "FAIL" : "ok  ", checks, failures);
  return failures ? 1 : 0;
}

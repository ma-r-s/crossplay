#pragma once

// The xkcd archive: what is on the card, and how a comic is placed on a panel
// that is smaller than most of them.
//
// Freestanding C++17 -- no Arduino, no renderer, no SD card. All I/O goes
// through ByteSource, which is what lets host-tests/xkcd drive a real pack on a
// laptop. Nothing here allocates: the archive is 3000-odd comics and the reader
// touches one at a time, so a per-comic heap round trip is exactly the churn
// that fragments a device with no room to spare.
//
// ---------------------------------------------------------------------------
// Two views, and only one of them has a sideways axis
// ---------------------------------------------------------------------------
//
// **The page view** is the whole comic: the artwork is stored already fitted
// so its full width is on the panel, so the page view has **no horizontal axis
// at all** and the only motion is down, half a screen at a time, snapped to a
// gap in the art. 93% of the archive opens here and never moves.
//
// That is a repair, not a preference. The previous version kept any comic
// wider than the panel at full size and read it in columns, which meant #1606
// -- 481px wide -- was given a whole second column to reveal **one pixel** of
// new artwork. 226 comics paid an extra column for under 96px. Worse, the
// column walk went down one column and back to the top of the next, so a
// multi-panel comic was read 1, 4, 7, 2, 5, 8; on e-ink, where there is no
// animation to show that the view moved sideways, that is indistinguishable
// from jumping somewhere at random.
//
// The obvious repair is to move the threshold rather than remove it. The
// archive says no: source widths are continuous from 200 to 760px with no
// empty stretch anywhere, so wherever a width threshold goes, real comics sit
// either side of it arbitrarily close and behave completely differently. **A
// rule that changes how the reader works cannot be decided by measuring the
// artwork.** So it is not decided automatically at all.
//
// **The closer view** is a second stored rendition -- it has to be stored,
// because the panel is 1-bit and resampling 1-bit art on the device is mush.
// It exists only for comics the page view cannot render legible: the big
// near-square ones like #3266, #256 and #1110, which rotation cannot help.
//
// **Those comics open in it.** Showing a comic too small to read and making
// the reader ask for the readable one is the wrong way round; the Confirm
// button pulls *back* to the whole comic, which is the thing you want
// occasionally rather than the thing you want first. That the builder decides
// which view a comic opens in is not the cliff this file spent so long
// avoiding: both views use the same controls, and one button press undoes the
// choice if it was wrong for a particular comic.
//
// It has a horizontal axis, so its guarantee has to be built into its
// dimensions rather than checked afterwards -- see kCloserWidth. It is always
// exactly two columns, and the second one always reveals at least half a screen
// of artwork the first did not.

#include <cstddef>
#include <cstdint>

namespace xkcd {

// Random-access bytes. Returning false must mean "did not read `length` bytes",
// never a short read, so callers can treat failure as fatal.
class ByteSource {
 public:
  virtual ~ByteSource() = default;
  virtual bool read(uint32_t offset, void* dst, uint32_t length) = 0;
  virtual uint32_t size() const = 0;
};

// --- The panel -----------------------------------------------------------

// The app is portrait, always, and the panel never rotates. A comic meant to
// be read sideways is stored already turned, so the reader turns the device
// and the screen layout stays exactly where it was. This is a fact about the
// app rather than a setting, which is what lets the guarantees below be stated
// in pixels.
inline constexpr int kPanelWidth = 480;

// --- The pack ------------------------------------------------------------
//
// Three files under /xkcd on the card, written by
// tools_local/xkcd/build_pack.py and appended to by the device when it fetches
// a new comic:
//
//   index.dat   a header then one fixed 40-byte record per comic, ascending by
//               number, so a lookup is a binary search over seeks and never a
//               scan. Fixed-width is what makes that possible; it is the only
//               reason the strings live somewhere else.
//   images.dat  the 1-bit bitmaps end to end, MSB first, rows padded to bytes.
//               A comic's page rendition is followed by its closer rendition
//               when it has one.
//   text.dat    title then alt text, each NUL-terminated, per comic.
//
// One pack rather than 3281 files: a directory that size makes every open slow
// on FAT, and the reader opens one on every list scroll.
//
// There is deliberately **no file of panel boundaries**. An earlier version
// precomputed them, which meant a per-comic cap, a truncation report, and a
// stored table that the device's own wifi conversion had to reproduce
// identically forever. Since a step only ever consults a 200-row window around
// one target, the reader reads those rows off the card when it steps -- about
// 18KB, against a 300ms screen refresh it is already paying. No file, no cap,
// no second implementation, and nothing that can go stale.

inline constexpr uint32_t kMagic = 0x44434B58;  // "XKCD" little-endian

// 3: the record carries a second rendition. Bumped whenever a record's layout
// changes, exactly like the reader's cache format versions. A pack from an
// older build is rejected whole rather than misread: every field would land
// one offset out and the failure would look like corrupt artwork rather than a
// stale file.
inline constexpr uint16_t kFormatVersion = 3;
inline constexpr uint32_t kIndexHeaderBytes = 16;
inline constexpr uint32_t kIndexRecordBytes = 40;

// --- What the builder guarantees about the artwork -----------------------

// The page rendition is fitted to the panel width, so it is never wider than
// the panel. `Comic::valid()` enforces it, which is what makes "the page view
// has one column" a property of the format rather than a hope about the data:
// a pack that violated it would be rejected, not silently read in columns.
inline constexpr int kMaxPageWidth = kPanelWidth;

// The closer rendition is **always exactly two columns**, overlapping by
// kColumnOverlap so a word split at the seam is readable on both sides.
//
// This is the whole anti-sliver mechanism, and it lives in the constants for
// the same reason kSnapTolerance does: a runtime check for "is this second
// column worth it?" was the shape of the rule that produced the one-pixel
// column in the first place. Build it in, do not test for it.
inline constexpr int kColumnOverlap = 48;
inline constexpr int kCloserWidth = 2 * kPanelWidth - kColumnOverlap;  // 912

// And the floor: a closer view whose second column reveals less than half a
// screen is not worth a tap, so the builder does not make one. Measured over
// the archive the real minimum is 378px, exactly half a screen, with no comic
// anywhere near the boundary.
inline constexpr int kMinCloserWidth = kPanelWidth + 378;

// #887 "Future Timeline" is 6370 rows, the tallest in the sampled archive.
// 16384 leaves room for whatever Randall does next without letting a corrupt
// header seek past the end of the card.
inline constexpr int kMaxComicHeight = 16384;

// Record flag: this comic is **stored rotated**, to be read with the device
// turned on its side.
//
// The panel itself never rotates. A first version called setOrientation per
// comic, which turned the whole UI -- bar, controls, everything -- around
// underneath the reader; it is the comic that is sideways, not the app. So the
// rotation happens once, in the pack, and the device just draws a tall image.
//
// It is a readability rule rather than a taste one. xkcd letters at a roughly
// constant size in source pixels, so how far a comic has been shrunk decides
// whether it can be read: #3269 fitted into a 480 panel is 0.69x and the
// lettering goes, but turned on its side it fits whole at 1.09x.
//
// The reader does not need this flag to draw -- a rotated comic is simply a
// tall one -- but it is kept so the app can tell the two apart if it ever
// needs to.
inline constexpr uint8_t kSideways = 1;

// One decoded index record. Trivially copyable, so a list screen can hold a
// page of them on the stack.
struct Comic {
  uint16_t num = 0;
  uint16_t width = 0;  // the page rendition
  uint16_t height = 0;
  uint16_t stride = 0;  // bytes per row; rows are byte-padded
  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
  uint32_t imageOffset = 0;
  uint32_t textOffset = 0;
  uint8_t flags = 0;

  // The closer rendition. A width of zero means this comic has none, which is
  // the normal case: 96% of the archive is legible fitted to the panel and has
  // nothing more to show.
  uint16_t closerWidth = 0;
  uint16_t closerHeight = 0;
  uint16_t closerStride = 0;
  uint32_t closerOffset = 0;

  bool sideways() const { return (flags & kSideways) != 0; }
  bool hasCloser() const { return closerWidth > 0 && closerHeight > 0 && closerStride > 0; }

  // A width of zero is how the index says "this slot is not filled in";
  // the offsets are meaningless then and must not be followed.
  bool valid() const {
    return num > 0 && width > 0 && height > 0 && stride > 0 && width <= kMaxPageWidth && height <= kMaxComicHeight &&
           stride >= (width + 7) / 8 &&
           (!hasCloser() ||
            (closerWidth <= kCloserWidth && closerHeight <= kMaxComicHeight && closerStride >= (closerWidth + 7) / 8));
  }
};

// Decode one 40-byte record. Exposed because both the index reader and the
// pack writer's own verification path need it, and two decoders would drift.
Comic decodeRecord(const uint8_t* rec);
void encodeRecord(const Comic& c, uint8_t* rec);

class Archive {
 public:
  // Parse the header. False for a missing, truncated or wrong-version pack,
  // which is not a crash -- the app says the card has no archive and offers to
  // fetch one.
  bool open(ByteSource& index);

  bool ready() const { return count_ > 0; }
  int count() const { return count_; }
  uint16_t maxNum() const { return maxNum_; }

  // The i'th record in number order. `i` is a position, not a comic number.
  bool at(ByteSource& index, int i, Comic& out) const;

  // Find by comic number. Binary search over the index, so ~12 seeks for the
  // whole archive rather than 3281. Returns the position, or -1.
  int find(ByteSource& index, uint16_t num) const;

  // The position of the first comic at or after `num`, for "jump to number"
  // when that exact one is missing (404 is, famously, not a comic).
  int lowerBound(ByteSource& index, uint16_t num) const;

 private:
  int count_ = 0;
  uint16_t maxNum_ = 0;
  uint32_t size_ = 0;
};

// Read a comic's title into `out` (NUL-terminated, truncated to `cap`).
// Returns the number of bytes written, not counting the terminator.
int readTitle(ByteSource& text, const Comic& c, char* out, int cap);

// Read a comic's alt text -- the joke hidden behind the hover on the website,
// which a touch panel has no equivalent for and which is half of why people
// read xkcd. It is a first-class screen here rather than a tooltip.
int readAlt(ByteSource& text, const Comic& c, char* out, int cap);

// --- Which rendition is on screen ----------------------------------------

// The page view is the whole comic at panel width. The closer view is the
// second rendition, entered deliberately and never automatically.
enum class Lens : uint8_t { Page, Closer };

// The image being drawn, independent of which rendition it came from. Every
// placement and step function below takes one of these rather than a Comic, so
// none of them has to branch on the lens -- the difference between the two
// views is entirely in these four numbers.
struct Rendition {
  uint16_t width = 0;
  uint16_t height = 0;
  uint16_t stride = 0;
  uint32_t offset = 0;

  bool valid() const { return width > 0 && height > 0 && stride >= (width + 7) / 8; }
  uint32_t bytes() const { return static_cast<uint32_t>(stride) * height; }
};

// Falls back to the page rendition when Closer is asked for and there is none,
// so a caller that gets its lens out of step with the comic draws something
// sensible rather than reading a zero-length image at offset zero.
Rendition renditionFor(const Comic& c, Lens lens);

// --- Reading the artwork for gaps ---------------------------------------
//
// **A gap is a threshold, not emptiness, and that was measured.** The first
// version looked for rows with no ink at all. Rendering a real pack showed why
// that finds almost nothing: **20 of 30 sampled comics are drawn inside a
// frame**, so every interior row crosses two vertical border strokes and no row
// is ever empty. On #1093, a framed table, it found one boundary in a thousand
// rows and the steps sliced straight through the table.

// The ink a row may carry and still count as a gap.
//
// The floor is doing more work than the percentage, and that was measured. The
// ink a blank row carries is a handful of *vertical strokes* -- a frame, a
// column divider, a table rule -- and that count does not scale with width. On
// #1093, a 340px framed table, a budget of 2% (6 pixels) fell just under the
// frame-plus-divider cost, so every row gap read as 1-2 rows of noise and the
// steps sliced through the table. Raising the budget to 12 made the same gaps
// resolve as clean 7-10 row runs.
inline constexpr int kGapInkPercent = 3;
inline constexpr int kGapInkFloor = 12;

// A gap has to be at least this tall to be worth stopping at. Below this it is
// the space between two hand-lettered lines inside one balloon, and stopping
// there would put half a sentence at the top of the screen.
inline constexpr int kMinGutterRows = 4;

// The step lands this far above the first inked row after a gap, so the art
// has a little air over it instead of being welded to the screen edge.
inline constexpr int kGutterPad = 6;

// Ink pixels in [0, width). Bits past the width are padding and never counted.
int rowInk(const uint8_t* row, int width);

// True when the row carries little enough ink to be a gap worth stopping at.
bool rowIsGap(const uint8_t* row, int width);

// --- Placing a rendition on the panel ------------------------------------

// How much of the viewport a step may be pulled by, to land on a gap. A
// fraction rather than a pixel count because the thing being avoided is
// proportional: cutting a balloon matters relative to how much you can see at
// once, and the same 40px nudge is generous on a 480px viewport and
// meaningless on a 96px one.
inline constexpr int kSnapToleranceNum = 1;
inline constexpr int kSnapToleranceDen = 5;

// **This is what guarantees that every tap moves the comic.** The step is half
// a viewport and a snap may pull it by at most this fraction, so as long as the
// tolerance is strictly under a half, the snapped position is always strictly
// past where we started. Widen it to a half or more and a gap just behind the
// reader becomes reachable: the tap would return the current position, the
// screen would not change, and the reader would be stuck on a control that
// looks perfectly live.
//
// It is asserted here rather than defended at the call site because a runtime
// guard for this was written first, and a mutation test showed it could never
// fire -- dead code that looked load-bearing. The constants are the mechanism,
// so the constants are what gets checked.
static_assert(kSnapToleranceNum * 2 < kSnapToleranceDen,
              "the snap tolerance must be under half a viewport, or a step "
              "could fail to make progress and freeze the reader");

// Where the reader is. `column` is always 0 in the page view -- the page
// rendition is never wider than the panel -- and 0 or 1 in the closer view.
struct Position {
  Lens lens = Lens::Page;
  int column = 0;
  int scrollY = 0;
};

struct Placement {
  int originX = 0;    // left edge of the image in screen coords
  int originY = 0;    // top edge of the image in screen coords
  int scrollX = 0;    // first image column drawn
  int scrollY = 0;    // first image row drawn
  int visibleW = 0;   // how many image columns are drawn
  int visibleH = 0;   // how many image rows are drawn
  bool pans = false;  // whether the image exceeds the viewport at all
};

// Where the image goes for a given position. Centred horizontally when it
// fits; centred vertically when it fits, and pinned to the scroll offset when
// it does not.
//
// Callers must derive their tap regions and their map from what this returns
// rather than recomputing them -- that rule has caught more bugs in this fork
// than any other. See docs/building-apps.md.
Placement place(const Rendition& r, int viewportW, int viewportH, const Position& at);

// The largest legal scroll offset. Zero when the image fits down the page.
int maxScroll(const Rendition& r, int viewportH);

// How many columns the image occupies: 1 for any page rendition, 2 for any
// closer rendition. Both are guaranteed by the builder and enforced by
// Comic::valid(), so this never returns 3 for a pack this build will open.
int columnsIn(const Rendition& r, int viewportW);

// Which rows near the target are gaps, so the core stays free of I/O.
//
// One byte per row rather than the packed artwork itself, and that is a RAM
// decision: the window is ~200 rows, which as pixels is 19KB the device would
// have to find on the heap for every tap. As flags it is 200 bytes on the
// stack. The caller streams the rows past `rowIsGap` in whatever chunks suit
// it and fills this in.
//
// Leaving `isGap` null is legal and means "no artwork available": the step
// falls back to a plain half-screen, which is exactly what should happen if
// the card hiccups.
struct GapWindow {
  const uint8_t* isGap = nullptr;  // non-zero means the row is a gap
  int firstRow = 0;
  int rowCount = 0;
};

// The rows a step from `scrollY` will consult. The caller reads exactly this
// range off the card and hands it straight back, so the window that was read is
// the window that gets used -- the same rule as hit-testing sharing geometry
// with drawing.
void gapWindowFor(const Rendition& r, int viewportH, int scrollY, bool down, int& firstRow, int& rowCount);

// The most rows gapWindowFor can ever ask for at a given viewport, so the
// caller can size its flag buffer from the same arithmetic the window is
// derived from.
//
// **This exists because writing the number down separately silently disabled
// gap snapping for the entire life of the app.** The Activity had a 256-row
// buffer justified by a comment that computed 203 "at a 480px viewport"; the
// reader's viewport is 756, the real ask is 313, the window was refused as too
// large on every step, and every pan fell back to a blind half-screen through
// the middle of a speech balloon. The host tests never saw it because they
// passed 480, where the comment was true.
constexpr int gapRowsFor(int viewportH) {
  return 2 * (viewportH * kSnapToleranceNum / kSnapToleranceDen) + kGutterPad + kMinGutterRows + 1;
}

// Advance or retreat one view, **in reading order**: across the columns of the
// current band left to right, then down to the next band and back to the left.
//
// In the page view there is only one column, so this is purely vertical and
// the horizontal half of the rule never fires. In the closer view it is what
// makes a two-column comic read 1, 2, 3, 4 rather than 1, 3, 2, 4 -- the
// scrambling that made the previous version feel like it was jumping at
// random.
Position stepForward(const Rendition& r, int viewportW, int viewportH, const Position& at, const GapWindow& window);
Position stepBack(const Rendition& r, int viewportW, int viewportH, const Position& at, const GapWindow& window);

// True when there is anywhere further to go, so the caller can stop rather
// than repaint an identical screen. Both are local: neither walks the image,
// which is what keeps them honest on a 740x14957 comic.
bool canStepForward(const Rendition& r, int viewportW, int viewportH, const Position& at);
bool canStepBack(const Rendition& r, int viewportW, int viewportH, const Position& at);

// The next scroll offset when the reader moves down (or up) a band. Half a
// viewport, pulled onto a gap in the artwork when one is within tolerance,
// clamped to the ends.
//
// Both guarantee **strict progress**: if the result is not already at the
// relevant end, it differs from `scrollY`.
int scrollDown(const Rendition& r, int viewportH, int scrollY, const GapWindow& window);
int scrollUp(const Rendition& r, int viewportH, int scrollY, const GapWindow& window);

// --- Moving between the two views ----------------------------------------

// The position in the other lens that shows the same part of the comic.
// Switching views should not lose your place, and the two renditions are the
// same artwork at different scales, so the mapping is the scroll offset scaled
// by the ratio of their heights. Entering the closer view starts at the left
// of the band; leaving it keeps whatever row you were on.
Position mapAcross(const Comic& c, int viewportW, int viewportH, const Position& at, Lens to);

// --- Search --------------------------------------------------------------

// Case-insensitive substring match, ASCII only, which is all the titles are:
// the pack writer folds them the same way the fonts are subset. Written here
// rather than reached for from <cstring> because strcasestr is not portable to
// the device toolchain.
bool titleMatches(const char* title, const char* needle);

// Fold one character for matching: upper to lower, everything else unchanged.
char foldChar(char c);

}  // namespace xkcd

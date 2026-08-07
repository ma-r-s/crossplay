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
// --- Why the reader is landscape, at native size, and only ever pans down ---
//
// Measured across 1048 comics sampled evenly over the whole archive, not
// guessed:
//
//   * The widest comic xkcd has ever published is **780px**. p99 is 740, which
//     is the width Randall has standardised on. Nothing comes close to the
//     800px landscape panel.
//   * So in landscape at 1:1, **no comic is ever downscaled and no comic ever
//     needs to pan sideways.** 75% of them need no panning at all; 21% are two
//     screens tall, and 4% more than that.
//   * Portrait would be the opposite trade: fit-to-width shrinks 56% of the
//     archive, the worst to 0.62.
//
// That last number is the one that decides it. Rendering #1205 at 1:1 and at
// 0.65 through this fork's own Atkinson ditherer, the 1:1 lettering is clean
// and the 0.65 lettering is visibly broken -- hand-lettered strokes are one or
// two pixels wide, and there is no such thing as a two-thirds-wide stroke on a
// 1-bit panel. Downscaling line art is where legibility dies here, which is the
// same reason this fork ships its own 1-bit font instead of using the reader's.
//
// So the horizontal panning that seemed necessary is not: the axis xkcd is
// wide on is the axis the landscape panel is long on. **The only motion in
// this reader is vertical**, which is why there is one step function below and
// not two.
//
// --- Why the step snaps to a gap in the art ---
//
// Half a screen is the right stride: every row of the comic is then seen twice,
// once low and once high, so nothing is only ever glimpsed at the edge. But a
// blind half-screen cuts speech balloons in half, and a balloon split across a
// step is read twice and understood once.
//
// So the step aims at half a screen and then looks for a horizontal gap in the
// artwork near that target, landing on one if it finds it. A gap is a run of
// rows carrying almost no ink -- the space between panel rows, or between two
// lines of lettering. Landing on either is a good place to stop.

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

// --- The pack ------------------------------------------------------------
//
// Three files under /xkcd on the card, written by
// tools_local/xkcd/build_pack.py and appended to by the device when it fetches
// a new comic:
//
//   index.dat   a header then one fixed 32-byte record per comic, ascending by
//               number, so a lookup is a binary search over seeks and never a
//               scan. Fixed-width is what makes that possible; it is the only
//               reason the strings live somewhere else.
//   images.dat  the 1-bit bitmaps end to end, MSB first, rows padded to bytes.
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
inline constexpr uint16_t kFormatVersion = 2;

// Bumped whenever a record's layout changes, exactly like the reader's cache
// format versions. A pack from an older build is rejected whole rather than
// misread: every field would land one offset out and the failure would look
// like corrupt artwork rather than a stale file.
inline constexpr uint32_t kIndexHeaderBytes = 16;
inline constexpr uint32_t kIndexRecordBytes = 32;

// The landscape panel is 800 and also the hard sanity bound on what the pack
// may contain: a wider image would silently draw off the edge. The widest
// source in the archive is 960px (#256), which the builder scales to fit.
//
// An earlier comment here said the archive's ceiling was 780. That came from a
// 1048-comic sample and was wrong -- worth remembering that a sampled maximum
// is not a maximum.
inline constexpr int kMaxComicWidth = 800;

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
// whether it can be read: a 694x272 strip fitted into a 480 panel is 0.69x and
// the lettering goes, but turned on its side it is 272 across and fits easily.
//
// The reader does not need this flag to draw -- a rotated comic is simply a
// tall one -- but it is kept so the app can tell the two apart if it ever
// needs to.
inline constexpr uint8_t kSideways = 1;

// #887 "Future Timeline" is 6370 rows, the tallest in the sampled archive.
// 16384 leaves room for whatever Randall does next without letting a corrupt
// header seek past the end of the card.
inline constexpr int kMaxComicHeight = 16384;

// One decoded index record. Trivially copyable and 32 bytes on the wire, so a
// list screen can hold a page of them on the stack.
struct Comic {
  uint16_t num = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  uint16_t stride = 0;  // bytes per row; rows are byte-padded
  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
  uint32_t imageOffset = 0;
  uint32_t textOffset = 0;
  // Which way round this comic is meant to be looked at, decided by the pack
  // builder from the source dimensions. See kLandscape.
  uint8_t flags = 0;

  bool sideways() const { return (flags & 1) != 0; }

  // A width of zero is how the index says "this slot is not filled in";
  // the offsets are meaningless then and must not be followed.
  bool valid() const {
    return num > 0 && width > 0 && height > 0 && stride > 0 && width <= kMaxComicWidth && height <= kMaxComicHeight &&
           stride >= (width + 7) / 8;
  }

  uint32_t imageBytes() const { return static_cast<uint32_t>(stride) * height; }
};

// Decode one 32-byte record. Exposed because both the index reader and the
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

// --- Reading the artwork for gaps ---------------------------------------
//
// **A gap is a threshold, not emptiness, and that was measured.** The first
// version looked for rows with no ink at all. Rendering a real pack showed why
// that finds almost nothing: **20 of 30 sampled comics are drawn inside a
// frame**, so every interior row crosses two vertical border strokes and no row
// is ever empty. On #1093, a framed table, it found one boundary in a thousand
// rows and the steps sliced straight through the table.
//
// Profiling ink-per-row across those 30 comics put the separation at about 2%
// of the width: a frame costs two strokes, any row with lettering costs an
// order of magnitude more.

// How far a comic may be shrunk before the builder turns the panel instead.
// A 15% reduction is barely perceptible; 0.69x, which is what a 694px strip
// costs in portrait, takes the lettering with it. Measured against the whole
// archive this puts 55% of comics in portrait and 45% in landscape, and --
// because landscape's 800px absorbs even the 960px outlier -- **nothing ever
// needs to pan sideways**. Vertical stays the only motion.
inline constexpr int kMinScalePercent = 85;

// And the other size rule: never enlarge past this, or a 106px comic becomes
// mush. Applied by the builder, recorded here so both ends agree.
inline constexpr int kMaxUpscalePercent = 300;

// The ink a row may carry and still count as a gap.
//
// The floor is doing more work than the percentage, and that was measured. The
// ink a blank row carries is a handful of *vertical strokes* -- a frame, a
// column divider, a table rule -- and that count does not scale with width. On
// #1093, a 340px framed table, a budget of 2% (6 pixels) fell just under the
// frame-plus-divider cost, so every row gap read as 1-2 rows of noise and the
// steps sliced through the table. Raising the budget to 12 made the same gaps
// resolve as clean 7-10 row runs.
//
// Checked against every comic in a 160-comic pack that is tall enough to pan:
// 3%/12 lands 93% of steps on a gap, identical to the tighter setting on the
// general archive while also fixing the dense-table case.
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

// --- Placing a comic on the panel ---------------------------------------

// How much of the viewport a step may be pulled by, to land on a gap. A
// fraction rather than a pixel count because the thing being avoided is
// proportional: cutting a balloon matters relative to how much you can see at
// once, and the same 40px nudge is generous on a 480px viewport and
// meaningless on a 96px one. A fifth is wide enough to catch the gutter of a
// normal panel row and narrow enough that the step is still recognisably half a
// screen.
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

// Where the reader is in a comic. A comic is a grid of panes: `column` counts
// across, `scrollY` runs down the current column.
//
// **Columns exist because some comics are big in both axes.** A near-square
// comic full of fine detail cannot be made readable by fitting it to a panel's
// width -- that is the shrink that turns lettering to mush. The builder zooms
// those back to full size instead and lets them be read a column at a time.
struct Position {
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
  bool pans = false;  // whether the comic exceeds the viewport at all
};

// Where the image goes for a given scroll offset. Centred horizontally always
// (nothing is ever wider than the panel); centred vertically when it fits, and
// pinned to the scroll offset when it does not.
//
// Callers must derive their tap regions from what this returns rather than
// recomputing them -- that rule has caught more bugs in this fork than any
// other. See docs/building-apps.md.
Placement place(const Comic& c, int viewportW, int viewportH, const Position& at);

// The largest legal scroll offset within a column. Zero when the column fits.
int maxScroll(const Comic& c, int viewportH);

// How many columns the comic occupies. One for all but the wide-and-detailed.
int columnCount(const Comic& c, int viewportW);

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
void gapWindowFor(const Comic& c, int viewportH, int scrollY, bool down, int& firstRow, int& rowCount);

// Advance or retreat one step in reading order: down the current column, and
// on past its bottom to the top of the next. **One control covers both axes**,
// which is why there is no separate left/right gesture -- the reader taps down
// and the comic keeps going.
Position stepForward(const Comic& c, int viewportW, int viewportH, const Position& at, const GapWindow& window);
Position stepBack(const Comic& c, int viewportW, int viewportH, const Position& at, const GapWindow& window);

// True when there is anywhere further to go, so the caller can stop rather
// than repaint an identical screen.
bool canStepForward(const Comic& c, int viewportW, int viewportH, const Position& at);
bool canStepBack(const Position& at);

// The next scroll offset when the reader pans down (or up). Half a viewport,
// pulled onto a gap in the artwork when one is within tolerance, clamped to
// the ends.
//
// Both guarantee **strict progress**: if the result is not already at the
// relevant end, it differs from `scrollY`.
int scrollDown(const Comic& c, int viewportH, int scrollY, const GapWindow& window);
int scrollUp(const Comic& c, int viewportH, int scrollY, const GapWindow& window);

// How far through the whole comic the reader is, across every column, as a
// fraction in [0, 1000].
// Drawn as a rail rather than counted as "2 of 3" on purpose: a step count
// would have to walk the whole comic through the snap rule to be right, which
// means reading every row off the card, and any cheaper formula would be a
// second implementation of the step that disagrees with the real one the first
// time a snap fires. The rail is derived from the scroll offset, which *is* the
// state, so it cannot disagree with anything.
int scrollPermille(const Comic& c, int viewportW, int viewportH, const Position& at);

// --- Search --------------------------------------------------------------

// Case-insensitive substring match, ASCII only, which is all the titles are:
// the pack writer folds them the same way the fonts are subset. Written here
// rather than reached for from <cstring> because strcasestr is not portable to
// the device toolchain.
bool titleMatches(const char* title, const char* needle);

// Fold one character for matching: upper to lower, everything else unchanged.
char foldChar(char c);

}  // namespace xkcd

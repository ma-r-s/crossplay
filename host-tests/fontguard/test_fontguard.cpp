// Does FontDecompressor index its group array out of bounds, and can a font
// make it do so?
//
// getGroupIndex() has two ways of naming a group that does not exist:
//
//   1. the not-found SENTINEL. The contiguous-group scan returns
//      fontData->groupCount when no group claims the glyph -- one past the last
//      valid index, by construction.
//   2. a FILE-SUPPLIED index, from the glyphToGroup fast path: a uint16_t taken
//      from the font's own data. getGroupIndex now clamps it to the same
//      sentinel; these tests hold that clamp in place.
//
// getBitmap() checks for both (`groupIndex >= fontData->groupCount` -> nullptr).
// prewarmCache() did not: it stored the result straight into neededGroups and
// then indexed fontData->groups with it, reading an EpdFontGroup that is not
// there and handing its uncompressedSize to malloc and its firstGlyphIndex to
// the glyph array.
//
// NEITHER IS REACHABLE WITH A FONT THIS FIRMWARE CAN LOAD TODAY, and the point
// of the suite is to keep it that way cheaply. Every built-in font takes the
// contiguous path with groups that tile the glyph array exactly; no generator in
// the tree emits glyphToGroup at all, so that path is anticipated rather than
// live; and SD-card fonts zero groups/groupCount/glyphToGroup, so they never
// enter the decompressor. What guards the sentinel today is therefore a property
// of the font generator's output that no build step and no CI job checks.
//
// Reading the code cannot settle what the decoder DOES with such a font, so this
// suite BUILDS them -- and puts a GUARD PAGE immediately after every array a
// malformed font would make the decoder walk off. The arrays sit flush against a
// PROT_NONE page, so a read of groups[groupCount] is not a value that happens to
// follow in memory: it is a SIGSEGV at the exact offending byte.
//
// That instrument, rather than AddressSanitizer, for two reasons. ASan's
// runtime hangs in its own init (get_dyld_hdr) on this machine's Apple clang 17
// / Darwin 27 pairing, so the suite would never start; and mmap + mprotect
// needs no sanitizer runtime at all, so this reads the same under CI's GCC.
//
// What the guard buys, stated exactly rather than generously: with the fix
// reverted and the guard moved away so the stray reads land in ordinary zeroed
// pages, `corrupt-glyphtogroup` and `glyphtogroup-equals-groupcount` PASS
// vacuously -- the decoder walks off the array and nothing notices. The other
// two, `sentinel` and `all-ungrouped`, fail either way on their missed counts.
// So the guard is what makes half these cases discriminate at all, and it is
// the half a read-only review was least able to settle.
//
//   host-tests/fontguard/run.sh

#include <sys/mman.h>
#include <unistd.h>

// glibc spells it MAP_ANONYMOUS and only exposes the BSD MAP_ANON alias under
// _DEFAULT_SOURCE, which -std=c++17 (rather than gnu++17) can switch off. CI is
// ubuntu-latest, so take whichever the platform actually has.
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "EpdFontData.h"
#include "FontDecompressor.h"

// An array of exactly `count` T, positioned so its last byte is the last byte
// of a writable page, with a PROT_NONE page immediately after it. Any read at
// index >= count touches the guard and traps.
template <typename T>
class GuardedArray {
 public:
  // Several guard pages, not one. A stray index is a font's number, not a
  // neighbouring byte: groups[99] on a 4KB-page CI box is already 1980 bytes
  // past the end, and a slightly wilder value would clear a single page and land
  // in whatever the allocator mapped next -- reading as a pass. Cheap insurance,
  // since the mapping is never touched.
  static constexpr size_t kGuardPages = 8;

  explicit GuardedArray(size_t count) : count_(count) {
    const size_t pageSize = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    const size_t bytes = count * sizeof(T);
    const size_t dataPages = (bytes + pageSize - 1) / pageSize;
    mapLen_ = (dataPages + kGuardPages) * pageSize;
    base_ = static_cast<char*>(mmap(nullptr, mapLen_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (base_ == MAP_FAILED) {
      perror("mmap");
      abort();
    }
    char* const guard = base_ + dataPages * pageSize;
    if (mprotect(guard, kGuardPages * pageSize, PROT_NONE) != 0) {
      perror("mprotect");
      abort();
    }
    // Flush against the guard. sizeof(T) is a multiple of alignof(T) and the
    // page size is a multiple of both, so this stays correctly aligned.
    //
    // count == 0 puts ptr_ ON the guard, so every subscript traps. That is the
    // right answer for a zero-length array, but a fixture that hit it would trap
    // inside itself and read exactly like the bug under test, so no test builds
    // one: buildFont's smallest group carries one member.
    ptr_ = reinterpret_cast<T*>(guard - bytes);
    for (size_t i = 0; i < count; i++) new (&ptr_[i]) T();
  }
  ~GuardedArray() {
    if (base_ != MAP_FAILED) munmap(base_, mapLen_);
  }
  GuardedArray(const GuardedArray&) = delete;
  GuardedArray& operator=(const GuardedArray&) = delete;

  T* get() const { return ptr_; }
  T& operator[](size_t i) const { return ptr_[i]; }
  size_t count() const { return count_; }

 private:
  char* base_ = static_cast<char*>(MAP_FAILED);
  size_t mapLen_ = 0;
  T* ptr_ = nullptr;
  size_t count_ = 0;
};

// A trap here is the finding, not a crash to debug: say so in the log, because
// check.sh surfaces lines matching FAIL and nothing else.
static void onFault(int) {
  static const char msg[] =
      "FAIL fontguard  SIGSEGV on a guard page: the decoder read past the end of a font array "
      "(groups[] or glyphToGroup[]). That is the out-of-bounds read this suite exists to catch.\n";
  ssize_t n = write(STDERR_FILENO, msg, sizeof(msg) - 1);
  (void)n;
  _exit(1);
}

static int checks = 0;
static int failed = 0;

static void ok(const char* what) {
  checks++;
  (void)what;
}
static void bad(const char* what, const std::string& detail) {
  checks++;
  failed++;
  printf("FAIL fontguard  %s: %s\n", what, detail.c_str());
}
static void expect(bool cond, const char* what, const std::string& detail = "") {
  if (cond)
    ok(what);
  else
    bad(what, detail);
}

// --- Fixture ---------------------------------------------------------------
//
// Glyphs are 4x2 at 2bpp: one byte per row, so the byte-aligned form a group
// decompresses to and the packed form getBitmap() returns are the same two
// bytes. That keeps the assertions about WHICH bytes came back readable, and
// compactSingleGlyph's memcpy fast path (width % 4 == 0) is the one real fonts
// hit most anyway.
static constexpr uint8_t GLYPH_W = 4;
static constexpr uint8_t GLYPH_H = 2;
static constexpr uint16_t GLYPH_BYTES = 2;
static constexpr uint32_t FIRST_CP = 'A';

// A stored (BTYPE=00) DEFLATE block. Uncompressed, but a real deflate stream:
// uzlib decodes it through exactly the same path as a font's Huffman-coded
// groups, and building one needs no compressor in the test.
static std::vector<uint8_t> deflateStored(const std::vector<uint8_t>& raw) {
  std::vector<uint8_t> out;
  out.push_back(0x01);  // BFINAL = 1, BTYPE = 00
  const uint16_t len = static_cast<uint16_t>(raw.size());
  const uint16_t nlen = static_cast<uint16_t>(~len);
  out.push_back(len & 0xFF);
  out.push_back((len >> 8) & 0xFF);
  out.push_back(nlen & 0xFF);
  out.push_back((nlen >> 8) & 0xFF);
  out.insert(out.end(), raw.begin(), raw.end());
  return out;
}

static uint8_t glyphByte0(uint32_t i) { return static_cast<uint8_t>(0x10 + i); }
static uint8_t glyphByte1(uint32_t i) { return static_cast<uint8_t>(0x20 + i); }

struct Fixture {
  std::vector<EpdGlyph> glyphs;
  std::vector<EpdUnicodeInterval> intervals;
  std::vector<uint8_t> bitmap;
  // Guard-paged, NOT vectors: these are the arrays a malformed font walks off
  // the end of, and the guard is what turns that from a plausible-looking value
  // into a trap. glyphToGroup is guarded too: the decoder walks it to a length
  // it derives from the interval table rather than from the array itself, which
  // is a second unchecked assumption worth pinning here.
  std::unique_ptr<GuardedArray<EpdFontGroup>> groups;
  std::unique_ptr<GuardedArray<uint16_t>> glyphToGroup;
  EpdFontData data{};

  const EpdFontData* operator->() { return &data; }
};

// groupMembers[g] lists, in layout order, the glyph indices group g holds.
// A glyph named by no group is exactly the "belongs to no group" case.
// mapped == true fills glyphToGroup (the O(1) path); false leaves it null so
// getGroupIndex() falls back to the contiguous scan.
static std::unique_ptr<Fixture> buildFont(uint32_t glyphCount, const std::vector<std::vector<uint32_t>>& groupMembers,
                                          bool mapped) {
  auto f = std::make_unique<Fixture>();

  f->glyphs.resize(glyphCount);
  for (uint32_t i = 0; i < glyphCount; i++) {
    f->glyphs[i] = EpdGlyph{GLYPH_W, GLYPH_H, static_cast<uint16_t>(GLYPH_W << 4), 0, 0, GLYPH_BYTES, 0};
  }

  f->intervals.push_back(EpdUnicodeInterval{FIRST_CP, FIRST_CP + glyphCount - 1, 0});

  const size_t groupCount = groupMembers.size();
  f->groups = std::make_unique<GuardedArray<EpdFontGroup>>(groupCount);
  for (size_t g = 0; g < groupCount; g++) {
    std::vector<uint8_t> alignedGroup;
    for (uint32_t gi : groupMembers[g]) {
      alignedGroup.push_back(glyphByte0(gi));
      alignedGroup.push_back(glyphByte1(gi));
    }
    const std::vector<uint8_t> compressed = deflateStored(alignedGroup);
    (*f->groups)[g] =
        EpdFontGroup{static_cast<uint32_t>(f->bitmap.size()), static_cast<uint32_t>(compressed.size()),
                     static_cast<uint32_t>(alignedGroup.size()), static_cast<uint16_t>(groupMembers[g].size()),
                     groupMembers[g].empty() ? 0u : groupMembers[g].front()};
    f->bitmap.insert(f->bitmap.end(), compressed.begin(), compressed.end());
  }

  if (mapped) {
    f->glyphToGroup = std::make_unique<GuardedArray<uint16_t>>(glyphCount);
    for (uint32_t i = 0; i < glyphCount; i++) (*f->glyphToGroup)[i] = 0;
    for (size_t g = 0; g < groupCount; g++)
      for (uint32_t gi : groupMembers[g]) (*f->glyphToGroup)[gi] = static_cast<uint16_t>(g);
  }

  f->data.bitmap = f->bitmap.data();
  f->data.glyph = f->glyphs.data();
  f->data.intervals = f->intervals.data();
  f->data.intervalCount = static_cast<uint32_t>(f->intervals.size());
  f->data.advanceY = GLYPH_H;
  f->data.is2Bit = true;
  f->data.groups = f->groups->get();
  f->data.groupCount = static_cast<uint16_t>(groupCount);
  f->data.glyphToGroup = mapped ? f->glyphToGroup->get() : nullptr;
  return f;
}

static std::string textFor(uint32_t glyphCount) {
  std::string s;
  for (uint32_t i = 0; i < glyphCount; i++) s.push_back(static_cast<char>(FIRST_CP + i));
  return s;
}

// Did glyph `i` come back with the bytes its group actually holds?
static void expectGlyph(FontDecompressor& fdc, Fixture& f, uint32_t i, const char* what) {
  const uint8_t* bm = fdc.getBitmap(&f.data, &f.glyphs[i], i);
  if (!bm) {
    bad(what, "glyph " + std::to_string(i) + ": getBitmap returned nullptr");
    return;
  }
  if (bm[0] != glyphByte0(i) || bm[1] != glyphByte1(i)) {
    char buf[128];
    snprintf(buf, sizeof(buf), "glyph %u: got %02X %02X, want %02X %02X", i, bm[0], bm[1], glyphByte0(i),
             glyphByte1(i));
    bad(what, buf);
    return;
  }
  ok(what);
}

static void expectNoGlyph(FontDecompressor& fdc, Fixture& f, uint32_t i, const char* what) {
  const uint8_t* bm = fdc.getBitmap(&f.data, &f.glyphs[i], i);
  expect(bm == nullptr, what, "glyph " + std::to_string(i) + ": expected nullptr for an ungrouped glyph");
}

// --- Tests -----------------------------------------------------------------

// The control. A font whose groups cover every glyph must keep working, byte
// for byte -- a bounds check that rejected a VALID glyph would show up here as
// text quietly going missing, which is worse than the bug being fixed.
static void testWellFormedContiguous() {
  auto f = buildFont(4, {{0, 1}, {2, 3}}, /*mapped=*/false);
  FontDecompressor fdc;
  fdc.init();
  const int missed = fdc.prewarmCache(&f->data, textFor(4).c_str());
  expect(missed == 0, "contiguous/well-formed prewarm reports no misses", "missed=" + std::to_string(missed));
  for (uint32_t i = 0; i < 4; i++) expectGlyph(fdc, *f, i, "contiguous/well-formed glyph round-trips");
}

// Same, through the glyphToGroup fast path.
static void testWellFormedMapped() {
  auto f = buildFont(4, {{0, 1}, {2, 3}}, /*mapped=*/true);
  FontDecompressor fdc;
  fdc.init();
  const int missed = fdc.prewarmCache(&f->data, textFor(4).c_str());
  expect(missed == 0, "mapped/well-formed prewarm reports no misses", "missed=" + std::to_string(missed));
  for (uint32_t i = 0; i < 4; i++) expectGlyph(fdc, *f, i, "mapped/well-formed glyph round-trips");
}

// Case 1: the SENTINEL. Glyphs 2 and 3 belong to no group, so the contiguous
// scan returns groupCount (== 1) for them. Unfixed, prewarm stores that 1 in
// neededGroups and reads groups[1] out of a one-element array.
static void testSentinelGlyphInNoGroup() {
  auto f = buildFont(4, {{0, 1}}, /*mapped=*/false);
  FontDecompressor fdc;
  fdc.init();
  const int missed = fdc.prewarmCache(&f->data, textFor(4).c_str());
  expect(missed == 2, "sentinel: prewarm reports the two ungrouped glyphs as missed",
         "missed=" + std::to_string(missed));
  expectGlyph(fdc, *f, 0, "sentinel: grouped glyph still round-trips");
  expectGlyph(fdc, *f, 1, "sentinel: grouped glyph still round-trips");
  expectNoGlyph(fdc, *f, 2, "sentinel: ungrouped glyph declines rather than reading OOB");
  expectNoGlyph(fdc, *f, 3, "sentinel: ungrouped glyph declines rather than reading OOB");
}

// Case 2: a FILE-SUPPLIED index. groupCount is 2, but the font says glyph 3
// lives in group 99. Nothing validates that, so unfixed prewarm reads
// groups[99] out of a two-element array -- then mallocs its uncompressedSize.
static void testCorruptGlyphToGroupIndex() {
  auto f = buildFont(4, {{0, 1}, {2, 3}}, /*mapped=*/true);
  (*f->glyphToGroup)[3] = 99;
  FontDecompressor fdc;
  fdc.init();
  const int missed = fdc.prewarmCache(&f->data, textFor(4).c_str());
  expect(missed == 1, "corrupt glyphToGroup: prewarm reports the bad glyph as missed",
         "missed=" + std::to_string(missed));
  for (uint32_t i = 0; i < 3; i++) expectGlyph(fdc, *f, i, "corrupt glyphToGroup: sound glyphs still round-trip");
  expectNoGlyph(fdc, *f, 3, "corrupt glyphToGroup: bad glyph declines rather than reading OOB");
}

// The boundary value specifically: glyphToGroup naming exactly groupCount is
// the off-by-one a `> groupCount` check would let through.
static void testGlyphToGroupExactlyGroupCount() {
  auto f = buildFont(4, {{0, 1}, {2, 3}}, /*mapped=*/true);
  (*f->glyphToGroup)[3] = 2;  // == groupCount
  FontDecompressor fdc;
  fdc.init();
  const int missed = fdc.prewarmCache(&f->data, textFor(4).c_str());
  expect(missed == 1, "glyphToGroup == groupCount is rejected", "missed=" + std::to_string(missed));
  expectNoGlyph(fdc, *f, 3, "glyphToGroup == groupCount declines rather than reading OOB");
}

// getBitmap's own guard, with no prewarm at all. This path was already correct;
// the assertion is here so a later edit cannot quietly drop it and leave the
// fix covering only half the callers.
static void testGetBitmapGuardsWithoutPrewarm() {
  auto f = buildFont(4, {{0, 1}}, /*mapped=*/false);
  FontDecompressor fdc;
  fdc.init();
  expectGlyph(fdc, *f, 0, "no-prewarm: grouped glyph round-trips");
  expectNoGlyph(fdc, *f, 2, "no-prewarm: ungrouped glyph returns nullptr");

  auto m = buildFont(4, {{0, 1}, {2, 3}}, /*mapped=*/true);
  (*m->glyphToGroup)[3] = 99;
  FontDecompressor fdc2;
  fdc2.init();
  expectGlyph(fdc2, *m, 0, "no-prewarm: sound mapped glyph round-trips");
  expectNoGlyph(fdc2, *m, 3, "no-prewarm: corrupt mapped glyph returns nullptr");
}

// A font where EVERY glyph is ungrouped: prewarm must come back empty-handed
// rather than allocating against a group that is not there.
static void testAllGlyphsUngrouped() {
  auto f = buildFont(2, {{}}, /*mapped=*/false);
  FontDecompressor fdc;
  fdc.init();
  const int missed = fdc.prewarmCache(&f->data, textFor(2).c_str());
  expect(missed == 2, "all-ungrouped: every glyph reported missed", "missed=" + std::to_string(missed));
  expectNoGlyph(fdc, *f, 0, "all-ungrouped: glyph declines rather than reading OOB");
  expectNoGlyph(fdc, *f, 1, "all-ungrouped: glyph declines rather than reading OOB");
}

// The three literals that had to agree by hand: neededGroups[128],
// groupAlignedTracker[128] and the `groupCount < 128` cap. Raising one alone
// turned a bounds check into an out-of-bounds WRITE. They are one constant now,
// and groupCount is a uint8_t, so the constant also has to fit in one.
static void testGroupCapIsOneConstant() {
  expect(FontDecompressor::MAX_PAGE_GROUPS > 0 && FontDecompressor::MAX_PAGE_GROUPS <= 255,
         "MAX_PAGE_GROUPS fits the uint8_t counter that indexes it",
         "MAX_PAGE_GROUPS=" + std::to_string(FontDecompressor::MAX_PAGE_GROUPS));

  // More groups than the cap, all of them sound: the excess must fall back to
  // the hot-group path, not overrun neededGroups.
  const uint32_t n = FontDecompressor::MAX_PAGE_GROUPS + 8;
  std::vector<std::vector<uint32_t>> members;
  for (uint32_t i = 0; i < n; i++) members.push_back({i});
  auto f = buildFont(n, members, /*mapped=*/true);
  FontDecompressor fdc;
  fdc.init();
  std::string text;
  for (uint32_t i = 0; i < n; i++) {
    const uint32_t cp = FIRST_CP + i;
    if (cp < 0x80) {
      text.push_back(static_cast<char>(cp));
    } else {
      text.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      text.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }
  fdc.prewarmCache(&f->data, text.c_str());
  // Every glyph is genuinely in a group, so every one must still render --
  // whether it came from the page buffer or the hot-group fallback.
  for (uint32_t i = 0; i < n; i++) expectGlyph(fdc, *f, i, "over-cap font: every sound glyph still renders");
}

struct NamedTest {
  const char* name;
  void (*fn)();
};

static const NamedTest kTests[] = {
    {"well-formed-contiguous", testWellFormedContiguous},
    {"well-formed-mapped", testWellFormedMapped},
    {"sentinel", testSentinelGlyphInNoGroup},
    {"corrupt-glyphtogroup", testCorruptGlyphToGroupIndex},
    {"glyphtogroup-equals-groupcount", testGlyphToGroupExactlyGroupCount},
    {"getbitmap-guard", testGetBitmapGuardsWithoutPrewarm},
    {"all-ungrouped", testAllGlyphsUngrouped},
    {"group-cap", testGroupCapIsOneConstant},
};

// FONTGUARD_ONLY=<name> runs one test. This exists so the guard can be
// FALSIFIED case by case: neuter the bounds check in prewarmCache, run each
// malformed-font test on its own, and watch it trap. Running the whole suite
// cannot show that, because the first trap takes the process down with it and
// every later case goes unobserved -- which would leave half the fix resting on
// an assumption rather than on a result.
int main() {
  signal(SIGSEGV, onFault);
  signal(SIGBUS, onFault);

  const char* only = getenv("FONTGUARD_ONLY");
  bool ran = false;
  for (const NamedTest& t : kTests) {
    if (only && strcmp(only, t.name) != 0) continue;
    t.fn();
    ran = true;
  }
  if (!ran) {
    printf("FAIL fontguard  FONTGUARD_ONLY=%s matches no test\n", only ? only : "");
    return 1;
  }

  printf("fontguard: %d checks, %d failed\n", checks, failed);
  return failed == 0 ? 0 : 1;
}

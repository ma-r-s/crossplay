// How long a prewarmed glyph cache lives, and what that rules out.
//
// The reader warmed the NEXT page's glyphs while the device sat idle, so a page
// turn would not have to. The simulator showed it prewarming each page twice
// with byte-for-byte identical results, 780ms apart -- once for the idle pass
// and once for the render that immediately followed it. The reason is here:
// FontCacheManager::PrewarmScope clears the cache when it is constructed AND
// when it is destroyed, so glyphs warmed inside a scope cannot outlive it, and
// the next render's own scope would drop them at its constructor anyway.
//
// Two independent reasons for the same outcome is exactly the sort of fact that
// should fail a test rather than be re-derived by the next person who tries to
// keep a cache across a page turn. That is what this suite is.
//
// The FontCacheManager under test is real. Only SdCardFont is faked, and only
// the five methods FontCacheManager reaches for, so the lifetime being asserted
// is the shipping one.

#include <FontCacheManager.h>
#include <SdCardFont.h>

#include <cstdio>
#include <map>
#include <string>

// -- the fake ---------------------------------------------------------------
//
// Real definitions of the SdCardFont methods FontCacheManager calls, linked
// instead of SdCardFont.cpp (which needs the SD card). Every call is counted.
namespace {
struct Counts {
  int clears = 0;
  int prewarms = 0;
  int resets = 0;
  std::string lastText;
  uint8_t lastStyleMask = 0;
};
Counts counts;
}  // namespace

void SdCardFont::clearCache() { counts.clears++; }
void SdCardFont::releaseResidentCaches() { counts.clears++; }
void SdCardFont::resetStats() { counts.resets++; }
void SdCardFont::logStats(const char*) {}
// Upstream's FontCacheManager::resolveScanStyle() (2026-09-04 sync) asks the
// SD font to fold a requested style down to one it actually HAS, so a page
// that draws bold in a regular-only face does not pay for a second prewarm
// pass. This fake is a single-face font -- that is the premise of the
// "one font is one prewarm" case below -- so every style folds to regular.
uint8_t SdCardFont::resolveStyle(uint8_t) const { return 0; }
int SdCardFont::prewarm(const char* utf8Text, const uint8_t styleMask, bool, bool) {
  counts.prewarms++;
  counts.lastText = utf8Text ? utf8Text : "";
  counts.lastStyleMask = styleMask;
  return 0;
}
SdCardFont::~SdCardFont() = default;

static int checks = 0;
static int failed = 0;

static void eq(const int got, const int want, const char* what) {
  checks++;
  if (got != want) {
    failed++;
    std::printf("FAIL prewarmscope  %s\n      want %d\n      got  %d\n", what, want, got);
  }
}

static void eqs(const std::string& got, const std::string& want, const char* what) {
  checks++;
  if (got != want) {
    failed++;
    std::printf("FAIL prewarmscope  %s\n      want \"%s\"\n      got  \"%s\"\n", what, want.c_str(), got.c_str());
  }
}

int main() {
  const std::map<int, EpdFontFamily> noBuiltins;
  SdCardFont font;
  const std::map<int, SdCardFont*> sdFonts{{7, &font}};
  FontCacheManager fcm(noBuiltins, sdFonts);

  // -- a scope starts from an empty cache -----------------------------------
  {
    counts = Counts{};
    auto scope = fcm.createPrewarmScope();
    eq(counts.clears, 1, "opening a scope drops whatever was cached before it");

    fcm.recordText("hello", 7, EpdFontFamily::REGULAR);
    eq(counts.prewarms, 0, "scanning does not touch the card");

    scope.endScanAndPrewarm();
    eq(counts.prewarms, 1, "ending the scan prewarms once for the font it saw");
    // Upstream (2026-09-04) hands prewarm the DEDUPLICATED, sorted character
    // set rather than the raw run, so the same glyph is not decompressed twice
    // on a page that uses it twice. "hello" scans as its four distinct chars.
    eqs(counts.lastText, "ehlo", "and prewarms exactly the distinct characters scanned");
    eq(counts.clears, 1, "prewarming does not clear what it just loaded");
  }
  // -- and does not survive it ----------------------------------------------
  //
  // THIS is why warming the next page ahead of time could not work. The scope
  // that did the warming ends, and the glyphs go with it.
  eq(counts.clears, 2, "closing the scope discards the cache it just warmed");

  // -- nothing carries from one render to the next --------------------------
  //
  // The second reason. Even a cache that somehow outlived its scope would be
  // dropped by the next render's scope before it drew anything.
  {
    counts = Counts{};
    {
      auto first = fcm.createPrewarmScope();
      fcm.recordText("page one", 7, EpdFontFamily::REGULAR);
      first.endScanAndPrewarm();
    }
    eq(counts.clears, 2, "one render's scope clears at both ends");
    auto second = fcm.createPrewarmScope();
    eq(counts.clears, 3, "and the next render's scope clears again before it scans");
  }

  // -- a page mixes fonts, and each one gets prewarmed ----------------------
  //
  // The status bar draws in the UI font while the body draws in the reader's.
  // Batching only the first font id seen would leave the other to fault in
  // glyph by glyph during the real draw pass, which is the slow path this
  // whole mechanism exists to avoid.
  {
    counts = Counts{};
    SdCardFont second;
    const std::map<int, SdCardFont*> two{{7, &font}, {8, &second}};
    FontCacheManager mixed(noBuiltins, two);
    auto scope = mixed.createPrewarmScope();
    mixed.recordText("body", 7, EpdFontFamily::REGULAR);
    mixed.recordText("bar", 8, EpdFontFamily::REGULAR);
    scope.endScanAndPrewarm();
    eq(counts.prewarms, 2, "a page using two fonts prewarms both");
  }

  // -- styles accumulate into one pass per font -----------------------------
  //
  // Bold and italic runs on one page are ONE prewarm for a face that has only
  // one style: an SD font pays per pass, not per glyph. Upstream buckets the
  // scan by RESOLVED style (resolveScanStyle -> SdCardFont::resolveStyle), so
  // a style the face does not have folds into the one it does instead of
  // costing a second pass. A face that really has both would now get one pass
  // each, which is the point: you pay only for styles that exist.
  {
    counts = Counts{};
    auto scope = fcm.createPrewarmScope();
    fcm.recordText("plain ", 7, EpdFontFamily::REGULAR);
    fcm.recordText("bold", 7, EpdFontFamily::BOLD);
    scope.endScanAndPrewarm();
    eq(counts.prewarms, 1, "one font is one prewarm however many styles it draws");
    eq(counts.lastStyleMask, 0x01, "and the pass asks only for the style the face has");
    eqs(counts.lastText, " abdilnop", "over the distinct characters of all of it");
  }

  std::printf("prewarmscope: %d checks, %d failed\n", checks, failed);
  return failed == 0 ? 0 : 1;
}

// Text on its way to the device fonts, and what has to be taken out of it.
//
// The device fonts have no glyph for a newline. Text that arrives as markup --
// an OPDS feed, EPUB body copy -- carries the line breaks and indentation of
// the document it came in, and those reach the rasteriser as codepoint 10 and
// are logged as a missing glyph. Nothing looked wrong on screen, so it sat
// there until a screenshot gate started failing on the log line.
//
// The tests that matter here are the two failure modes, not the happy path:
// leaving whitespace in, and deleting it instead of collapsing it.

#include <BookmarkUtil.h>
#include <Utf8.h>

#include <cstdio>
#include <string>

static int checks = 0;
static int failed = 0;

static void eq(const std::string& got, const std::string& want, const char* what) {
  checks++;
  if (got != want) {
    failed++;
    std::printf("FAIL screentext  %s\n      want \"%s\"\n      got  \"%s\"\n", what, want.c_str(), got.c_str());
  }
}

int main() {
  // -- what a real feed actually sends -------------------------------------
  //
  // Gutenberg pretty-prints its Atom, so every title element looks like this.
  // This exact shape is what logged "No glyph for codepoint 10".
  eq(utf8CollapseWhitespace("\n      Moby Dick; Or, The Whale\n    "), "Moby Dick; Or, The Whale",
     "a pretty-printed feed title loses its markup whitespace");

  // -- collapse, never delete ----------------------------------------------
  //
  // The bug in the code this replaced. Deleting the newline joins the words on
  // either side of it, and wrapped body text puts a newline between two words
  // far more often than at a natural break.
  eq(utf8CollapseWhitespace("call me\nIshmael"), "call me Ishmael", "a line break between two words becomes a space");
  eq(utf8CollapseWhitespace("a\n\n\nb"), "a b", "a run of newlines becomes ONE space, not three and not none");
  eq(utf8CollapseWhitespace("a \t\r\n b"), "a b", "every ASCII whitespace kind collapses, not just the space");

  // -- the ends -------------------------------------------------------------
  eq(utf8CollapseWhitespace("   lead"), "lead", "leading whitespace is dropped, not turned into a space");
  eq(utf8CollapseWhitespace("trail   "), "trail", "trailing whitespace is dropped");
  eq(utf8CollapseWhitespace("  \n\t  "), "", "whitespace only collapses to empty, not to a single space");
  eq(utf8CollapseWhitespace(""), "", "empty stays empty");

  // -- it must not touch anything else --------------------------------------
  eq(utf8CollapseWhitespace("Already clean."), "Already clean.", "clean text is returned unchanged");
  // UTF-8 safety is by construction (every ASCII whitespace byte is < 0x80, so
  // none can appear inside a multi-byte sequence) and this is the check that
  // says so: a continuation byte must never be mistaken for whitespace.
  eq(utf8CollapseWhitespace("Sea Salt\n& Paper \xE2\x80\x94 d\xC3\xA9j\xC3\xA0"), "Sea Salt & Paper \xE2\x80\x94 d\xC3\xA9j\xC3\xA0",
     "multi-byte characters survive byte-for-byte");

  // -- the bookmark summary, which is the same text one screen over ---------
  eq(BookmarkUtil::sanitizeBookmarkSummary("call me\nIshmael"), "call me Ishmael",
     "bookmark summaries stop joining words across a line break");
  eq(BookmarkUtil::sanitizeBookmarkSummary("\n  wrapped\n  body copy\n"), "wrapped body copy",
     "a bookmark summary lifted out of wrapped body text reads as one line");
  checks++;
  if (BookmarkUtil::sanitizeBookmarkSummary(std::string(200, 'x')).size() != 72) {
    failed++;
    std::printf("FAIL screentext  the 72-character cap on bookmark summaries is gone\n");
  }

  std::printf("%d checks, %d failed\n", checks, failed);
  return failed == 0 ? 0 : 1;
}

// What the app cuts can actually draw, and what utf8FoldTypography has to do
// about it.
//
// A glyph a font does not have draws as NOTHING. EpdFont::getGlyph returns
// nullptr, GfxRenderer skips it without advancing the pen, and there is no box
// and no fallback -- so a real Hacker News headline arrives on the panel with
// holes in it. The renderer does log it ("No glyph for codepoint N"), and
// scripts_local/sim-shot.sh even fails on that line, but only for the screens a
// scripted run reaches with real data, which is why a headline out of a live
// feed was never one of them. That
// is why this suite compiles the FONT DATA rather than describing it: a claim
// about which cut carries which codepoint is exactly the kind of claim that
// rots, and the only witness that cannot be talked round is the interval table
// itself.
//
// Three questions, and the last is the one a review cannot settle by reading:
//
//   1. Is every replacement drawable in every cut? A fold that swaps a hole for
//      a hole is worse than none, because it looks fixed.
//   2. Is every source codepoint genuinely missing somewhere? A row that folds
//      something the cuts can already draw is a silent downgrade.
//   3. What is LEFT? The gap this change does not close is asserted here on
//      purpose, so it cannot be mistaken later for an oversight and cannot be
//      quietly widened.

#include <EpdFont.h>
#include <Utf8.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "fonts/instrument_10.h"
#include "fonts/instrument_13.h"
#include "fonts/instrument_24.h"
#include "fonts/reading_serif_11.h"
#include "fonts/reading_serif_14.h"
#include "fonts/reading_serif_bold_12.h"
#include "fonts/reading_serif_bold_16.h"
#include "fonts/toybox_10.h"
#include "fonts/toybox_14.h"
#include "fonts/toybox_20.h"
#include "fonts/toybox_30.h"
#include "fonts/toybox_44.h"
#include "fonts/toybox_64.h"

// The system UI and the EPUB reader draw in these instead of the Toybox cuts:
// src/components/UIScale.h binds UI_12/UI_10 to the Ubuntu faces, and
// CrossPointSettings::getReaderFontId() picks a Noto for the page. They are
// here to be MEASURED, because this change's scope rests on what they carry.
#include "builtinFonts/notosans_8_regular.h"
#include "builtinFonts/notoserif_16_regular.h"
#include "builtinFonts/ubuntu_10_regular.h"
#include "builtinFonts/ubuntu_12_regular.h"

static int checks = 0;
static int failed = 0;

static void ok(const bool condition, const char* what) {
  checks++;
  if (!condition) {
    failed++;
    std::printf("FAIL typefold  %s\n", what);
  }
}

static void eq(const std::string& got, const std::string& want, const char* what) {
  checks++;
  if (got != want) {
    failed++;
    std::printf("FAIL typefold  %s\n      want \"%s\"\n      got  \"%s\"\n", what, want.c_str(), got.c_str());
  }
}

namespace {

// One hand-written bit per cut, and only one: does external text -- a headline,
// a comment, a peer's name, an imported puzzle -- ever reach the panel in it?
// That is a fact about where each app calls toybox::makeTarget with a Faces set
// (ToyboxTheme.h) and what it puts in the three slots, so no font can answer it.
// Whether a cut reaches PAST ASCII is NOT written here: it is measured, because
// a written-down claim about font coverage is the exact thing this suite exists
// to stop anybody trusting.
struct Cut {
  const char* name;
  const EpdFontData* data;
  bool carriesExternalText;
};

const Cut kCuts[] = {
    {"toybox_10", &toybox_10, true},                          // HN / Instapaper small slot
    {"toybox_14", &toybox_14, true},                          // xkcd / Trivia small slot
    {"toybox_20", &toybox_20, true},                          // link peer names, Study menus, shelf
    {"toybox_30", &toybox_30, true},                          // xkcd header title
    {"toybox_44", &toybox_44, false},                         // Forehead cards
    {"toybox_64", &toybox_64, false},                         // Forehead cards
    {"instrument_10", &instrument_10, true},                  // Connections imported puzzles
    {"instrument_13", &instrument_13, true},                  // Connections imported puzzles
    {"instrument_24", &instrument_24, false},                 // Connections chrome
    {"reading_serif_11", &reading_serif_11, true},            // HN / Instapaper / Trivia prose
    {"reading_serif_14", &reading_serif_14, true},            // HN / Instapaper / Trivia prose
    {"reading_serif_bold_12", &reading_serif_bold_12, true},  // reader chrome
    {"reading_serif_bold_16", &reading_serif_bold_16, true},  // reader chrome
};
constexpr size_t kCutCount = sizeof(kCuts) / sizeof(kCuts[0]);

// The one question that matters, asked the way drawText asks it. hasCodepoint()
// reports coverage without the replacement-glyph fallback, which is what "will
// this leave a hole" means: these cuts carry no U+FFFD either, so getGlyph()
// answers nullptr for anything uncovered.
bool draws(const EpdFontData* data, const uint32_t cp) { return EpdFont(data).hasCodepoint(cp); }

// Measured, not declared: a cut "reaches past ASCII" if it can draw anything
// above U+007E at all. That is what separates a cut whose failure to draw a
// character is evidence (it carries other characters, so it chose not to carry
// this one) from a cut subset to ASCII, which lacks EVERYTHING above U+007E and
// would therefore justify any row at all.
bool reachesPastAscii(const EpdFontData* data) {
  for (uint32_t cp = 0x80; cp <= 0xFFFF; ++cp) {
    if (draws(data, cp)) return true;
  }
  return false;
}

// Every codepoint in a UTF-8 string that no glyph exists for in `data`.
std::vector<uint32_t> holes(const EpdFontData* data, const std::string& text) {
  std::vector<uint32_t> out;
  const unsigned char* p = reinterpret_cast<const unsigned char*>(text.c_str());
  while (*p != 0) {
    const uint32_t cp = utf8NextCodepoint(&p);
    if (cp == 0) break;
    if (!draws(data, cp)) out.push_back(cp);
  }
  return out;
}

int widthOf(const EpdFontData* data, const std::string& text) {
  int w = 0, h = 0;
  EpdFont(data).getTextDimensions(text.c_str(), &w, &h);
  return w;
}

// Live Hacker News, fetched from the Algolia API on 2026-09-03 and pasted in
// verbatim: fourteen of 3665 strings (titles, authors and comment bodies) that
// carried a codepoint above U+007E. A corpus somebody invents contains the
// characters they remembered; this one contains the characters HN actually
// sends, which is a different set and the reason U+20AC is in the fold table.
const char* const kLiveHackerNews[] = {
    "Three sites made 215,128 \xE2\x80\x9C"
    "best software\xE2\x80\x9D"
    " pages for AI. Perplexity cites them",
    "Launch HN: RonanRX (YC S26) \xE2\x80\x93"
    " Personalized Peptides and GLP-1s",
    "Huawei Watch D3, Built-In Air Pump Blood Pressure Sensor \xE2\x80\x93"
    " Thoughts?",
    "Show HN: Mcptunnels \xE2\x80\x93"
    " ngrok for MCP with basic OAuth",
    "The Silo \xC2\xB7"
    " interactive 3D cutaway",
    "Consciousness requires qualia. My 0,02\xE2\x82\xAC"
    ".",
    "I assume because \xE2\x80\x9C"
    "land\xE2\x80\x9D"
    " is a word Claude would choose.",
    "They\xE2\x80\x99"
    "re not dead. They\xE2\x80\x99"
    "re resting ;-)",
    "Cool! But for the sake of my eyes, I won\xE2\x80\x99"
    "t play for more than 5 minutes lol",
    "\"Understood \xE2\x80\x94"
    " I am no longer using any em dashes\"",
    "I read an article, and within seconds my personal data was shared with 229 \xE2\x80\x9C"
    "partners\xE2\x80\x9D"
    ".",
    "However, wouldn\xE2\x80\x99"
    "t you want to be able to set a higher price is someone is obviously drunk (in a "
    "bad way).",
    "The Snow/Leavis \xE2\x80\x98"
    "two cultures\xE2\x80\x99"
    " clash",
    "Reading the sources makes it way worse\xE2\x80\xA6"
    "",
};
constexpr size_t kLiveCount = sizeof(kLiveHackerNews) / sizeof(kLiveHackerNews[0]);

}  // namespace

int main() {
  size_t foldCount = 0;
  const Utf8TypographyFold* folds = utf8TypographyFolds(&foldCount);
  ok(foldCount > 0, "the fold table is not empty");

  // -- the table's own shape ------------------------------------------------
  //
  // foldFor() binary-searches it, so an entry inserted in the wrong place is
  // not a style problem: the row is simply never found and the character keeps
  // vanishing, which is indistinguishable from never having added it.
  {
    bool sorted = true;
    for (size_t i = 1; i < foldCount; ++i) {
      if (folds[i].cp <= folds[i - 1].cp) {
        sorted = false;
        std::printf("      U+%04X follows U+%04X\n", folds[i].cp, folds[i - 1].cp);
      }
    }
    ok(sorted, "the fold table is sorted, so the binary search can find every row");
  }
  {
    bool allAboveAscii = true;
    for (size_t i = 0; i < foldCount; ++i) {
      if (folds[i].cp < 0xA0) {
        allAboveAscii = false;
        std::printf("      U+%04X is at or below the ASCII range\n", folds[i].cp);
      }
    }
    // This is the proof behind the fast path, not a tidiness rule: ASCII is a
    // no-op BECAUSE no row can match it.
    ok(allAboveAscii, "no row is an ASCII codepoint, which is what makes ASCII provably untouched");
  }

  // -- a fold must not swap one hole for another ----------------------------
  for (size_t i = 0; i < foldCount; ++i) {
    // One check per ROW, including a row that folds to nothing at all -- which
    // the previous shape skipped entirely, because it counted replacement bytes
    // and an empty replacement has none.
    checks++;
    bool drawable = true;
    for (const char* c = folds[i].ascii; *c != '\0' && drawable; ++c) {
      const auto byte = static_cast<unsigned char>(*c);
      if (byte >= 0x80) {
        drawable = false;
        std::printf("FAIL typefold  U+%04X folds to \"%s\", which is not ASCII\n", folds[i].cp, folds[i].ascii);
        break;
      }
      for (size_t f = 0; f < kCutCount; ++f) {
        if (!draws(kCuts[f].data, byte)) {
          drawable = false;
          std::printf("FAIL typefold  U+%04X folds to \"%s\" but '%c' has no glyph in %s\n", folds[i].cp,
                      folds[i].ascii, *c, kCuts[f].name);
        }
      }
    }
    if (!drawable) failed++;
  }

  // -- and it must be fixing something --------------------------------------
  //
  // Asked of the fonts rather than of the author. The probe has to be one that
  // CAN fail: "missing from some cut that carries external text" is satisfied
  // by every codepoint above U+007E, because three of those cuts are subset to
  // ASCII and lack everything -- a check that green-lights any row at all is
  // not a check. So the question is asked of the RICH cuts only, the ones that
  // reach past ASCII. A codepoint all of those can draw is one the panel can
  // already show wherever showing it matters, and folding it is a downgrade
  // that looks like a fix. That is exactly the trade this change declined for
  // the accented Latin-1 letters.
  // A row is justified by EITHER of two measurements, and reporting which one
  // matters because they mean different things:
  //
  //   * a rich cut cannot draw it, so the character is a hole today; or
  //   * it puts no ink on the panel and neither does its replacement, so
  //     folding it cannot change a pixel anywhere.
  //
  // The second is what carries the space and formatting rows, and it is
  // measured rather than assumed: a glyph's `width` is its ink box, and U+00A0
  // is width 0 with a space's advance in every cut that has it.
  for (size_t i = 0; i < foldCount; ++i) {
    const bool replacementIsInkless = std::strcmp(folds[i].ascii, "") == 0 || std::strcmp(folds[i].ascii, " ") == 0;
    bool sourceIsInkless = replacementIsInkless;
    if (replacementIsInkless) {
      for (size_t f = 0; f < kCutCount; ++f) {
        const EpdGlyph* glyph = EpdFont(kCuts[f].data).getGlyph(folds[i].cp);
        if (glyph != nullptr && glyph->width != 0) sourceIsInkless = false;
      }
    }
    bool missingFromARichCut = false;
    for (size_t f = 0; f < kCutCount; ++f) {
      if (kCuts[f].carriesExternalText && reachesPastAscii(kCuts[f].data) && !draws(kCuts[f].data, folds[i].cp)) {
        missingFromARichCut = true;
        break;
      }
    }
    checks++;
    if (!sourceIsInkless && !missingFromARichCut) {
      failed++;
      std::printf(
          "FAIL typefold  U+%04X draws ink AND every rich cut has it; folding it loses a glyph the panel "
          "could have shown\n",
          folds[i].cp);
    }
  }

  // -- ASCII in, the same bytes out -----------------------------------------
  {
    std::string everyPrintable;
    for (char c = 0x20; c > 0 && c <= 0x7E; ++c) everyPrintable.push_back(c);
    eq(utf8FoldTypography(everyPrintable), everyPrintable, "every printable ASCII character survives byte for byte");
    eq(utf8FoldTypography(""), "", "empty stays empty");
    eq(utf8FoldTypography("Show HN: a 'quoted' word -- and an ellipsis..."),
       "Show HN: a 'quoted' word -- and an "
       "ellipsis...",
       "text that is already ASCII is returned unchanged, so the fold cannot widen our own labels");
    // Pure ASCII takes a shortcut that returns the input without looking at it,
    // so this is the case that actually exercises the walk: a string with one
    // high byte in it, whose ASCII must come out the other side untouched.
    eq(utf8FoldTypography("It's \"quoted\" -- caf\xC3\xA9"), "It's \"quoted\" -- caf\xC3\xA9",
       "ASCII inside a string that is not pure ASCII is untouched too");
  }

  // -- what the fold does, spelled out --------------------------------------
  eq(utf8FoldTypography("\xE2\x80\x9C"
                        "best software\xE2\x80\x9D"),
     "\"best software\"", "curly double quotes become straight ones");
  eq(utf8FoldTypography("They\xE2\x80\x99"
                        "re"),
     "They're", "a curly apostrophe becomes a straight one");
  eq(utf8FoldTypography("YC S26 \xE2\x80\x93"
                        " Peptides"),
     "YC S26 - Peptides", "an en dash becomes a hyphen");
  eq(utf8FoldTypography("Understood \xE2\x80\x94"
                        " no dashes"),
     "Understood -- no dashes", "an em dash becomes two hyphens, which is wider than the nothing it drew before");
  eq(utf8FoldTypography("way worse\xE2\x80\xA6"), "way worse...", "an ellipsis becomes three dots");
  eq(utf8FoldTypography("0,02\xE2\x82\xAC"), "0,02EUR", "the euro sign becomes EUR");
  eq(utf8FoldTypography("a\xC2\xA0"
                        "b"),
     "a b", "a no-break space becomes a plain one rather than vanishing");
  eq(utf8FoldTypography("soft\xC2\xAD"
                        "hyphen"),
     "softhyphen", "a soft hyphen is removed");
  // Worth stating because it is the surprise this suite turned up: a soft
  // hyphen is NOT invisible in the reading cuts. Noto Serif ships a real hyphen
  // glyph for it, so "soft<AD>hyphen" draws as "soft-hyphen" today -- a hyphen
  // in the middle of a word, in body text, with nothing in this firmware
  // breaking a line on it. Removing it is a second fix riding along.
  {
    const EpdGlyph* glyph = EpdFont(&reading_serif_14).getGlyph(0x00AD);
    ok(glyph != nullptr && glyph->width > 0,
       "reading_serif_14 draws U+00AD as visible ink, which is why it is removed");
  }
  eq(utf8FoldTypography("caf\xC3\xA9"), "caf\xC3\xA9",
     "an accented Latin-1 letter is NOT folded: the reading cut draws it, and 'cafe' would be a downgrade");
  eq(utf8FoldTypography("\xE6\xBC\xA2\xE5\xAD\x97"), "\xE6\xBC\xA2\xE5\xAD\x97",
     "CJK is left alone: there is no ASCII spelling, and the CJK fallback font is a different mechanism");

  // -- a NUL in the middle is a byte, not an ending -------------------------
  //
  // An article read off the card comes back from readWholeFile() as raw bytes,
  // and a walk bounded by the terminator returned everything up to the first
  // NUL and silently dropped the rest of the file. Only on the path that does
  // work, too: the pure-ASCII shortcut returns the whole string, so the bug
  // appeared exactly when the fold had something to fold.
  {
    const std::string embedded("a\0b", 3);
    eq(utf8FoldTypography(embedded), embedded, "an ASCII string keeps everything after an embedded NUL");
    const std::string mixed(
        "\xE2\x80\x9C"
        "a\0b",
        6);
    const std::string wanted("\"a\0b", 4);
    eq(utf8FoldTypography(mixed), wanted, "and so does one the fold actually walks");
    checks++;
    if (utf8FoldTypography(mixed).size() != wanted.size()) {
      failed++;
      std::printf("FAIL typefold  a NUL truncated the fold: %zu bytes out of %zu\n", utf8FoldTypography(mixed).size(),
                  wanted.size());
    }
  }

  // -- malformed input is copied, never rewritten ---------------------------
  eq(utf8FoldTypography("\xE2\x80"), "\xE2\x80", "a truncated sequence off a socket passes through byte for byte");
  eq(utf8FoldTypography("a\xFF"
                        "b"),
     "a\xFF"
     "b",
     "a stray invalid byte is copied, not replaced");
  eq(utf8FoldTypography("\xE2\x80\x9C"
                        "\xE2\x80"),
     "\"\xE2\x80", "a good character before a broken one still folds");

  // -- the live corpus, measured against the cut that carries it ------------
  //
  // reading_serif_14 is the BODY slot under readingFaces(), which is what Hacker
  // News, Instapaper and Trivia draw a headline, a comment and a clue in.
  {
    int holesBefore = 0;
    int holesAfter = 0;
    int worstGrowthPx = 0;
    bool everNarrower = false;
    for (size_t i = 0; i < kLiveCount; ++i) {
      const std::string raw(kLiveHackerNews[i]);
      const std::string folded = utf8FoldTypography(raw);
      holesBefore += static_cast<int>(holes(&reading_serif_14, raw).size());
      const std::vector<uint32_t> after = holes(&reading_serif_14, folded);
      holesAfter += static_cast<int>(after.size());
      for (const uint32_t cp : after) {
        std::printf("      U+%04X still has no glyph in reading_serif_14: \"%s\"\n", cp, folded.c_str());
      }
      const int before = widthOf(&reading_serif_14, raw);
      const int now = widthOf(&reading_serif_14, folded);
      if (now < before) everNarrower = true;
      if (now - before > worstGrowthPx) worstGrowthPx = now - before;
    }
    ok(holesBefore > 0, "the live corpus really does lose characters before the fold");
    ok(holesAfter == 0, "after folding, every character of the live corpus has a glyph in the reading cut");
    // This corpus gets wider, and it is worth saying WHY rather than
    // generalising it: every character it loses had no glyph, so it contributed
    // no advance either, and giving it one adds pixels. That is not a law about
    // the fold. U+00AD breaks it -- see below -- and an assertion that folding
    // "never narrows" would have been green here only because these fourteen
    // strings happen to contain no soft hyphen.
    ok(!everNarrower && worstGrowthPx > 0,
       "this corpus gets wider, because every character it lost was drawing nothing");
    std::printf("      live corpus: %d holes before, %d after; widest line grew %dpx in reading_serif_14\n",
                holesBefore, holesAfter, worstGrowthPx);

    // The counter-example, measured, so the paragraph above cannot be read as a
    // rule. U+00AD has real ink in this cut, so removing it makes the line
    // NARROWER -- which is the correct result and the opposite direction.
    const int withSoftHyphen = widthOf(&reading_serif_14,
                                       "soft\xC2\xAD"
                                       "hyphen");
    const int without = widthOf(&reading_serif_14, utf8FoldTypography("soft\xC2\xAD"
                                                                      "hyphen"));
    ok(without < withSoftHyphen, "removing a soft hyphen makes the line narrower, because it was drawing a hyphen");
    std::printf("      soft hyphen: %dpx before, %dpx after\n", withSoftHyphen, without);
  }

  // -- folding twice is folding once ---------------------------------------
  //
  // Not a nicety. Hacker News text passes through the fold twice on its way to
  // the card and back: hn::decodeEntities folds what it decodes, and
  // serializeSavedIndex and parseSavedIndex each fold the title again. (NOT
  // hn::sanitizeField, which runs on the URL column too and must not fold --
  // host-tests/hackernews/test_saved.cpp holds that line.) If a second pass
  // changed anything, a saved article would drift every time it was opened.
  //
  // Stated plainly because it is nearly free to satisfy: the walk never even
  // decodes an ASCII byte, so a fold output, which is ASCII wherever it changed
  // anything, cannot be folded again. This asserts the property; the two checks
  // above (no ASCII row, every replacement ASCII) are what enforce it.
  {
    bool stable = true;
    for (size_t i = 0; i < kLiveCount; ++i) {
      const std::string once = utf8FoldTypography(kLiveHackerNews[i]);
      if (utf8FoldTypography(once) != once) stable = false;
    }
    for (size_t i = 0; i < foldCount; ++i) {
      std::string source;
      utf8AppendCodepoint(folds[i].cp, source);
      const std::string once = utf8FoldTypography(source);
      if (utf8FoldTypography(once) != once) {
        stable = false;
        std::printf("      U+%04X does not settle after one pass\n", folds[i].cp);
      }
    }
    ok(stable, "folding an already-folded string changes nothing");
  }

  // -- how much longer a fold can make a string --------------------------
  //
  // Stated as a RATIO, because that is the shape every caller cares about and a
  // byte count is not: XkcdActivity reads four times its output buffer before
  // folding, so what matters there is that folding cannot turn a field that
  // would have fitted into one that does not. The widest row is U+2122, three
  // bytes in and four out. Nothing in the tree relies on the exact number; this
  // exists so that a row which changed the ORDER of magnitude cannot be added
  // without somebody reading this comment.
  {
    size_t worstIn = 1;
    size_t worstOut = 1;
    for (size_t i = 0; i < foldCount; ++i) {
      std::string source;
      utf8AppendCodepoint(folds[i].cp, source);
      const size_t out = std::strlen(folds[i].ascii);
      if (out * worstIn > worstOut * source.size()) {
        worstIn = source.size();
        worstOut = out;
      }
    }
    checks++;
    if (worstOut > worstIn * 2) {
      failed++;
      std::printf(
          "FAIL typefold  a fold now expands %zu bytes into %zu; past 2x, re-read every caller that sizes a "
          "buffer from the input\n",
          worstIn, worstOut);
    }
    std::printf("      widest expansion: %zu bytes in, %zu out\n", worstIn, worstOut);
  }

  // -- the scope claim, checked against the system faces --------------------
  //
  // This change fixes the Toybox apps and leaves the system UI, the OPDS
  // browser, the Wi-Fi picker and the EPUB reader alone. That is only defensible
  // if their faces carry the punctuation, so the faces are asked rather than
  // trusted -- and the first version of this claim, which said they "carry every
  // quote and dash and show a box for what they lack", was wrong on both halves.
  {
    struct SystemCut {
      const char* name;
      const EpdFontData* data;
    };
    const SystemCut kSystem[] = {
        {"ubuntu_10_regular", &ubuntu_10_regular},        // UI_10: list subtitles, OPDS authors
        {"ubuntu_12_regular", &ubuntu_12_regular},        // UI_12: list titles, OPDS titles, SSIDs
        {"notosans_8_regular", &notosans_8_regular},      // SMALL
        {"notoserif_16_regular", &notoserif_16_regular},  // a reader body face
    };

    // The common set: what a feed, an SSID or a book title actually carries, and
    // what the reported bug was about. If any of these ever went missing from a
    // system face, the scope of this change would be wrong.
    const uint32_t kCommon[] = {0x00A0, 0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2026, 0x20AC};
    for (const SystemCut& cut : kSystem) {
      bool all = true;
      for (const uint32_t cp : kCommon) {
        if (!draws(cut.data, cp)) {
          all = false;
          std::printf("FAIL typefold  %s cannot draw U+%04X; the system UI is in scope after all\n", cut.name, cp);
        }
      }
      checks++;
      if (!all) failed++;
    }

    // And the half that is NOT true, pinned so it cannot be believed again. The
    // Ubuntu faces carry no U+FFFD, so a codepoint they lack draws as nothing
    // there too -- the same failure, in the system UI, for the rarer marks.
    ok(!draws(&ubuntu_12_regular, 0xFFFD),
       "ubuntu_12_regular has NO replacement glyph, so a missing codepoint is a hole there too");
    ok(draws(&notoserif_16_regular, 0xFFFD), "the Noto faces do have one, which is where the belief came from");
    for (const SystemCut& cut : kSystem) {
      int missing = 0;
      for (size_t i = 0; i < foldCount; ++i) {
        if (!draws(cut.data, folds[i].cp)) missing++;
      }
      std::printf("      %-22s replacement glyph: %-3s  cannot draw %d of the %zu folded codepoints\n", cut.name,
                  draws(cut.data, 0xFFFD) ? "yes" : "NO", missing, foldCount);
    }
  }

  // -- what this change does NOT fix, asserted so it cannot be mistaken -----
  //
  // Two gaps stay open on purpose, and both are recorded here rather than in a
  // commit message, because a gap nobody can see is one somebody re-discovers.
  //
  // (a) Latin Extended-A. The reading cut stops at U+00FF, so a Polish or
  //     Turkish name still loses a letter. Folding it would mean writing a
  //     DIFFERENT letter, and the tile cut draws it correctly today; that trade
  //     needs a decision this change did not make.
  ok(!draws(&reading_serif_14, 0x0142), "reading_serif_14 still cannot draw U+0142 (l with stroke)");
  ok(draws(&toybox_10, 0x0142), "toybox_10 can, which is why folding it fork-wide would be a downgrade");
  eq(utf8FoldTypography("\xC5\x82"
                        "ukasz"),
     "\xC5\x82"
     "ukasz",
     "Latin Extended-A is deliberately left alone");
  //
  // (b) The ASCII-only Jersey cuts. An accented letter drawn in one of them is
  //     still a hole, and the fold cannot help without changing the letter.
  //     xkcd is the app this reaches: its header title is the TITLE slot.
  ok(!draws(&toybox_30, 0x00E9), "toybox_30 is ASCII only, so an accented letter in a title band is still lost");
  ok(draws(&reading_serif_14, 0x00E9), "the reading cut draws it, which is the whole reason it is not folded");

  std::printf("%d checks, %d failed\n", checks, failed);
  return failed == 0 ? 0 : 1;
}

// Host tests for the Hacker News text pipeline. No device, no PlatformIO:
// HackerNewsCore is freestanding C++17 precisely so this can run on a laptop.
// See run.sh.
//
// Two things are under test and they fail in different ways.
//
// The decoders fail loudly: a tag leaks, an entity survives, a paragraph splits
// in the wrong place. Those are pinned with small cases whose expected output
// can be read at a glance.
//
// The readability gate fails quietly, which is why the fixtures are real. It
// decides whether a page gets shown as an article or gets the mark that says it
// cannot be read here, and both of its interesting inputs arrive as HTTP 200
// with a body that looks fine until you count what is in it.

#include <cstdio>
#include <string>
#include <vector>

#include "../../src/apps_local/hackernews/HackerNewsCore.h"
#include "fixtures.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  ++checksRun;
  if (!condition) {
    ++checksFailed;
    std::printf("FAIL test_hackernews.cpp:%d  %s\n", line, what);
  }
}

void checkEqual(const std::string& actual, const std::string& expected, const char* what, const int line) {
  ++checksRun;
  if (actual != expected) {
    ++checksFailed;
    std::printf("FAIL test_hackernews.cpp:%d  %s\n  expected [%s]\n  actual   [%s]\n", line, what, expected.c_str(),
                actual.c_str());
  }
}

void checkProse(const char* fixture, const int low, const int high, const char* what, const int line) {
  ++checksRun;
  const int actual = hn::proseChars(fixture);
  if (actual < low || actual > high) {
    ++checksFailed;
    std::printf("FAIL test_hackernews.cpp:%d  %s: proseChars = %d, expected %d..%d\n", line, what, actual, low, high);
  }
}

#define CHECK(cond) check((cond), #cond, __LINE__)
#define CHECK_EQ(actual, expected) checkEqual((actual), (expected), #actual, __LINE__)
#define CHECK_PROSE(fixture, low, high) checkProse((fixture), (low), (high), #fixture, __LINE__)

std::string decoded(const std::string& input) {
  std::string text = input;
  hn::decodeEntities(text);
  return text;
}

// --- The readability gate, against pages that really shipped -------------

void testGateOnRealPages() {
  // A PDF. The extractor answers 200 OK with a header and an empty body, so
  // anything keying on the status code or on the response being non-empty would
  // show the reader a blank page and call it an article.
  CHECK_PROSE(fixtures::kPdfExtract, 0, 0);
  CHECK(!hn::readsAsProse(fixtures::kPdfExtract));

  // A page that is entirely JavaScript. What comes back is the site's own error
  // sentence, which is text, is non-empty, and is not an article.
  CHECK_PROSE(fixtures::kTwitterExtract, 0, 0);
  CHECK(!hn::readsAsProse(fixtures::kTwitterExtract));

  // The two smallest real articles on that front page. Both must pass, and
  // pinning the smallest is what stops the threshold drifting up until only
  // long-form survives.
  //
  // Scored exactly, the way perft is: a range would hide a decoder change that
  // moves every article a little. These numbers were cross-checked against an
  // independent implementation of the same rule and agree with it.
  CHECK_PROSE(fixtures::kBlogExtract, 1082, 1082);
  CHECK(hn::readsAsProse(fixtures::kBlogExtract));
  CHECK_PROSE(fixtures::kArxivExtract, 1308, 1308);
  CHECK(hn::readsAsProse(fixtures::kArxivExtract));

  // The gap the threshold sits in, stated as the property that matters: every
  // real failure scores nothing at all, and the smallest real success clears
  // the bar by a wide margin. Nothing observed lands in between, which is why
  // 600 is a fixed number rather than a knob somebody will have to tune.
  CHECK(hn::proseChars(fixtures::kBlogExtract) > hn::kReadableThreshold * 3 / 2);
}

void testGateShape() {
  // Structure is not prose, however much of it there is. A long list of link
  // labels is what a gallery, a download page or an index extracts to.
  std::string linkFarm;
  for (int i = 0; i < 60; ++i) linkFarm += "* [Some project with a reasonably long name](https://example.com/x)\n";
  CHECK(!hn::readsAsProse(linkFarm));

  // Neither is a wall of headings.
  std::string headings;
  for (int i = 0; i < 40; ++i) headings += "## A section title that is quite long indeed and keeps going\n\n";
  CHECK(!hn::readsAsProse(headings));

  // A table.
  std::string table;
  for (int i = 0; i < 40; ++i) table += "| a fairly wide cell | another one | and a third to pad the row |\n";
  CHECK(!hn::readsAsProse(table));

  // One genuine paragraph is not enough to call a page an article, but three
  // are. The boundary is the only thing worth asserting here.
  const std::string paragraph(220, 'x');
  CHECK(!hn::readsAsProse(paragraph));
  CHECK(hn::readsAsProse(paragraph + "\n\n" + paragraph + "\n\n" + paragraph));

  // Short blocks never count, so a page of one-line captions cannot add up.
  std::string captions;
  for (int i = 0; i < 100; ++i) captions += "A caption under a picture.\n\n";
  CHECK(!hn::readsAsProse(captions));

  // A paragraph mostly made of citations is still a paragraph. This is the case
  // that talked me out of using link density: the Waymo post scored 0.69 and
  // reads perfectly well.
  std::string cited;
  for (int i = 0; i < 20; ++i) cited += "As [the original paper](https://example.com/paper-one) explains at length, ";
  CHECK(hn::readsAsProse(cited));
  // And the reason link density was rejected outright: measured on the raw
  // Markdown this paragraph is mostly URL, yet what a reader sees is a
  // paragraph. The gate has to score the words, not the bytes.
  CHECK(cited.size() > 3 * static_cast<size_t>(hn::proseChars(cited)) / 2);
}

void testUrlPreFilter() {
  CHECK(!hn::urlCanBeArticle("https://users.wpi.edu/~z/Docs/BradburyStories(1).pdf"));
  CHECK(!hn::urlCanBeArticle("https://twitter.com/gwern/status/2084739205071343837"));
  CHECK(!hn::urlCanBeArticle("https://x.com/someone/status/12345"));
  CHECK(!hn::urlCanBeArticle("https://www.youtube.com/watch?v=abc"));
  CHECK(!hn::urlCanBeArticle("https://example.com/diagram.PNG"));
  CHECK(!hn::urlCanBeArticle(""));

  CHECK(hn::urlCanBeArticle("https://blog.hartwork.org/posts/libexpat-city-of-munich/"));
  CHECK(hn::urlCanBeArticle("https://arxiv.org/abs/2602.16763"));
  CHECK(hn::urlCanBeArticle("https://queue.acm.org/detail.cfm?id=3807963"));

  // The suffix test reads the path, not the whole URL, so a tracking parameter
  // cannot make an article look like a PDF.
  CHECK(hn::urlCanBeArticle("https://example.com/article?from=x.pdf"));
  // And a query string cannot hide a real one.
  CHECK(!hn::urlCanBeArticle("https://example.com/paper.pdf?download=1"));
}

// --- Hacker News's own text ----------------------------------------------

void testEntities() {
  // The five HN actually emits, measured across a real thread.
  CHECK_EQ(decoded("It&#x27;s"), "It's");
  CHECK_EQ(decoded("&quot;quoted&quot;"), "\"quoted\"");
  CHECK_EQ(decoded("https:&#x2F;&#x2F;example.com"), "https://example.com");
  CHECK_EQ(decoded("&gt; quoting you"), "> quoting you");
  CHECK_EQ(decoded("a &lt; b"), "a < b");

  CHECK_EQ(decoded("&#65;&#66;"), "AB");
  CHECK_EQ(decoded("&amp;"), "&");

  // Non-ASCII arrives as a numeric escape and has to come out as UTF-8, because
  // that is what the renderer reads.
  CHECK_EQ(decoded("caf&#233;"), "caf\xc3\xa9");

  // ...unless the reading cut has no glyph for it, in which case UTF-8 is what
  // the renderer reads and NOTHING is what it draws. This expectation used to
  // be "\xe2\x80\x94" and it was a test agreeing with the bug: the em dash
  // decoded correctly, reached drawText correctly, and vanished. Decoding is
  // where HN's punctuation comes into existence -- it is an entity on the wire
  // and a codepoint after this function -- so it is where it gets folded.
  CHECK_EQ(decoded("&#x2014;"), "--");
  CHECK_EQ(decoded("&#8217;"), "'");
  CHECK_EQ(decoded("&#x201C;quoted&#x201D;"), "\"quoted\"");
  CHECK_EQ(decoded("wait&#8230;"), "wait...");
  // Latin-1 letters are not folded: the reading cut draws them, and "cafe" for
  // "caf\xc3\xa9" would be a downgrade rather than a fix. The line above says
  // so; this says the fold did not quietly grow to cover letters.
  CHECK_EQ(decoded("Bj&#246;rn"), "Bj\xc3\xb6rn");

  // One pass, so a double-encoded entity decodes exactly one level. Decoding
  // until nothing changes would turn text *about* entities into the thing it
  // describes.
  CHECK_EQ(decoded("&amp;lt;"), "&lt;");

  // Things that are not entities survive untouched.
  CHECK_EQ(decoded("Tom & Jerry"), "Tom & Jerry");
  CHECK_EQ(decoded("&notanentity;"), "&notanentity;");
  CHECK_EQ(decoded("a & b; c"), "a & b; c");
  CHECK_EQ(decoded("100 &"), "100 &");
}

void testHnHtml() {
  // Tags are stripped before entities are decoded. A comment discussing HTML
  // contains &lt;p&gt;, and decoding first would manufacture a tag out of the
  // words somebody wrote and then split their sentence on it.
  const auto talkingAboutTags = hn::paragraphsFromHnHtml("Use &lt;p&gt; to break a paragraph.");
  CHECK(talkingAboutTags.size() == 1);
  if (talkingAboutTags.size() == 1) CHECK_EQ(talkingAboutTags[0], "Use <p> to break a paragraph.");

  // A real comment: <p> separators, <i> emphasis, a <pre><code> block.
  const auto verse = hn::paragraphsFromHnHtml(fixtures::kComment1);
  CHECK(verse.size() >= 3);
  // "...", not U+2026: the ellipsis is folded on the way out of the decoder,
  // because this cut has no glyph for one and it drew as nothing.
  CHECK(verse[0] == "Thank you for the quote.  Here is the verse in full...");
  for (const std::string& paragraph : verse) {
    CHECK(paragraph.find('<') == std::string::npos);
    CHECK(paragraph.find("&#") == std::string::npos);
  }

  // A real comment carrying a link. The href is full of &#x2F; escapes and none
  // of it may reach the screen; only the label the reader was shown.
  const auto linked = hn::paragraphsFromHnHtml(fixtures::kComment2);
  CHECK(!linked.empty());
  for (const std::string& paragraph : linked) {
    CHECK(paragraph.find("href") == std::string::npos);
    CHECK(paragraph.find('<') == std::string::npos);
  }

  // Empty and tag-only input produce nothing rather than a blank paragraph.
  CHECK(hn::paragraphsFromHnHtml("").empty());
  CHECK(hn::paragraphsFromHnHtml("<p><p><p>").empty());

  // An unclosed angle bracket is a character somebody typed, not a tag.
  const auto maths = hn::paragraphsFromHnHtml("if a < b then");
  CHECK(maths.size() == 1);
  if (maths.size() == 1) CHECK_EQ(maths[0], "if a < b then");
}

// --- The extractor's answer ----------------------------------------------

void testExtractorSplit() {
  const hn::Extracted blog = hn::splitExtractorResponse(fixtures::kBlogExtract);
  CHECK(blog.title.find("libexpat") != std::string::npos);
  // The header must not reach the body, or "URL Source:" would be scored as
  // prose and drawn as the first line of every article.
  CHECK(blog.body.find("URL Source:") == std::string::npos);
  CHECK(blog.body.find("Markdown Content:") == std::string::npos);

  // A PDF's body really is empty. That is the signal, so it must survive the
  // split rather than being papered over with the header.
  const hn::Extracted pdf = hn::splitExtractorResponse(fixtures::kPdfExtract);
  CHECK(hn::proseChars(pdf.body) == 0);

  // The title is drawn as the article's first line and as the header band, and
  // it comes from the same page as the body, so it gets the same fold.
  const hn::Extracted curly = hn::splitExtractorResponse(
      "Title: The \xe2\x80\x9c"
      "best\xe2\x80\x9d"
      " way \xe2\x80\x94"
      " revisited\nMarkdown Content:\nbody");
  CHECK_EQ(curly.title, "The \"best\" way -- revisited");

  // No marker: keep everything and report no title, rather than losing the body.
  const hn::Extracted plain = hn::splitExtractorResponse("just some text");
  CHECK_EQ(plain.body, "just some text");
  CHECK(plain.title.empty());
}

void testMarkdownFlattening() {
  const auto links = hn::paragraphsFromMarkdown("See [the paper](https://example.com/x) for more.");
  CHECK(links.size() == 1);
  if (links.size() == 1) CHECK_EQ(links[0], "See the paper for more.");

  // An image has no label worth keeping and no pixels this path can draw.
  const auto image = hn::paragraphsFromMarkdown("Before ![a diagram](https://example.com/d.png) after.");
  CHECK(image.size() == 1);
  if (image.size() == 1) CHECK_EQ(image[0], "Before  after.");

  const auto heading = hn::paragraphsFromMarkdown("## A heading");
  CHECK(heading.size() == 1);
  if (heading.size() == 1) CHECK_EQ(heading[0], "A heading");

  const auto quote = hn::paragraphsFromMarkdown("> quoted line");
  CHECK(quote.size() == 1);
  if (quote.size() == 1) CHECK_EQ(quote[0], "quoted line");

  const auto emphasis = hn::paragraphsFromMarkdown("This is **bold** and *italic* and `code`.");
  CHECK(emphasis.size() == 1);
  if (emphasis.size() == 1) CHECK_EQ(emphasis[0], "This is bold and italic and code.");

  // A bullet keeps its shape, because a list read as running prose is worse
  // than a list read as a list.
  const auto bullets = hn::paragraphsFromMarkdown("* first\n* second");
  CHECK(bullets.size() == 1);
  if (bullets.size() == 1) CHECK_EQ(bullets[0], "- first - second");

  // The extractor answers with somebody else's page, so unlike HN's own HTML it
  // carries real curly quotes and em dashes rather than numeric entities. This
  // is the article body -- the screen a reader spends the most time on -- and
  // the reading cut has no glyph for any of it, so it drew with holes in it.
  const auto typographic = hn::paragraphsFromMarkdown(
      "He said \xe2\x80\x9c"
      "it\xe2\x80\x99"
      "s fine\xe2\x80\x9d"
      " \xe2\x80\x94"
      " and left\xe2\x80\xa6");
  CHECK(typographic.size() == 1);
  if (typographic.size() == 1) CHECK_EQ(typographic[0], "He said \"it's fine\" -- and left...");

  // And the letters the reading cut CAN draw are left alone, so an author's
  // name is not quietly rewritten on the way to the panel.
  const auto accented = hn::paragraphsFromMarkdown("Written by Bj\xc3\xb6rn in a caf\xc3\xa9.");
  CHECK(accented.size() == 1);
  if (accented.size() == 1) CHECK_EQ(accented[0], "Written by Bj\xc3\xb6rn in a caf\xc3\xa9.");

  // Blank lines separate paragraphs; a single newline is soft wrapping.
  const auto wrapped = hn::paragraphsFromMarkdown("one\nline\n\ntwo");
  CHECK(wrapped.size() == 2);
  if (wrapped.size() == 2) {
    CHECK_EQ(wrapped[0], "one line");
    CHECK_EQ(wrapped[1], "two");
  }

  // A real article flattens to real paragraphs, with no Markdown punctuation
  // and no leftover URLs.
  const hn::Extracted blog = hn::splitExtractorResponse(fixtures::kBlogExtract);
  const auto paragraphs = hn::paragraphsFromMarkdown(blog.body);
  CHECK(paragraphs.size() > 3);
  for (const std::string& paragraph : paragraphs) {
    CHECK(paragraph.find("](") == std::string::npos);
    CHECK(paragraph.find("https://") == std::string::npos);
  }
}

// --- Reading a comment tree without holding it ---------------------------

std::vector<hn::Comment> scan(const std::string& json, const size_t chunk, hn::CommentScanner::Limits limits,
                              int* seen = nullptr, bool* truncated = nullptr) {
  std::vector<hn::Comment> out;
  hn::CommentScanner scanner(out, limits);
  for (size_t i = 0; i < json.size(); i += chunk) {
    const size_t take = json.size() - i < chunk ? json.size() - i : chunk;
    if (!scanner.feed(json.data() + i, take)) break;
  }
  if (seen != nullptr) *seen = scanner.totalSeen();
  if (truncated != nullptr) *truncated = scanner.truncated();
  return out;
}

void testCommentOrdering() {
  // The trap, minimised: keys in the order Algolia really sends them, so
  // "children" precedes "text" at every level. A scanner that emits a comment
  // when its object closes puts the reply above the comment it answers.
  const std::string json = R"({"author":"story","children":[)"
                           R"({"author":"alice","children":[)"
                           R"({"author":"bob","children":[],"text":"bob replies"})"
                           R"(],"text":"alice speaks"})"
                           R"(],"text":"the story"})";

  const auto comments = scan(json, 4096, {});
  CHECK(comments.size() == 2);
  if (comments.size() == 2) {
    // Parent first, at the shallower depth. This is the whole assertion.
    CHECK_EQ(comments[0].author, "alice");
    CHECK(comments[0].depth == 0);
    CHECK(comments[0].paragraphs.size() == 1);
    if (!comments[0].paragraphs.empty()) CHECK_EQ(comments[0].paragraphs[0], "alice speaks");

    CHECK_EQ(comments[1].author, "bob");
    CHECK(comments[1].depth == 1);
    if (!comments[1].paragraphs.empty()) CHECK_EQ(comments[1].paragraphs[0], "bob replies");
  }
  // The root is the story, not a comment, so it must not appear.
  for (const hn::Comment& comment : comments) CHECK(comment.author != "story");

  // An author's name is drawn beside their comment and is not run through the
  // HTML decoder the body is, so it needs the fold of its own. HN usernames are
  // ASCII, but the scanner is fed whatever the API returns.
  const std::string typographic = R"({"author":"story","children":[)"
                                  "{\"author\":\"o\xe2\x80\x99"
                                  "brien\",\"children\":[],\"text\":\"hi\"}"
                                  R"(],"text":"the story"})";
  const auto folded = scan(typographic, 4096, {});
  CHECK(folded.size() == 1);
  if (folded.size() == 1) CHECK_EQ(folded[0].author, "o'brien");
}

void testCommentTree() {
  int seen = 0;
  const auto comments = scan(fixtures::kCommentTree, 4096, {}, &seen);

  // The thread really has 56 commenters plus the story's own node.
  CHECK(seen == 56);
  CHECK(comments.size() == 56);

  bool foundTypedAngleBracket = false;
  for (const hn::Comment& comment : comments) {
    CHECK(!comment.author.empty());
    CHECK(comment.depth >= 0 && comment.depth <= hn::kMaxCommentDepth);
    for (const std::string& paragraph : comment.paragraphs) {
      // Tags stripped, entities decoded, JSON escapes resolved. Nothing from
      // any of the three layers may reach the panel.
      CHECK(paragraph.find("<p>") == std::string::npos);
      CHECK(paragraph.find("<a href") == std::string::npos);
      CHECK(paragraph.find("<pre>") == std::string::npos);
      CHECK(paragraph.find("&#") == std::string::npos);
      CHECK(paragraph.find("&quot;") == std::string::npos);
      CHECK(paragraph.find("\\u") == std::string::npos);
      CHECK(paragraph.find("\\\"") == std::string::npos);
    }
    // This thread contains the case that justifies stripping tags before
    // decoding entities, and it is not a contrived one: somebody opened a
    // comment with "&lt;edited&gt;". Decoded first, that becomes <edited>, which
    // the tag stripper would then delete -- the reader would see a sentence
    // beginning mid-word and nothing would report it. Kept in this order, the
    // word survives exactly as typed.
    if (!comment.paragraphs.empty() && comment.paragraphs[0].rfind("<edited>", 0) == 0) {
      foundTypedAngleBracket = true;
    }
  }
  CHECK(foundTypedAngleBracket);

  // A reply is always below the comment it answers, never above it.
  for (size_t i = 1; i < comments.size(); ++i) {
    CHECK(comments[i].depth <= comments[i - 1].depth + 1);
  }
  // And the thread genuinely nests, or the ordering assertion above is vacuous.
  int deepest = 0;
  for (const hn::Comment& comment : comments) {
    if (comment.depth > deepest) deepest = comment.depth;
  }
  CHECK(deepest >= 2);
}

void testChunkBoundaries() {
  // The response arrives in whatever pieces the socket hands over, so every
  // byte is a possible split. Feeding one byte at a time has to produce exactly
  // what one big feed produces: this is what catches a \uXXXX escape, a tag or
  // a key that straddles a chunk.
  const auto whole = scan(fixtures::kCommentTree, fixtures::kCommentTreeSize + 1, {});
  const auto byByte = scan(fixtures::kCommentTree, 1, {});
  const auto odd = scan(fixtures::kCommentTree, 7, {});

  CHECK(whole.size() == byByte.size());
  CHECK(whole.size() == odd.size());
  const size_t count = whole.size() < byByte.size() ? whole.size() : byByte.size();
  for (size_t i = 0; i < count; ++i) {
    CHECK(whole[i].author == byByte[i].author);
    CHECK(whole[i].depth == byByte[i].depth);
    CHECK(whole[i].paragraphs == byByte[i].paragraphs);
  }
  for (size_t i = 0; i < count && i < odd.size(); ++i) {
    CHECK(whole[i].paragraphs == odd[i].paragraphs);
  }

  // A \u escape split across a feed, on purpose and in isolation.
  const std::string split = R"({"author":"s","children":[{"author":"a","children":[],"text":"café time"}],"text":"x"})";
  for (size_t at = 1; at < split.size(); ++at) {
    std::vector<hn::Comment> out;
    hn::CommentScanner scanner(out, {});
    scanner.feed(split.data(), at);
    scanner.feed(split.data() + at, split.size() - at);
    ++checksRun;
    if (out.size() != 1 || out[0].paragraphs.size() != 1 || out[0].paragraphs[0] != "caf\xc3\xa9 time") {
      ++checksFailed;
      std::printf("FAIL test_hackernews.cpp:%d  \\u escape broke when split at byte %zu\n", __LINE__, at);
      break;
    }
  }
}

void testCommentBudget() {
  hn::CommentScanner::Limits limits;
  limits.maxComments = 10;

  int seen = 0;
  bool truncated = false;
  const auto comments = scan(fixtures::kCommentTree, 512, limits, &seen, &truncated);

  CHECK(comments.size() == 10);
  CHECK(truncated);
  // Counting continues past the budget, so the screen can say how much it is
  // not showing rather than pretending the thread ended.
  CHECK(seen == 56);

  // The ones kept are still the first ten in thread order, with their text.
  CHECK(!comments.front().author.empty());
  CHECK(!comments.front().paragraphs.empty());

  // A budget that nothing exceeds does not claim truncation.
  bool untruncated = false;
  const auto all = scan(fixtures::kCommentTree, 512, {}, nullptr, &untruncated);
  CHECK(!untruncated);
  CHECK(all.size() == 56);
}

void testOnlyChildrenArraysNest() {
  // Depth is counted in "children" arrays and in nothing else. Algolia sends an
  // "options" array on every node, and a response that ever carried objects in
  // one would otherwise manufacture comments out of them and indent the real
  // replies by an extra level.
  const std::string json = R"({"author":"story","children":[)"
                           R"({"author":"alice","options":[{"kind":"noise"},{"kind":"more noise"}],)"
                           R"("children":[{"author":"bob","options":[],"children":[],"text":"reply"}],"text":"top"})"
                           R"(],"text":"the story"})";

  int seen = 0;
  const auto comments = scan(json, 4096, {}, &seen);
  CHECK(comments.size() == 2);
  CHECK(seen == 2);
  if (comments.size() == 2) {
    CHECK_EQ(comments[0].author, "alice");
    CHECK(comments[0].depth == 0);
    CHECK_EQ(comments[1].author, "bob");
    CHECK(comments[1].depth == 1);
  }
}

void testOneHugeComment() {
  // A single enormous comment must not be allowed to set the memory ceiling on
  // its own. It is clipped; the thread around it still arrives.
  hn::CommentScanner::Limits limits;
  limits.maxCommentBytes = 100;

  const std::string huge(4000, 'x');
  const std::string json = R"({"author":"story","children":[{"author":"a","children":[],"text":")" + huge +
                           R"("},{"author":"b","children":[],"text":"short one"}],"text":"s"})";

  const auto comments = scan(json, 256, limits);
  CHECK(comments.size() == 2);
  if (comments.size() == 2) {
    CHECK(!comments[0].paragraphs.empty());
    if (!comments[0].paragraphs.empty()) CHECK(comments[0].paragraphs[0].size() == 100);
    // The comment after the clipped one is untouched, so clipping does not
    // desynchronise the scan.
    CHECK_EQ(comments[1].author, "b");
    if (!comments[1].paragraphs.empty()) CHECK_EQ(comments[1].paragraphs[0], "short one");
  }
}

void testMalformedInput() {
  // Nesting past the scanner's stack must stop it rather than walk off the end.
  std::string deep = R"({"children":[)";
  for (int i = 0; i < 80; ++i) deep += R"({"children":[)";
  std::vector<hn::Comment> out;
  hn::CommentScanner scanner(out, {});
  CHECK(!scanner.feed(deep.data(), deep.size()));

  // Truncated mid-string: whatever was complete survives, and nothing crashes.
  const std::string cut = R"({"author":"s","children":[{"author":"a","children":[],"text":"hello"},{"author":"b","chi)";
  const auto partial = scan(cut, 64, {});
  CHECK(partial.size() >= 1);
  if (!partial.empty()) CHECK_EQ(partial[0].author, "a");

  // Empty and trivial documents.
  CHECK(scan("", 16, {}).empty());
  CHECK(scan("{}", 16, {}).empty());
  CHECK(scan(R"({"children":[]})", 16, {}).empty());
}

}  // namespace

int main() {
  testCommentOrdering();
  testCommentTree();
  testChunkBoundaries();
  testCommentBudget();
  testOnlyChildrenArraysNest();
  testOneHugeComment();
  testMalformedInput();
  testGateOnRealPages();
  testGateShape();
  testUrlPreFilter();
  testEntities();
  testHnHtml();
  testExtractorSplit();
  testMarkdownFlattening();

  std::printf("%d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}

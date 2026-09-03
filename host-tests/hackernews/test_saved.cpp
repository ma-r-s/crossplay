// Host tests for the local save-for-later index.
//
// This file is the thing that has to survive: the network can be absent, the
// card can be pulled mid-write, and a reader should still open the device and
// find their articles. So the tests care most about
// the failure paths -- a half-written row, a title full of tabs, an index from
// a future version -- because those are what stand between a damaged file and a
// lost library.

#include <cstdio>
#include <string>
#include <vector>

#include "../../src/apps_local/hackernews/HackerNewsSaved.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  ++checksRun;
  if (!condition) {
    ++checksFailed;
    std::printf("FAIL test_saved.cpp:%d  %s\n", line, what);
  }
}

void checkEqual(const std::string& actual, const std::string& expected, const char* what, const int line) {
  ++checksRun;
  if (actual != expected) {
    ++checksFailed;
    std::printf("FAIL test_saved.cpp:%d  %s\n  expected [%s]\n  actual   [%s]\n", line, what, expected.c_str(),
                actual.c_str());
  }
}

#define CHECK(cond) check((cond), #cond, __LINE__)
#define CHECK_EQ(actual, expected) checkEqual((actual), (expected), #actual, __LINE__)

hn::SavedArticle make(const char* title, const char* url, const uint32_t at = 1000) {
  hn::SavedArticle article;
  article.id = hn::savedIdFor(url);
  article.title = title;
  article.url = url;
  article.savedAt = at;
  return article;
}

void testIdIsStableAndPerUrl() {
  // The same URL must give the same name across reboots, or a re-save writes a
  // second copy and the first is orphaned on the card forever.
  CHECK_EQ(hn::savedIdFor("https://example.com/a"), hn::savedIdFor("https://example.com/a"));
  CHECK(hn::savedIdFor("https://example.com/a") != hn::savedIdFor("https://example.com/b"));

  // Permutations of the same characters, which any hash that merely adds bytes
  // would collide -- and a collision here means opening one saved article and
  // getting a different one.
  CHECK(hn::savedIdFor("https://example.com/ab") != hn::savedIdFor("https://example.com/ba"));
  CHECK(hn::savedIdFor("https://example.com/xy/z") != hn::savedIdFor("https://example.com/zy/x"));
  CHECK(hn::savedIdFor("https://a.com/1") != hn::savedIdFor("https://a.com/2"));

  // Eight lowercase hex characters: short enough for a FAT filename, and
  // nothing in it needs escaping.
  const std::string id = hn::savedIdFor("https://example.com/a");
  CHECK(id.size() == 8);
  for (const char c : id) CHECK((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));

  CHECK(hn::savedIdFor("").size() == 8);
}

void testRoundTrip() {
  std::vector<hn::SavedArticle> articles{
      make("A Story About Things", "https://example.com/one", 111),
      make("Another One", "https://example.com/two", 222),
  };

  std::vector<hn::SavedArticle> parsed;
  CHECK(hn::parseSavedIndex(hn::serializeSavedIndex(articles), parsed));
  CHECK(parsed.size() == 2);
  if (parsed.size() == 2) {
    CHECK_EQ(parsed[0].title, "A Story About Things");
    CHECK_EQ(parsed[0].url, "https://example.com/one");
    CHECK(parsed[0].savedAt == 111);
    // Order is the order it was written. A saved list that reshuffles itself
    // between boots is unusable.
    CHECK_EQ(parsed[1].title, "Another One");
  }
}

void testEmptyLibrary() {
  std::vector<hn::SavedArticle> parsed;
  const std::string empty = hn::serializeSavedIndex({});
  CHECK(hn::parseSavedIndex(empty, parsed));
  CHECK(parsed.empty());

  // A header with no rows is a valid empty library, not a parse failure. It is
  // what the first save writes into.
  CHECK(hn::parseSavedIndex("hnsaved 1\n", parsed));
  CHECK(parsed.empty());
  CHECK(hn::parseSavedIndex("hnsaved 1", parsed));
  CHECK(parsed.empty());
}

void testNotOurFile() {
  std::vector<hn::SavedArticle> parsed;
  CHECK(!hn::parseSavedIndex("", parsed));
  CHECK(!hn::parseSavedIndex("{\"json\": true}", parsed));
  CHECK(!hn::parseSavedIndex("some other file entirely\n", parsed));
}

void testDamageCostsOneEntry() {
  // A power cut mid-write leaves a truncated last row. Everything before it
  // must still load: this is the whole argument for a line format over one
  // document that either parses or does not.
  std::string text = hn::serializeSavedIndex({
      make("First", "https://example.com/1"),
      make("Second", "https://example.com/2"),
  });
  text += "0badbeef\t999\tA title with no url yet";

  std::vector<hn::SavedArticle> parsed;
  CHECK(hn::parseSavedIndex(text, parsed));
  CHECK(parsed.size() == 3);  // the partial row still has an id and a title
  if (parsed.size() == 3) CHECK_EQ(parsed[2].url, "");

  // A row missing its title is damage and is dropped, but its neighbours live.
  std::string holed = hn::serializeSavedIndex({make("Keep Me", "https://example.com/keep")});
  holed += "\t\t\t\n";
  holed += "deadbeef\t5\tAlso Keep Me\thttps://example.com/also\n";
  std::vector<hn::SavedArticle> survivors;
  CHECK(hn::parseSavedIndex(holed, survivors));
  CHECK(survivors.size() == 2);
  if (survivors.size() == 2) {
    CHECK_EQ(survivors[0].title, "Keep Me");
    CHECK_EQ(survivors[1].title, "Also Keep Me");
  }
}

void testFieldsCannotSwallowEachOther() {
  // A title carrying a tab would end its own field and shift every one after
  // it, so the URL would be read out of the middle of the title.
  hn::SavedArticle nasty = make("Tabbed\tTitle\nWith Newlines", "https://example.com/x");
  std::vector<hn::SavedArticle> parsed;
  CHECK(hn::parseSavedIndex(hn::serializeSavedIndex({nasty}), parsed));
  CHECK(parsed.size() == 1);
  if (parsed.size() == 1) {
    CHECK_EQ(parsed[0].title, "Tabbed Title With Newlines");
    CHECK_EQ(parsed[0].url, "https://example.com/x");
  }

  CHECK_EQ(hn::sanitizeField("  padded  "), "padded");
  CHECK_EQ(hn::sanitizeField("a\t\tb"), "a b");
  CHECK_EQ(hn::sanitizeField(""), "");
  CHECK_EQ(hn::sanitizeField("\t\n "), "");

  // sanitizeField itself must NOT fold. It runs on the URL column as well as
  // the title, savedIdFor() hashes the URL, and an article whose stored URL no
  // longer hashes to its id can never be unsaved again.
  CHECK_EQ(hn::sanitizeField("They\xe2\x80\x99"
                             "re"),
           "They\xe2\x80\x99"
           "re");
}

void testTypographyIsFoldedButUrlsAreNot() {
  // A title with a curly apostrophe, a curly pair, an em dash and an ellipsis.
  // The reading cut has no glyph for any of them and draws them as nothing, so
  // the title is folded at BOTH ends of the index: written folded, and folded
  // again on the read, which is what makes an index saved before this existed
  // stop having holes without a migration.
  hn::SavedArticle article;
  article.id = hn::savedIdFor("https://example.com/one");
  article.savedAt = 1000;
  article.title =
      "It\xe2\x80\x99"
      "s a \xe2\x80\x9c"
      "big\xe2\x80\x9d"
      " one \xe2\x80\x94"
      " really\xe2\x80\xa6";
  article.url = "https://example.com/one";

  std::vector<hn::SavedArticle> read;
  CHECK(hn::parseSavedIndex(hn::serializeSavedIndex({article}), read));
  CHECK(read.size() == 1);
  if (read.size() == 1) {
    CHECK_EQ(read[0].title, "It's a \"big\" one -- really...");
    // The URL survives byte for byte and still hashes to the id it was saved
    // under. This is the assertion that stops the fold being applied one field
    // to the left.
    CHECK_EQ(read[0].url, "https://example.com/one");
    CHECK_EQ(hn::savedIdFor(read[0].url), read[0].id);
  }

  // A URL with a real non-ASCII character in it: unencoded paths happen, and a
  // fold would rewrite one into a different address.
  hn::SavedArticle exotic;
  exotic.url =
      "https://example.com/caf\xc3\xa9\xe2\x80\x94"
      "notes";
  exotic.id = hn::savedIdFor(exotic.url);
  exotic.savedAt = 2000;
  exotic.title = "Notes";
  CHECK(hn::parseSavedIndex(hn::serializeSavedIndex({exotic}), read));
  CHECK(read.size() == 1);
  if (read.size() == 1) {
    CHECK_EQ(read[0].url, exotic.url);
    CHECK_EQ(hn::savedIdFor(read[0].url), exotic.id);
  }

  // The read-side fold, on its own. An index WRITTEN BY AN OLDER BUILD holds
  // the raw codepoints, and serializeSavedIndex can no longer produce one, so
  // this is the raw bytes an old card really carries. Without the fold on the
  // read these rows keep their holes for as long as they stay saved, and the
  // round trip above cannot tell -- it folds on the write.
  const std::string oldIndex =
      "hnsaved 2\nabc123\t1000\tIt\xe2\x80\x99"
      "s a \xe2\x80\x9c"
      "big\xe2\x80\x9d"
      " one\thttps://example.com/old\n";
  CHECK(hn::parseSavedIndex(oldIndex, read));
  CHECK(read.size() == 1);
  if (read.size() == 1) {
    CHECK_EQ(read[0].title, "It's a \"big\" one");
    CHECK_EQ(read[0].url, "https://example.com/old");
  }

  // And the letters the reading cut can draw are left in the title.
  hn::SavedArticle accented;
  accented.url = "https://example.com/two";
  accented.id = hn::savedIdFor(accented.url);
  accented.savedAt = 3000;
  accented.title =
      "Bj\xc3\xb6"
      "rn in a caf\xc3\xa9";
  CHECK(hn::parseSavedIndex(hn::serializeSavedIndex({accented}), read));
  if (read.size() == 1)
    CHECK_EQ(read[0].title,
             "Bj\xc3\xb6"
             "rn in a caf\xc3\xa9");
}

void testVersionOneStillOpens() {
  // The bug that shipped to Mario's card, pinned. Version 1 carried two extra
  // columns between savedAt and title; read with the version-2 field order it
  // hands back "0" as every title and "1" as every URL, so the library shows a
  // list of zeroes and the reader can never recognise anything as saved.
  // Ids derived, not typed: pairing a real id with a made-up URL is how this
  // test first "failed" against correct code.
  const std::string urlA = "https://blog.cloudflare.com/x";
  const std::string urlB = "https://example.com/y";
  std::string v1 = "hnsaved 1\n";
  v1 += hn::savedIdFor(urlA) + "\t1785957276\t0\t1\tCloudflare OS: an open platform\t" + urlA + "\n";
  v1 += hn::savedIdFor(urlB) + "\t5\t4242\t0\tSomething Else\t" + urlB + "\n";

  std::vector<hn::SavedArticle> parsed;
  CHECK(hn::parseSavedIndex(v1, parsed));
  CHECK(parsed.size() == 2);

  // A version-1 library with FOUR columns rather than six. This is not
  // hypothetical: it is the shape of Mario's own saved.tsv, written by the
  // build this feature shipped in, and it is the file the feature exists to
  // open. Deciding the column count from the version header alone read the
  // title out of a field past the end of the row, produced an empty title,
  // and discarded the entry as damage -- so his shelf came up empty while
  // every assertion in this file passed, because they were all written from
  // the same assumption as the parser.
  {
    const std::string urlC = "https://blog.google/company-news/inside-google/message-ceo/next";
    std::string flat = "hnsaved 1\n";
    flat += hn::savedIdFor(urlC) + "\t1785960335\tChanges at Google DeepMind\t" + urlC + "\n";

    std::vector<hn::SavedArticle> four;
    CHECK(hn::parseSavedIndex(flat, four));
    CHECK(four.size() == 1);
    if (four.size() == 1) {
      CHECK_EQ(four[0].title, "Changes at Google DeepMind");
      CHECK_EQ(four[0].url, urlC);
      CHECK(four[0].savedAt == 1785960335);
    }
  }
  if (parsed.size() == 2) {
    CHECK_EQ(parsed[0].title, "Cloudflare OS: an open platform");
    CHECK_EQ(parsed[0].url, urlA);
    CHECK(parsed[0].savedAt == 1785957276);
    CHECK_EQ(parsed[1].title, "Something Else");
    CHECK_EQ(parsed[1].url, urlB);

    // The symptom, stated directly: no title may come back as the dropped
    // column's value, and the URL has to survive or nothing can be recognised
    // as already saved.
    CHECK(parsed[0].title != "0");
    CHECK(parsed[0].url != "1");
    // And the id derived from the recovered URL is what the save path will
    // compute, which is what makes the mark show filled instead of saving again.
    CHECK_EQ(hn::savedIdFor(parsed[0].url), parsed[0].id);
  }
}

void testUnknownVersionIsLeftAlone() {
  // A library written by a newer build is not something to guess at. Half-read
  // and then rewritten is how a version bump destroys the thing it exists to
  // protect.
  std::vector<hn::SavedArticle> parsed;
  CHECK(!hn::parseSavedIndex("hnsaved 99\nabc\t1\tTitle\thttps://example.com\n", parsed));
  CHECK(parsed.empty());
  CHECK(!hn::parseSavedIndex("hnsaved 0\n", parsed));
  CHECK(!hn::parseSavedIndex("hnsaved x\n", parsed));
}

// Keeping the conversation, which is the only thing there is to keep when the
// linked page will not render here. The property that matters is that a thread
// is a SECOND entry rather than the article's: sharing a key would mean saving
// the discussion silently replaced the article, and the reader's mark would
// then say the article was kept when the thread was.
void testAThreadIsItsOwnEntry() {
  CHECK_EQ(hn::savedThreadUrl(45123001), "https://news.ycombinator.com/item?id=45123001");
  // Stable, because it is the library key and a re-save has to land on the same
  // row rather than growing a duplicate.
  CHECK_EQ(hn::savedThreadUrl(7), hn::savedThreadUrl(7));
  CHECK(hn::savedThreadUrl(7) != hn::savedThreadUrl(8));

  // The whole point: an article and its discussion are two names on the card.
  const std::string article = "https://playaphone.com/";
  CHECK(hn::savedIdFor(hn::savedThreadUrl(45123001)) != hn::savedIdFor(article));

  // And the shelf can tell them apart, in FRONT, where a headline cut to the
  // row width still shows it.
  CHECK_EQ(hn::savedThreadTitle("Playa Phone"), "Comments: Playa Phone");
  CHECK(hn::savedThreadTitle("Playa Phone").rfind("Comments: ", 0) == 0);

  // Both together in one index, surviving the round trip as two rows.
  std::vector<hn::SavedArticle> shelf{
      make("Playa Phone", article.c_str()),
      make(hn::savedThreadTitle("Playa Phone").c_str(), hn::savedThreadUrl(45123001).c_str()),
  };
  std::vector<hn::SavedArticle> parsed;
  CHECK(hn::parseSavedIndex(hn::serializeSavedIndex(shelf), parsed));
  CHECK_EQ(std::to_string(parsed.size()), "2");
  if (parsed.size() == 2) {
    CHECK(parsed[0].id != parsed[1].id);
    CHECK_EQ(parsed[1].title, "Comments: Playa Phone");
    CHECK_EQ(parsed[1].url, "https://news.ycombinator.com/item?id=45123001");
  }
}

// A post with no link of its own -- an Ask HN, a Show HN with a body -- is its
// own text, so Story::url is empty and Library::save refuses an empty URL.
// Before the item page became the thread's key, those could not be kept by any
// route at all.
void testAPostWithNoLinkStillHasAKey() {
  CHECK(!hn::savedThreadUrl(45123001).empty());
  CHECK(!hn::savedIdFor(hn::savedThreadUrl(45123001)).empty());
}

void testWeWriteTheCurrentVersion() {
  // What we write must be what we read, or the next boot migrates its own
  // output. The round trip below would pass even if both were wrong, so the
  // header is asserted literally.
  const std::string written = hn::serializeSavedIndex({make("A", "https://example.com/a")});
  CHECK(written.compare(0, 10, "hnsaved 2\n") == 0);
}

}  // namespace

int main() {
  testIdIsStableAndPerUrl();
  testRoundTrip();
  testEmptyLibrary();
  testNotOurFile();
  testDamageCostsOneEntry();
  testFieldsCannotSwallowEachOther();
  testTypographyIsFoldedButUrlsAreNot();
  testVersionOneStillOpens();
  testUnknownVersionIsLeftAlone();
  testWeWriteTheCurrentVersion();
  testAThreadIsItsOwnEntry();
  testAPostWithNoLinkStillHasAKey();

  std::printf("%d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}

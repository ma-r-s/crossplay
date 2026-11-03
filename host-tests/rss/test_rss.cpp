// Host tests for the RSS app's freestanding half: the three feed formats, the
// page a person pastes instead of a feed, what a careless or broken feed does,
// the limits, the dates, and the files on the card.
//
// The fixtures are written in the shapes real feeds take -- WordPress's RSS
// 2.0, a static site's Atom, Slashdot-style RSS 1.0 -- with made-up words.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../src/apps_local/rss/RssCore.h"
#include "../../src/apps_local/rss/RssDesk.h"
#include "../../src/apps_local/rss/RssFeed.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  ++checksRun;
  if (!condition) {
    ++checksFailed;
    std::printf("FAIL test_rss.cpp:%d  %s\n", line, what);
  }
}

void checkEqual(const std::string& actual, const std::string& expected, const char* what, const int line) {
  ++checksRun;
  if (actual != expected) {
    ++checksFailed;
    std::printf("FAIL test_rss.cpp:%d  %s\n  expected [%s]\n  actual   [%s]\n", line, what, expected.c_str(),
                actual.c_str());
  }
}

void checkNum(const long long actual, const long long expected, const char* what, const int line) {
  ++checksRun;
  if (actual != expected) {
    ++checksFailed;
    std::printf("FAIL test_rss.cpp:%d  %s\n  expected %lld\n  actual   %lld\n", line, what, expected, actual);
  }
}

#define CHECK(cond) check((cond), #cond, __LINE__)
#define CHECK_EQ(actual, expected) checkEqual((actual), (expected), #actual, __LINE__)
#define CHECK_NUM(actual, expected) \
  checkNum(static_cast<long long>(actual), static_cast<long long>(expected), #actual, __LINE__)

// Collects every item a parser hands over, already ingested.
struct Collector : rss::ItemSink {
  std::string feedUrl = "https://example.com/feed.xml";
  std::vector<rss::Item> items;
  std::vector<rss::Post> posts;
  std::vector<std::string> texts;
  int stopAfter = -1;

  bool onItem(rss::Item& item) override {
    items.push_back(item);
    rss::Post post;
    std::string text;
    if (rss::ingest(item, feedUrl, rss::keyFor(feedUrl), post, text)) {
      posts.push_back(post);
      texts.push_back(text);
    }
    return stopAfter < 0 || static_cast<int>(items.size()) < stopAfter;
  }
};

// Feeds `doc` to a fresh parser in chunks of `chunk` bytes (0 = all at once).
rss::Kind parse(const std::string& doc, Collector& out, const size_t chunk = 0, std::string* title = nullptr,
                std::string* site = nullptr, bool* malformed = nullptr) {
  rss::FeedParser parser(&out);
  const size_t step = chunk == 0 ? doc.size() : chunk;
  for (size_t i = 0; i < doc.size(); i += step) {
    if (!parser.feed(doc.data() + i, std::min(step, doc.size() - i))) break;
  }
  parser.finish();
  if (title != nullptr) *title = parser.title();
  if (site != nullptr) *site = parser.siteLink();
  if (malformed != nullptr) *malformed = parser.malformed();
  return parser.kind();
}

// --- Fixtures ------------------------------------------------------------

const char* kRss2 = R"(<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0" xmlns:content="http://purl.org/rss/1.0/modules/content/"
     xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:atom="http://www.w3.org/2005/Atom">
<channel>
  <title>The Quiet Kettle</title>
  <atom:link href="https://kettle.example/feed/" rel="self" type="application/rss+xml" />
  <link>https://kettle.example</link>
  <description>Notes on tea &amp; patience</description>
  <image><title>Not the feed title</title><link>https://kettle.example/logo</link><url>x.png</url></image>
  <item>
    <title>Why &#8220;slow&#8221; is a feature &amp; not a bug</title>
    <link>https://kettle.example/2026/10/slow/</link>
    <dc:creator><![CDATA[Ada]]></dc:creator>
    <pubDate>Fri, 16 Oct 2026 09:30:00 +0000</pubDate>
    <guid isPermaLink="false">https://kettle.example/?p=101</guid>
    <description><![CDATA[<p>A short teaser [&#8230;]</p>]]></description>
    <content:encoded><![CDATA[<p>First paragraph of the <em>whole</em> post.</p>
<p>Second paragraph, with a <a href="/x">link</a>.</p>
<script>alert("no")</script>
<ul><li>one</li><li>two</li></ul>]]></content:encoded>
  </item>
  <item>
    <title>A summary only</title>
    <link>/2026/10/summary/</link>
    <pubDate>Thu, 15 Oct 2026 18:00:00 GMT</pubDate>
    <description>&lt;p&gt;Only the &lt;b&gt;start&lt;/b&gt; of it.&lt;/p&gt;</description>
  </item>
  <item>
    <title>No words at all</title>
    <guid>https://kettle.example/2026/10/link-only/</guid>
    <pubDate>Wed, 14 Oct 2026 07:00:00 -0700</pubDate>
  </item>
</channel>
</rss>)";

const char* kAtom = R"(<?xml version="1.0" encoding="utf-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <title type="text">Field Notes</title>
  <link href="https://notes.example/atom.xml" rel="self"/>
  <link href="https://notes.example/" rel="alternate" type="text/html"/>
  <id>urn:uuid:feed</id>
  <updated>2026-10-17T08:00:00Z</updated>
  <entry>
    <title type="html">Tags &lt;b&gt;in&lt;/b&gt; a title</title>
    <link href="https://notes.example/comments/1" rel="replies"/>
    <link href="https://notes.example/posts/one/"/>
    <id>tag:notes.example,2026:1</id>
    <updated>2026-10-17T07:00:00Z</updated>
    <published>2026-10-10T07:00:00+02:00</published>
    <content type="html">&lt;p&gt;Escaped markup.&lt;/p&gt;&lt;p&gt;Two paragraphs.&lt;/p&gt;</content>
  </entry>
  <entry>
    <title>Inline XHTML</title>
    <link rel="alternate" href="posts/two/"/>
    <id>tag:notes.example,2026:2</id>
    <updated>2026-10-12T12:30:15.123-05:00</updated>
    <content type="xhtml"><div xmlns="http://www.w3.org/1999/xhtml"><p>Alpha &amp; beta.</p><p>Gamma &lt; delta.</p></div></content>
  </entry>
  <entry>
    <title>Plain text</title>
    <link href="https://notes.example/posts/three/"/>
    <id>tag:notes.example,2026:3</id>
    <updated>2026-10-11</updated>
    <content type="text">Line one
still line one.

Second paragraph &lt;not a tag&gt;.</content>
  </entry>
  <entry>
    <title>Summary only</title>
    <link href="https://notes.example/posts/four/"/>
    <id>tag:notes.example,2026:4</id>
    <updated>2026-10-09T00:00:00Z</updated>
    <summary>Just the gist.</summary>
    <content src="https://notes.example/four.pdf" type="application/pdf"/>
  </entry>
</feed>)";

const char* kRdf = R"(<?xml version="1.0" encoding="ISO-8859-1"?>
<rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#"
         xmlns="http://purl.org/rss/1.0/" xmlns:dc="http://purl.org/dc/elements/1.1/">
  <channel rdf:about="https://slash.example/">
    <title>Slash Example</title>
    <link>https://slash.example/</link>
    <description>News for nerds</description>
    <items><rdf:Seq><rdf:li rdf:resource="https://slash.example/story/1"/></rdf:Seq></items>
  </channel>
  <item rdf:about="https://slash.example/story/1">
    <title>Caf&#233; opens on the moon</title>
    <link>https://slash.example/story/1</link>
    <description>Everyone is very excited.</description>
    <dc:date>2026-10-16T21:15:00+00:00</dc:date>
  </item>
  <item rdf:about="https://slash.example/story/2">
    <title>Second story</title>
    <link>https://slash.example/story/2</link>
    <dc:date>2026-10-15T10:00:00Z</dc:date>
  </item>
</rdf:RDF>)";

std::string rssWithItems(const int count, const bool newestFirst, const bool dated) {
  std::string doc = "<rss version=\"2.0\"><channel><title>Many</title>";
  for (int i = 0; i < count; ++i) {
    const int n = newestFirst ? count - i : i + 1;  // larger n = newer
    char item[512];
    if (dated) {
      std::snprintf(item, sizeof(item),
                    "<item><title>Post %d</title><link>https://many.example/%d</link>"
                    "<pubDate>%02d Oct 2026 10:%02d:00 GMT</pubDate></item>",
                    n, n, 1 + n / 60, n % 60);
    } else {
      std::snprintf(item, sizeof(item), "<item><title>Post %d</title><link>https://many.example/%d</link></item>", n,
                    n);
    }
    doc += item;
  }
  doc += "</channel></rss>";
  return doc;
}

// --- The three formats ---------------------------------------------------

void testRss2() {
  Collector c;
  std::string title;
  std::string site;
  bool malformed = true;
  CHECK(parse(kRss2, c, 0, &title, &site, &malformed) == rss::Kind::Rss);
  CHECK(!malformed);
  CHECK_EQ(title, "The Quiet Kettle");
  // The channel's own <link>, not the <image>'s and not atom:link's self.
  CHECK_EQ(site, "https://kettle.example");
  CHECK_NUM(c.posts.size(), 3);

  const rss::Post& first = c.posts[0];
  // Numeric quote entities decoded and folded to the ASCII the reading cut has.
  CHECK_EQ(first.title, "Why \"slow\" is a feature & not a bug");
  CHECK_EQ(first.link, "https://kettle.example/2026/10/slow/");
  CHECK_NUM(first.date, 1792143000);  // 2026-10-16 09:30:00 UTC
  CHECK(first.body == rss::Body::Full);
  // content:encoded wins over the teaser, script vanishes, the list survives.
  CHECK_EQ(c.texts[0], "First paragraph of the whole post.\n\nSecond paragraph, with a link.\n\n- one\n\n- two");

  // A relative link is resolved against the feed's address.
  CHECK_EQ(c.posts[1].link, "https://example.com/2026/10/summary/");
  CHECK(c.posts[1].body == rss::Body::Summary);
  CHECK_EQ(c.texts[1], "Only the start of it.");

  // Only a guid: it is the link, and there are no words to keep.
  CHECK_EQ(c.posts[2].link, "https://kettle.example/2026/10/link-only/");
  CHECK(c.posts[2].body == rss::Body::None);
  CHECK(c.texts[2].empty());
  CHECK_NUM(c.posts[2].date, 1791986400);  // 07:00 at -0700 is 14:00 UTC

  // The guid names the post, so a changed headline does not make a new one.
  CHECK_EQ(c.posts[0].id, rss::keyFor(rss::keyFor(c.feedUrl) + "\nhttps://kettle.example/?p=101"));
}

void testAtom() {
  Collector c;
  c.feedUrl = "https://notes.example/atom.xml";
  std::string title;
  std::string site;
  CHECK(parse(kAtom, c, 0, &title, &site) == rss::Kind::Atom);
  CHECK_EQ(title, "Field Notes");
  // rel="alternate", never rel="self".
  CHECK_EQ(site, "https://notes.example/");
  CHECK_NUM(c.posts.size(), 4);

  // type="html" titles carry markup; it is stripped, not shown.
  CHECK_EQ(c.posts[0].title, "Tags in a title");
  // The alternate link, not the replies one listed first.
  CHECK_EQ(c.posts[0].link, "https://notes.example/posts/one/");
  // <published> beats <updated>, whichever comes first in the entry.
  CHECK_NUM(c.posts[0].date, 1791608400);  // 2026-10-10T05:00:00Z
  CHECK(c.posts[0].body == rss::Body::Full);
  CHECK_EQ(c.texts[0], "Escaped markup.\n\nTwo paragraphs.");

  // Inline XHTML keeps its paragraphs and its escaped characters stay text.
  CHECK_EQ(c.posts[1].link, "https://notes.example/posts/two/");
  CHECK_EQ(c.texts[1], "Alpha & beta.\n\nGamma < delta.");
  CHECK_NUM(c.posts[1].date, 1791826215);  // 17:30:15 UTC, fraction dropped

  // type="text": a blank line is a paragraph; "<not a tag>" is words.
  CHECK_EQ(c.texts[2], "Line one still line one.\n\nSecond paragraph <not a tag>.");
  CHECK_NUM(c.posts[2].date, 1791676800);  // a bare date is midnight UTC

  // Content by reference is not words; the summary is what there is.
  CHECK(c.posts[3].body == rss::Body::Summary);
  CHECK_EQ(c.texts[3], "Just the gist.");
}

void testRdf() {
  Collector c;
  std::string title;
  std::string site;
  CHECK(parse(kRdf, c, 0, &title, &site) == rss::Kind::Rdf);
  CHECK_EQ(title, "Slash Example");
  CHECK_EQ(site, "https://slash.example/");
  CHECK_NUM(c.posts.size(), 2);
  // ISO-8859-1 declared; the numeric reference is a Latin-1 letter the reading
  // cut carries, so it is kept rather than folded.
  CHECK_EQ(c.posts[0].title, "Caf\xC3\xA9 opens on the moon");
  CHECK_NUM(c.posts[0].date, 1792185300);  // dc:date
  CHECK(c.posts[0].body == rss::Body::Summary);
  // rdf:about names the post.
  CHECK_EQ(c.items[0].guid, "https://slash.example/story/1");
  CHECK(c.posts[1].body == rss::Body::None);
}

void testChunkingChangesNothing() {
  // The socket decides the chunk size. Every boundary -- inside a tag, inside
  // an entity, inside "<![CDATA[" -- has to give the same answer.
  for (const char* doc : {kRss2, kAtom, kRdf}) {
    Collector whole;
    parse(doc, whole);
    for (const size_t chunk : {size_t{1}, size_t{2}, size_t{3}, size_t{7}, size_t{64}}) {
      Collector pieces;
      parse(doc, pieces, chunk);
      CHECK_NUM(pieces.posts.size(), whole.posts.size());
      bool same = pieces.posts.size() == whole.posts.size();
      for (size_t i = 0; same && i < whole.posts.size(); ++i) {
        same = pieces.posts[i].id == whole.posts[i].id && pieces.posts[i].title == whole.posts[i].title &&
               pieces.texts[i] == whole.texts[i] && pieces.posts[i].date == whole.posts[i].date;
      }
      CHECK(same);
    }
  }
}

// --- Careless and broken feeds -------------------------------------------

void testHtmlEntitiesOutsideCdata() {
  // Undeclared HTML entities and a bare ampersand. expat alone stops at the
  // first of them -- here, in the first title -- and the feed shows nothing.
  const std::string doc =
      "<rss><channel><title>AT&T Blog</title>"
      "<item><title>Caf&eacute;&nbsp;society &mdash; part 1</title><link>https://a.example/1?x=1&y=2</link>"
      "<description>Q&amp;A &hellip; and &copy; and &#8217;</description></item>"
      "<item><title>Second</title><link>https://a.example/2</link></item></channel></rss>";
  Collector c;
  std::string title;
  bool malformed = true;
  CHECK(parse(doc, c, 0, &title, nullptr, &malformed) == rss::Kind::Rss);
  CHECK(!malformed);
  CHECK_EQ(title, "AT&T Blog");
  CHECK_NUM(c.posts.size(), 2);
  CHECK_EQ(c.posts[0].title, "Caf\xC3\xA9 society -- part 1");
  CHECK_EQ(c.posts[0].link, "https://a.example/1?x=1&y=2");
  CHECK_EQ(c.texts[0], "Q&A ... and \xC2\xA9 and '");
}

void testEntityFixerLeavesCdataAlone() {
  rss::EntityFixer fixer;
  std::string out;
  const std::string in = "<a>&nbsp;&amp;&#38;&#x26;&lt;&bogus</a><![CDATA[&nbsp; ]] ]]><!-- & --><b x=\"1&2\"/>";
  // One byte at a time, the worst case for the state it carries.
  for (const char ch : in) fixer.feed(&ch, 1, out);
  fixer.finish(out);
  CHECK_EQ(out, "<a>&amp;nbsp;&amp;&#38;&#x26;&lt;&amp;bogus</a><![CDATA[&nbsp; ]] ]]><!-- & --><b x=\"1&amp;2\"/>");
}

void testFeedsExpatWouldRefuse() {
  // Whitespace before the declaration (a WordPress plugin fault), a reference
  // to a character XML forbids, a lone surrogate, and a raw control byte. Each
  // stops expat at the first byte; each is absorbed.
  const std::string doc =
      "\n\n  <?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<rss><channel><title>Fault&#1;y</title>"
      "<item><title>One &#0; two &#xD83D; three\x01\x0b</title><link>https://f.example/1</link></item>"
      "<item><title>Second</title><link>https://f.example/2</link></item></channel></rss>";
  Collector c;
  std::string title;
  bool malformed = true;
  CHECK(parse(doc, c, 0, &title, nullptr, &malformed) == rss::Kind::Rss);
  CHECK(!malformed);
  CHECK_EQ(title, "Faulty");
  CHECK_NUM(c.posts.size(), 2);
  if (!c.posts.empty()) CHECK_EQ(c.posts[0].title, "One two three");

  // A byte-order mark is not leading whitespace and stays; space after it goes.
  Collector bom;
  bool bomMalformed = true;
  CHECK(parse("\xEF\xBB\xBF \n<rss><channel><title>B</title></channel></rss>", bom, 1, nullptr, nullptr,
              &bomMalformed) == rss::Kind::Rss);
  CHECK(!bomMalformed);

  // UTF-16 is not ASCII-shaped and is handed to expat untouched.
  rss::EntityFixer fixer;
  std::string out;
  const char utf16[] = {'\xFF', '\xFE', '<', '\0', '&', '\0'};
  fixer.feed(utf16, sizeof(utf16), out);
  fixer.finish(out);
  CHECK(out == std::string(utf16, sizeof(utf16)));
}

void testWindows1252() {
  const std::string doc =
      "<?xml version=\"1.0\" encoding=\"windows-1252\"?><rss><channel><title>W</title>"
      "<item><title>It\x92s \x93quoted\x94</title><link>https://w.example/1</link></item></channel></rss>";
  Collector c;
  bool malformed = true;
  CHECK(parse(doc, c, 0, nullptr, nullptr, &malformed) == rss::Kind::Rss);
  CHECK(!malformed);
  CHECK_NUM(c.posts.size(), 1);
  if (!c.posts.empty()) CHECK_EQ(c.posts[0].title, "It's \"quoted\"");
}

void testTruncatedFeedKeepsItsTop() {
  // A feed cut off mid-post -- the byte limit, or a dropped connection -- still
  // has the posts above the cut.
  std::string doc = kRss2;
  doc = doc.substr(0, doc.find("<title>No words at all"));
  Collector c;
  bool malformed = false;
  CHECK(parse(doc, c, 0, nullptr, nullptr, &malformed) == rss::Kind::Rss);
  CHECK(malformed);
  CHECK_NUM(c.posts.size(), 2);
}

void testGarbageMidFeed() {
  const std::string doc =
      "<rss><channel><title>G</title><item><title>Kept</title><link>https://g.example/1</link></item>"
      "<item><title>Broken <<< </title></item><item><title>Lost</title></item></channel></rss>";
  Collector c;
  bool malformed = false;
  parse(doc, c, 0, nullptr, nullptr, &malformed);
  CHECK(malformed);
  CHECK_NUM(c.posts.size(), 1);
  if (!c.posts.empty()) CHECK_EQ(c.posts[0].title, "Kept");
}

void testNotAFeed() {
  Collector c;
  CHECK(parse("<!DOCTYPE html><html><head><title>x</title></head><body>hi</body></html>", c) == rss::Kind::NotFeed);
  CHECK(c.posts.empty());
  Collector d;
  // Lowercase doctype is not XML at all: no root is ever seen.
  rss::FeedParser parser(&d);
  CHECK(!parser.feed("<!doctype html><html>", 21) || parser.kind() != rss::Kind::Rss);
  CHECK(!parser.isFeed());
  Collector e;
  CHECK(parse("", e) == rss::Kind::Unknown);
  Collector f;
  CHECK(parse("{\"version\": \"https://jsonfeed.org/version/1\"}", f) == rss::Kind::Unknown);
}

void testSinkCanStop() {
  Collector c;
  c.stopAfter = 2;
  rss::FeedParser parser(&c);
  const std::string doc = rssWithItems(10, true, true);
  CHECK(!parser.feed(doc.data(), doc.size()));
  CHECK(parser.stoppedBySink());
  CHECK(!parser.malformed());
  CHECK_NUM(c.items.size(), 2);
}

void testFieldLimits() {
  std::string big(200 * 1024, 'x');
  const std::string doc = "<rss><channel><title>L</title><item><title>" + std::string(1000, 'T') +
                          "</title><link>https://l.example/1</link><content:encoded><![CDATA[<p>" + big +
                          "</p>]]></content:encoded></item></channel></rss>";
  Collector c;
  parse(doc, c, 4096);
  CHECK_NUM(c.posts.size(), 1);
  if (c.items.empty() || c.posts.empty()) return;
  CHECK(c.items[0].clipped);
  CHECK(c.items[0].content.size() <= rss::kMaxContentBytes);
  CHECK(c.texts[0].size() <= rss::kMaxContentBytes);
  CHECK(c.posts[0].title.size() <= rss::kMaxTitleBytes);
}

void testUntitledPost() {
  // A microblog's post has no headline; its first words stand in.
  const std::string doc =
      "<rss><channel><title>M</title><item><description>Just a thought about tea, longer than most "
      "headlines would ever dare to be in a list of headlines, which is the whole problem here.</description>"
      "<guid isPermaLink=\"false\">m-1</guid></item></channel></rss>";
  Collector c;
  parse(doc, c);
  CHECK_NUM(c.posts.size(), 1);
  if (c.posts.empty()) return;
  CHECK(c.posts[0].title.rfind("Just a thought about tea", 0) == 0);
  CHECK(c.posts[0].title.size() <= 123);
  CHECK(c.posts[0].link.empty());
}

void testIdsAreScopedToTheirFeed() {
  // Two feeds that both number their posts from 1 must not share a file.
  rss::Item item;
  item.title = "Hello";
  item.guid = "1";
  rss::Post a;
  rss::Post b;
  std::string text;
  CHECK(rss::ingest(item, "https://a.example/feed", rss::keyFor("https://a.example/feed"), a, text));
  CHECK(rss::ingest(item, "https://b.example/feed", rss::keyFor("https://b.example/feed"), b, text));
  CHECK(a.id != b.id);
  // And the same feed gives the same id every time.
  rss::Post again;
  rss::ingest(item, "https://a.example/feed", rss::keyFor("https://a.example/feed"), again, text);
  CHECK_EQ(again.id, a.id);
}

// --- Keeping the newest twenty -------------------------------------------

std::vector<rss::Post> keepFrom(const std::string& doc, bool* stoppedEarly = nullptr) {
  struct KeepSink : rss::ItemSink {
    rss::Keeper keeper;
    int offered = 0;
    bool onItem(rss::Item& item) override {
      rss::Post post;
      std::string text;
      if (!rss::ingest(item, "https://many.example/feed", "k", post, text)) return true;
      ++offered;
      std::string evicted;
      keeper.offer(std::move(post), evicted);
      return !keeper.saturated();
    }
  } sink;
  rss::FeedParser parser(&sink);
  parser.feed(doc.data(), doc.size());
  parser.finish();
  if (stoppedEarly != nullptr) *stoppedEarly = parser.stoppedBySink();
  return sink.keeper.take();
}

void testKeepsNewestTwentyNewestFirst() {
  bool stopped = false;
  const std::vector<rss::Post> kept = keepFrom(rssWithItems(60, true, true), &stopped);
  CHECK_NUM(kept.size(), rss::kPostsPerFeed);
  CHECK_EQ(kept.front().title, "Post 60");
  CHECK_EQ(kept.back().title, "Post 41");
  // A newest-first feed stops being read soon after the twentieth post.
  CHECK(stopped);
}

void testKeepsNewestTwentyOldestFirst() {
  bool stopped = true;
  const std::vector<rss::Post> kept = keepFrom(rssWithItems(60, false, true), &stopped);
  CHECK_NUM(kept.size(), rss::kPostsPerFeed);
  CHECK_EQ(kept.front().title, "Post 60");
  CHECK_EQ(kept.back().title, "Post 41");
  // Every post was newer than the last, so nothing could be cut short.
  CHECK(!stopped);
}

void testUndatedFeedKeepsItsOrder() {
  const std::vector<rss::Post> kept = keepFrom(rssWithItems(30, true, false));
  CHECK_NUM(kept.size(), rss::kPostsPerFeed);
  // No dates: the feed's own order is the only order, so its first twenty.
  CHECK_EQ(kept.front().title, "Post 30");
  CHECK_EQ(kept.back().title, "Post 11");
}

void testKeeperReportsEviction() {
  rss::Keeper keeper(2);
  std::string evicted;
  rss::Post p;
  p.id = "old";
  p.date = 100;
  CHECK(keeper.offer(rss::Post(p), evicted));
  p.id = "mid";
  p.date = 200;
  CHECK(keeper.offer(rss::Post(p), evicted));
  p.id = "new";
  p.date = 300;
  CHECK(keeper.offer(rss::Post(p), evicted));
  CHECK_EQ(evicted, "old");
  p.id = "ancient";
  p.date = 50;
  CHECK(!keeper.offer(rss::Post(p), evicted));
  CHECK(evicted.empty());
  // The same post listed twice is kept once.
  p.id = "new";
  p.date = 300;
  CHECK(!keeper.offer(rss::Post(p), evicted));
  const std::vector<rss::Post> kept = keeper.take();
  CHECK_NUM(kept.size(), 2);
  if (kept.size() == 2) {
    CHECK_EQ(kept[0].id, "new");
    CHECK_EQ(kept[1].id, "mid");
  }
}

// --- Finding the feed a page names ---------------------------------------

void testLinkScannerWordPressHead() {
  const std::string page =
      "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">"
      "<!-- <link rel=\"alternate\" type=\"application/rss+xml\" href=\"/commented-out\"> -->"
      "<script>var s = '<link rel=\"alternate\" type=\"application/rss+xml\" href=\"/in-script\">';</script>"
      "<style>a{}</style>"
      "<LINK REL='Alternate' TYPE='application/rss+xml' TITLE='Kettle &raquo; Feed' "
      "HREF='https://kettle.example/feed/?a=1&amp;b=2' />"
      "<link rel=\"alternate\" type=\"application/rss+xml\" title=\"Comments\" href=\"/comments/feed/\">"
      "<link rel=alternate type=application/atom+xml href=/atom.xml>"
      "<link rel=\"alternate\" type=\"application/json\" href=\"/feed.json\">"
      "<link rel=\"stylesheet\" type=\"text/css\" href=\"/style.css\">"
      "</head><body><link rel=\"alternate\" type=\"application/rss+xml\" href=\"/after-head\"></body></html>";
  rss::LinkScanner scanner;
  scanner.feed(page.data(), page.size());
  CHECK(scanner.done());
  const std::vector<std::string>& feeds = scanner.feeds();
  CHECK_NUM(feeds.size(), 3);
  if (feeds.size() == 3) {
    CHECK_EQ(feeds[0], "https://kettle.example/feed/?a=1&b=2");
    CHECK_EQ(feeds[1], "/comments/feed/");
    CHECK_EQ(feeds[2], "/atom.xml");
  }

  // Byte at a time, for the state carried across chunks.
  rss::LinkScanner bytes;
  for (const char ch : page) bytes.feed(&ch, 1);
  CHECK(bytes.feeds() == scanner.feeds());
}

void testLinkScannerStopsAtBody() {
  rss::LinkScanner scanner;
  const std::string page = "<html><body><p>No head at all</p>";
  scanner.feed(page.data(), page.size());
  CHECK(scanner.done());
  CHECK(scanner.feeds().empty());
}

void testProbe() {
  {
    Collector c;
    rss::Probe probe(&c);
    const std::string doc = kAtom;
    for (size_t i = 0; i < doc.size(); i += 100) {
      if (!probe.feed(doc.data() + i, std::min<size_t>(100, doc.size() - i))) break;
    }
    probe.finish();
    CHECK(probe.isFeed());
    CHECK_NUM(c.posts.size(), 4);
    CHECK_EQ(probe.parser().title(), "Field Notes");
  }
  {
    Collector c;
    rss::Probe probe(&c);
    const std::string page =
        "<!doctype html><html><head><link rel=\"alternate\" type=\"application/rss+xml\" href=\"/feed\">"
        "</head><body>" +
        std::string(100000, 'z');
    bool more = true;
    size_t fed = 0;
    for (size_t i = 0; more && i < page.size(); i += 512) {
      more = probe.feed(page.data() + i, std::min<size_t>(512, page.size() - i));
      fed = i + 512;
    }
    probe.finish();
    CHECK(!probe.isFeed());
    // The download stops at the end of the head, not at the end of the page.
    CHECK(fed < 4096);
    CHECK_NUM(probe.links().feeds().size(), 1);
    CHECK(c.posts.empty());
  }
}

// --- Addresses -----------------------------------------------------------

void testNormalizeAddress() {
  CHECK_EQ(rss::normalizeAddress("  kettle.example "), "https://kettle.example");
  CHECK_EQ(rss::normalizeAddress("HTTPS://Kettle.Example/Feed"), "https://kettle.example/Feed");
  CHECK_EQ(rss::normalizeAddress("http://kettle.example/rss"), "http://kettle.example/rss");
  CHECK_EQ(rss::normalizeAddress("feed://kettle.example/rss"), "https://kettle.example/rss");
  CHECK_EQ(rss::normalizeAddress("feed:https://kettle.example/rss"), "https://kettle.example/rss");
  CHECK_EQ(rss::normalizeAddress("kettle.example:8080/x"), "https://kettle.example:8080/x");
  CHECK_EQ(rss::normalizeAddress(""), "");
  CHECK_EQ(rss::normalizeAddress("javascript:alert(1)"), "");
  CHECK_EQ(rss::normalizeAddress("ftp://kettle.example/"), "");
  CHECK_EQ(rss::normalizeAddress("two words"), "");
}

void testResolveUrl() {
  const char* base = "https://a.example/blog/2026/post.html?q=1#top";
  CHECK_EQ(rss::resolveUrl(base, "https://b.example/x"), "https://b.example/x");
  CHECK_EQ(rss::resolveUrl(base, "//c.example/y"), "https://c.example/y");
  CHECK_EQ(rss::resolveUrl(base, "/feed.xml"), "https://a.example/feed.xml");
  CHECK_EQ(rss::resolveUrl(base, "feed.xml"), "https://a.example/blog/2026/feed.xml");
  CHECK_EQ(rss::resolveUrl(base, "../feed/"), "https://a.example/blog/feed/");
  CHECK_EQ(rss::resolveUrl(base, "../../../../feed"), "https://a.example/feed");
  CHECK_EQ(rss::resolveUrl(base, "./x?y=1#z"), "https://a.example/blog/2026/x?y=1#z");
  CHECK_EQ(rss::resolveUrl(base, "?page=2"), "https://a.example/blog/2026/post.html?page=2");
  CHECK_EQ(rss::resolveUrl("https://a.example", "feed"), "https://a.example/feed");
  CHECK_EQ(rss::resolveUrl("https://a.example/", ""), "https://a.example/");
}

void testShortName() {
  CHECK_EQ(rss::shortName("Hacker News: Front Page"), "Hacker News");
  CHECK_EQ(rss::shortName("Ars Technica - All content"), "Ars Technica");
  CHECK_EQ(rss::shortName("BBC News | Home"), "BBC News");
  CHECK_EQ(rss::shortName("Daring Fireball"), "Daring Fireball");
  CHECK_EQ(rss::shortName("Simon Willison's Weblog"), "Simon Willison's Weblog");
  // A first half too short to be a name keeps the whole.
  CHECK_EQ(rss::shortName("A: Story"), "A: Story");
  // The earliest separator wins, and the result is trimmed.
  CHECK_EQ(rss::shortName("  Kettle - notes: tea  "), "Kettle");
}

void testLocalAddresses() {
  CHECK(rss::isLocalAddress("http://localhost/r"));
  CHECK(rss::isLocalAddress("http://LOCALHOST:8080/feed"));
  CHECK(rss::isLocalAddress("http://127.0.0.1/r"));
  CHECK(rss::isLocalAddress("https://127.12.0.9:443/x"));
  CHECK(rss::isLocalAddress("http://[::1]/feed"));
  CHECK(rss::isLocalAddress("http://0.0.0.0/"));
  CHECK(rss::isLocalAddress("http://169.254.1.1/feed"));
  CHECK(rss::isLocalAddress("http://reader.localhost/"));
  CHECK(rss::isLocalAddress("http://user@127.0.0.1/"));
  // A house's own feeds on its own network are a reader's business.
  CHECK(!rss::isLocalAddress("http://192.168.1.20/feed.xml"));
  CHECK(!rss::isLocalAddress("https://kettle.example/feed"));
  CHECK(!rss::isLocalAddress("https://127example.com/feed"));
  CHECK(!rss::isLocalAddress("not a url"));
}

void testHostOf() {
  CHECK_EQ(rss::hostOf("https://www.Kettle.example:443/feed"), "kettle.example");
  CHECK_EQ(rss::hostOf("http://user@notes.example/x"), "notes.example");
  CHECK_EQ(rss::hostOf("not a url"), "");
}

// --- Dates ---------------------------------------------------------------

void testDates() {
  CHECK_NUM(rss::parseDate("Fri, 16 Oct 2026 09:30:00 +0000"), 1792143000);
  CHECK_NUM(rss::parseDate("16 Oct 2026 09:30:00 GMT"), 1792143000);
  CHECK_NUM(rss::parseDate("Fri, 16 Oct 2026 09:30 GMT"), 1792143000);
  CHECK_NUM(rss::parseDate("Fri, 16 Oct 26 09:30:00 Z"), 1792143000);
  CHECK_NUM(rss::parseDate("Fri, 16 October 2026 05:30:00 EDT"), 1792143000);
  CHECK_NUM(rss::parseDate("Fri, 16 Oct 2026 11:30:00 +02:00"), 1792143000);
  CHECK_NUM(rss::parseDate("Fri 16 Oct 2026 09:30:00 XYZ"), 1792143000);  // unknown zone reads as UTC
  CHECK_NUM(rss::parseDate("Fri, 16 Oct 2026"), 1792108800);
  CHECK_NUM(rss::parseDate("2026-10-16T09:30:00Z"), 1792143000);
  CHECK_NUM(rss::parseDate("2026-10-16T09:30:00.123456Z"), 1792143000);
  CHECK_NUM(rss::parseDate("2026-10-16T11:30:00+02:00"), 1792143000);
  CHECK_NUM(rss::parseDate("2026-10-16T04:30:00-0500"), 1792143000);
  CHECK_NUM(rss::parseDate("2026-10-16 09:30:00"), 1792143000);
  CHECK_NUM(rss::parseDate("2026-10-16"), 1792108800);
  CHECK_NUM(rss::parseDate("  2000-01-01T00:00:00Z  "), 946684800);
  CHECK_NUM(rss::parseDate(""), 0);
  CHECK_NUM(rss::parseDate("yesterday"), 0);
  CHECK_NUM(rss::parseDate("2026-13-01"), 0);
  CHECK_NUM(rss::parseDate("32 Foo 2026 10:00:00 GMT"), 0);
  CHECK_NUM(rss::parseDate("1969-12-31T23:59:59Z"), 0);

  CHECK_EQ(rss::formatDate(1792143000), "16 Oct 2026");
  CHECK_EQ(rss::formatDate(946684800), "1 Jan 2000");
  CHECK_EQ(rss::formatDate(951782400), "29 Feb 2000");
  CHECK_EQ(rss::formatDate(0), "");
  // Round trip across a range, so the two halves of the calendar agree.
  bool agree = true;
  for (uint32_t t = 1000000000u; t < 2000000000u; t += 86400u * 37u) {
    agree = agree && rss::parseDate(rss::formatDate(t)) == t - t % 86400u;
  }
  CHECK(agree);
}

// --- Text ----------------------------------------------------------------

void testParagraphs() {
  const std::vector<std::string> p = rss::paragraphsFromHtml(
      "<div><h2>Head</h2><p>One &amp; <b>two</b>.<br>Three</p><figure><img src=x alt=\"no\">"
      "<figcaption>Cap</figcaption></figure><style>p{}</style><p>a &lt; b and 1 < 2</p>"
      "<table><tr><td>x</td><td>y</td></tr></table><pre>line 1\nline 2</pre>"
      "<p>&lt;script&gt;shown&lt;/script&gt;</p><p>   </p><p>\xE2\x80\x9CQ\xE2\x80\x9D \xE2\x80\x94 ok</p></div>");
  const std::vector<std::string> want = {"Head",       "One & two.",      "Three",
                                         "Cap",        "a < b and 1 < 2", "x y",
                                         "line 1",     "line 2",          "<script>shown</script>",
                                         "\"Q\" -- ok"};
  CHECK_NUM(p.size(), want.size());
  for (size_t i = 0; i < p.size() && i < want.size(); ++i) CHECK_EQ(p[i], want[i]);
}

void testParagraphsAreBounded() {
  std::string html;
  for (int i = 0; i < 2000; ++i) html += "<p>Paragraph number " + std::to_string(i) + " of many.</p>";
  size_t total = 0;
  for (const std::string& p : rss::paragraphsFromHtml(html, 1000)) total += p.size();
  CHECK(total <= 1000);
  CHECK(total > 900);
}

void testPlainLine() {
  CHECK_EQ(rss::plainLine("  A <em>very</em>\n  long &amp;amp; title  "), "A very long &amp; title");
  CHECK_EQ(rss::plainLine("x &lt; y"), "x < y");
  CHECK_EQ(rss::plainLine("Price &euro;5 &#8230;"), "Price EUR5 ...");
  // Daring Fireball marks its own posts with a star, and no cut here has one:
  // it would draw as a hole. Emoji and their joiners go the same way; the
  // arrow the fold knows becomes ASCII first; letters of any script stay.
  CHECK_EQ(rss::plainLine("\xE2\x98\x85 Apple&#8217;s Q4 \xE2\x98\x85"), "Apple's Q4");
  CHECK_EQ(rss::plainLine("Ship it \xF0\x9F\x9A\x80\xEF\xB8\x8F now &#x1F469;&#x200D;&#x1F4BB;"), "Ship it now");
  CHECK_EQ(rss::plainLine("A &rarr; B"), "A -> B");
  CHECK_EQ(rss::plainLine("\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82 caf\xC3\xA9"),
           "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82 caf\xC3\xA9");
}

void testDecodeEntities() {
  CHECK_EQ(rss::decodeEntities("&eacute;&Eacute;&yuml;&nbsp;&#233;&#xE9;&#146;&unknown;&;& x"),
           "\xC3\xA9\xC3\x89\xC3\xBF\xC2\xA0\xC3\xA9\xC3\xA9\xE2\x80\x99&unknown;&;& x");
  // A reference to a control character is nothing to see, and goes.
  CHECK_EQ(rss::decodeEntities("a&#1;b&#x1F;c&#10;d"), "abc\nd");
  CHECK_EQ(rss::decodeEntities("x&#55357;&#56832;y"), "xy");
}

// --- Dates across refreshes, and the combined list -----------------------

rss::Post post(const char* id, const char* feed, const uint32_t date, const char* title = "t") {
  rss::Post p;
  p.id = id;
  p.feed = feed;
  p.date = date;
  p.title = title;
  return p;
}

void testSettleDates() {
  const uint32_t now = 1800000000u;
  std::vector<rss::Post> previous = {post("a", "f", now - 500)};
  std::vector<rss::Post> fresh = {post("a", "f", 0), post("b", "f", 0), post("c", "f", 0), post("d", "f", now + 999)};
  rss::settleDates(fresh, previous, now);
  // Seen before: keeps the date it was first seen at.
  CHECK_NUM(fresh[0].date, now - 500);
  // New and undated: now, one second apart in feed order.
  CHECK_NUM(fresh[1].date, now);
  CHECK_NUM(fresh[2].date, now - 1);
  // From the future: pulled back to now.
  CHECK_NUM(fresh[3].date, now);

  // With no clock nothing is invented.
  std::vector<rss::Post> unset = {post("x", "f", 0)};
  rss::settleDates(unset, previous, 12345);
  CHECK_NUM(unset[0].date, 0);
}

void testCombine() {
  std::vector<rss::Feed> feeds(3);
  feeds[0].url = "https://one.example/feed";
  feeds[1].url = "https://two.example/feed";
  feeds[2].url = "https://three.example/feed";
  const std::string k1 = rss::keyFor(feeds[0].url);
  const std::string k2 = rss::keyFor(feeds[1].url);
  const std::string k3 = rss::keyFor(feeds[2].url);
  const std::vector<rss::Post> previous = {post("old1", k1.c_str(), 10, "was"), post("old2", k2.c_str(), 20),
                                           post("gone", "removed-feed", 99), post("old3", k3.c_str(), 30)};
  // Feed one sends a new post and its old one again with a new headline; feed
  // two sends one post; feed three sends nothing.
  const std::vector<rss::Post> fresh = {post("new1", k1.c_str(), 15), post("old1", k1.c_str(), 10, "now"),
                                        post("new2", k2.c_str(), 5)};
  const std::vector<rss::Post> out = rss::combine(feeds, previous, fresh);
  CHECK_NUM(out.size(), 5);
  if (out.size() == 5) {
    CHECK_EQ(out[0].id, "old3");  // kept: its feed sent nothing
    CHECK_EQ(out[1].id, "old2");  // kept beside the new one
    CHECK_EQ(out[2].id, "new1");
    CHECK_EQ(out[3].id, "old1");
    CHECK_EQ(out[3].title, "now");  // the fresh copy wins
    CHECK_EQ(out[4].id, "new2");
  }
  // The posts of a feed no longer subscribed are dropped.
  bool sawGone = false;
  for (const rss::Post& p : out) sawGone = sawGone || p.id == "gone";
  CHECK(!sawGone);

  // A feed cut short keeps what it had: three fresh posts beside fifteen old
  // ones is eighteen, not three. And never more than twenty per feed.
  std::vector<rss::Post> many;
  for (int i = 0; i < 15; ++i) many.push_back(post(("o" + std::to_string(i)).c_str(), k1.c_str(), 100 + i));
  std::vector<rss::Post> cut;
  for (int i = 0; i < 3; ++i) cut.push_back(post(("n" + std::to_string(i)).c_str(), k1.c_str(), 200 + i));
  CHECK_NUM(rss::combine(feeds, many, cut).size(), 18);
  for (int i = 3; i < 10; ++i) cut.push_back(post(("n" + std::to_string(i)).c_str(), k1.c_str(), 200 + i));
  const std::vector<rss::Post> capped = rss::combine(feeds, many, cut);
  CHECK_NUM(capped.size(), rss::kPostsPerFeed);
  // The ones that fell off are the oldest.
  if (!capped.empty()) CHECK_NUM(capped.back().date, 105);
}

// --- On the card ---------------------------------------------------------

void testFeedsRoundTrip() {
  std::vector<rss::Feed> feeds(2);
  feeds[0].url = "https://kettle.example/feed/";
  feeds[0].title = "The\tQuiet\nKettle";
  feeds[1].url = "https://notes.example/atom.xml";
  feeds[1].title = "";
  feeds[1].failed = true;
  std::vector<rss::Feed> back;
  CHECK(rss::parseFeeds(rss::serializeFeeds(feeds), back));
  CHECK_NUM(back.size(), 2);
  if (back.size() != 2) return;
  CHECK_EQ(back[0].title, "The Quiet Kettle");
  CHECK(!back[0].failed);
  // A feed with no name is named by its host.
  CHECK_EQ(back[1].title, "notes.example");
  CHECK(back[1].failed);

  CHECK(!rss::parseFeeds("rssfeeds 2\nhttps://x\tX\t0\n", back));  // from a newer build: left alone
  CHECK(!rss::parseFeeds("hnsaved 2\n", back));
  CHECK(rss::parseFeeds("rssfeeds 1\n\t\t\nhttps://x.example\tX\t0\r\nhttps://x.example\tDup\t0\n", back));
  CHECK_NUM(back.size(), 1);  // the empty row and the duplicate are skipped
}

void testPostsRoundTrip() {
  std::vector<rss::Post> posts = {post("a1", "f1", 123, "One\ttab"), post("b2", "f2", 0, "Two")};
  posts[0].link = "https://x.example/1";
  posts[0].body = rss::Body::Full;
  posts[1].body = rss::Body::Summary;
  std::vector<rss::Post> back;
  CHECK(rss::parsePosts(rss::serializePosts(posts), back));
  CHECK_NUM(back.size(), 2);
  if (back.size() != 2) return;
  CHECK_EQ(back[0].title, "One tab");
  CHECK_EQ(back[0].link, "https://x.example/1");
  CHECK_NUM(back[0].date, 123);
  CHECK(back[0].body == rss::Body::Full);
  CHECK(back[1].body == rss::Body::Summary);
  CHECK(back[1].link.empty());

  // A damaged row is skipped and the rest survive.
  CHECK(rss::parsePosts("rssposts 1\nonly-an-id\nc3\tf\t5\t9\tKept\thttps://k\n", back));
  CHECK_NUM(back.size(), 1);
  if (!back.empty()) CHECK(back[0].body == rss::Body::None);
}

void testReadState() {
  rss::ReadSet read;
  CHECK(read.add("bbb"));
  CHECK(read.add("aaa"));
  CHECK(!read.add("aaa"));
  CHECK(!read.add(""));
  CHECK(read.contains("aaa"));
  CHECK(!read.contains("ccc"));

  rss::ReadSet back;
  CHECK(rss::parseRead(rss::serializeRead(read), back));
  CHECK(back.contains("aaa") && back.contains("bbb"));
  CHECK_NUM(back.ids().size(), 2);

  const std::vector<rss::Post> posts = {post("bbb", "f", 1), post("zzz", "g", 2)};
  CHECK_NUM(rss::unreadCount(posts, back, ""), 1);
  CHECK_NUM(rss::unreadCount(posts, back, "f"), 0);
  CHECK_NUM(rss::unreadCount(posts, back, "g"), 1);

  // Read marks for posts that have left every feed are forgotten.
  back.retainOnly(posts);
  CHECK(!back.contains("aaa"));
  CHECK(back.contains("bbb"));

  CHECK(!rss::parseRead("rssread 9\naaa\n", back));
}

// --- Between the phone page and the app ----------------------------------

void testDeskQueuesAndAnswers() {
  rss::Desk desk;
  // Nothing to add is not a request, and neither is a pasted book.
  CHECK_NUM(desk.requestAdd("   \n"), -1);
  CHECK_NUM(desk.requestAdd(std::string(rss::Desk::kMaxRequestBytes + 1, 'a')), -1);

  const int a = desk.requestAdd("kettle.example");
  const int b = desk.requestRemove("https://notes.example/atom.xml");
  CHECK(a > 0 && b > a);
  // A page cannot queue more than a handful behind a slow one.
  for (size_t i = 2; i < rss::Desk::kMaxQueued; ++i) CHECK(desk.requestAdd("x.example") > 0);
  CHECK_NUM(desk.requestAdd("one.too.many"), -1);

  rss::Request request;
  CHECK(desk.take(request));
  CHECK(request.kind == rss::Request::Kind::Add);
  CHECK_NUM(request.job, a);
  CHECK_EQ(request.text, "kettle.example");
  // Taken is working, and the page can see it.
  CHECK(desk.stateJson().find("\"id\":" + std::to_string(a) + ",\"state\":\"working\"") != std::string::npos);
  desk.finish(a, false, "No feed at that address.");
  const std::string json = desk.stateJson();
  CHECK(json.find("\"id\":" + std::to_string(a) + ",\"state\":\"failed\"") != std::string::npos);
  CHECK(json.find("No feed at that address.") != std::string::npos);

  CHECK(desk.take(request));
  CHECK(request.kind == rss::Request::Kind::Remove);
  CHECK_EQ(request.text, "https://notes.example/atom.xml");
  desk.finish(request.job, true, "Removed.");
  CHECK(desk.stateJson().find("\"state\":\"done\"") != std::string::npos);

  // What the page lists, with each feed's post count.
  std::vector<rss::Feed> feeds(1);
  feeds[0].url = "https://kettle.example/feed";
  feeds[0].title = "The \"Quiet\" Kettle";
  feeds[0].failed = true;
  rss::Post p;
  p.id = "1";
  p.feed = rss::keyFor(feeds[0].url);
  p.title = "t";
  desk.publish(feeds, {p, p});
  const std::string state = desk.stateJson();
  CHECK(state.rfind("{\"max\":30,\"feeds\":[{\"url\":\"https://kettle.example/feed\"", 0) == 0);
  CHECK(state.find("\"title\":\"The \\\"Quiet\\\" Kettle\",\"failed\":true,\"posts\":2}") != std::string::npos);

  desk.clear();
  CHECK_EQ(desk.stateJson(), "{\"max\":30,\"feeds\":[],\"jobs\":[]}");
}

void testJsonString() {
  CHECK_EQ(rss::jsonString("plain"), "\"plain\"");
  CHECK_EQ(rss::jsonString("a\"b\\c\nd\te\x01"), "\"a\\\"b\\\\c\\nd\\te\\u0001\"");
  // UTF-8 passes through: a feed's name in its own language reaches the page.
  CHECK_EQ(rss::jsonString("caf\xC3\xA9"), "\"caf\xC3\xA9\"");
}

}  // namespace

int main() {
  testRss2();
  testAtom();
  testRdf();
  testChunkingChangesNothing();
  testHtmlEntitiesOutsideCdata();
  testEntityFixerLeavesCdataAlone();
  testFeedsExpatWouldRefuse();
  testWindows1252();
  testTruncatedFeedKeepsItsTop();
  testGarbageMidFeed();
  testNotAFeed();
  testSinkCanStop();
  testFieldLimits();
  testUntitledPost();
  testIdsAreScopedToTheirFeed();
  testKeepsNewestTwentyNewestFirst();
  testKeepsNewestTwentyOldestFirst();
  testUndatedFeedKeepsItsOrder();
  testKeeperReportsEviction();
  testLinkScannerWordPressHead();
  testLinkScannerStopsAtBody();
  testProbe();
  testNormalizeAddress();
  testResolveUrl();
  testShortName();
  testLocalAddresses();
  testHostOf();
  testDates();
  testParagraphs();
  testParagraphsAreBounded();
  testPlainLine();
  testDecodeEntities();
  testSettleDates();
  testCombine();
  testFeedsRoundTrip();
  testPostsRoundTrip();
  testReadState();
  testDeskQueuesAndAnswers();
  testJsonString();
  std::printf("%d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}

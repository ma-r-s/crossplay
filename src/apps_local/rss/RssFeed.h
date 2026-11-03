#pragma once

// Reading a feed off the socket, and finding the feed a web page names.
//
// Freestanding apart from expat (lib/expat), which is plain C and builds on a
// laptop, so host-tests/rss/ drives every byte of this with real feeds.
//
// ---------------------------------------------------------------------------
// Nothing here holds a feed. Bytes arrive in whatever chunks the socket hands
// over, expat turns them into elements, and each post is handed to a sink the
// moment its closing tag arrives -- so a feed of a thousand posts, each carrying
// its whole text, costs one post's worth of RAM.
//
// Three formats, told apart by their root element and nothing else:
//
//   <rss>       RSS 2.0 (and 0.9x): channel/item, pubDate, content:encoded
//   <feed>      Atom: entry, link href=, published/updated, content type=
//   <rdf:RDF>   RSS 1.0: item beside the channel, dc:date, content:encoded
//
// Expat here is built without namespace support (XML_NS is not defined), so
// names arrive as written -- "content:encoded", "dc:date" -- and are matched by
// their local part. A prefixed element inside RSS is an extension unless it is
// one of the few this knows; inside Atom every prefix is Atom's own.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "RssCore.h"

namespace rss {

// One post as the feed wrote it. Markup and entities are still in the text;
// ingest() is what turns it into something to draw.
struct Item {
  std::string title;
  std::string link;
  std::string guid;
  std::string dateText;
  // The whole post: content:encoded, or Atom <content>. Always markup: Atom's
  // type="text" is escaped into it on the way in.
  std::string content;
  // The summary: <description>, or Atom <summary>. Markup, as above.
  std::string summary;
  // Either was cut at its limit.
  bool clipped = false;

  void clear();
};

class ItemSink {
 public:
  virtual ~ItemSink() = default;
  // Returns false to stop reading the feed.
  virtual bool onItem(Item& item) = 0;
};

// ---------------------------------------------------------------------------
// Making a careless feed parse.
//
// XML has five named entities. Feeds are written by HTML tools and routinely
// carry "&nbsp;" or "&mdash;" outside CDATA, or a bare "AT&T", and expat stops
// dead at the first one -- often in the first post's title, which would leave
// a feed that every browser reads showing nothing here. So every ampersand
// that does not begin one of XML's own references is escaped before expat sees
// it: "&nbsp;" arrives in the text as the five characters "&nbsp;", and
// decodeEntities() turns it into a space later, the same as it would for a
// feed that had escaped it properly. CDATA sections and comments pass through
// untouched, because nothing in them is a reference.
// ---------------------------------------------------------------------------
class EntityFixer {
 public:
  // Appends the corrected bytes to `out`. State carries across calls, so an
  // entity or a CDATA marker split between two chunks is still seen whole.
  void feed(const char* data, size_t length, std::string& out);
  // Flushes anything held back at the end of the document.
  void finish(std::string& out);

 private:
  enum class State : uint8_t { Text, Markup, Cdata, Comment, Entity };
  void text(char c, std::string& out);
  void endEntity(char next, bool terminated, std::string& out);

  State state_ = State::Text;
  // The bytes of "<![CDATA[" or "<!--" matched so far, or of an entity.
  std::string held_;
  int closeMatch_ = 0;  // how much of "]]>" or "-->" has been seen
};

enum class Kind : uint8_t {
  Unknown,  // no root element yet
  Rss,      // RSS 2.0 or 0.9x
  Atom,
  Rdf,      // RSS 1.0
  NotFeed,  // a document whose root is something else -- an HTML page
};

struct ParserLimits {
  size_t titleBytes = kMaxTitleBytes;
  size_t linkBytes = kMaxLinkBytes;
  size_t contentBytes = kMaxContentBytes;
  size_t summaryBytes = kMaxSummaryBytes;
};

class FeedParser {
 public:
  explicit FeedParser(ItemSink* sink, const ParserLimits& limits = ParserLimits{});
  ~FeedParser();
  FeedParser(const FeedParser&) = delete;
  FeedParser& operator=(const FeedParser&) = delete;

  // Returns false once there is no point feeding more: the document is not a
  // feed, it stopped parsing, or the sink asked to stop. Posts already handed
  // to the sink stand either way -- a feed broken halfway down still has a top.
  bool feed(const char* data, size_t length);
  void finish();

  Kind kind() const { return kind_; }
  bool isFeed() const { return kind_ == Kind::Rss || kind_ == Kind::Atom || kind_ == Kind::Rdf; }
  // The feed's own name, as one plain line. Empty when it gives none.
  std::string title() const;
  // The site the feed belongs to (channel <link>, or Atom's alternate).
  const std::string& siteLink() const { return siteLink_; }
  int items() const { return items_; }
  // Stopped on a document that is not well-formed. Whatever came before stands.
  bool malformed() const { return malformed_; }
  bool stoppedBySink() const { return stoppedBySink_; }

 private:
  // How a field's text is collected.
  enum class Mode : uint8_t {
    Plain,  // character data as it comes; child elements ignored
    Html,   // character data IS markup (escaped HTML); child elements re-serialised
    Xhtml,  // character data is text, child elements are markup (Atom type="xhtml")
    Text,   // plain text that must be escaped to become markup (Atom type="text")
  };
  enum class Field : uint8_t { None, Title, Link, Guid, Date, Content, Summary, FeedTitle, FeedLink };

  static void onStart(void* self, const char* name, const char** attrs);
  static void onEnd(void* self, const char* name);
  static void onText(void* self, const char* text, int length);
  void start(const char* name, const char** attrs);
  void end(const char* name);
  void text(const char* data, int length);
  void begin(Field field, Mode mode, size_t limit);
  void commit();
  void stop(bool bySink);

  ItemSink* sink_;
  ParserLimits limits_;
  void* parser_ = nullptr;  // XML_Parser, kept opaque so expat stays out of this header
  EntityFixer fixer_;
  std::string scratch_;

  Kind kind_ = Kind::Unknown;
  int depth_ = 0;
  bool inChannel_ = false;
  bool inItem_ = false;
  int itemDepth_ = 0;
  Item item_;
  // Which of the date elements the current item's date came from. A post's
  // publication date beats the date it was last touched.
  int datePriority_ = 0;
  bool haveAltLink_ = false;

  Field field_ = Field::None;
  Mode mode_ = Mode::Plain;
  int fieldDepth_ = 0;
  size_t fieldLimit_ = 0;
  int pendingDatePriority_ = 0;
  std::string buffer_;
  bool fieldClipped_ = false;

  std::string title_;
  std::string siteLink_;
  int items_ = 0;
  bool done_ = false;
  bool malformed_ = false;
  bool stoppedBySink_ = false;
};

// ---------------------------------------------------------------------------
// The feeds a web page names in its <head>:
//
//   <link rel="alternate" type="application/rss+xml" href="/feed.xml">
//
// A tag scanner rather than a parser: HTML is not XML, and the only thing
// wanted is a handful of attributes on one kind of tag before the body starts.
// Script, style and comments are skipped so a "<link" in a string is not one.
// ---------------------------------------------------------------------------
class LinkScanner {
 public:
  void feed(const char* data, size_t length);
  // The head has ended (</head> or <body>), so no feed link can follow.
  bool done() const { return done_; }
  // The feeds named, in the order the page lists them, as written (relative
  // hrefs are resolved by the caller, which knows the page's address).
  const std::vector<std::string>& feeds() const { return feeds_; }

 private:
  enum class State : uint8_t { Text, Tag, Comment, Skip };
  void endTag();

  State state_ = State::Text;
  std::string tag_;
  bool tagTooLong_ = false;
  char quote_ = 0;
  // While skipping a <script> or <style>: the element's name, and how much of
  // its closing tag has been matched.
  std::string skipName_;
  size_t skipMatch_ = 0;
  int commentMatch_ = 0;
  std::vector<std::string> feeds_;
  bool done_ = false;
};

// ---------------------------------------------------------------------------
// One address, either kind. What a person pastes is a feed or a page, and
// nothing says which until the bytes arrive -- so both readers take the same
// bytes, and whichever recognises the document decides. A feed is read in full
// on the spot (its posts reach the sink, which is what lets adding a feed and
// reading it be one request); a page is read only as far as its head.
// ---------------------------------------------------------------------------
class Probe {
 public:
  explicit Probe(ItemSink* sink) : parser_(sink) {}

  // Returns false when the download can stop.
  bool feed(const char* data, size_t length);
  void finish();

  bool isFeed() const { return parser_.isFeed(); }
  const FeedParser& parser() const { return parser_; }
  const LinkScanner& links() const { return links_; }

 private:
  FeedParser parser_;
  LinkScanner links_;
};

// ---------------------------------------------------------------------------
// One item made into a post. The link is resolved against the feed's address,
// the id comes from the feed's own guid or id (else the link, else the title),
// the date is parsed, and the words become paragraphs: the whole post when the
// feed carried it, the summary otherwise.
//
// `text` receives the words to keep on the card (empty when there are none),
// and post.body says which kind they are.
// ---------------------------------------------------------------------------
bool ingest(const Item& item, const std::string& feedUrl, const std::string& feedKey, Post& post, std::string& text);

}  // namespace rss

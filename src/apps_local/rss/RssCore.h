#pragma once

// RSS: the rules, the formats on the card, and the text handling. Freestanding
// on purpose -- no Arduino, no renderer, no SD card -- so host-tests/rss/ can
// drive all of it on a laptop.
//
// ---------------------------------------------------------------------------
// The shape of it.
//
// A FEED is an address and the name the feed gives itself. A POST is one item
// out of a feed: its headline, its link, its date, and whether its words came
// with it. The words themselves never live in RAM beside the list: a post the
// feed carried whole is a text file on the card, written once, the moment it
// arrives, and read back only when somebody opens it.
//
// The list the reader sees is every feed's posts at once, newest first. Each
// feed keeps its latest kPostsPerFeed; nothing older is kept and nothing is
// fetched except when the app opens.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rss {

// --- Limits --------------------------------------------------------------
//
// Each of these bounds something a stranger's server decides the size of.

// The design's number: the latest twenty posts of each feed.
constexpr int kPostsPerFeed = 20;
// Thirty feeds is six hundred rows. Past that the refresh on opening is the
// better part of a minute, and the list is a different product.
constexpr int kMaxFeeds = 30;
// One feed's download, whole. A feed that carries every post in full can run to
// megabytes; the newest twenty are near the top, and the rest is not kept.
constexpr size_t kMaxFeedBytes = 1024u * 1024u;
// One post's markup as the feed carried it. The same ceiling Hacker News puts on
// an article, for the same reason: the largest real one measured was a third
// of it.
constexpr size_t kMaxContentBytes = 96u * 1024u;
constexpr size_t kMaxSummaryBytes = 16u * 1024u;
constexpr size_t kMaxTitleBytes = 300;
constexpr size_t kMaxLinkBytes = 1024;
// A web page is read only as far as its <head>, looking for the feed it names.
// Some heads carry a hundred kilobytes of inline style before that line.
constexpr size_t kMaxPageBytes = 512u * 1024u;

// --- The model -----------------------------------------------------------

struct Feed {
  // What is fetched: the feed's own address, never the site's.
  std::string url;
  // The name the feed gives itself, folded for display. The site's host when
  // the feed has no title.
  std::string title;
  // The last refresh did not arrive. Its posts are the previous ones.
  bool failed = false;
};

// Where a post's words are.
enum class Body : uint8_t {
  None = 0,     // only the link: the page is read through the extractor
  Full = 1,     // the feed carried the whole post; it is on the card
  Summary = 2,  // the feed carried a summary only; it is on the card as a fallback
};

struct Post {
  // Stable across refreshes: derived from the feed's own id for the post (its
  // guid or Atom id), else its link. Also the body's filename.
  std::string id;
  // Which feed it came from: keyFor(feed.url). A key rather than an index, so
  // removing a feed does not move every other feed's posts onto its neighbour.
  std::string feed;
  std::string title;
  std::string link;
  // Unix seconds, UTC. 0 when the feed gave no date and the clock was unset.
  uint32_t date = 0;
  Body body = Body::None;
};

// The FNV-1a name of a string, the same one Hacker News's saved library uses.
std::string keyFor(std::string_view text);

// --- Addresses -----------------------------------------------------------

// What a person typed on their phone, as an address the device can fetch, or
// empty when it cannot be one. Adds https:// to a bare host, turns feed:// into
// https://, and refuses any other scheme.
std::string normalizeAddress(std::string_view typed);

// `href` resolved against `base`. Handles an absolute URL, a scheme-relative
// one, an absolute path, a relative path, a query and a fragment. A feed's
// links and a page's <link> are routinely relative.
std::string resolveUrl(std::string_view base, std::string_view href);

// The host, without a leading "www.", for naming a feed that names nothing.
std::string hostOf(std::string_view url);

// An address that points back at the reader itself or at nothing on a network:
// localhost, 127.x, ::1, 0.0.0.0, link-local 169.254.x. The phone page is open
// to anyone on the network while it is up, and a feed added at the reader's own
// address would have it wait on a server its own loop is busy not answering.
bool isLocalAddress(std::string_view url);

// A feed's name as the footnote on each of its posts: the part before a
// separator, because feeds name themselves "Hacker News: Front Page" and
// "Ars Technica - All content" and the site is the first half. A name with no
// separator, or one whose first half is too short to be a name, is kept whole.
std::string shortName(std::string_view title);

// --- Dates ---------------------------------------------------------------

// RFC 822 (RSS 2.0's pubDate, with or without the weekday, the seconds or a
// four-digit year, with a numeric offset or a US zone name) and ISO 8601 /
// RFC 3339 (Atom and Dublin Core, with Z, an offset, fractional seconds or a
// bare date). Unix seconds in UTC, or 0 when it is neither.
uint32_t parseDate(std::string_view text);

// "16 Oct 2026", for the line under a post's headline. Empty for 0.
std::string formatDate(uint32_t unixSeconds);

// Below this the clock has never been set: the device boots at the epoch. The
// same floor Instapaper and Study use.
constexpr uint32_t kClockFloor = 1700000000u;

// --- Text ----------------------------------------------------------------

// HTML entities decoded to UTF-8: every numeric form, and the named ones feeds
// actually send (the Latin-1 letters and the typographic punctuation). An
// unknown name is left exactly as written. NOT folded.
std::string decodeEntities(std::string_view text);

// A headline as one line of plain text: tags stripped, entities decoded,
// whitespace collapsed, typography folded, pictographs (stars, emoji, the
// shapes no cut here can draw) dropped, bounded by kMaxTitleBytes. Titles
// arrive with markup and double-escaped entities often enough that every title
// goes through this.
std::string plainLine(std::string_view htmlish);

// A post's markup as paragraphs of plain text, folded for display. Block tags
// break paragraphs, a list item becomes "- ", script and style vanish with
// their contents, an image vanishes. `maxBytes` bounds what is kept.
std::vector<std::string> paragraphsFromHtml(std::string_view html, size_t maxBytes = kMaxContentBytes);

// Paragraphs joined the way the reader's document wants them.
std::string joinParagraphs(const std::vector<std::string>& paragraphs);

// --- Keeping the newest --------------------------------------------------
//
// A feed is read once, top to bottom, and only its newest kPostsPerFeed
// survive. Most feeds list newest first, so the first twenty are the twenty;
// some list oldest first, and some list no dates at all. So the keeper ranks
// rather than counts: a post with no date ranks by where it stood in the feed,
// earlier being newer, which is what an undated feed's order means.
class Keeper {
 public:
  explicit Keeper(int capacity = kPostsPerFeed) : capacity_(capacity) {}

  // Whether a post with this date would be kept, asked BEFORE its words are
  // written anywhere. `evicted` receives the id of a kept post this one
  // displaces, or is cleared.
  bool offer(Post&& post, std::string& evicted);

  // True once the keeper is full and several posts in a row have been turned
  // away: the rest of a newest-first feed cannot change the answer, so the
  // download can stop.
  bool saturated() const;

  // The kept posts, newest first.
  std::vector<Post> take();
  size_t size() const { return kept_.size(); }

 private:
  struct Entry {
    Post post;
    uint32_t order;
  };
  static bool newer(uint32_t dateA, uint32_t orderA, uint32_t dateB, uint32_t orderB);

  int capacity_;
  uint32_t seen_ = 0;
  int refusedInARow_ = 0;
  std::vector<Entry> kept_;
};

// How many refusals in a row mean a feed has nothing newer further down.
constexpr int kRefusalsToStop = 5;

// Gives undated posts a date so they take a place in the all-feeds list:
// the one they were first seen at, kept across refreshes so a post does not
// climb back to the top every time the app opens. `now` below kClockFloor
// means the clock is unset, and they stay at 0. Dates in the future are pulled
// back to now, so a feed with a wrong clock cannot pin itself to the top.
void settleDates(std::vector<Post>& fresh, const std::vector<Post>& previous, uint32_t now);

// The new list after a refresh. Each feed keeps its newest kPostsPerFeed of
// what it just sent and what it had before, the fresh copy winning where both
// have a post; posts of feeds no longer subscribed are dropped. Merging rather
// than replacing is what lets a feed that arrives cut short -- a server that
// stops at 16KB, as Slashdot's did while this was written -- add what it did
// send without taking away the posts it did not. Newest first; ties keep feed
// order.
std::vector<Post> combine(const std::vector<Feed>& feeds, const std::vector<Post>& previous,
                          const std::vector<Post>& fresh);

// Newest first, stable.
void sortNewestFirst(std::vector<Post>& posts);

// --- Read state ----------------------------------------------------------

class ReadSet {
 public:
  bool contains(std::string_view id) const;
  // Returns false when it was already there, so the caller knows whether the
  // card needs writing.
  bool add(std::string_view id);
  // Forgets every id that is not one of these posts, so the file cannot grow
  // forever with posts that have scrolled out of every feed.
  void retainOnly(const std::vector<Post>& posts);
  const std::vector<std::string>& ids() const { return ids_; }
  void clear() { ids_.clear(); }

 private:
  std::vector<std::string> ids_;  // sorted
};

int unreadCount(const std::vector<Post>& posts, const ReadSet& read, std::string_view feedKey);

// --- On the card ---------------------------------------------------------
//
// Three small text files, each a header line naming its format and version and
// then one tab-separated row per entry: readable in any editor and recoverable
// by hand. A damaged row is skipped rather than failing the file.

std::string serializeFeeds(const std::vector<Feed>& feeds);
bool parseFeeds(std::string_view text, std::vector<Feed>& out);

std::string serializePosts(const std::vector<Post>& posts);
bool parsePosts(std::string_view text, std::vector<Post>& out);

std::string serializeRead(const ReadSet& read);
bool parseRead(std::string_view text, ReadSet& out);

// Tabs and line breaks out of a field, so it cannot swallow the next one.
std::string flatField(std::string_view text);

}  // namespace rss

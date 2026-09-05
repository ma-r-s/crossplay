#pragma once

// Turning what Hacker News and a text extractor return into something this
// panel can draw. Freestanding on purpose: no Arduino, no renderer, no SD card,
// so host-tests/hackernews/ can drive all of it on a laptop.
//
// Two sources feed this, and they speak different dialects.
//
//   * Hacker News itself, through the Algolia API. Story and comment bodies are
//     small HTML fragments. The whole vocabulary, measured across a real thread
//     rather than guessed, is five tags (p, i, a, pre, code) and five entities
//     (&#x27; &quot; &#x2F; &gt; &lt;). That is a decoder, not a renderer.
//
//   * The linked article, through a text extractor that answers in Markdown.
//     Here the job is the opposite: decide whether what came back is prose at
//     all, and only then flatten it.
//
// The Algolia API is the one to use and the choice is not close. The official
// Firebase API needs one request per item, so a front page costs 31 round trips
// and a 57-comment thread costs 58. Algolia answers each in exactly one, with
// the comment tree already nested. On a battery-powered device over TLS that is
// the difference between a usable app and a spinner.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hn {

// --- The readability gate ------------------------------------------------
//
// Whether a page "is basically someone talking about something", which is the
// only question that decides between showing the article and showing the mark
// that says we cannot.
//
// Both numbers are measured, not chosen. Ten links from a real front page went
// through the extractor and were scored by the rule below:
//
//     PDF                    0        Twitter/X              0
//     blog post           1082        arXiv abstract      1288
//     Waymo blog          1937        news article        1983
//     SEC Form D          2787        Show HN essay      17986
//     GitHub blog        18789        ACM Queue          22028
//
// Nothing lands between 0 and 1082, so the threshold sits in a two-order-of-
// magnitude gap rather than on a slope. That is why this is a fixed number and
// not something that will need tuning: a page either has paragraphs or it does
// not, and pages without them fail by producing nothing at all.
//
// The failure modes it catches are the ones that actually occur. A PDF extracts
// to an empty body. A JS-only page (Twitter) extracts to an error sentence. A
// gallery or a download page extracts to a pile of link labels, and link labels
// are not prose because they never form a long block.

// A block has to be at least this long to count as a paragraph. Nav rows, link
// lists, captions and table cells are all shorter; the shortest real paragraph
// in the sample above was comfortably past it.
constexpr int kProseBlockMin = 200;

// And the blocks together have to reach this before we call a page readable.
constexpr int kReadableThreshold = 600;

// Sum of the lengths of Markdown blocks that read as paragraphs: at least
// kProseBlockMin long, and not a list, heading, table row or rule. Link targets
// are removed before measuring, so a paragraph is judged on its words rather
// than on the URLs hiding inside it. That matters: a short post that is mostly
// citations scored 0.69 link density and still reads fine.
int proseChars(std::string_view markdown);

// The gate itself.
inline bool readsAsProse(const std::string_view markdown) { return proseChars(markdown) >= kReadableThreshold; }

// The free half of the gate: URLs that cannot be an article no matter what
// comes back, so the list can mark them before anything is fetched and tapping
// one never spends a round trip to learn nothing. Deliberately short. A URL we
// are unsure about is fetched and judged on its content, which is the honest
// test; this list only holds the cases where fetching is certain waste.
bool urlCanBeArticle(std::string_view url);

// --- Hacker News's own text ----------------------------------------------

// Decode the entity forms HN emits: the five named ones it actually uses, plus
// &#NN; and &#xNN; numeric escapes.
//
// ALSO FOLDS. HN writes its apostrophes and quotes as numeric entities, so this
// function is where U+2019 and U+201C come into existence -- and the reading cut
// has no glyph for either, so they draw as nothing. utf8FoldTypography is
// applied on the way out. Text through here is for DISPLAY; do not point it at
// a URL or anything else that is hashed or compared.
void decodeEntities(std::string& text);

// An HN comment or text post as paragraphs of plain text, folded for display
// (see decodeEntities).
//
// Tags are stripped before entities are decoded, never the other way round: a
// comment discussing HTML contains &lt;script&gt;, and decoding first would
// turn the words someone wrote into a tag this function then deletes.
std::vector<std::string> paragraphsFromHnHtml(std::string_view html);

// --- The extractor's answer ----------------------------------------------

// The extractor prefixes its Markdown with a header block: Title, URL Source,
// sometimes Published Time and a cache Warning, then a "Markdown Content:"
// line. Split it, because the title is worth showing and the header must not be
// scored as prose.
struct Extracted {
  std::string title;  // folded for display; the body is folded per paragraph below
  std::string body;
};
Extracted splitExtractorResponse(std::string_view response);

// Markdown as paragraphs of plain text, folded for display (see
// decodeEntities). Link labels survive and their targets do not, images go
// entirely, and heading, emphasis, quote and rule syntax is removed. There is no styled text on this panel, so anything
// that cannot become a word is noise.
std::vector<std::string> paragraphsFromMarkdown(std::string_view markdown);

// --- The model the screens draw ------------------------------------------

struct Story {
  uint32_t id = 0;
  std::string title;
  std::string url;  // empty for an Ask HN or a text post
  std::string author;
  int points = 0;
  int commentCount = 0;
  // Set from urlCanBeArticle() when the list is parsed, so the row can carry
  // the mark without a fetch. A story with no URL is its own text and is always
  // readable; see ConnectionsActivity for why a flag beats recomputing.
  bool mayBeReadable = false;
};

struct Comment {
  std::string author;
  std::vector<std::string> paragraphs;
  int depth = 0;
};

// Indentation stops here rather than replies being dropped: a deep thread stays
// readable on a 480px panel instead of walking off the edge.
constexpr int kMaxCommentDepth = 5;

// --- Reading a comment tree without holding it ---------------------------
//
// Algolia answers items/<id> with the whole nested thread in one response, and
// that response is big: the largest thread on a real front page was 249KB of
// JSON carrying 146KB of comment text. Handing that to a DOM parser costs
// roughly half a megabyte to end up with the 146KB we wanted, which is the
// wrong trade on a device whose smaller target has 380KB of RAM in total.
//
// So it is scanned as it arrives off the socket and never assembled.
// HttpDownloader's chunk callback exists for exactly this.
//
// The obvious reuse, lib/JsonParser/StreamingJsonParser, does not fit: its
// token buffer is a fixed 512 bytes, and real comments run past that. A
// truncated comment is worse than a slow one, so this scanner accumulates a
// string value of any length and bounds itself where bounding is honest, on the
// total kept.
// ---------------------------------------------------------------------------
// One detail decides this class's shape. Algolia serialises an item's keys in
// alphabetical order, so `children` arrives BEFORE `text`:
//
//     {"author":"pg", "children":[ ...the whole subtree... ], "text":"..."}
//
// A scanner that emitted a comment when its object closed would therefore emit
// every reply before the comment being replied to, and would not even know the
// parent's text at the time. So a comment claims its place in the output the
// moment its object opens, and its fields are filled in later, in whatever
// order they turn up. Replies land after the slot their parent already holds,
// which is depth-first thread order, which is how the thread is read.
// ---------------------------------------------------------------------------
class CommentScanner {
 public:
  struct Limits {
    // Stop keeping comments past this much decoded text. A reader who wants
    // more of a 400-comment thread than this wants a laptop.
    size_t maxTextBytes = 48u * 1024u;
    int maxComments = 250;
    // A single comment longer than this is clipped rather than allowed to set
    // the memory ceiling on its own.
    size_t maxCommentBytes = 8u * 1024u;
  };

  CommentScanner(std::vector<Comment>& out, const Limits& limits);

  // Feed the response in whatever chunks arrive off the socket. Returns false
  // if the document is malformed or nested past what this will follow, which is
  // the caller's signal to abort the transfer.
  bool feed(const char* data, size_t length);

  // Comments seen across the whole response, including any past the budget, so
  // the screen can honestly say "first 250 of 434".
  int totalSeen() const { return seen_; }
  bool truncated() const { return truncated_; }

 private:
  // A children array is tracked as its own kind because depth is counted in
  // those and nothing else; the arrays inside `options` must not indent a
  // reply.
  enum class Container : uint8_t { Object, Array, ChildrenArray };

  struct Frame {
    Container kind = Container::Object;
    // Where this object's comment sits in `out_`, or -1 when the object is not
    // a kept comment: the root story, or one past the budget.
    int slot = -1;
  };

  bool push(Container kind);
  void pop();
  void onValue();
  Frame* enclosingObject();

  std::vector<Comment>& out_;
  Limits limits_;

  // Bounded for the same reason the SDK's parser bounds it: a malformed or
  // hostile response must not walk off the end of a stack.
  static constexpr size_t kMaxNesting = 48;
  Frame stack_[kMaxNesting];
  size_t depth_ = 0;
  int childDepth_ = 0;  // how many children arrays are currently open

  bool inString_ = false;
  bool escaped_ = false;
  bool isKey_ = false;  // the string being read sits before a colon
  bool wantValue_ = false;
  std::string buffer_;
  std::string key_;
  // A \uXXXX escape can straddle a chunk boundary, so its digits are gathered
  // across feed() calls rather than read ahead.
  uint32_t unicode_ = 0;
  int unicodePending_ = 0;

  size_t kept_ = 0;
  int seen_ = 0;
  bool truncated_ = false;
  bool failed_ = false;
};

}  // namespace hn

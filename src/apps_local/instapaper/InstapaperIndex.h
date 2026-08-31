#pragma once

// The read-later queue as it sits on the card, and the one piece of logic
// that decides what a sync changed.
//
// Freestanding on purpose: parsing, formatting and merging only -- no SD
// card, no ArduinoJson, no network -- so host-tests/instapaper/ drives all of
// it on a laptop. The Library owns the card and the Activity owns the screen;
// neither of them owns a rule.
//
// ---------------------------------------------------------------------------
// The format is versioned from birth, and that is not caution for its own
// sake. HackerNewsSaved shipped a field change without a version bump and a
// version-1 row handed back its bookmark id as its title: every saved article
// displayed as a number, and because the row no longer matched, the library
// saved a second copy on every tap. One unbumped integer, two bugs, and
// neither of them looks like a format problem from the outside.
//
// Tab-separated rather than JSON for the reason the Hacker News library gives:
// this file is the thing that has to survive. It is readable in any editor,
// recoverable by hand, and needs no parser to inspect. The free-form field --
// the title -- is LAST, so a tab that somehow survives sanitising can only
// swallow the end of its own row rather than the next column's meaning.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace instapaper {

// What the reader can hold. Well under the 500 Instapaper will list in one
// call, which is what keeps its delete_ids trustworthy: an id the device
// holds that falls outside the server's listing window is reported as
// deleted, and the bridge suppresses those rather than acting on them.
// Raising this without raising that window is how a reading list disappears.
inline constexpr size_t kMaxArticles = 120;

struct Article {
  // Instapaper's bookmark_id. Also the article's filename, so it never has
  // to be sanitised: it is an integer from the API and nothing else.
  int64_t id = 0;
  // Instapaper's own hash of url+title+description+progress. The delta key:
  // it goes back up in the `have` string and an unchanged hash means the
  // whole article can be skipped server-side.
  std::string hash;
  // First 16 hex of the sha256 of the TEXT. Separate from `hash` because
  // they answer different questions: `hash` moves when the title or the
  // progress moves, `sha` moves only when the words do. Downloading on
  // `hash` would re-fetch a whole article every time the phone read a
  // paragraph.
  std::string sha;
  std::string title;
  std::string domain;  // the row's subtitle; "saved by email" for private ones
  uint32_t savedAt = 0;
  uint32_t words = 0;
  uint16_t minutes = 0;
  // 0..1, Instapaper's definition: the top edge of the viewport as a share of
  // the article's length. The pager's topLine over its line count IS that
  // number, so nothing here models reading position twice.
  float progress = 0.0f;
  uint32_t progressAt = 0;
  // False when the bridge judged the text mostly outside the reading cut's
  // glyphs. The row says so instead of opening a blank page.
  bool renderable = true;
  // Progress moved on this reader and has not been sent yet. Survives a
  // reboot, which is the whole reason it is a column and not a member of
  // some session object.
  bool progressDirty = false;
  // ARCHIVE was pressed here. The row hides immediately and the intent is
  // re-sent until the bridge confirms it, which is safe because archiving an
  // archived bookmark is a no-op on Instapaper's side.
  bool archivePending = false;
};

// Header line then one row per article. Version 1.
std::string serializeIndex(const std::vector<Article>& articles);

// False only when the text is not this format at all. A single damaged row is
// skipped rather than failing the file: losing one article beats losing the
// queue.
bool parseIndex(std::string_view text, std::vector<Article>& out);

// Strip anything that would break the row format, and collapse whitespace.
std::string sanitizeField(std::string_view text);

// "a1234.txt". The only place the filename is spelled.
std::string articleFileName(int64_t id);

// What a sync did to the queue.
struct MergePlan {
  std::vector<int64_t> download;  // text must be fetched for these
  std::vector<int64_t> drop;      // their files must be deleted
};

// Fold one sync summary into the local queue.
//
// `incoming` is what the bridge said changed, `deleted` what Instapaper no
// longer has in the unread folder, `archived` the intents it confirmed, and
// `hasText` names the ids whose text file is actually on the card right now.
// That last one matters: an article can be perfectly up to date in the index
// and have no file, because a download can fail after the index was written.
//
// Progress is resolved by timestamp, which is the same rule Instapaper uses,
// so the phone and the reader cannot fight: the newer stamp wins and the
// loser is simply overwritten.
MergePlan mergeSummary(std::vector<Article>& local, const std::vector<Article>& incoming,
                       const std::vector<int64_t>& deleted, const std::vector<int64_t>& archived,
                       const std::vector<int64_t>& hasText);

// The queue's reading order: newest saved first, which is Instapaper's own
// unread order and the one a reader expects to find their last save at the
// top of.
void sortForQueue(std::vector<Article>& articles);

// Rows the queue draws: everything except what is waiting to be archived.
// Hiding a pending archive immediately is the difference between "it worked"
// and "did I press it?", and it costs nothing -- the intent is durable.
std::vector<const Article*> visible(const std::vector<Article>& articles);

}  // namespace instapaper

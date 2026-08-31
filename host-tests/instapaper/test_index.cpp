// Host tests for the read-later index and the merge that a sync runs.
//
// Two halves, and the second is the one worth the file. The format tests care
// about damage: a half-written row, a title full of tabs, an index from a
// version this build does not know. The merge tests care about the rules that
// decide what a sync did -- who wins a progress conflict, what has to be
// downloaded, what gets deleted -- because every one of those failures is
// silent. A wrong merge does not crash; it quietly loses a reading position
// or leaves a row that opens nothing.

#include <cstdio>
#include <string>
#include <vector>

#include "../../src/apps_local/instapaper/InstapaperIndex.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  ++checksRun;
  if (!condition) {
    ++checksFailed;
    std::printf("FAIL test_index.cpp:%d  %s\n", line, what);
  }
}

void checkEqual(const std::string& actual, const std::string& expected, const char* what, const int line) {
  ++checksRun;
  if (actual != expected) {
    ++checksFailed;
    std::printf("FAIL test_index.cpp:%d  %s\n  expected [%s]\n  actual   [%s]\n", line, what, expected.c_str(),
                actual.c_str());
  }
}

#define CHECK(cond) check((cond), #cond, __LINE__)
#define CHECK_EQ(actual, expected) checkEqual((actual), (expected), #actual, __LINE__)

instapaper::Article make(const int64_t id, const char* title, const uint32_t savedAt = 1000) {
  instapaper::Article a;
  a.id = id;
  a.hash = "HASH";
  a.sha = "0123456789abcdef";
  a.title = title;
  a.domain = "example.com";
  a.savedAt = savedAt;
  a.words = 900;
  a.minutes = 4;
  return a;
}

bool holds(const std::vector<int64_t>& ids, const int64_t id) {
  for (const int64_t v : ids) {
    if (v == id) return true;
  }
  return false;
}

const instapaper::Article* find(const std::vector<instapaper::Article>& list, const int64_t id) {
  for (const instapaper::Article& a : list) {
    if (a.id == id) return &a;
  }
  return nullptr;
}

// --------------------------------------------------------------- the format
void testRoundTrip() {
  std::vector<instapaper::Article> written = {make(101, "The first article"), make(102, "Another", 2000)};
  written[0].progress = 0.375f;
  written[0].progressAt = 1756100000;
  written[0].progressDirty = true;
  written[1].renderable = false;
  written[1].archivePending = true;

  std::vector<instapaper::Article> read;
  CHECK(instapaper::parseIndex(instapaper::serializeIndex(written), read));
  CHECK(read.size() == 2);
  CHECK(read[0].id == 101);
  CHECK_EQ(read[0].title, "The first article");
  CHECK_EQ(read[0].domain, "example.com");
  CHECK(read[0].words == 900 && read[0].minutes == 4);
  CHECK(read[0].progressAt == 1756100000);
  // Four decimal places is enough to place a reader inside any article this
  // device can hold, and progress is a share of a document, not a measurement.
  CHECK(read[0].progress > 0.374f && read[0].progress < 0.376f);
  CHECK(read[0].progressDirty && !read[0].archivePending && read[0].renderable);
  CHECK(!read[1].renderable && read[1].archivePending && !read[1].progressDirty);
}

void testDamage() {
  std::vector<instapaper::Article> out;
  CHECK(!instapaper::parseIndex("", out));
  CHECK(!instapaper::parseIndex("something else\t1\n", out));
  CHECK(!instapaper::parseIndex("instapaper\t99\n", out));
  CHECK(instapaper::parseIndex("instapaper\t1\n", out) && out.empty());

  // A row cut off mid-write keeps what it had and loses the rest, and the
  // rows around it survive. This is the state a power cut leaves.
  const std::string torn =
      "instapaper\t1\n"
      "101\tHASH\tSHA\t1000\t900\t4\t0.5000\t50\t1\texample.com\tGood row\n"
      "102\tHASH\tSHA\t1000\t900\n"
      "\n"
      "103\tHASH\tSHA\t1000\t900\t4\t0.0000\t0\t1\texample.com\tAlso good\n";
  CHECK(instapaper::parseIndex(torn, out));
  CHECK(out.size() == 3);
  CHECK_EQ(out[2].title, "Also good");
  CHECK(out[1].id == 102 && out[1].title.empty());

  // No id is damage; no title is an untitled bookmark, which is real.
  const std::string idless =
      "instapaper\t1\n"
      "\tHASH\tSHA\t1\t1\t1\t0\t0\t1\texample.com\tNo id\n"
      "104\tHASH\tSHA\t1\t1\t1\t0\t0\t1\texample.com\t\n";
  CHECK(instapaper::parseIndex(idless, out));
  CHECK(out.size() == 1 && out[0].id == 104);
}

void testSanitising() {
  instapaper::Article a = make(101, "Tabbed\ttitle\nwith  breaks");
  a.domain = "ex\tample";
  std::vector<instapaper::Article> read;
  CHECK(instapaper::parseIndex(instapaper::serializeIndex({a}), read));
  CHECK(read.size() == 1);
  CHECK_EQ(read[0].title, "Tabbed title with breaks");
  // A tab becomes a space rather than vanishing: the row cannot be broken,
  // and nothing silently changes length.
  CHECK_EQ(read[0].domain, "ex ample");

  // The hash and the sha reach a URL path and a filename respectively, so
  // anything that is not alphanumeric is dropped rather than escaped.
  instapaper::Article nasty = make(102, "x");
  nasty.hash = "../../etc/passwd";
  nasty.sha = "ab/cd";
  CHECK(instapaper::parseIndex(instapaper::serializeIndex({nasty}), read));
  CHECK_EQ(read[0].hash, "etcpasswd");
  CHECK_EQ(read[0].sha, "abcd");

  CHECK_EQ(instapaper::articleFileName(1234), "a1234.txt");
}

void testProgressBounds() {
  // Values off the wire, and NaN in particular: a NaN progress compares false
  // against everything, so it would silently win or lose every later
  // comparison depending on which side it landed on.
  const std::string odd =
      "instapaper\t1\n"
      "101\tH\tS\t1\t1\t1\t-4\t0\t1\tx\tnegative\n"
      "102\tH\tS\t1\t1\t1\t9.5\t0\t1\tx\ttoo big\n"
      "103\tH\tS\t1\t1\t1\tnan\t0\t1\tx\tnot a number\n";
  std::vector<instapaper::Article> out;
  CHECK(instapaper::parseIndex(odd, out));
  CHECK(out.size() == 3);
  CHECK(out[0].progress == 0.0f);
  CHECK(out[1].progress == 1.0f);
  CHECK(out[2].progress == 0.0f);
}

// ---------------------------------------------------------------- the merge
void testMergeNewAndChanged() {
  std::vector<instapaper::Article> local = {make(101, "Known"), make(102, "Also known")};
  std::vector<instapaper::Article> incoming = {make(103, "Brand new", 3000)};
  instapaper::Article changed = make(101, "Known, retitled");
  changed.sha = "ffffffffffffffff";
  incoming.push_back(changed);

  const instapaper::MergePlan plan = instapaper::mergeSummary(local, incoming, {}, {}, {101, 102});
  CHECK(local.size() == 3);
  CHECK(holds(plan.download, 103));
  CHECK(holds(plan.download, 101));
  CHECK(!holds(plan.download, 102));
  CHECK_EQ(find(local, 101)->title, "Known, retitled");
}

void testMergeMetadataOnlyChangeDoesNotDownload() {
  // The case the sha column exists for: the phone read a paragraph, so
  // Instapaper's hash moved, but the words did not. Re-downloading here would
  // spend a round trip and a card write on bytes already present.
  std::vector<instapaper::Article> local = {make(101, "Known")};
  instapaper::Article same = make(101, "Known");
  same.hash = "MOVED";
  const instapaper::MergePlan plan = instapaper::mergeSummary(local, {same}, {}, {}, {101});
  CHECK(plan.download.empty());
  CHECK_EQ(find(local, 101)->hash, "MOVED");
}

void testMergeMissingFileIsDownloaded() {
  // Metadata perfectly current, file absent: a download that failed after the
  // index was written. Without this rule the row opens nothing, forever.
  std::vector<instapaper::Article> local = {make(101, "Known")};
  const instapaper::MergePlan plan = instapaper::mergeSummary(local, {}, {}, {}, {});
  CHECK(holds(plan.download, 101));
}

void testMergeProgressConflict() {
  std::vector<instapaper::Article> local = {make(101, "Read here"), make(102, "Read on the phone")};
  local[0].progress = 0.60f;
  local[0].progressAt = 2000;
  local[0].progressDirty = true;
  local[1].progress = 0.10f;
  local[1].progressAt = 1000;

  instapaper::Article ours = make(101, "Read here");
  ours.progress = 0.20f;
  ours.progressAt = 1500;  // older than this reader's: ours must stand
  instapaper::Article theirs = make(102, "Read on the phone");
  theirs.progress = 0.80f;
  theirs.progressAt = 5000;  // newer: the phone read further

  std::vector<instapaper::Article> incoming = {ours, theirs};
  instapaper::mergeSummary(local, incoming, {}, {}, {101, 102});
  CHECK(find(local, 101)->progress > 0.59f && find(local, 101)->progress < 0.61f);
  CHECK(find(local, 101)->progressAt == 2000);
  CHECK(find(local, 102)->progress > 0.79f);
  CHECK(find(local, 102)->progressAt == 5000);
  // A completed sync sent every row's progress, so nothing stays dirty.
  CHECK(!find(local, 101)->progressDirty);
}

void testMergeArchiveAndDelete() {
  std::vector<instapaper::Article> local = {make(101, "Archived here"), make(102, "Gone elsewhere"),
                                            make(103, "Still here")};
  local[0].archivePending = true;

  const instapaper::MergePlan plan = instapaper::mergeSummary(local, {}, {102}, {101}, {101, 102, 103});
  CHECK(local.size() == 1 && local[0].id == 103);
  CHECK(holds(plan.drop, 101));
  CHECK(holds(plan.drop, 102));
  CHECK(!holds(plan.drop, 103));
  // A dropped article must never also be queued for download.
  CHECK(!holds(plan.download, 101));
  CHECK(!holds(plan.download, 102));
}

void testMergeUnconfirmedArchiveStaysPending() {
  // The bridge could not archive it this time. The intent has to survive, or
  // the article silently comes back on the next sync and the press is lost.
  std::vector<instapaper::Article> local = {make(101, "Archived here")};
  local[0].archivePending = true;
  instapaper::mergeSummary(local, {make(101, "Archived here")}, {}, {}, {101});
  CHECK(local.size() == 1);
  CHECK(local[0].archivePending);
}

void testMergeCap() {
  std::vector<instapaper::Article> local;
  std::vector<int64_t> present;
  for (size_t i = 0; i < instapaper::kMaxArticles + 5; ++i) {
    local.push_back(make(static_cast<int64_t>(500 + i), "Article", static_cast<uint32_t>(1000 + i)));
    present.push_back(static_cast<int64_t>(500 + i));
  }
  const instapaper::MergePlan plan = instapaper::mergeSummary(local, {}, {}, {}, present);
  CHECK(local.size() == instapaper::kMaxArticles);
  CHECK(plan.drop.size() == 5);
  // Newest kept, oldest trimmed: the queue is in Instapaper's unread order.
  CHECK(local.front().savedAt > local.back().savedAt);
  CHECK(holds(plan.drop, 500));
}

void testVisibleHidesPendingArchives() {
  std::vector<instapaper::Article> local = {make(101, "Shown"), make(102, "Hidden")};
  local[1].archivePending = true;
  const std::vector<const instapaper::Article*> rows = instapaper::visible(local);
  CHECK(rows.size() == 1);
  CHECK(rows[0]->id == 101);
}

}  // namespace

int main() {
  testRoundTrip();
  testDamage();
  testSanitising();
  testProgressBounds();
  testMergeNewAndChanged();
  testMergeMetadataOnlyChangeDoesNotDownload();
  testMergeMissingFileIsDownloaded();
  testMergeProgressConflict();
  testMergeArchiveAndDelete();
  testMergeUnconfirmedArchiveStaysPending();
  testMergeCap();
  testVisibleHidesPendingArchives();
  std::printf("%d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}

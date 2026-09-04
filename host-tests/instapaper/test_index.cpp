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

  // Typographic punctuation is folded on the way OUT of the index, not only on
  // the way in. Written this way on purpose: an index saved before the fold
  // existed carries real curly quotes, and the reading cut has no glyph for
  // them, so those rows would draw with holes in them until the queue was
  // re-synced. Parsing is where the reader's copy is read, so parsing is where
  // it is fixed -- no migration, no version bump.
  instapaper::Article typographic = make(103, "x");
  typographic.title =
      "It\xe2\x80\x99"
      "s a \xe2\x80\x9c"
      "big\xe2\x80\x9d"
      " one \xe2\x80\x94"
      " really\xe2\x80\xa6";
  typographic.domain = "caf\xc3\xa9.example.com";
  CHECK(instapaper::parseIndex(instapaper::serializeIndex({typographic}), read));
  CHECK_EQ(read[0].title, "It's a \"big\" one -- really...");
  // The domain keeps its accent: the reading cut draws Latin-1, and folding it
  // would rewrite a name the panel can show correctly.
  CHECK_EQ(read[0].domain, "caf\xc3\xa9.example.com");

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

// ------------------------------------------------- what a sync may claim
void testComposeHaveOmitsARowWithNoText() {
  // The deadlock this function exists to break, and it cost a live account a
  // day of "1 did not arrive; sync again". `have` is a delta: claiming 102
  // makes Instapaper suppress it, the summary then carries no size for it,
  // and a download with no size is refused because the length is the only
  // proof the file arrived whole. Claim it once and it can never be fetched
  // again, however many times the sync is repeated.
  std::vector<instapaper::Article> local = {make(101, "On the card"), make(102, "Row without a file")};
  const std::vector<instapaper::Article> have = instapaper::composeHave(local, {101});
  CHECK(have.size() == 1);
  CHECK(have[0].id == 101);
}

void testComposeHaveCarriesTheRowItClaims() {
  // Claiming is also how a reading position travels, so the entry has to
  // arrive whole and not as a bare id.
  std::vector<instapaper::Article> local = {make(101, "On the card")};
  local[0].progress = 0.5f;
  local[0].progressAt = 4242;
  local[0].progressDirty = true;
  const std::vector<instapaper::Article> have = instapaper::composeHave(local, {101});
  CHECK(have.size() == 1);
  CHECK(have[0].progressAt == 4242);
  CHECK(have[0].progress > 0.4f);
}

void testMergeKeepsProgressUnsentForARowItCouldNotClaim() {
  // The twin of the rule above, and the reason both take the same hasText.
  // A row left out of `have` had its position sent nowhere, so clearing its
  // dirty flag on a completed sync would drop a reading the reader did.
  std::vector<instapaper::Article> local = {make(101, "On the card"), make(102, "Row without a file")};
  for (instapaper::Article& a : local) {
    a.progress = 0.5f;
    a.progressAt = 4242;
    a.progressDirty = true;
  }
  instapaper::mergeSummary(local, {}, {}, {}, {101});
  CHECK(!find(local, 101)->progressDirty);
  CHECK(find(local, 102)->progressDirty);
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

// --- The pager -------------------------------------------------------------
//
// Numbers chosen so the last page OVERLAPS: 36 lines in a 17-line viewport is
// three pages whose tops are 0, 17 and 19, and 19 is not a multiple of 17.
// That overlap is the whole bug -- dividing by the span put the last page at
// 19/17+1 = 2, so a three-page article ended on "2 / 3" and 3/3 could not be
// reached from anywhere.

void testTheLastPageIsTheLastPage() {
  CHECK(instapaper::pagesFor(0, 17, 36).count == 3);
  CHECK(instapaper::pagesFor(0, 17, 36).page == 1);
  CHECK(instapaper::pagesFor(17, 17, 36).page == 2);
  // The clamped final position, which is where a second page turn lands.
  CHECK(instapaper::maxTopLine(17, 36) == 19);
  CHECK(instapaper::pagesFor(19, 17, 36).page == 3);
  CHECK(instapaper::pagesFor(19, 17, 36).count == 3);
}

void testAShortArticleIsOnePage() {
  CHECK(instapaper::pagesFor(0, 17, 10).page == 1);
  CHECK(instapaper::pagesFor(0, 17, 10).count == 1);
  // An exact fit is one page too, not two: the viewport already holds it all.
  CHECK(instapaper::pagesFor(0, 17, 17).count == 1);
  CHECK(instapaper::pagesFor(0, 17, 17).page == 1);
}

// Nothing measured yet, and nothing to measure. Neither may print "1 / 0" or
// divide by zero on the way there.
void testThePagerSurvivesNothingToShow() {
  CHECK(instapaper::pagesFor(0, 0, 0).page == 1);
  CHECK(instapaper::pagesFor(0, 0, 0).count == 1);
  CHECK(instapaper::pagesFor(5, 17, 0).count == 1);
  CHECK(instapaper::pagesFor(0, 0, 40).count == 1);
}

// Every page turn must be reachable and the last one must stop, or paging past
// the end reads as a frozen panel.
void testPagingWalksToTheEndAndStops() {
  const uint32_t span = 17;
  const uint32_t lines = 36;
  uint32_t top = 0;
  top = instapaper::turnedTopLine(top, span, lines, 1);
  CHECK(top == 17);
  top = instapaper::turnedTopLine(top, span, lines, 1);
  CHECK(top == 19);
  top = instapaper::turnedTopLine(top, span, lines, 1);
  CHECK(top == 19);
  top = instapaper::turnedTopLine(top, span, lines, -1);
  CHECK(top == 2);
  top = instapaper::turnedTopLine(top, span, lines, -1);
  CHECK(top == 0);
  top = instapaper::turnedTopLine(top, span, lines, -1);
  CHECK(top == 0);
}

// Reaching the end is 1.0, not "the top of the last page over the length" --
// which for somebody who just read the last word would report about 53%.
void testProgressCountsTheEndAsFinished() {
  CHECK(instapaper::progressFor(19, 17, 36) == 1.0f);
  CHECK(instapaper::progressFor(0, 17, 10) == 1.0f);
  const float middle = instapaper::progressFor(17, 17, 36);
  CHECK(middle > 0.46f && middle < 0.48f);
  CHECK(instapaper::progressFor(0, 17, 36) == 0.0f);
}

// A finished article reopens on its LAST page, like a half-read one reopens on
// the page it was left on. It used to reopen at the top, and nothing on the
// screen said why that one behaved differently.
void testAFinishedArticleReopensAtItsEnd() {
  CHECK(instapaper::topLineFor(1.0f, 17, 36) == 19);
  CHECK(instapaper::topLineFor(0.0f, 17, 36) == 0);
  // Leaving a page and reopening lands on the same line, which is the only
  // reason a position may be stored as a fraction at all.
  CHECK(instapaper::topLineFor(instapaper::progressFor(17, 17, 36), 17, 36) == 17);
  // Anything past the end is clamped rather than trusted: the value comes off
  // the card and off the wire.
  CHECK(instapaper::topLineFor(2.0f, 17, 36) == 19);
  CHECK(instapaper::topLineFor(1.0f, 17, 10) == 0);
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
  testComposeHaveOmitsARowWithNoText();
  testComposeHaveCarriesTheRowItClaims();
  testMergeKeepsProgressUnsentForARowItCouldNotClaim();
  testMergeProgressConflict();
  testMergeArchiveAndDelete();
  testMergeUnconfirmedArchiveStaysPending();
  testMergeCap();
  testVisibleHidesPendingArchives();
  testTheLastPageIsTheLastPage();
  testAShortArticleIsOnePage();
  testThePagerSurvivesNothingToShow();
  testPagingWalksToTheEndAndStops();
  testProgressCountsTheEndAsFinished();
  testAFinishedArticleReopensAtItsEnd();
  std::printf("%d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}

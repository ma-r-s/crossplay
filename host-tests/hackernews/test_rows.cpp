// Host tests for the story list's rows.
//
// These exist because of a bug that no amount of testing the pieces would have
// caught: every function involved was correct, and one call site changed the
// view without rebuilding the rows. The screen then showed the saved shelf's
// headlines with the front page's indices under them, so a tap opened an
// article the reader had never seen the title of.
//
// So the thing under test here is not "does buildRows fill a vector". It is the
// staleness question -- the one the paint asks before it draws -- because that
// is what turns a forgotten call from a wrong article into a rebuild.

#include <cstdio>
#include <string>
#include <vector>

#include "../../src/apps_local/hackernews/HackerNewsRows.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  ++checksRun;
  if (!condition) {
    ++checksFailed;
    std::printf("FAIL test_rows.cpp:%d  %s\n", line, what);
  }
}

void checkEqual(const std::string& actual, const std::string& expected, const char* what, const int line) {
  ++checksRun;
  if (actual != expected) {
    ++checksFailed;
    std::printf("FAIL test_rows.cpp:%d  %s\n  expected [%s]\n  actual   [%s]\n", line, what, expected.c_str(),
                actual.c_str());
  }
}

#define CHECK(cond) check((cond), #cond, __LINE__)
#define CHECK_EQ(actual, expected) checkEqual((actual), (expected), #actual, __LINE__)

std::vector<hn::Story> frontPage() {
  std::vector<hn::Story> stories;
  for (int i = 0; i < 3; ++i) {
    hn::Story story;
    story.title = "Front story " + std::to_string(i);
    story.commentCount = 10 + i;
    stories.push_back(story);
  }
  return stories;
}

std::vector<hn::SavedArticle> shelf() {
  std::vector<hn::SavedArticle> saved;
  for (int i = 0; i < 2; ++i) {
    hn::SavedArticle article;
    article.title = "Saved article " + std::to_string(i);
    saved.push_back(article);
  }
  return saved;
}

// The bug itself, as a test. Switching the view and drawing without asking is
// what shipped; asking is the fix.
void testTheViewChangingMakesTheRowsStale() {
  hn::Rows rows;
  const auto stories = frontPage();
  const auto saved = shelf();

  // Nothing built yet: a paint must not draw an empty list as a real one.
  CHECK(hn::rowsStale(rows, hn::ListView::FrontPage));

  hn::buildRows(rows, hn::ListView::FrontPage, stories, saved);
  CHECK(!hn::rowsStale(rows, hn::ListView::FrontPage));

  // The whole bug in one line. Before this type existed, the answer here was
  // effectively "no" and the front page's rows went on screen under the saved
  // shelf's title.
  CHECK(hn::rowsStale(rows, hn::ListView::Saved));

  hn::buildRows(rows, hn::ListView::Saved, stories, saved);
  CHECK(!hn::rowsStale(rows, hn::ListView::Saved));
  // And back, which is the direction that actually shipped broken.
  CHECK(hn::rowsStale(rows, hn::ListView::FrontPage));
}

// The consequence, stated the way the reader experienced it: the row a tap
// opens is `titles[i]` of the shelf now showing, so the titles must belong to
// that shelf and be the same length as it.
void testEachViewsRowsAreItsOwn() {
  hn::Rows rows;
  const auto stories = frontPage();
  const auto saved = shelf();

  hn::buildRows(rows, hn::ListView::FrontPage, stories, saved);
  CHECK_EQ(std::to_string(rows.size()), std::to_string(stories.size()));
  CHECK_EQ(rows.titles[0], "Front story 0");
  CHECK_EQ(rows.titles[2], "Front story 2");
  // The second column is the comment count and only the front page has one.
  CHECK_EQ(rows.values[0], "10");
  CHECK_EQ(rows.values[2], "12");

  hn::buildRows(rows, hn::ListView::Saved, stories, saved);
  // The length is the shelf's, not the front page's. A stale length is how a
  // tap reaches past the end of the list it is indexing.
  CHECK_EQ(std::to_string(rows.size()), std::to_string(saved.size()));
  CHECK_EQ(rows.titles[0], "Saved article 0");
  CHECK_EQ(rows.titles[1], "Saved article 1");
  // One value per row even with nothing to say, so the two vectors stay
  // indexable together.
  CHECK_EQ(std::to_string(rows.values.size()), std::to_string(rows.titles.size()));
  CHECK_EQ(rows.values[0], "");
}

// The other half of the same bug, and the one with no wrong article to notice:
// with nothing saved, returning to the front page drew an empty list while the
// stories sat in memory. It read as "Hacker News has nothing today".
void testAnEmptyShelfDoesNotEmptyTheFrontPage() {
  hn::Rows rows;
  const auto stories = frontPage();
  const std::vector<hn::SavedArticle> nothingSaved;

  hn::buildRows(rows, hn::ListView::Saved, stories, nothingSaved);
  CHECK_EQ(std::to_string(rows.size()), "0");

  CHECK(hn::rowsStale(rows, hn::ListView::FrontPage));
  hn::buildRows(rows, hn::ListView::FrontPage, stories, nothingSaved);
  CHECK_EQ(std::to_string(rows.size()), "3");
}

// A front page that legitimately came back empty is sourced, not stale. Getting
// this wrong turns a quiet failure into a rebuild on every paint.
void testAnEmptyFrontPageIsStillBuilt() {
  hn::Rows rows;
  const std::vector<hn::Story> noStories;
  const std::vector<hn::SavedArticle> nothingSaved;

  hn::buildRows(rows, hn::ListView::FrontPage, noStories, nothingSaved);
  CHECK_EQ(std::to_string(rows.size()), "0");
  CHECK(!hn::rowsStale(rows, hn::ListView::FrontPage));
}

// Labels are fitted at paint time against a width only a draw target knows, so
// they must never outlive the titles they were fitted from. A stale label list
// is the same bug one field along: the right row height, the wrong words.
void testRebuildingDropsTheFittedLabels() {
  hn::Rows rows;
  const auto stories = frontPage();
  const auto saved = shelf();

  hn::buildRows(rows, hn::ListView::FrontPage, stories, saved);
  rows.labels = {"fitted 0", "fitted 1", "fitted 2"};
  rows.fitted = true;

  hn::buildRows(rows, hn::ListView::Saved, stories, saved);
  CHECK(!rows.fitted);
  CHECK_EQ(std::to_string(rows.labels.size()), "0");
}

// The staleness the view cannot see: the shelf changed under a view that did
// not. Saving or removing an article while the shelf is showing is exactly
// this, and it is why invalidate() exists as well as rowsStale().
void testInvalidateCoversAShelfThatChangedUnderTheSameView() {
  hn::Rows rows;
  const auto stories = frontPage();
  auto saved = shelf();

  hn::buildRows(rows, hn::ListView::Saved, stories, saved);
  CHECK(!hn::rowsStale(rows, hn::ListView::Saved));

  saved.pop_back();
  // The view is unchanged, so this alone still answers "fresh" -- correctly,
  // because it is not the question being asked.
  CHECK(!hn::rowsStale(rows, hn::ListView::Saved));

  rows.invalidate();
  CHECK(hn::rowsStale(rows, hn::ListView::Saved));
  hn::buildRows(rows, hn::ListView::Saved, stories, saved);
  CHECK_EQ(std::to_string(rows.size()), "1");
}

}  // namespace

int main() {
  testTheViewChangingMakesTheRowsStale();
  testEachViewsRowsAreItsOwn();
  testAnEmptyShelfDoesNotEmptyTheFrontPage();
  testAnEmptyFrontPageIsStillBuilt();
  testRebuildingDropsTheFittedLabels();
  testInvalidateCoversAShelfThatChangedUnderTheSameView();

  std::printf("%d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}

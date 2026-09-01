#include "HackerNewsRows.h"

#include <cstdio>

namespace hn {

bool rowsStale(const Rows& rows, const ListView view) { return !rows.sourced || rows.builtFor != view; }

void buildRows(Rows& rows, const ListView view, const std::vector<Story>& stories,
               const std::vector<SavedArticle>& saved) {
  rows.titles.clear();
  rows.values.clear();
  // Cleared with the titles, never separately: `labels` is indexed in lockstep
  // with them at paint time.
  rows.labels.clear();
  rows.fitted = false;

  if (view == ListView::Saved) {
    rows.titles.reserve(saved.size());
    rows.values.reserve(saved.size());
    for (const SavedArticle& article : saved) {
      rows.titles.push_back(article.title);
      // One entry per row even though the shelf draws no second column, so the
      // two vectors can be indexed together without a length check at paint
      // time.
      rows.values.emplace_back();
    }
  } else {
    rows.titles.reserve(stories.size());
    rows.values.reserve(stories.size());
    for (const Story& story : stories) {
      rows.titles.push_back(story.title);
      // The bare comment count. The list is already Hacker News's own ranking,
      // so points would restate the order; how much discussion a story drew is
      // what the order does not tell you.
      char count[16];
      std::snprintf(count, sizeof(count), "%d", story.commentCount);
      rows.values.emplace_back(count);
    }
  }

  rows.builtFor = view;
  rows.sourced = true;
}

}  // namespace hn

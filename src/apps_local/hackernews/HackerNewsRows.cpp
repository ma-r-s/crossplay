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

EmptyState emptyState(const ListView view, const bool frontPageFailed) {
  EmptyState state;
  if (view == ListView::Saved) {
    // No control, deliberately. An empty shelf on a new device is not a fault
    // and not something a button can fix; a control here would offer to fetch
    // the front page from the one screen that is about not needing it.
    state.headline = "NOTHING SAVED YET";
    // Measured in the face this actually resolves to. See buildList: the
    // display cut's predecessors here shipped cut at 480px.
    state.message = "Tap SAVE while you read.";
    return state;
  }
  if (frontPageFailed) {
    state.headline = "NO LUCK";
    state.message = "Could not reach Hacker News. Saved articles still work.";
    state.actionLabel = "TRY AGAIN";
    return state;
  }
  // The state a device that has never joined a network opens in. It says what
  // is missing and what is not, because the SAVED half sitting one tap away is
  // the whole reason the app opens without a radio.
  state.headline = "NOT LOADED YET";
  state.message = "The front page needs a connection. Saved articles do not.";
  state.actionLabel = "LOAD";
  return state;
}

}  // namespace hn

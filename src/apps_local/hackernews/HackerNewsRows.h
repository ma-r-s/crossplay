#pragma once

// The story list's rows, and the one thing that has to be true about them.
//
// ---------------------------------------------------------------------------
// This exists because of a bug it now makes unrepresentable.
//
// The list draws two different shelves -- the front page and the saved
// articles -- from one set of row vectors, and a tap opens whatever sits at the
// tapped INDEX in the shelf the flag says is showing. Filling those vectors was
// two separate jobs at two separate call sites, so returning to the front page
// left the saved titles in place: the screen showed one shelf's headlines and
// the taps opened the other shelf's articles. With nothing saved, the same gap
// drew the front page as an empty list while thirty stories sat in memory.
//
// Both are the same missing call, and no amount of care at the call sites fixes
// a shape where care is what stands between you and the wrong article. So the
// rows carry the view they were built FOR, the paint asks whether that still
// matches the view being drawn, and "the titles say SAVED while the taps open
// the front page" stops being a state this type can hold.
//
// HackerNewsLibrary.h says building the rows that display the library is
// deliberately not the Library's job. Agreed, and it is not the renderer's
// either: it is a transform from a shelf to some strings, so it lives on its
// own where host-tests/hackernews/ can drive it without a panel.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <string>
#include <vector>

#include "HackerNewsCore.h"
#include "HackerNewsSaved.h"

namespace hn {

// Which shelf the list is showing. An enum rather than a bool because it is
// stored next to the rows it produced, and `builtFor == FrontPage` says what
// `!saved` does not.
enum class ListView : uint8_t { FrontPage, Saved };

struct Rows {
  // As the source wrote them. Kept because fitting is lossy and the fitted
  // label cannot be re-fitted to a different width.
  std::vector<std::string> titles;
  // The comment count on the front page; empty strings on the shelf, which has
  // no second column but still needs one entry per row.
  std::vector<std::string> values;
  // Fitted to the row width, which needs a draw target, so these are filled at
  // paint time rather than here. Cleared by every rebuild: a label list that
  // outlives its titles is the same class of bug one field along.
  std::vector<std::string> labels;

  ListView builtFor = ListView::FrontPage;
  // Whether `titles`/`values` have ever been filled. Distinct from "not empty":
  // a front page that legitimately returned nothing is sourced and empty, and
  // rebuilding it on every paint would be a fetch loop.
  bool sourced = false;
  // Whether `labels` match `titles`.
  bool fitted = false;

  size_t size() const { return titles.size(); }

  // Forces the next paint to rebuild. For the case view-tracking cannot see:
  // the SHELF changed while the view did not. Saving or removing an article,
  // and a freshly fetched front page, all leave `builtFor` correct and the
  // contents out of date.
  void invalidate() {
    sourced = false;
    fitted = false;
  }
};

// Whether the rows on hand can be drawn as `view`. True before the first build
// and after any view change, so a paint that honours it cannot draw one shelf's
// titles over another shelf's indices.
bool rowsStale(const Rows& rows, ListView view);

// Fills `rows` from whichever shelf `view` names and marks them as built for
// it. The other shelf's vector is ignored rather than required to be empty, so
// a caller can pass both and let the view decide.
void buildRows(Rows& rows, ListView view, const std::vector<Story>& stories, const std::vector<SavedArticle>& saved);

// What a list with no rows says, and whether it offers a way to fill itself.
//
// Three different screens, and collapsing any two of them is how a working app
// reads as a broken one. An empty SAVED shelf is the ordinary state of a new
// device and is COMPLETE: there is nothing to fetch and offering a button would
// promise one. An unfetched front page is an invitation. A front page that was
// asked for and did not arrive is an error, and both of those carry the control
// that tries again.
//
// It lives here rather than in the screen because it is the app's whole answer
// to having no network, and the screen cannot be asked what it would have said.
struct EmptyState {
  const char* headline = nullptr;
  const char* message = nullptr;
  // The label on the control that fetches the front page. nullptr means there
  // is no control, which is the SAVED shelf's answer and only its answer.
  const char* actionLabel = nullptr;
};

EmptyState emptyState(ListView view, bool frontPageFailed);

}  // namespace hn

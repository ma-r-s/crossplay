#pragma once

// The saved library: the index, the article files, and nothing else.
//
// Split out of HackerNewsActivity, which had grown to a thousand lines holding
// five unrelated jobs at once. This is the one with a boundary you can state in
// a sentence -- it owns a directory on the SD card and answers questions about
// what is in it -- so it is the one that comes out first.
//
// What deliberately did NOT come with it: deciding *what* to save, building the
// rows that display it, and everything to do with the reader. Those are the
// Activity's, and dragging them here would trade one oversized file for two
// entangled ones. The Library never knows a story is on screen; it is handed a
// URL, a title and some text, and it puts them somewhere.
//
// Behaviour is unchanged by the move. `HackerNewsSaved` still owns the format;
// this owns the card.

#include <string>
#include <vector>

#include "HackerNewsSaved.h"

namespace hn {

class Library {
 public:
  // Reads the index once. Cheap and idempotent, so callers do not have to
  // remember whether they are the first.
  void load();

  const std::vector<SavedArticle>& articles() const { return articles_; }
  bool empty() const { return articles_.empty(); }

  // Whether this URL is already in the library. The URL rather than an index,
  // because the reader knows what it is showing and not where it sits.
  bool contains(const std::string& url) const;

  // Writes the words and adds or updates the row. The words go first: an index
  // row pointing at a file that is not there is the one state that would make
  // the library look broken, and it is the state a failed write would leave.
  bool save(const std::string& url, const std::string& title, const std::string& text);

  // Removes the row and then the file, which is the opposite order for the same
  // reason -- a file with no row is a few unreferenced kilobytes that the next
  // save of the same URL overwrites.
  bool remove(const std::string& url);

  // The stored text, or false when the row exists and the file does not.
  bool readArticle(const SavedArticle& article, std::string& out) const;

 private:
  bool writeIndex();

  std::vector<SavedArticle> articles_;
  bool loaded_ = false;
};

}  // namespace hn

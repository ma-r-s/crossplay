#pragma once

// The queue on the card: the index, the article files, and the pairing state.
//
// One boundary you can state in a sentence -- it owns a directory and answers
// questions about what is in it -- which is why it is a class and not part of
// the Activity. It never knows an article is on screen; it is handed an id and
// it finds bytes.
//
// Device-only (HalStorage), so it has NO host test. That is the untested seam
// in this app; every rule worth asserting lives in InstapaperIndex, which is
// freestanding, and this file is deliberately kept dull enough that reading it
// is the review.

#include <cstdint>
#include <string>
#include <vector>

#include "InstapaperIndex.h"

namespace instapaper {

// The pairing token and nothing else that a re-pair should not reset. Article
// state lives in the index; putting it here too would give two files an
// opinion about the same thing.
struct BridgeState {
  bool paired = false;
  std::string token;
  int64_t lastSyncAt = 0;
  // The Instapaper username this reader paired as. Kept so the account screen
  // can name whose reading list is on the card before it is wiped -- the device
  // learns it once, at pair-confirm, and otherwise never shows it. Empty on a
  // device paired before this column existed, which the screen tolerates.
  std::string user;
};

class Library {
 public:
  // Reads the index once. Cheap and idempotent, so callers do not have to
  // remember whether they are the first.
  void load();

  std::vector<Article>& articles() { return articles_; }
  const std::vector<Article>& articles() const { return articles_; }
  bool empty() const { return articles_.empty(); }

  Article* find(int64_t id);

  // The whole index, written beside itself and renamed. Opening the real path
  // truncates first, so a power cut mid-write would leave an unparseable index
  // -- which reads exactly like an empty queue, and would send the reader
  // through a fresh download of everything.
  bool saveIndex();

  bool readArticle(int64_t id, std::string& out) const;
  bool hasArticle(int64_t id) const;
  // The ids whose text is really on the card right now, for mergeSummary.
  std::vector<int64_t> presentIds() const;

  // Downloads land beside the real name and are renamed on success, so a
  // failed or cancelled download can never be read as an article.
  std::string partPathFor(int64_t id) const;
  bool commitPart(int64_t id) const;
  void discardPart(int64_t id) const;
  void removeArticle(int64_t id) const;

  bool loadBridgeState(BridgeState& out) const;
  bool saveBridgeState(const BridgeState& state) const;
  void clearBridgeState() const;

  // Disconnecting the account: erase everything a sync ever put on the card --
  // the pairing token, the index, and every downloaded article -- so the app is
  // left exactly as a reader that never paired. Persisted immediately (files are
  // removed from the card here, not just from RAM), so a wipe survives the chip
  // reset that a wake is. Clears the in-memory queue too and leaves the library
  // loaded-and-empty, so no later read re-reads a file this just deleted.
  void wipeAccount();

  static const char* directory();

 private:
  std::string pathFor(int64_t id) const;

  std::vector<Article> articles_;
  bool loaded_ = false;
};

}  // namespace instapaper

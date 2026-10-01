#pragma once

// The tickets on the card: every `.json` file directly under /tickets/.
//
// Device-only, and deliberately dull: list the directory, read the bytes, hand
// them to TicketsCore, keep what parses. Every rule about what a ticket IS
// lives in TicketsCore, which has a host suite.
//
// A file that does not parse is skipped with a log line naming it and the
// reason -- the list shows the tickets that work rather than refusing them all
// for one bad file, and a card whose EVERY file is bad reads as an empty
// wallet, which is the true state of things either way.

#include <string>
#include <vector>

#include "TicketsCore.h"

namespace tickets {

class Library {
 public:
  // Creates /tickets on first use, so a person looking for where to put the
  // files finds the directory already there. False only when the card itself
  // refuses, which the app shows as a notice rather than an empty list.
  bool begin();

  // Re-reads the directory. Called on every entry to the list: the card can
  // be filled from a computer between sessions, and an app that trusts a
  // cached list offers tickets that are not there.
  void scan();

  const std::vector<Ticket>& entries() const { return entries_; }
  int count() const { return static_cast<int>(entries_.size()); }
  // How many .json files were skipped at the last scan because they did not
  // parse. The empty state says so when the directory held files at all --
  // "NO TICKETS" over a directory of bad files would read as the card being
  // empty, and it is not.
  int skipped() const { return skipped_; }

  // Removes the selected file, even when titles are duplicated or differ from filenames.
  bool remove(int index);

 private:
  std::vector<Ticket> entries_;
  int skipped_ = 0;
};

// Saves one ticket JSON document received over the browser upload. Parses it,
// names the file from its CONTENT (never from the client, which deletes the
// traversal question rather than answering it), dedupes the stem, writes
// beside the file and renames. False with `refusal` set when the document is
// not a ticket, too many files share its name, or the card would not take it;
// `savedName` is the ticket's display name on success.
bool saveUploaded(const std::string& json, std::string& savedName, std::string& refusal);

}  // namespace tickets

#pragma once

// The notes on the card. Device-only: SD I/O and nothing else, deliberately
// kept dull enough that reading it is the review, the way InstapaperLibrary is.
// Every rule about what a note IS lives in NotesCore, which has a host suite.
//
// ---------------------------------------------------------------------------
// The name of a note is its filename
// ---------------------------------------------------------------------------
//
// `/notes/Shopping.md` is the note called Shopping. One source of truth: a note
// whose first line is a task still has a name, renaming is a file rename rather
// than a content rewrite, and nothing has to decide which of two titles wins.
// It also means a person who drops `.md` files on the card over the reader's
// own file transfer gets exactly the notes they expect, named as they named
// them, with no import step and no database.
//
// ---------------------------------------------------------------------------
// Every write lands beside itself and is renamed
// ---------------------------------------------------------------------------
//
// Opening the real path truncates it first, so a power cut mid-write would
// leave a note that parses as empty -- which reads exactly like a note somebody
// deleted. Writes go to `<name>.part` and are renamed on success, the way
// InstapaperLibrary does it. A `.part` left by a torn write is swept on the
// next scan.

#include <cstdint>
#include <string>
#include <vector>

namespace notes {

// A row of the deck. The tally is counted at scan time, which costs one read
// per note; notes are small and few, and the alternative is an index file that
// can disagree with the notes it indexes.
struct Entry {
  std::string name;  // the filename stem: what the user sees and types
  int done = 0;
  int total = 0;
  bool hasTasks = false;
  // The note's first words, for the deck card. Read at scan time, which already
  // reads the whole file to count it; capped so a long paragraph does not put a
  // kilobyte per note in RAM.
  std::string preview;
};

class Library {
 public:
  // Creates /notes on first use. False only when the card refuses, which is
  // reported to the user rather than retried: Storage.mkdir() is O_CREAT|O_EXCL
  // and returns false on a directory that is already there, so this asks
  // ensureDirectoryExists and never mkdir (card #475).
  bool begin();

  // Re-reads the directory. Called on every entry to the deck, because the card
  // can be edited from a computer between sessions and an app that trusts a
  // cached list shows notes that are not there.
  void scan();

  const std::vector<Entry>& entries() const { return entries_; }
  int count() const { return static_cast<int>(entries_.size()); }

  bool load(const std::string& name, std::string& doc) const;
  // Writes beside the file and renames. `message` says what went wrong, and a
  // full card is told apart from a broken one only AFTER a write has actually
  // failed: Storage.freeBytes() walks the FAT, which was measured on hardware
  // at 5.3 SECONDS, and asking it before a 200-byte write spent that on every
  // first add of a session to pre-empt a failure the write itself reports.
  bool save(const std::string& name, const std::string& doc, std::string& message);

  // A new, empty note. False when the name is unusable or already taken, with
  // `message` saying which.
  bool create(const std::string& name, std::string& message);
  bool rename(const std::string& from, const std::string& to, std::string& message);
  bool remove(const std::string& name);

  // What a name may be. A filename on a FAT card, so: no separators, no
  // wildcards, nothing that would name a different file than the one the user
  // typed. Trimmed of surrounding space, which is otherwise invisible in a list
  // and makes two notes look identical.
  static std::string sanitise(const std::string& name);
  static bool exists(const std::vector<Entry>& entries, const std::string& name);

 private:
  // Asked only when a write has already failed, to say WHY.
  bool cardIsFull() const;

  void sortEntries();
  std::string pathFor(const std::string& name) const;
  std::string partPathFor(const std::string& name) const;
  void sweepPartFiles() const;

  std::vector<Entry> entries_;
};

}  // namespace notes

#pragma once

// What the review log says about how study has actually been going.
//
// Mario asked for stats and for retraining in the first breath of this app.
// Retraining turned out not to need building: reviews sync back into Anki, so
// Anki's own "Optimize FSRS parameters" is the optimiser, and re-running
// anki_to_deck.py carries the new weights back to the device. See
// docs/study-deck-format.md.
//
// Stats do need building, and revlog.dat is the whole input. Freestanding C++17
// over the same ByteSource as StudyDeck, so host-tests/study can drive it with
// a synthetic log rather than a device.
//
// The log is append-only and grows without bound -- a year of heavy study is a
// couple of megabytes -- so this reads *backwards* from the end and stops as
// soon as it is past the window it cares about. A stats screen must not get
// slower the longer someone has been studying.

#include <cstdint>

#include "StudyDeck.h"

namespace study {

// Two weeks, matching the deck screen's forecast so the two panels read as one
// timeline: what happened, then what is coming.
inline constexpr int kHistoryDays = 14;

struct Stats {
  int reviewsPerDay[kHistoryDays] = {};  // index 0 is today
  int totalReviews = 0;                  // within the window
  int recalled = 0;                      // of those, not Again
  int daysStudied = 0;                   // days in the window with any review
  int streak = 0;                        // consecutive days back from today
  int lifetimeReviews = 0;               // every record in the file

  // Percent of reviews in the window that were not Again, or -1 when the
  // window is empty. -1 rather than 0 because "no data" and "you got
  // everything wrong" must not render the same.
  int retention() const;
};

// Read the log and summarise it. `todayDay` is the deck's day number and
// `collectionCreated` its epoch, so a record's timestamp can be turned into the
// same day numbering everything else uses.
bool readStats(ByteSource& revlog, int todayDay, int64_t collectionCreated, Stats& out);

}  // namespace study

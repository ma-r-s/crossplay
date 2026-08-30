#include "ForeheadCore.h"

namespace forehead {

namespace {

// A category's slice of the flat entry array, clamped so a bad index cannot
// walk off either end of the table.
struct Slice {
  int first;
  int end;
  bool valid;
};

Slice sliceOf(const int category) {
  if (category < 0 || category >= kCategoryCount) return {0, 0, false};
  const CategoryInfo& info = kCategories[category];
  return {info.first, info.first + info.count, true};
}

}  // namespace

uint32_t Rng::below(const uint32_t bound) {
  if (bound <= 1) return 0;
  // 2^32 mod bound, computed without 64-bit arithmetic. Rejecting exactly this
  // many values from the top makes what is left an exact multiple of `bound`.
  //
  // The first version subtracted (bound - 1) too many, which is harmless at the
  // bounds this app passes (a category count, at most 189: a skew of 4e-8) and
  // collapses at large ones -- at a bound near 3e9 it accepted about one draw in
  // two billion, which is a hang rather than a bias. Correct is cheaper than
  // documenting where it may be called from.
  const uint32_t excess = (UINT32_MAX % bound + 1u) % bound;
  uint32_t value = next();
  if (excess != 0) {
    const uint32_t limit = 0u - excess;  // 2^32 - excess, by wraparound
    while (value >= limit) value = next();
  }
  return value % bound;
}

void Deck::reset() {
  for (auto& byte : seen_) byte = 0;
}

bool Deck::seen(const int entry) const {
  if (entry < 0 || entry >= kEntryCount) return true;
  return (seen_[entry / 8] & (1u << (entry % 8))) != 0;
}

void Deck::markSeen(const int entry) {
  if (entry < 0 || entry >= kEntryCount) return;
  seen_[entry / 8] |= static_cast<uint8_t>(1u << (entry % 8));
}

void Deck::setMask(const uint8_t* bytes) {
  for (int i = 0; i < kMaskBytes; ++i) seen_[i] = bytes[i];
  // Anything past the last real entry is not a card and must not count towards
  // a category looking spent.
  for (int entry = kEntryCount; entry < kMaskBytes * 8; ++entry) {
    seen_[entry / 8] &= static_cast<uint8_t>(~(1u << (entry % 8)));
  }
}

int Deck::remainingIn(const int category) const {
  const Slice slice = sliceOf(category);
  if (!slice.valid) return 0;
  int count = 0;
  for (int entry = slice.first; entry < slice.end; ++entry) {
    if (!seen(entry)) ++count;
  }
  return count;
}

void Deck::lapExcept(const int category, const int16_t* keep, const int keepCount) {
  const Slice slice = sliceOf(category);
  for (int entry = slice.first; entry < slice.end; ++entry) {
    seen_[entry / 8] &= static_cast<uint8_t>(~(1u << (entry % 8)));
  }
  // Put back the cards the caller is still holding. Without this a round that
  // crosses a lap boundary re-deals words that are already on its own results
  // screen -- measured at 63% of evenings on a busy category before this
  // existed, and twice back to back often enough to see.
  for (int i = 0; i < keepCount; ++i) {
    const int entry = keep[i];
    if (entry >= slice.first && entry < slice.end) markSeen(entry);
  }
}

int Deck::draw(const int category, Rng& rng) {
  const Slice slice = sliceOf(category);
  if (!slice.valid) return -1;

  const int remaining = remainingIn(category);
  if (remaining == 0) return -1;

  // Pick the Nth unseen rather than rejection-sampling positions: with one card
  // left in a 189-entry category, rejection would expect 189 draws to find it,
  // and the very last card of a lap is a case every deck reaches.
  int wanted = static_cast<int>(rng.below(static_cast<uint32_t>(remaining)));
  for (int entry = slice.first; entry < slice.end; ++entry) {
    if (seen(entry)) continue;
    if (wanted-- > 0) continue;
    markSeen(entry);
    return entry;
  }
  return -1;
}

void Round::begin(const int category, const int lengthSeconds, Deck& deck, Rng& rng) {
  count_ = 0;
  got_ = 0;
  current_ = -1;
  category_ = static_cast<uint8_t>(category < 0 || category >= kCategoryCount ? 0 : category);
  lengthSeconds_ = static_cast<uint16_t>(lengthSeconds > 0 ? lengthSeconds : kDefaultRoundSeconds);
  live_ = true;
  dealNext(deck, rng);
}

void Round::dealNext(Deck& deck, Rng& rng) {
  if (count_ >= kMaxCards) {
    // Not reachable by a person; see kMaxCards. Ending here rather than
    // dropping the tail keeps the results list and the score the same story.
    current_ = -1;
    live_ = false;
    return;
  }
  current_ = static_cast<int16_t>(deck.draw(category_, rng));
  if (current_ >= 0) return;

  // The category ran out mid-round. Lap it, but keep this round's own cards
  // marked so the lap cannot hand back a word already on the results screen.
  deck.lapExcept(category_, entries_, count_);
  current_ = static_cast<int16_t>(deck.draw(category_, rng));
  // Still nothing means the round has answered every entry in the category,
  // which ends it honestly rather than by repeating itself.
  if (current_ < 0) live_ = false;
}

void Round::answer(const Mark mark, Deck& deck, Rng& rng) {
  if (!live_ || current_ < 0) return;
  entries_[count_] = current_;
  marks_[count_] = mark;
  ++count_;
  if (mark == Mark::Got) ++got_;
  dealNext(deck, rng);
}

void Round::expire() {
  if (!live_) return;
  live_ = false;
  if (current_ < 0 || count_ >= kMaxCards) return;
  entries_[count_] = current_;
  marks_[count_] = Mark::Unanswered;
  ++count_;
  current_ = -1;
}

const char* Round::cardText() const {
  if (current_ < 0 || current_ >= kEntryCount) return "";
  return kEntries[current_];
}

int Round::cardEntry() const { return current_; }

int Round::entryAt(const int index) const {
  if (index < 0 || index >= count_) return -1;
  return entries_[index];
}

const char* Round::textAt(const int index) const {
  const int entry = entryAt(index);
  return entry < 0 || entry >= kEntryCount ? "" : kEntries[entry];
}

Mark Round::markAt(const int index) const {
  if (index < 0 || index >= count_) return Mark::Unanswered;
  return marks_[index];
}

void Record::push(const int category, const int score) {
  const int clamped = score < 0 ? 0 : (score > 255 ? 255 : score);
  if (rounds < 65535) ++rounds;
  words = static_cast<uint16_t>(words + clamped > 65535 ? 65535 : words + clamped);
  if (clamped > best) best = static_cast<uint8_t>(clamped);

  if (recentCount < kRecentCount) {
    recent[recentCount++] = static_cast<uint8_t>(clamped);
  } else {
    for (int i = 1; i < kRecentCount; ++i) recent[i - 1] = recent[i];
    recent[kRecentCount - 1] = static_cast<uint8_t>(clamped);
  }

  if (category >= 0 && category < kCategoryCount) {
    if (clamped > bestIn[category]) bestIn[category] = static_cast<uint8_t>(clamped);
    played |= (1u << category);
  }
}

bool Record::everPlayed(const int category) const {
  if (category < 0 || category >= kCategoryCount) return false;
  return (played & (1u << category)) != 0;
}

int Record::recentAt(const int index) const {
  // Right-aligned: the newest round is always the last cell, so the chart grows
  // leftwards out of the same corner instead of sliding under the reader.
  const int offset = kRecentCount - recentCount;
  if (index < offset || index >= kRecentCount) return -1;
  return recent[index - offset];
}

int Record::recentPeak() const {
  int peak = 1;
  for (int i = 0; i < recentCount; ++i) {
    if (recent[i] > peak) peak = recent[i];
  }
  return peak;
}

int barSegments(const int secondsLeft, const int lengthSeconds) {
  if (lengthSeconds <= 0) return 0;
  const int left = secondsLeft < 0 ? 0 : (secondsLeft > lengthSeconds ? lengthSeconds : secondsLeft);
  // Rounds UP, so the bar is full the instant the round starts and empties only
  // when the time is genuinely gone. Rounding down would show seven eighths on
  // the first card, which reads as the clock having stolen a second.
  return (left * kBarSegments + lengthSeconds - 1) / lengthSeconds;
}

}  // namespace forehead

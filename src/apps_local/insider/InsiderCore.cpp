#include "InsiderCore.h"

#include <cstring>

namespace insider {

void Deck::reset() { std::memset(seen_, 0, sizeof(seen_)); }

bool Deck::seen(const int index) const {
  if (index < 0 || index >= kWordCount) return true;
  return (seen_[index >> 3] >> (index & 7)) & 1u;
}

void Deck::markSeen(const int index) {
  if (index < 0 || index >= kWordCount) return;
  seen_[index >> 3] |= static_cast<uint8_t>(1u << (index & 7));
}

int Deck::remaining() const {
  int count = 0;
  for (int i = 0; i < kWordCount; ++i) {
    if (!seen(i)) ++count;
  }
  return count;
}

void Deck::setMask(const uint8_t* bytes) {
  std::memcpy(seen_, bytes, sizeof(seen_));
  // The last byte covers indices past the end of the deck. Left set, they would
  // be counted as words that have been played, so remaining() would never reach
  // kWordCount and a full lap would come up short.
  constexpr int kSpare = kMaskBytes * 8 - kWordCount;
  if (kSpare > 0) seen_[kMaskBytes - 1] &= static_cast<uint8_t>(0xFFu >> kSpare);
}

int Deck::draw(Rng& rng) {
  int left = remaining();
  if (left == 0) {
    reset();
    left = kWordCount;
  }
  // Walk to the nth unseen word rather than guessing and retrying: with one
  // word left, rejection sampling would spin for an unbounded time, and this is
  // 560 iterations at worst on a path that runs once per round.
  int nth = static_cast<int>(rng.below(static_cast<uint32_t>(left)));
  for (int i = 0; i < kWordCount; ++i) {
    if (seen(i)) continue;
    if (nth-- == 0) {
      markSeen(i);
      return i;
    }
  }
  return 0;  // unreachable while left > 0
}

void Round::deal(const int players, const int wordIndex, Rng& rng) {
  players_ = static_cast<uint8_t>(players < kMinPlayers   ? kMinPlayers
                                  : players > kMaxPlayers ? kMaxPlayers
                                                          : players);
  wordIndex_ = static_cast<int16_t>(wordIndex);

  // The bag: everyone but one is a Citizen, and one is the Insider.
  Role bag[kMaxPlayers];
  const int bagSize = players_;
  for (int i = 0; i < bagSize - 1; ++i) bag[i] = Role::Citizen;
  bag[bagSize - 1] = Role::Insider;

  // Throw one away, unseen. This is the whole variant: a 1-in-n chance that the
  // discarded role was the Insider, and therefore that the honest answer to
  // "who was it" is nobody.
  const int discard = static_cast<int>(rng.below(static_cast<uint32_t>(bagSize)));
  for (int i = discard; i < bagSize - 1; ++i) bag[i] = bag[i + 1];

  // The Master takes the freed slot, so the bag is n roles again.
  bag[bagSize - 1] = Role::Master;

  // Fisher-Yates over the seats. Unbiased because Rng::below is.
  for (int i = bagSize - 1; i > 0; --i) {
    const int j = static_cast<int>(rng.below(static_cast<uint32_t>(i + 1)));
    const Role swap = bag[i];
    bag[i] = bag[j];
    bag[j] = swap;
  }
  for (int i = 0; i < bagSize; ++i) roles_[i] = bag[i];
}

Role Round::roleOf(const int seat) const {
  if (seat < 0 || seat >= players_) return Role::Citizen;
  return roles_[seat];
}

int Round::masterSeat() const {
  for (int i = 0; i < players_; ++i) {
    if (roles_[i] == Role::Master) return i;
  }
  return 0;
}

int Round::insiderSeat() const {
  for (int i = 0; i < players_; ++i) {
    if (roles_[i] == Role::Insider) return i;
  }
  return kNoInsider;
}

const char* Round::secretWord() const {
  if (wordIndex_ < 0 || wordIndex_ >= kWordCount) return kWords[0];
  return kWords[wordIndex_];
}

Outcome Round::judge(const int accused) const {
  // "Nobody here" is right exactly when the discarded role was the Insider.
  if (accused == kNoInsider) return hasInsider() ? Outcome::Lost : Outcome::Won;
  // Naming a seat is right only if that seat is the Insider. When there is no
  // Insider at all, naming anyone is still wrong -- the table had the option to
  // say so and did not take it.
  return accused == insiderSeat() ? Outcome::Won : Outcome::Lost;
}

void Record::push(const Outcome outcome) {
  if (rounds < UINT16_MAX) ++rounds;
  switch (outcome) {
    case Outcome::Won:
      if (won < UINT16_MAX) ++won;
      break;
    case Outcome::Lost:
      if (lost < UINT16_MAX) ++lost;
      break;
    case Outcome::OutOfTime:
      if (outOfTime < UINT16_MAX) ++outOfTime;
      break;
    case Outcome::Unplayed:
      break;
  }
  // The newest round takes the high pair, so at(15) is always the last one
  // played and the ornament fills left to right.
  recent = (recent >> 2) | (static_cast<uint32_t>(outcome) << 30);
}

Outcome Record::at(const int index) const {
  if (index < 0 || index >= 16) return Outcome::Unplayed;
  return static_cast<Outcome>((recent >> (index * 2)) & 0x3u);
}

int clockTick(const int secondsLeft) {
  if (secondsLeft >= kQuestionSeconds) return 0;
  if (secondsLeft <= 0) {
    return (kQuestionSeconds - kUrgentSeconds) / kCoarseStep + kUrgentSeconds / kFineStep;
  }
  if (secondsLeft > kUrgentSeconds) return (kQuestionSeconds - secondsLeft) / kCoarseStep;
  return (kQuestionSeconds - kUrgentSeconds) / kCoarseStep + (kUrgentSeconds - secondsLeft) / kFineStep;
}

}  // namespace insider

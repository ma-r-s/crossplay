#include "WavelengthCore.h"

namespace wavelength {
namespace {

int absDiff(const int a, const int b) { return a > b ? a - b : b - a; }

bool inRange(const int slot) { return slot >= 1 && slot <= kSlots; }

}  // namespace

int scoreForGuess(const int guess, const int target) {
  if (!inRange(guess) || !inRange(target)) return 0;
  switch (absDiff(guess, target)) {
    case 0:
      return kPointsExact;
    case 1:
      return kPointsOffByOne;
    case 2:
      return kPointsOffByTwo;
    default:
      return 0;
  }
}

bool endCallCorrect(const int guess, const int target, const Call call) {
  if (!inRange(guess) || !inRange(target)) return false;
  if (target == guess) return true;  // see the header: keeps scoring monotone
  return target > guess ? call == Call::TowardTop : call == Call::TowardBottom;
}

int scoreRound(const int guess, const int target, const Call call) {
  return scoreForGuess(guess, target) + (endCallCorrect(guess, target, call) ? kPointsEndCall : 0);
}

Deck::Deck(const int pairCount) : pairCount_(pairCount < 0 ? 0 : pairCount), seen_{} {
  if (pairCount_ > kMaxPairs) pairCount_ = kMaxPairs;
}

void Deck::markSeen(const int index) {
  if (index < 0 || index >= pairCount_) return;
  seen_[index / 32] |= (1u << (index % 32));
}

bool Deck::isSeen(const int index) const {
  if (index < 0 || index >= pairCount_) return true;
  return (seen_[index / 32] & (1u << (index % 32))) != 0;
}

int Deck::unseenCount() const {
  int n = 0;
  for (int i = 0; i < pairCount_; ++i)
    if (!isSeen(i)) ++n;
  return n;
}

void Deck::forgetSeen() {
  for (int i = 0; i < kSeenWords; ++i) seen_[i] = 0;
}

// Walks rather than rejection-samples. A nearly exhausted deck would make
// rejection sampling run long exactly when the player is least patient, and
// walking is bounded by the pair count either way.
int Deck::nthUnseen(const int n) const {
  int seen = 0;
  for (int i = 0; i < pairCount_; ++i) {
    if (isSeen(i)) continue;
    if (seen == n) return i;
    ++seen;
  }
  return -1;
}

int Deck::dealChoice(Rng& rng, int out[2]) const {
  out[0] = -1;
  out[1] = -1;
  const int available = unseenCount();
  if (available <= 0) return 0;

  out[0] = nthUnseen(static_cast<int>(rng.below(static_cast<uint32_t>(available))));
  if (available == 1) return 1;

  // The second is drawn from the unseen with the first removed, so the two are
  // distinct by construction rather than by retrying until they differ.
  const int wanted = static_cast<int>(rng.below(static_cast<uint32_t>(available - 1)));
  int k = 0;
  for (int i = 0; i < pairCount_; ++i) {
    if (isSeen(i) || i == out[0]) continue;
    if (k == wanted) {
      out[1] = i;
      break;
    }
    ++k;
  }
  return 2;
}

int Session::record(const int guess, const int target, const Call call) {
  const bool practice = isPractice();
  const int points = practice ? 0 : scoreRound(guess, target, call);
  if (!practice) {
    total += points;
    ++scoredRounds;
  }
  ++round;
  return points;
}

int Session::averageTenths() const {
  if (scoredRounds <= 0) return 0;
  return (total * 10 + scoredRounds / 2) / scoredRounds;
}

int bucketFor(const int guess, const int target) {
  if (!inRange(guess) || !inRange(target)) return kBucketCount - 1;
  const int d = absDiff(guess, target);
  if (d == 0) return 0;
  if (d == 1) return 1;
  if (d == 2) return 2;
  if (d <= 5) return 3;
  return 4;
}

void Record::add(const int guess, const int target, const Call call) {
  if (rounds < UINT16_MAX) ++rounds;
  const int gained = scoreRound(guess, target, call);
  if (points + gained <= UINT16_MAX) points = static_cast<uint16_t>(points + gained);
  const int b = bucketFor(guess, target);
  if (buckets[b] < UINT16_MAX) ++buckets[b];
}

int Record::averageTenths() const {
  if (rounds == 0) return 0;
  return (points * 10 + rounds / 2) / rounds;
}

uint16_t Record::peak() const {
  uint16_t top = 0;
  for (const uint16_t b : buckets)
    if (b > top) top = b;
  return top;
}

}  // namespace wavelength

#include "HeartsCore.h"

namespace hearts {
namespace {

// Sort key: suit first, then trick rank ascending. A hand held this way reads
// the way a person fans one, and "I am out of clubs" is a gap you see rather
// than a list you scan.
int sortKey(const uint8_t card) { return static_cast<int>(cards::suitOf(card)) * 16 + trickRank(card); }

}  // namespace

uint32_t nextRandom(uint32_t& seed) {
  // xorshift32. Never seeded with zero: that is the one fixed point, and it
  // would make every "random" deal identical while looking like it worked.
  if (seed == 0) seed = 0x9E3779B9u;
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  return seed;
}

void Hand::add(const uint8_t card) {
  if (count >= kHandSize) return;
  const int key = sortKey(card);
  int at = count;
  while (at > 0 && sortKey(cards[at - 1]) > key) {
    cards[at] = cards[at - 1];
    --at;
  }
  cards[at] = card;
  ++count;
}

int Hand::indexOf(const uint8_t card) const {
  for (int i = 0; i < count; ++i) {
    if (cards[i] == card) return i;
  }
  return -1;
}

bool Hand::remove(const uint8_t card) {
  const int at = indexOf(card);
  if (at < 0) return false;
  for (int i = at; i + 1 < count; ++i) cards[i] = cards[i + 1];
  --count;
  return true;
}

bool Hand::hasSuit(const Suit suit) const {
  for (int i = 0; i < count; ++i) {
    if (cards::suitOf(cards[i]) == suit) return true;
  }
  return false;
}

int Hand::countSuit(const Suit suit) const {
  int n = 0;
  for (int i = 0; i < count; ++i) {
    if (cards::suitOf(cards[i]) == suit) ++n;
  }
  return n;
}

bool Hand::onlyHearts() const {
  if (count == 0) return false;
  for (int i = 0; i < count; ++i) {
    if (!isHeart(cards[i])) return false;
  }
  return true;
}

Suit Trick::ledSuit() const { return cards::suitOf(played[seatIndex(leader)]); }

int Trick::points() const {
  int total = 0;
  for (int i = 0; i < kSeats; ++i) {
    if (played[i] != kNoCard) total += penaltyOf(played[i]);
  }
  return total;
}

Seat Trick::winner() const {
  const Suit led = ledSuit();
  Seat best = leader;
  int bestRank = -1;
  for (int i = 0; i < kSeats; ++i) {
    const uint8_t card = played[i];
    if (card == kNoCard || cards::suitOf(card) != led) continue;
    const int rank = trickRank(card);
    if (rank > bestRank) {
      bestRank = rank;
      best = static_cast<Seat>(i);
    }
  }
  return best;
}

void Trick::clear() {
  for (int i = 0; i < kSeats; ++i) played[i] = kNoCard;
  count = 0;
}

void deal(Game& game, uint32_t& seed) {
  uint8_t deck[cards::kDeck];
  int n = 0;
  for (int s = 0; s < cards::kSuits; ++s) {
    for (int r = 0; r < cards::kRanks; ++r) {
      deck[n++] = cards::makeCard(static_cast<Suit>(s), r);
    }
  }
  // Fisher-Yates, downward, so every permutation is reachable.
  for (int i = cards::kDeck - 1; i > 0; --i) {
    const int j = static_cast<int>(nextRandom(seed) % static_cast<uint32_t>(i + 1));
    const uint8_t swap = deck[i];
    deck[i] = deck[j];
    deck[j] = swap;
  }

  for (int s = 0; s < kSeats; ++s) {
    game.hands[s].clear();
    game.taken[s] = 0;
    game.tricksWon[s] = 0;
    game.passingCount[s] = 0;
    game.receivedCount[s] = 0;
    for (int su = 0; su < cards::kSuits; ++su) game.voidShown[s][su] = false;
  }
  for (int i = 0; i < cards::kDeck; ++i) game.played[i] = false;
  for (int i = 0; i < cards::kDeck; ++i) game.hands[i % kSeats].add(deck[i]);

  game.trick.clear();
  game.heartsBroken = false;
  game.trickNumber = 0;

  // Whoever holds the two of clubs leads, and until the pass is committed we
  // do not know who that will be -- so the turn is set in commitPass(), not
  // here. Hold still goes through commitPass() for exactly that reason.
  game.phase = Phase::Passing;
  game.turn = Seat::South;
}

void newGame(Game& game, uint32_t& seed) {
  for (int s = 0; s < kSeats; ++s) game.total[s] = 0;
  game.handNumber = 0;
  game.lastHand = HandResult{};
  deal(game, seed);
}

bool isLegalCard(const Hand& hand, const Trick& trick, const bool heartsBroken, const bool firstTrick,
                 const uint8_t card) {
  if (!hand.has(card)) return false;

  const bool leading = trick.empty();

  if (firstTrick && leading) {
    // Nothing but the two of clubs opens a hand.
    return card == twoOfClubs();
  }

  if (!leading) {
    const Suit led = trick.ledSuit();
    if (hand.hasSuit(led)) {
      if (cards::suitOf(card) != led) return false;
    } else if (firstTrick && penaltyOf(card) > 0) {
      // Void in the led suit on the first trick: still no blood, unless blood
      // is all that is left. `onlyPenalty` rather than onlyHearts, because a
      // hand of twelve hearts plus the queen has no safe card either.
      bool onlyPenalty = true;
      for (int i = 0; i < hand.count; ++i) {
        if (penaltyOf(hand.cards[i]) == 0) {
          onlyPenalty = false;
          break;
        }
      }
      if (!onlyPenalty) return false;
    }
    return true;
  }

  // Leading a later trick: hearts stay shut until somebody breaks them, unless
  // hearts are the whole hand.
  if (isHeart(card) && !heartsBroken && !hand.onlyHearts()) return false;
  return true;
}

bool isLegalPlay(const Game& game, const Seat seat, const uint8_t card) {
  if (game.phase != Phase::Playing) return false;
  if (seat != game.turn) return false;
  return isLegalCard(game.hands[seatIndex(seat)], game.trick, game.heartsBroken, game.firstTrick(), card);
}

int legalPlays(const Game& game, const Seat seat, uint8_t* out) {
  const Hand& hand = game.hands[seatIndex(seat)];
  int n = 0;
  for (int i = 0; i < hand.count; ++i) {
    if (isLegalPlay(game, seat, hand.cards[i])) out[n++] = hand.cards[i];
  }
  return n;
}

bool playCard(Game& game, const uint8_t card) {
  const Seat seat = game.turn;
  if (!isLegalPlay(game, seat, card)) return false;

  // Failing to follow shows the whole table that this seat is out of that
  // suit. Recorded before the card lands, while the led suit is still the one
  // this play was answering.
  if (!game.trick.empty()) {
    const Suit led = game.trick.ledSuit();
    if (cards::suitOf(card) != led) game.voidShown[seatIndex(seat)][static_cast<int>(led)] = true;
  }

  game.hands[seatIndex(seat)].remove(card);
  game.played[static_cast<int>(cards::suitOf(card)) * cards::kRanks + cards::rankOf(card)] = true;
  game.trick.played[seatIndex(seat)] = card;
  ++game.trick.count;
  if (isHeart(card)) game.heartsBroken = true;

  if (game.trick.complete()) {
    // The trick stays on the table. Sweeping it here would mean the winning
    // card is drawn and removed in the same repaint, so on a panel that takes
    // a third of a second to settle the fourth card is never actually seen.
    game.phase = Phase::TrickTaken;
    return true;
  }
  game.turn = nextSeat(seat);
  return true;
}

void sweepTrick(Game& game) {
  if (game.phase != Phase::TrickTaken) return;
  const Seat winner = game.trick.winner();
  game.taken[seatIndex(winner)] += game.trick.points();
  ++game.tricksWon[seatIndex(winner)];

  game.trick.clear();
  ++game.trickNumber;

  if (game.trickNumber >= kTricks) {
    scoreHand(game);
    return;
  }
  game.trick.leader = winner;
  game.turn = winner;
  game.phase = Phase::Playing;
}

void scoreHand(Game& game) {
  HandResult result;
  int shooter = -1;
  for (int s = 0; s < kSeats; ++s) {
    result.taken[s] = game.taken[s];
    if (game.taken[s] == kMoonPoints) shooter = s;
  }

  if (shooter >= 0) {
    result.moon = true;
    result.shooter = static_cast<Seat>(shooter);
    for (int s = 0; s < kSeats; ++s) result.scored[s] = (s == shooter) ? 0 : kMoonPoints;
  } else {
    for (int s = 0; s < kSeats; ++s) result.scored[s] = game.taken[s];
  }

  for (int s = 0; s < kSeats; ++s) {
    game.total[s] += result.scored[s];
    result.total[s] = game.total[s];
  }
  game.lastHand = result;

  bool over = false;
  for (int s = 0; s < kSeats; ++s) {
    if (game.total[s] >= kTargetScore) over = true;
  }
  game.phase = over ? Phase::GameOver : Phase::HandOver;
}

bool setPass(Game& game, const Seat seat, const uint8_t* selection, const int count) {
  if (game.phase != Phase::Passing) return false;
  if (count != kPassCount) return false;
  const Hand& hand = game.hands[seatIndex(seat)];
  for (int i = 0; i < count; ++i) {
    if (!hand.has(selection[i])) return false;
    for (int j = 0; j < i; ++j) {
      if (selection[i] == selection[j]) return false;
    }
  }
  for (int i = 0; i < count; ++i) game.passing[seatIndex(seat)][i] = selection[i];
  game.passingCount[seatIndex(seat)] = static_cast<uint8_t>(count);
  return true;
}

bool passReady(const Game& game) {
  if (game.passDirection() == Pass::Hold) return true;
  for (int s = 0; s < kSeats; ++s) {
    if (game.passingCount[s] != kPassCount) return false;
  }
  return true;
}

namespace {

// Whoever holds the two of clubs leads the first trick. After a pass that can
// be anyone, which is why this is asked here and not at deal time.
Seat holderOfTwoOfClubs(const Game& game) {
  for (int s = 0; s < kSeats; ++s) {
    if (game.hands[s].has(twoOfClubs())) return static_cast<Seat>(s);
  }
  return Seat::South;  // unreachable with a full deal
}

}  // namespace

void commitPass(Game& game) {
  if (game.phase != Phase::Passing || !passReady(game)) return;

  const Pass direction = game.passDirection();
  if (direction != Pass::Hold) {
    // Lift every seat's three cards out FIRST, then deal them in. Done seat by
    // seat, a card passed left can be picked up again by the next seat's own
    // pass before it has left, and the hand ends up with fourteen cards.
    uint8_t lifted[kSeats][kPassCount] = {};
    for (int s = 0; s < kSeats; ++s) {
      for (int i = 0; i < kPassCount; ++i) {
        lifted[s][i] = game.passing[s][i];
        game.hands[s].remove(lifted[s][i]);
      }
    }
    for (int s = 0; s < kSeats; ++s) {
      const Seat target = passTarget(static_cast<Seat>(s), direction);
      const int t = seatIndex(target);
      for (int i = 0; i < kPassCount; ++i) {
        game.hands[t].add(lifted[s][i]);
        game.received[t][i] = lifted[s][i];
      }
      game.receivedCount[t] = kPassCount;
    }
  }

  for (int s = 0; s < kSeats; ++s) game.passingCount[s] = 0;
  const Seat opener = holderOfTwoOfClubs(game);
  game.trick.clear();
  game.trick.leader = opener;
  game.turn = opener;
  game.phase = Phase::Playing;
}

void nextHand(Game& game, uint32_t& seed) {
  if (game.phase != Phase::HandOver) return;
  ++game.handNumber;
  deal(game, seed);
}

bool isConsistent(const Game& game) {
  switch (game.phase) {
    case Phase::Passing:
    case Phase::Playing:
    case Phase::TrickTaken:
    case Phase::HandOver:
    case Phase::GameOver:
      break;
    default:
      return false;
  }
  if (seatIndex(game.turn) < 0 || seatIndex(game.turn) >= kSeats) return false;
  if (seatIndex(game.trick.leader) < 0 || seatIndex(game.trick.leader) >= kSeats) return false;
  if (game.trick.count > kSeats) return false;
  if (game.trickNumber > kTricks) return false;

  int onTable = 0;
  for (int s = 0; s < kSeats; ++s) {
    if (game.hands[s].count > kHandSize) return false;
    if (game.passingCount[s] > kPassCount) return false;
    if (game.receivedCount[s] > kPassCount) return false;
    if (game.total[s] < 0 || game.taken[s] < 0 || game.taken[s] > kMoonPoints) return false;
    if (game.tricksWon[s] > kTricks) return false;
    if (game.trick.played[s] != kNoCard) ++onTable;
  }
  if (onTable != game.trick.count) return false;

  // The deck has to add up: each of the 52 is either still in a hand or has
  // been played, never both and never neither. This is what a torn save fails.
  //
  // A card ON THE TABLE is counted by `played`, not separately -- playCard
  // records it there the moment it leaves the hand. Counting the table as well
  // makes every live trick look like a duplicated card, which is what the first
  // version of this function did and what its test caught.
  int seen[cards::kDeck] = {};
  const auto index = [](const uint8_t card) {
    const int rank = cards::rankOf(card);
    if (rank < 0 || rank >= cards::kRanks) return -1;
    return static_cast<int>(cards::suitOf(card)) * cards::kRanks + rank;
  };
  for (int s = 0; s < kSeats; ++s) {
    for (int i = 0; i < game.hands[s].count; ++i) {
      const int at = index(game.hands[s].at(i));
      if (at < 0) return false;
      ++seen[at];
    }
    // Whatever is face up must also be recorded as spent.
    if (game.trick.played[s] != kNoCard) {
      const int at = index(game.trick.played[s]);
      if (at < 0 || !game.played[at]) return false;
    }
  }
  for (int i = 0; i < cards::kDeck; ++i) {
    if (game.played[i]) ++seen[i];
    if (seen[i] != 1) return false;
  }
  return true;
}

Seat leader(const Game& game) {
  int best = 0;
  for (int s = 1; s < kSeats; ++s) {
    if (game.total[s] < game.total[best]) best = s;
  }
  return static_cast<Seat>(best);
}

bool isTied(const Game& game) {
  const int best = game.total[seatIndex(leader(game))];
  int n = 0;
  for (int s = 0; s < kSeats; ++s) {
    if (game.total[s] == best) ++n;
  }
  return n > 1;
}

}  // namespace hearts

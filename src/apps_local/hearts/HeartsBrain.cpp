#include "HeartsBrain.h"

namespace hearts {
namespace {

namespace c = cards;

int cardIndex(const uint8_t card) { return static_cast<int>(c::suitOf(card)) * c::kRanks + c::rankOf(card); }

// How many cards of `suit` are still unaccounted for from this seat's point of
// view: not in my hand, not yet played, not on the table. These are the ones
// somebody else is holding.
int outstanding(const Observation& obs, const Suit suit) {
  int held = 0;
  for (int r = 0; r < c::kRanks; ++r) {
    if (!obs.wasPlayed(suit, r)) ++held;
  }
  for (int i = 0; i < obs.hand.count; ++i) {
    if (c::suitOf(obs.hand.at(i)) == suit) --held;
  }
  return held < 0 ? 0 : held;
}

// The highest card of the led suit currently on the table.
int currentHigh(const Trick& trick) {
  if (trick.empty()) return -1;
  const Suit led = trick.ledSuit();
  int best = -1;
  for (int s = 0; s < kSeats; ++s) {
    const uint8_t card = trick.played[s];
    if (card == kNoCard || c::suitOf(card) != led) continue;
    const int rank = trickRank(card);
    if (rank > best) best = rank;
  }
  return best;
}

bool holdsQueen(const Observation& obs) { return obs.hand.has(c::makeCard(Suit::Spades, c::kQueen)); }

// A seat that has shown void in a suit will throw whatever it likes on it --
// the queen included. Knowing that is most of what separates a Sharp from a
// Rookie.
bool anyoneVoidIn(const Observation& obs, const Suit suit) {
  for (int s = 0; s < kSeats; ++s) {
    if (s == seatIndex(obs.me)) continue;
    if (obs.showsVoid[s][static_cast<int>(suit)]) return true;
  }
  return false;
}

// Is the queen still a live danger to me? She is if she has not fallen and I am
// not the one holding her.
bool queenLoose(const Observation& obs) { return !obs.queenGone() && !holdsQueen(obs); }

bool lastToPlay(const Observation& obs) { return obs.trick.count == kSeats - 1; }

// Points already on the table.
int tablepoints(const Trick& trick) { return trick.points(); }

}  // namespace

void observe(const Game& game, const Seat seat, Observation& out) {
  out = Observation{};
  out.me = seat;
  out.hand = game.hands[seatIndex(seat)];
  out.trick = game.trick;
  out.phase = game.phase;
  out.heartsBroken = game.heartsBroken;
  out.trickNumber = game.trickNumber;
  out.passDirection = game.passDirection();
  for (int s = 0; s < kSeats; ++s) {
    out.total[s] = game.total[s];
    out.taken[s] = game.taken[s];
  }

  // What has gone is COPIED from the record the rules keep, never worked out
  // from who is holding what. That matters twice over: this function now reads
  // no other seat's hand at all, so the no-cheating rule is visible rather than
  // argued; and the derivation it replaces was only correct when all 52 cards
  // were accounted for, which silently made every card look spent in a
  // position built with fewer.
  //
  // A card still face up on the table is VISIBLE, not gone: it is deciding the
  // trick in front of you, and treating it as spent hides it from the one
  // decision it is part of.
  for (int i = 0; i < c::kDeck; ++i) out.gone[i] = game.played[i];
  for (int s = 0; s < kSeats; ++s) {
    if (game.trick.played[s] != kNoCard) out.gone[cardIndex(game.trick.played[s])] = false;
  }

  // Void table. Only what was SHOWN: a seat that failed to follow the led suit
  // does not hold it. Nothing here is inferred from anyone's hand.
  for (int s = 0; s < kSeats; ++s) {
    for (int su = 0; su < c::kSuits; ++su) out.showsVoid[s][su] = game.voidShown[s][su];
  }
}

int passUrgency(const Observation& obs, const uint8_t card) {
  const Suit suit = c::suitOf(card);
  const int rank = trickRank(card);
  const int lengthInSuit = obs.hand.countSuit(suit);
  int want = 0;

  if (isQueenOfSpades(card)) {
    // The queen is thirteen points that only leave by being played. Keep her
    // only behind a real wall: with four or more spades you can afford to duck
    // under a lead twice before she is exposed.
    want = lengthInSuit >= 5 ? 20 : 120;
  } else if (suit == Suit::Spades && rank > trickRank(c::makeCard(Suit::Spades, c::kQueen))) {
    // The ace and king of spades win tricks you do not want, and they draw the
    // queen onto themselves. Short in spades, they are a liability.
    want = lengthInSuit >= 5 ? 25 : 90 + rank;
  } else if (suit == Suit::Hearts) {
    // High hearts take heart tricks. Low ones are cheap protection.
    want = rank >= 9 ? 55 + rank * 2 : rank * 2;
    if (lengthInSuit <= 2) want += 10;
  } else {
    // Side suits: a high card is a trick you will be made to win. A short suit
    // is worth emptying completely, because a void is a free discard all hand.
    want = rank * 3;
    if (lengthInSuit <= 3) want += (4 - lengthInSuit) * 12;
    // Keep the low clubs: they are the safest cards in the deck early, and the
    // two of clubs decides who opens.
    if (suit == Suit::Clubs && rank <= 2) want -= 25;
  }
  return want;
}

void decidePass(const Observation& obs, const Skill skill, uint32_t& rng, uint8_t* out) {
  // Score every card, then take the three keenest. A Rookie passes its three
  // highest cards flat, which is exactly the naive thing a beginner does and
  // leaves it holding short suits it cannot use.
  int score[kHandSize];
  for (int i = 0; i < obs.hand.count; ++i) {
    score[i] = (skill == Skill::Sharp) ? passUrgency(obs, obs.hand.at(i)) : trickRank(obs.hand.at(i)) * 4;
    // A whisper of noise so two brains with the same hand do not pass the same
    // three cards every single time.
    score[i] += static_cast<int>(nextRandom(rng) % 5u);
  }

  // A hand shorter than the pass cannot fill it, and `best` then stays -1 --
  // which the loop below used to write to, one slot before the array.
  // Unreachable in a legal game and reachable from a corrupt save, which is
  // exactly the class of input this has to survive.
  for (int pick = 0; pick < kPassCount; ++pick) out[pick] = kNoCard;
  if (obs.hand.count < kPassCount) return;

  bool used[kHandSize] = {};
  for (int pick = 0; pick < kPassCount; ++pick) {
    int best = -1;
    for (int i = 0; i < obs.hand.count; ++i) {
      if (used[i]) continue;
      if (best < 0 || score[i] > score[best]) best = i;
    }
    if (best < 0) return;
    used[best] = true;
    out[pick] = obs.hand.at(best);
  }
}

bool shootingTheMoon(const Observation& obs) {
  const int me = seatIndex(obs.me);
  if (obs.taken[me] <= 0) return false;
  for (int s = 0; s < kSeats; ++s) {
    if (s != me && obs.taken[s] > 0) return false;  // somebody else has points: it is already dead
  }
  // Committed, not hopeful. Below this a "shoot" is just a seat that took one
  // early heart, and abandoning it later costs nothing while pursuing it costs
  // the hand.
  if (obs.taken[me] < 8) return false;

  // And still able to finish. Every point still out there has to come to me, so
  // I need cards that win: count how many of my cards nothing outstanding beats.
  int commanding = 0;
  for (int i = 0; i < obs.hand.count; ++i) {
    const uint8_t card = obs.hand.at(i);
    const Suit suit = c::suitOf(card);
    const int rank = trickRank(card);
    bool beatable = false;
    for (int r = rank + 1; r < c::kRanks; ++r) {
      const int deckRank = (r == c::kRanks - 1) ? c::kAce : r + 1;
      if (!obs.wasPlayed(suit, deckRank) && !obs.hand.has(c::makeCard(suit, deckRank))) {
        beatable = true;
        break;
      }
    }
    if (!beatable) ++commanding;
  }
  return commanding * 2 >= obs.hand.count;
}

int moonThreat(const Observation& obs) {
  // Somebody is shooting when every point taken so far is theirs, and enough
  // have been taken that it is not just the first heart falling. Ten is a
  // deliberate floor: below it the "threat" is noise and a brain that eats
  // point tricks on noise simply loses.
  int holder = -1;
  int held = 0;
  for (int s = 0; s < kSeats; ++s) {
    if (obs.taken[s] <= 0) continue;
    if (holder >= 0) return -1;  // points are split: nobody is shooting
    holder = s;
    held = obs.taken[s];
  }
  if (holder < 0 || holder == seatIndex(obs.me)) return -1;
  // The old bar was ten points, which is calibrated for a shoot that opens with
  // the queen. A hearts-only shoot reaches ten only after ten of the thirteen
  // hearts have gone to one seat, by which point the hand is decided and the
  // defence fires too late to be worth having. Six from the fourth trick on
  // catches both shapes while still ignoring the single early heart that means
  // nothing.
  return (held >= 10 || (held >= 6 && obs.trickNumber >= 4)) ? holder : -1;
}

namespace {

// Pick the card that leads a trick.
uint8_t chooseLead(const Observation& obs, const Skill skill, const uint8_t* legal, const int n, uint32_t& rng) {
  const bool shooting = skill == Skill::Sharp && shootingTheMoon(obs);
  int bestAt = 0;
  int bestScore = -1000000;
  for (int i = 0; i < n; ++i) {
    const uint8_t card = legal[i];
    const Suit suit = c::suitOf(card);
    const int rank = trickRank(card);
    int score = 0;

    if (shooting) {
      // Lead the cards nothing can beat, so the points keep coming to me.
      score += rank * 10;
      score += static_cast<int>(nextRandom(rng) % 7u);
      if (score > bestScore) {
        bestScore = score;
        bestAt = i;
      }
      continue;
    }

    // Leading low is the whole art: a low card wins nothing and gives nothing.
    score += (12 - rank) * 6;

    if (skill == Skill::Sharp) {
      // Hunt the queen. While she is out there and I am holding small spades,
      // leading spades drags her out of somebody's hand onto a trick I am not
      // going to win. Once she has fallen, spades are just another suit.
      if (suit == Suit::Spades && queenLoose(obs)) {
        const int queenRank = trickRank(c::makeCard(Suit::Spades, c::kQueen));
        if (rank < queenRank) {
          score += 30;
        } else {
          // Leading a spade ABOVE the queen invites her straight onto my trick.
          score -= 70;
        }
      }

      // Leading a suit somebody is void in hands them a free discard, and the
      // queen is the discard they want to make.
      if (anyoneVoidIn(obs, suit)) score -= 45;

      // Emptying a short suit early buys discards later.
      const int lengthInSuit = obs.hand.countSuit(suit);
      if (lengthInSuit <= 2 && suit != Suit::Hearts) score += 18;

      // A card nobody can beat is a trick I am about to win for nothing.
      bool unbeatable = true;
      for (int r = rank + 1; r < c::kRanks; ++r) {
        // trickRank r maps back to the deck's numbering: 12 is the ace (0).
        const int deckRank = (r == c::kRanks - 1) ? c::kAce : r + 1;
        if (!obs.wasPlayed(suit, deckRank)) {
          unbeatable = false;
          break;
        }
      }
      if (unbeatable && outstanding(obs, suit) > 0) score -= 35;

      // Leading hearts pushes points around for no gain unless I am short of
      // everything else.
      if (suit == Suit::Hearts) score -= 25;
    }

    score += static_cast<int>(nextRandom(rng) % 7u);
    if (score > bestScore) {
      bestScore = score;
      bestAt = i;
    }
  }
  return legal[bestAt];
}

// Pick the card that follows one.
uint8_t chooseFollow(const Observation& obs, const Skill skill, const uint8_t* legal, const int n, uint32_t& rng) {
  const Suit led = obs.trick.ledSuit();
  const int high = currentHigh(obs.trick);
  const int onTable = tablepoints(obs.trick);
  const int threat = (skill == Skill::Sharp) ? moonThreat(obs) : -1;
  const bool shooting = skill == Skill::Sharp && shootingTheMoon(obs);
  const bool last = lastToPlay(obs);

  int bestAt = 0;
  int bestScore = -1000000;

  if (skill == Skill::Rookie) {
    // The beginner's habit, and a real one: follow with your lowest card and
    // dump your highest when you are void. It never gets caught winning a
    // trick it could have ducked, but it also never SHEDS anything, so by the
    // last few tricks it is holding the aces and takes the points with them.
    for (int i = 0; i < n; ++i) {
      const uint8_t card = legal[i];
      const bool following = c::suitOf(card) == led;
      const int rank = trickRank(card);
      int score = following ? (400 - rank * 6) : (100 + rank * 6 + penaltyOf(card) * 9);
      score += static_cast<int>(nextRandom(rng) % 11u);
      if (score > bestScore) {
        bestScore = score;
        bestAt = i;
      }
    }
    return legal[bestAt];
  }

  for (int i = 0; i < n; ++i) {
    const uint8_t card = legal[i];
    const Suit suit = c::suitOf(card);
    const int rank = trickRank(card);
    const bool following = suit == led;
    const bool wouldLead = following && rank > high;
    int score = 0;

    if (shooting) {
      // EVERYTHING INVERTS. I need every remaining point, so I want to win this
      // trick and I want it to be worth something.
      if (following) {
        score += wouldLead ? 800 + rank * 6 : rank;
      } else {
        score += 50 + (12 - rank) * 4;  // void: throw rubbish, keep the winners
      }
      score += static_cast<int>(nextRandom(rng) % 7u);
      if (score > bestScore) {
        bestScore = score;
        bestAt = i;
      }
      continue;
    }

    if (following) {
      if (!wouldLead) {
        // Ducking. Play the HIGHEST card that still loses: it sheds a dangerous
        // card for free, and that is the single most valuable habit in Hearts.
        score += 600 + rank * 4;
      } else {
        // Taking it. How bad that is depends on what is on the table and on
        // whether anyone can still pile on after me.
        score += 200 - rank * 5 - onTable * 12;
        if (last && onTable == 0) {
          // Last to play, nothing at stake: winning is free and gets rid of a
          // high card that would otherwise be trouble later.
          score += 260;
        }
        if (skill == Skill::Sharp && queenLoose(obs) && led == Suit::Spades &&
            rank > trickRank(c::makeCard(Suit::Spades, c::kQueen))) {
          // Beating the queen's rank with the queen still out is how you get
          // handed thirteen points.
          score -= 180;
        }
      }
      // Stop a moon by taking a point trick on purpose. This is the behaviour
      // random play never has, and it is the one that makes the table feel like
      // it is paying attention.
      if (threat >= 0 && onTable > 0 && wouldLead) score += 420;
    } else {
      // Void in the led suit: this is a discard, so throw the most dangerous
      // thing I own.
      score += 100;
      if (isQueenOfSpades(card)) {
        // Getting rid of her is worth almost anything, unless the trick is
        // already mine, in which case I would be giving myself thirteen.
        score += 500;
      } else if (suit == Suit::Spades && skill == Skill::Sharp && queenLoose(obs) &&
                 rank > trickRank(c::makeCard(Suit::Spades, c::kQueen))) {
        score += 220;  // shed the ace and king before she arrives
      } else if (suit == Suit::Hearts) {
        score += 60 + rank * 8;  // high hearts first
      } else {
        score += rank * 6;
      }
      if (threat >= 0) {
        // Do not feed a shooter. Throwing points on a trick they are taking is
        // exactly how a moon lands.
        if (penaltyOf(card) > 0) score -= 300;
      }
    }

    score += static_cast<int>(nextRandom(rng) % 7u);
    if (score > bestScore) {
      bestScore = score;
      bestAt = i;
    }
  }
  return legal[bestAt];
}

}  // namespace

uint8_t decidePlay(const Observation& obs, const Skill skill, uint32_t& rng) {
  // Legality is asked of the RULES, never re-derived here. A brain with its own
  // opinion about what it may play is a brain that will eventually disagree
  // with the game in a way no test of either one alone can catch.
  uint8_t legal[kHandSize];
  int n = 0;
  for (int i = 0; i < obs.hand.count; ++i) {
    const uint8_t card = obs.hand.at(i);
    if (isLegalCard(obs.hand, obs.trick, obs.heartsBroken, obs.firstTrick(), card)) legal[n++] = card;
  }
  if (n == 0) return kNoCard;  // the rules guarantee this cannot happen on turn
  if (n == 1) return legal[0];

  return obs.trick.empty() ? chooseLead(obs, skill, legal, n, rng) : chooseFollow(obs, skill, legal, n, rng);
}

}  // namespace hearts

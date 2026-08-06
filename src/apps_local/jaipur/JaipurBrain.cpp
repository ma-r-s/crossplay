#include "JaipurBrain.h"

namespace jaipur {

namespace {

// Moves are visited rather than collected. A full enumeration is a few hundred
// exchanges, and at 14 bytes each that is several KB on a stack whose budget is
// 256 bytes -- so nothing is ever materialised on device. `legalMoves` exists
// for the tests, which supply their own storage.
struct MoveSink {
  void* ctx;
  void (*fn)(void*, const Move&);
};

int handSizeOf(const Observation& obs) {
  int n = 0;
  for (int g = 0; g < kGoodCount; ++g) n += obs.hand[g];
  return n;
}

int marketCamelsOf(const Observation& obs) {
  int n = 0;
  for (int i = 0; i < kMarketSlots; ++i) {
    if (obs.market[i] == kCamel) ++n;
  }
  return n;
}

// Every way to give `need` goods out of the hand, skipping types being taken.
// Recursive over the six types, which is at most six deep and holds one byte
// per level.
void giveCombinations(const Observation& obs, const int* taken, const int firstGood, const int need, Move& move,
                      const MoveSink& sink) {
  if (need == 0) {
    sink.fn(sink.ctx, move);
    return;
  }
  if (firstGood >= kGoodCount) return;

  const int g = firstGood;
  // "The same goods type cannot be both surrendered and taken at the market."
  const int most = taken[g] > 0 ? 0 : (obs.hand[g] < need ? obs.hand[g] : need);
  for (int n = 0; n <= most; ++n) {
    move.give[g] = static_cast<uint8_t>(n);
    giveCombinations(obs, taken, g + 1, need - n, move, sink);
  }
  move.give[g] = 0;
}

void forEachLegalMove(const Observation& obs, const MoveSink& sink) {
  const int held = handSizeOf(obs);

  // A: take one good. Only from a slot holding a good, and only with room.
  if (held < kHandLimit) {
    for (int i = 0; i < kMarketSlots; ++i) {
      if (obs.market[i] >= kGoodCount) continue;  // camel, empty or unknown
      sink.fn(sink.ctx, Move::takeOne(i));
    }
  }

  // B: take every camel. All or nothing, and never mixed with goods.
  if (marketCamelsOf(obs) > 0) sink.fn(sink.ctx, Move::takeCamels());

  // C: sell. Diamonds, gold and silver need two.
  for (int g = 0; g < kGoodCount; ++g) {
    const Good good = static_cast<Good>(g);
    const int lowest = sellsInPairs(good) ? 2 : 1;
    for (int n = lowest; n <= obs.hand[g]; ++n) sink.fn(sink.ctx, Move::sell(good, n));
  }

  // D: exchange. Every market subset of two or more goods, against every way of
  // paying for it out of the hand and the herd.
  for (int mask = 0; mask < (1 << kMarketSlots); ++mask) {
    int want = 0;
    int taken[kGoodCount] = {};
    bool usable = true;
    for (int i = 0; i < kMarketSlots; ++i) {
      if (((mask >> i) & 1) == 0) continue;
      const uint8_t card = obs.market[i];
      // Camels can be given in an exchange but never taken in one, and an empty
      // or unseen slot holds nothing to take.
      if (card >= kGoodCount) {
        usable = false;
        break;
      }
      ++taken[card];
      ++want;
    }
    // "An exchange always involves at least 2 cards for 2 cards."
    if (!usable || want < 2) continue;

    for (int camels = 0; camels <= want && camels <= obs.herd[obs.seat]; ++camels) {
      const int need = want - camels;
      // Giving camels grows the hand, so the limit is checked on the result.
      if (held + want - need > kHandLimit) continue;
      Move move;
      move.kind = Move::Kind::Exchange;
      move.marketMask = static_cast<uint8_t>(mask);
      move.giveCamels = static_cast<uint8_t>(camels);
      giveCombinations(obs, taken, 0, need, move, sink);
    }
  }
}

// What the cards in hand are worth if sold right now, which is the only honest
// price: token piles only ever go down.
int handValue(const Observation& obs) {
  int total = 0;
  for (int g = 0; g < kGoodCount; ++g) {
    int depth = obs.goodsDepth[g];
    for (int i = 0; i < obs.hand[g]; ++i) {
      if (depth >= kPileDepth[g]) break;
      total += kGoodsTokens[g][depth];
      ++depth;
    }
  }
  return total;
}

// The mean value still sitting in a bonus stack, for weighing a run you are
// building towards. Its face is down, so a mean is all anyone can use.
int meanBonus(const int stack) {
  int total = 0;
  for (int i = 0; i < kBonusDepth[stack]; ++i) total += kBonusTokens[stack][i];
  return total / kBonusDepth[stack];
}

}  // namespace

int evaluate(const Observation& obs) {
  const int me = obs.seat;
  const int them = 1 - me;

  // Banked rupees are the only certain quantity, so they carry full weight and
  // everything else is a discount against them. Scaled by 16 throughout so the
  // fractional weights below stay in integers.
  int score = static_cast<int>(obs.yourGoodsRupees + obs.yourBonusRupees) * 16;
  score -= static_cast<int>(obs.theirGoodsRupees) * 16;
  // Their bonus tokens are face down. The count is public and the stacks are
  // known, so the mean of everything still unclaimed is the fair estimate.
  score -= obs.theirBonusCount * ((meanBonus(0) + meanBonus(1) + meanBonus(2)) / 3) * 16;

  // Cards in hand are worth what they would fetch, discounted because turning
  // them into rupees costs turns and the piles may fall first.
  score += handValue(obs) * 11;

  // A run worth a bonus token. Three is the threshold that pays, and the jump
  // from four to five is the biggest in the game, so the reward is not linear.
  for (int g = 0; g < kGoodCount; ++g) {
    const int n = obs.hand[g];
    if (n >= 5) {
      score += meanBonus(2) * 14;
    } else if (n == 4) {
      score += meanBonus(1) * 12;
    } else if (n == 3) {
      score += meanBonus(0) * 10;
    } else if (n == 2 && !sellsInPairs(static_cast<Good>(g))) {
      // Two of a cheap good is a start on a run, worth a nudge and no more.
      score += 6;
    }
  }

  // The camel token, and the trading power a herd represents. Whoever is ahead
  // holds the 5 rupees; being one behind is worth chasing, being four behind is
  // not.
  const int lead = obs.herd[me] - obs.herd[them];
  if (lead > 0) {
    score += kCamelTokenValue * 16;
  } else if (lead == 0) {
    score += kCamelTokenValue * 6;
  } else if (lead >= -2) {
    score += kCamelTokenValue * 3;
  }
  // Camels are also how a big exchange gets paid for, which is worth something
  // even when the token is out of reach. Flattened, because a huge herd is
  // mostly dead weight.
  score += (obs.herd[me] < 5 ? obs.herd[me] : 5) * 4;

  // Room to manoeuvre. A full hand cannot take, which is how a turn gets wasted.
  score += (kHandLimit - handSizeOf(obs)) * 3;

  return score;
}

int legalMoves(const Observation& obs, Move* out, const int capacity) {
  struct Collect {
    Move* out;
    int capacity;
    int count;
  } state{out, capacity, 0};

  const MoveSink sink{&state, [](void* ctx, const Move& move) {
                        auto* self = static_cast<Collect*>(ctx);
                        if (self->count < self->capacity) self->out[self->count] = move;
                        ++self->count;  // counted even when dropped, so a caller can tell
                      }};
  forEachLegalMove(obs, sink);
  return state.count;
}

Move chooseMove(const Observation& obs, const Skill skill, uint32_t& rng) {
  struct Best {
    const Observation* obs;
    Skill skill;
    uint32_t* rng;
    Move move;
    int score;
    bool any;
  } state{&obs, skill, &rng, Move(), 0, false};

  const MoveSink sink{&state, [](void* ctx, const Move& move) {
                        auto* self = static_cast<Best*>(ctx);
                        const Observation next = after(*self->obs, move);
                        int score = evaluate(next);

                        if (self->skill == Skill::Maharaja) {
                          // Racing the round's end. Three empty piles stops it immediately, so
                          // emptying one is worth chasing when ahead and worth avoiding when
                          // behind: the player in front wants the round over.
                          const int ahead = static_cast<int>(next.yourGoodsRupees + next.yourBonusRupees) -
                                            static_cast<int>(next.theirGoodsRupees);
                          if (next.emptyPiles > self->obs->emptyPiles) score += ahead > 0 ? 40 : -40;

                          // Leaving the market rich is a gift. What is on the table after this move
                          // is what the opponent gets to choose from.
                          int gift = 0;
                          for (int i = 0; i < kMarketSlots; ++i) {
                            const uint8_t card = next.market[i];
                            if (card >= kGoodCount) continue;
                            gift += kGoodsTokens[card][next.goodsDepth[card] < kPileDepth[card] ? next.goodsDepth[card]
                                                                                                : kPileDepth[card] - 1];
                          }
                          score -= gift * 2;
                        }

                        // A deterministic opponent is a solved opponent by the third game. The jitter
                        // is small enough never to pick a clearly worse move.
                        uint32_t& r = *self->rng;
                        r ^= r << 13;
                        r ^= r >> 17;
                        r ^= r << 5;
                        score += static_cast<int>(r % 7u);

                        if (!self->any || score > self->score) {
                          self->any = true;
                          self->score = score;
                          self->move = move;
                        }
                      }};

  forEachLegalMove(obs, sink);

  // Every reachable position has a legal move: with a full hand you can still
  // sell, and with an empty one the market always holds something. This is the
  // belt to that braces.
  if (!state.any) return Move::takeCamels();
  return state.move;
}

}  // namespace jaipur

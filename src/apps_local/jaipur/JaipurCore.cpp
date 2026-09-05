#include "JaipurCore.h"

namespace jaipur {

namespace {

// One deterministic stream per seed. Both devices rebuild the identical deck
// and the identical bonus stacks from the four bytes that travel in the state,
// which is what keeps 52 cards and 18 tokens out of the wire format.
uint32_t nextRandom(uint32_t& state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

// A stream that cannot collide with the deck's, so shuffling the bonus stacks
// never perturbs the cards.
uint32_t streamFor(const uint32_t seed, const uint32_t stream) {
  uint32_t s = seed ^ (0x9E3779B9u * (stream + 1));
  return s ? s : 0x1234567u;  // xorshift dies on zero
}

void shuffle(uint8_t* cards, const int count, uint32_t& state) {
  for (int i = count - 1; i > 0; --i) {
    const int j = static_cast<int>(nextRandom(state) % static_cast<uint32_t>(i + 1));
    const uint8_t tmp = cards[i];
    cards[i] = cards[j];
    cards[j] = tmp;
  }
}

// The 52 shuffled cards: every good, plus the 8 camels that did not open the
// market. Rebuilt on demand rather than stored.
void buildDeck(const uint32_t seed, uint8_t out[kDeckCards]) {
  int n = 0;
  for (int g = 0; g < kGoodCount; ++g) {
    for (int i = 0; i < kGoodSupply[g]; ++i) out[n++] = static_cast<uint8_t>(g);
  }
  for (int i = 0; i < kCamelSupply - kOpeningCamels; ++i) out[n++] = kCamel;
  uint32_t state = streamFor(seed, 0);
  shuffle(out, kDeckCards, state);
}

void buildBonusStack(const uint32_t seed, const int stack, uint8_t out[7]) {
  const int depth = kBonusDepth[stack];
  for (int i = 0; i < depth; ++i) out[i] = kBonusTokens[stack][i];
  uint32_t state = streamFor(seed, static_cast<uint32_t>(stack) + 1);
  shuffle(out, depth, state);
}

int popcount16(uint16_t v) {
  int n = 0;
  while (v) {
    v &= static_cast<uint16_t>(v - 1);
    ++n;
  }
  return n;
}

}  // namespace

Move Move::takeOne(const int slot) {
  Move m;
  m.kind = Kind::TakeOne;
  m.slot = static_cast<uint8_t>(slot);
  return m;
}

Move Move::takeCamels() {
  Move m;
  m.kind = Kind::TakeCamels;
  return m;
}

Move Move::sell(const Good good, const int count) {
  Move m;
  m.kind = Kind::Sell;
  m.good = static_cast<uint8_t>(good);
  m.count = static_cast<uint8_t>(count);
  return m;
}

// --- set-up ---------------------------------------------------------------

void Game::deal(const uint32_t roundSeed, const uint8_t starter) {
  seed = roundSeed;
  // A fresh table has no last move, so nothing is narrated over the top of it.
  lastKind = kNoMove;
  lastCard = 0;
  lastCount = 0;
  lastValue = 0;
  uint8_t deck[kDeckCards];
  buildDeck(seed, deck);

  for (int s = 0; s < kSeats; ++s) {
    herd[s] = 0;
    for (int g = 0; g < kGoodCount; ++g) hand[s][g] = 0;
  }
  for (int g = 0; g < kGoodCount; ++g) {
    sold[g] = 0;
    goodsTakenBy0[g] = 0;
    goodsDepth[g] = 0;
  }
  for (int b = 0; b < kBonusStacks; ++b) {
    bonusTakenBy0[b] = 0;
    bonusDepth[b] = 0;
  }

  // Five each. A dealt camel goes straight to that player's herd, face up.
  int at = 0;
  for (int s = 0; s < kSeats; ++s) {
    for (int i = 0; i < 5; ++i) {
      const uint8_t card = deck[at++];
      if (card == kCamel) {
        ++herd[s];
      } else {
        ++hand[s][card];
      }
    }
  }

  // Three camels opened the market; the last two slots come off the deck and
  // may themselves be camels.
  market[0] = kCamel;
  market[1] = kCamel;
  market[2] = kCamel;
  market[3] = deck[at++];
  market[4] = deck[at++];
  deckTaken = static_cast<uint8_t>(at);

  turn = starter;
  roundStarter = starter;
  phase = static_cast<uint8_t>(Phase::Playing);
}

void Game::newGame(const uint32_t roundSeed, const uint8_t starter) {
  seals[0] = 0;
  seals[1] = 0;
  round = 1;
  deal(roundSeed, starter);
}

// --- derived --------------------------------------------------------------

uint8_t Game::cardAt(const int index) const {
  if (index < 0 || index >= kDeckCards) return kEmpty;
  uint8_t deck[kDeckCards];
  buildDeck(seed, deck);
  return deck[index];
}

uint8_t Game::bonusValueAt(const int stack, const int index) const {
  if (stack < 0 || stack >= kBonusStacks) return 0;
  if (index < 0 || index >= kBonusDepth[stack]) return 0;
  uint8_t tokens[7];
  buildBonusStack(seed, stack, tokens);
  return tokens[index];
}

int Game::handSize(const int seat) const {
  int n = 0;
  for (int g = 0; g < kGoodCount; ++g) n += hand[seat][g];
  return n;
}

int Game::marketCamels() const {
  int n = 0;
  for (int i = 0; i < kMarketSlots; ++i) {
    if (market[i] == kCamel) ++n;
  }
  return n;
}

int Game::emptyPiles() const {
  int n = 0;
  for (int g = 0; g < kGoodCount; ++g) {
    if (goodsDepth[g] >= kPileDepth[g]) ++n;
  }
  return n;
}

int Game::goodsRupees(const int seat, const Good good) const {
  const int g = static_cast<int>(good);
  int total = 0;
  for (int i = 0; i < goodsDepth[g]; ++i) {
    const bool mine = ((goodsTakenBy0[g] >> i) & 1) == (seat == 0 ? 1 : 0);
    if (mine) total += kGoodsTokens[g][i];
  }
  return total;
}

int Game::goodsRupees(const int seat) const {
  int total = 0;
  for (int g = 0; g < kGoodCount; ++g) total += goodsRupees(seat, static_cast<Good>(g));
  return total;
}

int Game::goodsTokenCount(const int seat) const {
  int n = 0;
  for (int g = 0; g < kGoodCount; ++g) {
    const uint16_t dug = static_cast<uint16_t>((1u << goodsDepth[g]) - 1u);
    const uint16_t byZero = static_cast<uint16_t>(goodsTakenBy0[g] & dug);
    n += seat == 0 ? popcount16(byZero) : popcount16(static_cast<uint16_t>(dug & ~byZero));
  }
  return n;
}

int Game::bonusTokenCount(const int seat) const {
  int n = 0;
  for (int b = 0; b < kBonusStacks; ++b) {
    const uint16_t dug = static_cast<uint16_t>((1u << bonusDepth[b]) - 1u);
    const uint16_t byZero = static_cast<uint16_t>(bonusTakenBy0[b] & dug);
    n += seat == 0 ? popcount16(byZero) : popcount16(static_cast<uint16_t>(dug & ~byZero));
  }
  return n;
}

int Game::bonusRupees(const int seat) const {
  int total = 0;
  for (int b = 0; b < kBonusStacks; ++b) {
    if (bonusDepth[b] == 0) continue;
    uint8_t tokens[7];
    buildBonusStack(seed, b, tokens);
    for (int i = 0; i < bonusDepth[b]; ++i) {
      const bool mine = ((bonusTakenBy0[b] >> i) & 1) == (seat == 0 ? 1 : 0);
      if (mine) total += tokens[i];
    }
  }
  return total;
}

int Game::camelTokenSeat() const {
  if (herd[0] > herd[1]) return 0;
  if (herd[1] > herd[0]) return 1;
  return -1;  // equal camels: nobody takes it
}

int Game::score(const int seat) const {
  int total = goodsRupees(seat) + bonusRupees(seat);
  if (camelTokenSeat() == seat) total += kCamelTokenValue;
  return total;
}

int Game::visibleScore(const int viewer, const int seat) const {
  int total = goodsRupees(seat);
  if (camelTokenSeat() == seat) total += kCamelTokenValue;
  if (viewer == seat) total += bonusRupees(seat);
  return total;
}

int Game::roundWinner() const {
  const int a = score(0);
  const int b = score(1);
  if (a != b) return a > b ? 0 : 1;
  const int ba = bonusTokenCount(0);
  const int bb = bonusTokenCount(1);
  if (ba != bb) return ba > bb ? 0 : 1;
  const int ga = goodsTokenCount(0);
  const int gb = goodsTokenCount(1);
  if (ga != gb) return ga > gb ? 0 : 1;
  // The rulebook stops here. Camels are the last public quantity left; if those
  // tie too the round is a genuine draw and gets replayed.
  if (herd[0] != herd[1]) return herd[0] > herd[1] ? 0 : 1;
  return -1;
}

int Game::matchWinner() const {
  for (int s = 0; s < kSeats; ++s) {
    if (seals[s] >= kSealsToWin) return s;
  }
  return -1;
}

int Game::nextTokenValue(const Good good, const int fromDepth) const {
  const int g = static_cast<int>(good);
  if (fromDepth < 0 || fromDepth >= kPileDepth[g]) return 0;
  return kGoodsTokens[g][fromDepth];
}

int Game::saleValue(const Good good, const int count) const {
  const int g = static_cast<int>(good);
  int total = 0;
  for (int i = 0; i < count; ++i) {
    const int depth = goodsDepth[g] + i;
    if (depth >= kPileDepth[g]) break;  // the pile ran dry; the rest is lost
    total += kGoodsTokens[g][depth];
  }
  return total;
}

// --- legality -------------------------------------------------------------

bool Game::isLegal(const Move& move) const {
  if (currentPhase() != Phase::Playing) return false;
  const int seat = turn;

  switch (move.kind) {
    case Move::Kind::TakeOne: {
      if (move.slot >= kMarketSlots) return false;
      const uint8_t card = market[move.slot];
      if (card == kCamel || card == kEmpty) return false;
      return handSize(seat) < kHandLimit;
    }

    case Move::Kind::TakeCamels:
      return marketCamels() > 0;

    case Move::Kind::Exchange: {
      int taken[kGoodCount] = {};
      int takenCount = 0;
      for (int i = 0; i < kMarketSlots; ++i) {
        if (((move.marketMask >> i) & 1) == 0) continue;
        const uint8_t card = market[i];
        // Camels can be given in an exchange but never taken in one, and an
        // empty slot holds nothing.
        if (card == kCamel || card == kEmpty) return false;
        ++taken[card];
        ++takenCount;
      }
      // "An exchange always involves at least 2 cards for 2 cards."
      if (takenCount < 2) return false;

      int givenCount = move.giveCamels;
      if (move.giveCamels > herd[seat]) return false;
      for (int g = 0; g < kGoodCount; ++g) {
        if (move.give[g] > hand[seat][g]) return false;
        // "The same goods type cannot be both surrendered and taken."
        if (move.give[g] > 0 && taken[g] > 0) return false;
        givenCount += move.give[g];
      }
      if (givenCount != takenCount) return false;

      // Giving camels grows the hand, so the limit has to be checked after.
      int after = handSize(seat) + takenCount;
      for (int g = 0; g < kGoodCount; ++g) after -= move.give[g];
      return after <= kHandLimit;
    }

    case Move::Kind::Sell: {
      if (move.good >= kGoodCount) return false;
      const Good good = static_cast<Good>(move.good);
      if (move.count < 1) return false;
      if (move.count > hand[seat][move.good]) return false;
      // Diamonds, gold and silver never sell alone, even with one token left.
      if (sellsInPairs(good) && move.count < 2) return false;
      return true;
    }
  }
  return false;
}

// --- applying -------------------------------------------------------------

bool Game::apply(const Move& move) {
  if (!isLegal(move)) return false;
  const int seat = turn;
  bool refillFailed = false;

  // What happened, recorded before the board changes under it: a take refills
  // the slot it emptied, and a sale is worth what the pile paid at the time.
  // The other device has no other way to learn any of it.
  const int bonusBefore = bonusTokenCount(seat);
  lastKind = static_cast<uint8_t>(static_cast<uint8_t>(move.kind) | (seat << 7));
  lastCard = 0;
  lastCount = 0;
  lastValue = 0;
  switch (move.kind) {
    case Move::Kind::TakeOne:
      lastCard = market[move.slot];
      lastCount = 1;
      break;
    case Move::Kind::TakeCamels:
      lastCount = static_cast<uint8_t>(marketCamels());
      break;
    case Move::Kind::Exchange:
      for (int i = 0; i < kMarketSlots; ++i) {
        if ((move.marketMask >> i) & 1) ++lastCount;
      }
      break;
    case Move::Kind::Sell:
      lastCard = move.good;
      lastCount = move.count;
      lastValue = static_cast<uint8_t>(saleValue(static_cast<Good>(move.good), move.count));
      break;
  }

  switch (move.kind) {
    case Move::Kind::TakeOne: {
      ++hand[seat][market[move.slot]];
      if (deckTaken < kDeckCards) {
        market[move.slot] = cardAt(deckTaken++);
      } else {
        market[move.slot] = kEmpty;
        refillFailed = true;
      }
      break;
    }

    case Move::Kind::TakeCamels: {
      // All of them, always, each replaced one at a time. The first draw that
      // finds an empty deck ends the round with the camels already taken.
      for (int i = 0; i < kMarketSlots; ++i) {
        if (market[i] != kCamel) continue;
        ++herd[seat];
        if (deckTaken < kDeckCards) {
          market[i] = cardAt(deckTaken++);
        } else {
          market[i] = kEmpty;
          refillFailed = true;
        }
      }
      break;
    }

    case Move::Kind::Exchange: {
      // Take first, so the vacated slots are known, then refill them from what
      // was surrendered. No card is drawn: the market is refilled by the trade.
      int slots[kMarketSlots];
      int slotCount = 0;
      for (int i = 0; i < kMarketSlots; ++i) {
        if (((move.marketMask >> i) & 1) == 0) continue;
        ++hand[seat][market[i]];
        slots[slotCount++] = i;
      }
      int at = 0;
      for (int g = 0; g < kGoodCount; ++g) {
        for (int i = 0; i < move.give[g]; ++i) market[slots[at++]] = static_cast<uint8_t>(g);
        hand[seat][g] -= move.give[g];
      }
      for (int i = 0; i < move.giveCamels; ++i) market[slots[at++]] = kCamel;
      herd[seat] -= move.giveCamels;
      break;
    }

    case Move::Kind::Sell: {
      const int g = move.good;
      hand[seat][g] -= move.count;
      sold[g] += move.count;

      // Tokens come off the top, highest first. A pile short of the sale pays
      // what it has and the rest is lost.
      for (int i = 0; i < move.count; ++i) {
        if (goodsDepth[g] >= kPileDepth[g]) break;
        if (seat == 0) goodsTakenBy0[g] |= static_cast<uint16_t>(1u << goodsDepth[g]);
        ++goodsDepth[g];
      }

      // Three or more earns a bonus, and it is earned even when the pile came
      // up short. Five or more all draw from the same stack.
      if (move.count >= 3) {
        const int stack = (move.count >= 5 ? 5 : move.count) - 3;
        if (bonusDepth[stack] < kBonusDepth[stack]) {
          if (seat == 0) bonusTakenBy0[stack] |= static_cast<uint8_t>(1u << bonusDepth[stack]);
          ++bonusDepth[stack];
        }
      }
      break;
    }
  }

  if (bonusTokenCount(seat) > bonusBefore) lastValue |= 0x80;

  // The move has resolved in full; only now does the round get to end. That
  // order is what makes a sale which empties the third pile still collect its
  // bonus token.
  if (refillFailed || emptyPiles() >= 3) {
    // The seal is won here, at the moment the round ends, because that is when
    // the rulebook awards it. It used to be handed out by startNextRound(),
    // which is a tap later: the scoring screen was drawn from a state where
    // nobody had won anything yet and showed the seals of the round before.
    const int winner = roundWinner();
    if (winner >= 0) seals[winner] += 1;
    // A match is over the instant somebody holds two, so the same screen that
    // reports the round reports the game rather than making you ask for it.
    phase = static_cast<uint8_t>(matchWinner() >= 0 ? Phase::GameOver : Phase::RoundOver);
    return true;
  }

  turn = static_cast<uint8_t>(1 - seat);
  return true;
}

void Game::startNextRound(const uint32_t roundSeed) {
  // Only ever deals. The seal and the end of the match were both settled the
  // moment the round ended, so Phase::RoundOver already means "one round scored,
  // nobody has two seals yet".
  if (currentPhase() != Phase::RoundOver) return;

  const int winner = roundWinner();

  // The loser starts. A genuine draw took no seal, so it keeps the same starter
  // and simply replays.
  const uint8_t starter = winner >= 0 ? static_cast<uint8_t>(1 - winner) : roundStarter;
  ++round;
  deal(roundSeed, starter);
}

// --- what a player is allowed to know -------------------------------------

Observation observe(const Game& game, const int seat) {
  Observation obs;
  obs.seat = static_cast<uint8_t>(seat);
  for (int i = 0; i < kMarketSlots; ++i) obs.market[i] = game.market[i];
  for (int g = 0; g < kGoodCount; ++g) {
    obs.hand[g] = game.hand[seat][g];
    obs.goodsDepth[g] = game.goodsDepth[g];
  }
  for (int b = 0; b < kBonusStacks; ++b) obs.bonusDepth[b] = game.bonusDepth[b];
  obs.herd[0] = game.herd[0];
  obs.herd[1] = game.herd[1];
  obs.opponentHandSize = static_cast<uint8_t>(game.handSize(1 - seat));
  obs.deckRemaining = static_cast<uint8_t>(game.deckRemaining());

  // Everything not yet seen: the deck plus their hand. Derived from public
  // facts only, which is what lets this struct exist without the opponent's
  // hand in it.
  for (int g = 0; g < kGoodCount; ++g) {
    int seen = obs.hand[g] + game.sold[g];
    for (int i = 0; i < kMarketSlots; ++i) {
      if (game.market[i] == g) ++seen;
    }
    const int left = kGoodSupply[g] - seen;
    obs.unseen[g] = static_cast<uint8_t>(left > 0 ? left : 0);
  }
  int camelsSeen = game.herd[0] + game.herd[1] + game.marketCamels();
  const int camelsLeft = kCamelSupply - camelsSeen;
  obs.unseenCamels = static_cast<uint8_t>(camelsLeft > 0 ? camelsLeft : 0);

  const int them = 1 - seat;
  obs.yourGoodsRupees = static_cast<uint16_t>(game.goodsRupees(seat));
  obs.yourBonusRupees = static_cast<uint16_t>(game.bonusRupees(seat));
  obs.theirGoodsRupees = static_cast<uint16_t>(game.goodsRupees(them));
  obs.yourBonusCount = static_cast<uint8_t>(game.bonusTokenCount(seat));
  obs.theirBonusCount = static_cast<uint8_t>(game.bonusTokenCount(them));
  obs.seals[0] = game.seals[0];
  obs.seals[1] = game.seals[1];
  obs.emptyPiles = static_cast<uint8_t>(game.emptyPiles());
  return obs;
}

Observation after(const Observation& obs, const Move& move) {
  Observation out = obs;
  switch (move.kind) {
    case Move::Kind::TakeOne: {
      const uint8_t card = obs.market[move.slot];
      if (card < kGoodCount) ++out.hand[card];
      // The deck is face down. What lands here is genuinely unknown, and saying
      // so is what keeps this function honest.
      out.market[move.slot] = kUnknown;
      if (out.deckRemaining > 0) --out.deckRemaining;
      break;
    }

    case Move::Kind::TakeCamels: {
      for (int i = 0; i < kMarketSlots; ++i) {
        if (obs.market[i] != kCamel) continue;
        ++out.herd[obs.seat];
        out.market[i] = kUnknown;
        if (out.deckRemaining > 0) --out.deckRemaining;
      }
      break;
    }

    case Move::Kind::Exchange: {
      int slots[kMarketSlots];
      int slotCount = 0;
      for (int i = 0; i < kMarketSlots; ++i) {
        if (((move.marketMask >> i) & 1) == 0) continue;
        const uint8_t card = obs.market[i];
        if (card < kGoodCount) ++out.hand[card];
        slots[slotCount++] = i;
      }
      int at = 0;
      for (int g = 0; g < kGoodCount; ++g) {
        for (int i = 0; i < move.give[g]; ++i) out.market[slots[at++]] = static_cast<uint8_t>(g);
        out.hand[g] = static_cast<uint8_t>(out.hand[g] - move.give[g]);
      }
      for (int i = 0; i < move.giveCamels; ++i) out.market[slots[at++]] = kCamel;
      out.herd[obs.seat] = static_cast<uint8_t>(out.herd[obs.seat] - move.giveCamels);
      break;
    }

    case Move::Kind::Sell: {
      const int g = move.good;
      out.hand[g] = static_cast<uint8_t>(out.hand[g] - move.count);
      int paid = 0;
      for (int i = 0; i < move.count; ++i) {
        if (out.goodsDepth[g] >= kPileDepth[g]) break;
        paid += kGoodsTokens[g][out.goodsDepth[g]];
        ++out.goodsDepth[g];
      }
      out.yourGoodsRupees = static_cast<uint16_t>(out.yourGoodsRupees + paid);
      if (move.count >= 3) {
        const int stack = (move.count >= 5 ? 5 : move.count) - 3;
        if (out.bonusDepth[stack] < kBonusDepth[stack]) {
          ++out.bonusDepth[stack];
          ++out.yourBonusCount;
          // The value is face down until the round is scored, so the mean of
          // what is left in that stack is the honest estimate.
          int total = 0;
          int n = 0;
          for (int i = 0; i < kBonusDepth[stack]; ++i) {
            total += kBonusTokens[stack][i];
            ++n;
          }
          out.yourBonusRupees = static_cast<uint16_t>(out.yourBonusRupees + (n > 0 ? total / n : 0));
        }
      }
      break;
    }
  }

  out.emptyPiles = 0;
  for (int g = 0; g < kGoodCount; ++g) {
    if (out.goodsDepth[g] >= kPileDepth[g]) ++out.emptyPiles;
  }
  return out;
}

}  // namespace jaipur

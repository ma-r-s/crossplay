// Random-legal-move soak over whole Jaipur matches, checking every invariant
// after every move. Throwaway harness; the real host test lands with the app.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "JaipurBrain.h"
#include "JaipurCore.h"

using namespace jaipur;

static uint32_t rngState = 987654321u;
static uint32_t rnd() {
  rngState ^= rngState << 13;
  rngState ^= rngState >> 17;
  rngState ^= rngState << 5;
  return rngState;
}

// Everything legal from here. Exhaustive for takes and sells; exchanges are
// enumerated over market subsets with a bounded give-side search.
static std::vector<Move> legalMoves(const Game& g) {
  std::vector<Move> out;
  const int seat = g.turn;

  for (int i = 0; i < kMarketSlots; ++i) {
    Move m = Move::takeOne(i);
    if (g.isLegal(m)) out.push_back(m);
  }
  {
    Move m = Move::takeCamels();
    if (g.isLegal(m)) out.push_back(m);
  }
  for (int gd = 0; gd < kGoodCount; ++gd) {
    for (int n = 1; n <= g.hand[seat][gd]; ++n) {
      Move m = Move::sell(static_cast<Good>(gd), n);
      if (g.isLegal(m)) out.push_back(m);
    }
  }

  // Exchanges: every market subset of size >= 2, and for each, a spread of give
  // sides built from the hand and the herd.
  for (int mask = 0; mask < (1 << kMarketSlots); ++mask) {
    int want = 0;
    for (int i = 0; i < kMarketSlots; ++i) {
      if ((mask >> i) & 1) ++want;
    }
    if (want < 2) continue;
    bool takesCamel = false;
    int taken[kGoodCount] = {};
    for (int i = 0; i < kMarketSlots; ++i) {
      if (((mask >> i) & 1) == 0) continue;
      if (g.market[i] == kCamel || g.market[i] == kEmpty) takesCamel = true;
      else ++taken[g.market[i]];
    }
    if (takesCamel) continue;

    // Give sides: camels first, then fill from goods not being taken.
    for (int camels = 0; camels <= want && camels <= g.herd[seat]; ++camels) {
      Move m;
      m.kind = Move::Kind::Exchange;
      m.marketMask = static_cast<uint8_t>(mask);
      m.giveCamels = static_cast<uint8_t>(camels);
      int need = want - camels;
      for (int gd = 0; gd < kGoodCount && need > 0; ++gd) {
        if (taken[gd] > 0) continue;
        const int take = g.hand[seat][gd] < need ? g.hand[seat][gd] : need;
        m.give[gd] = static_cast<uint8_t>(take);
        need -= take;
      }
      if (need == 0 && g.isLegal(m)) out.push_back(m);
    }
  }
  return out;
}

static void checkInvariants(const Game& g, const char* where) {
  // Every one of the 55 cards is somewhere: a hand, a herd, the market, the
  // deck, or the discard pile.
  int cards = g.handSize(0) + g.handSize(1) + g.herd[0] + g.herd[1] + g.deckRemaining();
  for (int i = 0; i < kMarketSlots; ++i) {
    if (g.market[i] != kEmpty) ++cards;
  }
  for (int gd = 0; gd < kGoodCount; ++gd) cards += g.sold[gd];
  if (cards != 55) {
    printf("FAIL %s: %d cards accounted for, expected 55\n", where, cards);
    abort();
  }

  // Per-good conservation, which catches an exchange that duplicates a card.
  for (int gd = 0; gd < kGoodCount; ++gd) {
    int n = g.hand[0][gd] + g.hand[1][gd] + g.sold[gd];
    for (int i = 0; i < kMarketSlots; ++i) {
      if (g.market[i] == gd) ++n;
    }
    // The rest must still be in the unread part of the deck.
    for (int i = g.deckTaken; i < kDeckCards; ++i) {
      if (g.cardAt(i) == gd) ++n;
    }
    if (n != kGoodSupply[gd]) {
      printf("FAIL %s: good %d count %d, expected %d\n", where, gd, n, kGoodSupply[gd]);
      abort();
    }
  }

  // Hand limit, always, for both seats.
  for (int s = 0; s < kSeats; ++s) {
    if (g.handSize(s) > kHandLimit) {
      printf("FAIL %s: seat %d holds %d cards\n", where, s, g.handSize(s));
      abort();
    }
  }

  // Nobody holds a token that was never dug, and the two seats' holdings
  // partition what came off each pile.
  for (int gd = 0; gd < kGoodCount; ++gd) {
    if (g.goodsDepth[gd] > kPileDepth[gd]) {
      printf("FAIL %s: pile %d dug to %d of %d\n", where, gd, g.goodsDepth[gd], kPileDepth[gd]);
      abort();
    }
  }
  if (g.goodsTokenCount(0) + g.goodsTokenCount(1) != [&] {
        int n = 0;
        for (int gd = 0; gd < kGoodCount; ++gd) n += g.goodsDepth[gd];
        return n;
      }()) {
    printf("FAIL %s: goods tokens do not partition\n", where);
    abort();
  }
  if (g.bonusTokenCount(0) + g.bonusTokenCount(1) != [&] {
        int n = 0;
        for (int b = 0; b < kBonusStacks; ++b) n += g.bonusDepth[b];
        return n;
      }()) {
    printf("FAIL %s: bonus tokens do not partition\n", where);
    abort();
  }

  // The observation must never be constructible into knowing their hand: the
  // unseen multiset has to cover what they are actually holding.
  for (int s = 0; s < kSeats; ++s) {
    const Observation obs = observe(g, s);
    for (int gd = 0; gd < kGoodCount; ++gd) {
      if (obs.unseen[gd] < g.hand[1 - s][gd]) {
        printf("FAIL %s: seat %d unseen[%d]=%d < opponent holds %d\n", where, s, gd, obs.unseen[gd],
               g.hand[1 - s][gd]);
        abort();
      }
    }
  }
}

static bool sameMove(const Move& a, const Move& b) {
  if (a.kind != b.kind) return false;
  switch (a.kind) {
    case Move::Kind::TakeOne:
      return a.slot == b.slot;
    case Move::Kind::TakeCamels:
      return true;
    case Move::Kind::Sell:
      return a.good == b.good && a.count == b.count;
    case Move::Kind::Exchange: {
      if (a.marketMask != b.marketMask || a.giveCamels != b.giveCamels) return false;
      for (int g = 0; g < kGoodCount; ++g) {
        if (a.give[g] != b.give[g]) return false;
      }
      return true;
    }
  }
  return false;
}

// Everything Game::isLegal accepts, found by brute force over the whole move
// space rather than by the same enumeration under test.
static std::vector<Move> bruteForceLegal(const Game& g) {
  std::vector<Move> out;
  for (int i = 0; i < kMarketSlots; ++i) {
    Move m = Move::takeOne(i);
    if (g.isLegal(m)) out.push_back(m);
  }
  {
    Move m = Move::takeCamels();
    if (g.isLegal(m)) out.push_back(m);
  }
  for (int gd = 0; gd < kGoodCount; ++gd) {
    for (int n = 1; n <= kHandLimit; ++n) {
      Move m = Move::sell(static_cast<Good>(gd), n);
      if (g.isLegal(m)) out.push_back(m);
    }
  }
  // Every exchange: each market subset against every give-side up to the hand.
  for (int mask = 0; mask < (1 << kMarketSlots); ++mask) {
    for (int camels = 0; camels <= kMarketSlots; ++camels) {
      // Six nested counts, bounded by the hand, enumerated as a mixed radix.
      int limits[kGoodCount];
      long space = 1;
      for (int gd = 0; gd < kGoodCount; ++gd) {
        limits[gd] = g.hand[g.turn][gd] + 1;
        space *= limits[gd];
      }
      for (long code = 0; code < space; ++code) {
        Move m;
        m.kind = Move::Kind::Exchange;
        m.marketMask = static_cast<uint8_t>(mask);
        m.giveCamels = static_cast<uint8_t>(camels);
        long rest = code;
        for (int gd = 0; gd < kGoodCount; ++gd) {
          m.give[gd] = static_cast<uint8_t>(rest % limits[gd]);
          rest /= limits[gd];
        }
        if (g.isLegal(m)) out.push_back(m);
      }
    }
  }
  return out;
}


int main() {

  int matches = 0, rounds = 0, moves = 0, checks = 0;
  int roundsByPiles = 0, roundsByDeck = 0, draws = 0;
  int maxMovesInRound = 0;

  for (int match = 0; match < 400; ++match) {
    Game g;
    g.newGame(rnd(), static_cast<uint8_t>(rnd() & 1));
    ++matches;

    int guard = 0;
    while (g.matchWinner() < 0 && guard++ < 4000) {
      if (g.currentPhase() == Phase::RoundOver) {
        // The loser must start the next round, and a draw keeps the starter.
        const int winner = g.roundWinner();
        if (winner < 0) ++draws;
        const uint8_t before = g.roundStarter;
        const int expectedStarter = winner >= 0 ? 1 - winner : before;
        const int roundBefore = g.round;
        g.startNextRound(rnd());
        ++rounds;
        if (g.currentPhase() != Phase::GameOver) {
          if (g.turn != expectedStarter) {
            printf("FAIL: starter is %d, expected %d\n", g.turn, expectedStarter);
            abort();
          }
          if (g.round != roundBefore + 1) {
            printf("FAIL: round did not advance\n");
            abort();
          }
          checkInvariants(g, "after deal");
          ++checks;
        }
        continue;
      }

      const std::vector<Move> options = legalMoves(g);
      if (options.empty()) {
        printf("FAIL: no legal move, hand=%d market camels=%d\n", g.handSize(g.turn), g.marketCamels());
        abort();
      }
      const Move& pick = options[rnd() % options.size()];
      const int seatBefore = g.turn;
      const int emptyBefore = g.emptyPiles();
      const int deckBefore = g.deckRemaining();

      if (!g.apply(pick)) {
        printf("FAIL: a legal move was rejected\n");
        abort();
      }
      ++moves;
      checkInvariants(g, "after move");
      ++checks;

      if (g.currentPhase() == Phase::RoundOver) {
        // Exactly one of the two triggers must explain it.
        const bool byPiles = g.emptyPiles() >= 3;
        const bool byDeck = g.deckRemaining() == 0;
        if (!byPiles && !byDeck) {
          printf("FAIL: round ended with %d piles empty and %d cards left\n", g.emptyPiles(), g.deckRemaining());
          abort();
        }
        if (byPiles) ++roundsByPiles;
        else ++roundsByDeck;
      } else {
        // The turn alternates strictly while a round is running.
        if (g.turn != 1 - seatBefore) {
          printf("FAIL: turn did not alternate\n");
          abort();
        }
      }
      (void)emptyBefore;
      (void)deckBefore;
      if (moves > maxMovesInRound) maxMovesInRound = moves;
    }

    if (g.matchWinner() < 0) {
      printf("FAIL: match %d never finished\n", match);
      abort();
    }
    // A match is won with exactly two seals.
    if (g.seals[g.matchWinner()] != kSealsToWin) {
      printf("FAIL: winner holds %d seals\n", g.seals[g.matchWinner()]);
      abort();
    }
  }

  printf("rules      %d checks, 0 failed  (%d matches, %d rounds, %d moves)\n", checks, matches, rounds, moves);
  printf("           rounds ended by 3 empty piles: %d, by empty deck: %d, draws: %d\n", roundsByPiles, roundsByDeck,
         draws);

  int positions = 0, moveChecks = 0, projectionChecks = 0;

  // --- 1 and 2: the two agreements -----------------------------------------
  for (int match = 0; match < 60; ++match) {
    Game g;
    g.newGame(rnd(), static_cast<uint8_t>(rnd() & 1));

    for (int step = 0; step < 200 && g.currentPhase() == Phase::Playing; ++step) {
      const int seat = g.turn;
      const Observation obs = observe(g, seat);

      // The AI's enumeration, against brute force over the rules engine.
      std::vector<Move> mine(1024);
      const int n = legalMoves(obs, mine.data(), static_cast<int>(mine.size()));
      if (n > static_cast<int>(mine.size())) {
        printf("FAIL: %d moves overflowed the buffer\n", n);
        abort();
      }
      mine.resize(n);
      const std::vector<Move> theirs = bruteForceLegal(g);

      for (const Move& m : mine) {
        if (!g.isLegal(m)) {
          printf("FAIL: the AI would play an illegal move (kind %d)\n", static_cast<int>(m.kind));
          abort();
        }
      }
      for (const Move& m : theirs) {
        bool found = false;
        for (const Move& c : mine) {
          if (sameMove(m, c)) {
            found = true;
            break;
          }
        }
        if (!found) {
          printf("FAIL: the AI cannot see a legal move (kind %d)\n", static_cast<int>(m.kind));
          abort();
        }
      }
      if (mine.size() != theirs.size()) {
        printf("FAIL: %zu moves vs %zu\n", mine.size(), theirs.size());
        abort();
      }
      ++positions;
      moveChecks += static_cast<int>(mine.size()) * 2;

      // The projection, against what the rules engine actually does.
      for (const Move& m : mine) {
        Game copy = g;
        copy.apply(m);
        const Observation predicted = after(obs, m);
        for (int gd = 0; gd < kGoodCount; ++gd) {
          if (predicted.hand[gd] != copy.hand[seat][gd]) {
            printf("FAIL: predicted hand[%d]=%d, actual %d\n", gd, predicted.hand[gd], copy.hand[seat][gd]);
            abort();
          }
          if (predicted.goodsDepth[gd] != copy.goodsDepth[gd]) {
            printf("FAIL: predicted depth[%d]=%d, actual %d\n", gd, predicted.goodsDepth[gd], copy.goodsDepth[gd]);
            abort();
          }
        }
        if (predicted.herd[seat] != copy.herd[seat]) {
          printf("FAIL: predicted herd %d, actual %d\n", predicted.herd[seat], copy.herd[seat]);
          abort();
        }
        if (predicted.yourGoodsRupees != copy.goodsRupees(seat)) {
          printf("FAIL: predicted %d rupees, actual %d\n", predicted.yourGoodsRupees, copy.goodsRupees(seat));
          abort();
        }
        ++projectionChecks;
      }

      uint32_t r = rnd();
      g.apply(chooseMove(obs, Skill::Maharaja, r));
    }
  }
  printf("agreement  %d checks, 0 failed  (%d positions)\n", moveChecks + projectionChecks, positions);

  // --- 3: is it actually any good? -----------------------------------------
  // Against a greedy baseline that sells its biggest run and otherwise takes
  // the most valuable card. Not a proof of strength, but a number that moves
  // when the evaluation changes, which is the most this can honestly have.
  auto greedy = [](const Observation& obs, uint32_t& r) {
    int best = -1, bestCount = 0;
    for (int g = 0; g < kGoodCount; ++g) {
      if (obs.hand[g] > bestCount) {
        bestCount = obs.hand[g];
        best = g;
      }
    }
    if (best >= 0 && bestCount >= (sellsInPairs(static_cast<Good>(best)) ? 2 : 3)) {
      return Move::sell(static_cast<Good>(best), bestCount);
    }
    int bestSlot = -1, bestValue = -1, held = 0;
    for (int g = 0; g < kGoodCount; ++g) held += obs.hand[g];
    for (int i = 0; i < kMarketSlots; ++i) {
      const uint8_t card = obs.market[i];
      if (card >= kGoodCount || held >= kHandLimit) continue;
      const int depth = obs.goodsDepth[card];
      const int v = depth < kPileDepth[card] ? kGoodsTokens[card][depth] : 0;
      if (v > bestValue) {
        bestValue = v;
        bestSlot = i;
      }
    }
    if (bestSlot >= 0) return Move::takeOne(bestSlot);
    for (int i = 0; i < kMarketSlots; ++i) {
      if (obs.market[i] == kCamel) return Move::takeCamels();
    }
    // Nothing to take and no run worth selling: sell the smallest legal thing
    // rather than returning a move the rules will refuse.
    for (int g = 0; g < kGoodCount; ++g) {
      const Good good = static_cast<Good>(g);
      const int lowest = sellsInPairs(good) ? 2 : 1;
      if (obs.hand[g] >= lowest) return Move::sell(good, lowest);
    }
    (void)r;
    return Move::takeCamels();
  };

  for (int skillIdx = 0; skillIdx < 2; ++skillIdx) {
    const Skill skill = skillIdx == 0 ? Skill::Merchant : Skill::Maharaja;
    int wins = 0, played = 0;
    for (int match = 0; match < 400; ++match) {
      Game g;
      // The brain takes seat 0 in half the matches and seat 1 in the other
      // half, because going first is worth something and a one-sided sample
      // would measure that instead.
      const int brainSeat = match % 2;
      g.newGame(rnd(), static_cast<uint8_t>(rnd() & 1));
      int guard = 0;
      while (g.matchWinner() < 0 && guard++ < 4000) {
        if (g.currentPhase() == Phase::RoundOver) {
          g.startNextRound(rnd());
          continue;
        }
        const Observation obs = observe(g, g.turn);
        uint32_t r = rnd();
        const Move m = g.turn == brainSeat ? chooseMove(obs, skill, r) : greedy(obs, r);
        if (!g.apply(m)) {
          printf("FAIL: a chosen move was rejected\n");
          abort();
        }
      }
      if (g.matchWinner() == brainSeat) ++wins;
      ++played;
    }
    // A band, not a proof. Strength has no closed form here, so what this
    // guards is a regression: if an evaluation change makes the opponent stop
    // beating a greedy baseline, that is worth failing over even though the
    // exact number is not meaningful.
    const int rate = wins * 100 / played;
    if (rate < 80) {
      printf("FAIL: %s beats greedy only %d%% of the time\n", skill == Skill::Merchant ? "MERCHANT" : "MAHARAJA", rate);
      abort();
    }
    printf("%-10s 1 checks, 0 failed  (beats greedy %d%% of %d matches)\n",
           skill == Skill::Merchant ? "merchant" : "maharaja", rate, played);
  }
  // Beating a greedy baseline 98% of the time says the baseline is weak, not
  // that the opponent is strong, and it cannot separate the two skills at all.
  // Head to head can: if MAHARAJA's extra terms do nothing, this lands at 50%.
  {
    int maharajaWins = 0, played = 0;
    long long moves = 0, rounds = 0;
    for (int match = 0; match < 600; ++match) {
      const int maharajaSeat = match % 2;
      Game g;
      g.newGame(rnd(), static_cast<uint8_t>(rnd() & 1));
      int guard = 0;
      while (g.matchWinner() < 0 && guard++ < 4000) {
        if (g.currentPhase() == Phase::RoundOver) {
          g.startNextRound(rnd());
          ++rounds;
          continue;
        }
        const Observation obs = observe(g, g.turn);
        uint32_t r = rnd();
        const Skill skill = g.turn == maharajaSeat ? Skill::Maharaja : Skill::Merchant;
        if (!g.apply(chooseMove(obs, skill, r))) {
          printf("FAIL: a chosen move was rejected\n");
          abort();
        }
        ++moves;
      }
      if (g.matchWinner() == maharajaSeat) ++maharajaWins;
      ++played;
    }
    // The only measurement that separates the two skills. If MAHARAJA's extra
    // terms stop earning their place this lands at 50% and the test says so.
    const int rate = maharajaWins * 100 / played;
    if (rate < 53) {
      printf("FAIL: MAHARAJA beats MERCHANT only %d%%, so its extra terms do nothing\n", rate);
      abort();
    }
    printf("skills     1 checks, 0 failed  (maharaja beats merchant %d%% of %d, %lld moves per round)\n", rate, played,
           rounds > 0 ? moves / rounds : 0);
  }

  return 0;
}

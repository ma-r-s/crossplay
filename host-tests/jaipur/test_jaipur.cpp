// Random-legal-move soak over whole Jaipur matches, checking every invariant
// after every move. Throwaway harness; the real host test lands with the app.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "JaipurBrain.h"
#include "JaipurCore.h"
#include "JaipurLink.h"

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
      if (g.market[i] == kCamel || g.market[i] == kEmpty)
        takesCamel = true;
      else
        ++taken[g.market[i]];
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

  // The running strip prints one quantity twice, once per seat, and both
  // columns are read from the same chair. The only thing a viewer may not know
  // is the other seat's bonus tokens, whose values are printed on the back.
  // Everything else is face up and counts for either seat, the camel token
  // included, because both herds are face up on the table.
  for (int v = 0; v < kSeats; ++v) {
    for (int s = 0; s < kSeats; ++s) {
      const int hidden = v == s ? 0 : g.bonusRupees(s);
      if (g.visibleScore(v, s) != g.score(s) - hidden) {
        printf("FAIL %s: seat %d reads %d for seat %d, expected %d\n", where, v, g.visibleScore(v, s), s,
               g.score(s) - hidden);
        abort();
      }
    }
  }

  // The observation must never be constructible into knowing their hand: the
  // unseen multiset has to cover what they are actually holding.
  for (int s = 0; s < kSeats; ++s) {
    const Observation obs = observe(g, s);
    for (int gd = 0; gd < kGoodCount; ++gd) {
      if (obs.unseen[gd] < g.hand[1 - s][gd]) {
        printf("FAIL %s: seat %d unseen[%d]=%d < opponent holds %d\n", where, s, gd, obs.unseen[gd], g.hand[1 - s][gd]);
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
        const uint8_t sealsBefore[2] = {g.seals[0], g.seals[1]};
        g.startNextRound(rnd());
        // It deals, and only deals. The seal was banked by the move that ended
        // the round, and a match that was already decided never reaches here.
        if (g.seals[0] != sealsBefore[0] || g.seals[1] != sealsBefore[1]) {
          printf("FAIL: startNextRound moved a seal\n");
          abort();
        }
        if (g.currentPhase() != Phase::Playing) {
          printf("FAIL: startNextRound left phase %d\n", static_cast<int>(g.currentPhase()));
          abort();
        }
        if (g.turn != expectedStarter) {
          printf("FAIL: starter is %d, expected %d\n", g.turn, expectedStarter);
          abort();
        }
        if (g.round != roundBefore + 1) {
          printf("FAIL: round did not advance\n");
          abort();
        }
        checkInvariants(g, "after deal");
        // A fresh table narrates nothing: the last move belonged to a round
        // that no longer exists.
        if (g.hasLastMove()) {
          printf("FAIL: a fresh deal still describes a move\n");
          abort();
        }
        checks += 4;
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
      const uint8_t sealsBefore[2] = {g.seals[0], g.seals[1]};

      if (!g.apply(pick)) {
        printf("FAIL: a legal move was rejected\n");
        abort();
      }
      ++moves;
      checkInvariants(g, "after move");
      ++checks;

      // The state has to describe the move that produced it: over a link that
      // record is the only account the other device gets of a turn it did not
      // see. Checked against the move actually played, every move.
      if (!g.hasLastMove() || g.lastMover() != seatBefore || g.lastMoveKind() != pick.kind) {
        printf("FAIL: the state does not describe the move that made it\n");
        abort();
      }
      if (pick.kind == Move::Kind::Sell && (g.lastCard != pick.good || g.lastCount != pick.count)) {
        printf("FAIL: a sale of %d good %d was recorded as %d of %d\n", pick.count, pick.good, g.lastCount, g.lastCard);
        abort();
      }
      checks += 2;

      // The seal is banked by the move that ends the round, not by the deal
      // that follows it. It used to be handed out by startNextRound(), which
      // meant the scoring screen was drawn from a state where nobody had won
      // anything yet -- it showed the seals of the round before.
      if (g.currentPhase() == Phase::Playing) {
        if (g.seals[0] != sealsBefore[0] || g.seals[1] != sealsBefore[1]) {
          printf("FAIL: a seal moved mid-round\n");
          abort();
        }
      } else {
        const int winner = g.roundWinner();
        for (int s = 0; s < kSeats; ++s) {
          const int expected = sealsBefore[s] + (s == winner ? 1 : 0);
          if (g.seals[s] != expected) {
            printf("FAIL: seat %d holds %d seals at round end, expected %d\n", s, g.seals[s], expected);
            abort();
          }
        }
        // And two seals ends the match on the spot, without waiting to be asked
        // for the next round.
        const bool decided = g.matchWinner() >= 0;
        if (decided != (g.currentPhase() == Phase::GameOver)) {
          printf("FAIL: phase %d with match winner %d\n", static_cast<int>(g.currentPhase()), g.matchWinner());
          abort();
        }
        checks += 3;
      }

      if (g.currentPhase() != Phase::Playing) {
        // Counted where the round actually ends, so the one that decides the
        // match counts too: it never reaches startNextRound().
        ++rounds;
        // Exactly one of the two triggers must explain it.
        const bool byPiles = g.emptyPiles() >= 3;
        const bool byDeck = g.deckRemaining() == 0;
        if (!byPiles && !byDeck) {
          printf("FAIL: round ended with %d piles empty and %d cards left\n", g.emptyPiles(), g.deckRemaining());
          abort();
        }
        if (byPiles)
          ++roundsByPiles;
        else
          ++roundsByDeck;
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

  // --- 6: a whole match over a link ---------------------------------------
  //
  // Two devices, a transport that alternates strictly, and nothing else: no
  // radio, no renderer. What is under test is the one decision the Activity
  // makes on every packet -- linkAction() -- and the property that decision
  // exists for: two devices that never disagree about the game, and never both
  // wait for each other.
  //
  // The round boundary is the whole reason this is hard. The loser starts the
  // next round, which has nothing to do with who sent last, so half the time
  // the transport is pointing at the wrong player and somebody has to pass.
  {
    int matches = 0, packets = 0, passes = 0, deals = 0, checks = 0;
    for (int match = 0; match < 200; ++match) {
      Game device[2];
      const int seatOf[2] = {0, 1};
      // Device 0 deals, exactly as JaipurActivity::onMatchStart does, and sends.
      device[0].newGame(rnd(), 0);
      device[1] = device[0];
      int holder = 1;  // the transport turn goes to whoever did not send
      ++matches;

      int guard = 0, consecutivePasses = 0;
      while (guard++ < 6000) {
        const int idle = 1 - holder;
        // The device without the turn has nothing it may do, ever. That is what
        // makes "exactly one device acts" true rather than merely likely.
        if (linkAction(device[idle], seatOf[idle], false) != LinkAction::Wait) {
          printf("FAIL: the device without the turn wanted to act\n");
          abort();
        }
        ++checks;

        const LinkAction action = linkAction(device[holder], seatOf[holder], true);
        if (action == LinkAction::Wait) break;  // GameOver: the chrome takes over

        const Game before = device[holder];
        if (action == LinkAction::Move) {
          consecutivePasses = 0;
          const Observation obs = observe(device[holder], seatOf[holder]);
          uint32_t r = rnd();
          if (!device[holder].apply(chooseMove(obs, Skill::Merchant, r))) {
            printf("FAIL: a chosen move was rejected over the link\n");
            abort();
          }
        } else if (action == LinkAction::Pass) {
          // A pass moves the turn and nothing else. If it changed a byte the
          // two devices would still agree and the game would still be wrong.
          ++passes;
          if (++consecutivePasses > 1) {
            printf("FAIL: two passes in a row, which is a loop\n");
            abort();
          }
        } else if (action == LinkAction::Deal) {
          consecutivePasses = 0;
          ++deals;
          device[holder].startNextRound(rnd());
        }

        if (action == LinkAction::Pass && memcmp(&before, &device[holder], sizeof(Game)) != 0) {
          printf("FAIL: a pass changed the game\n");
          abort();
        }
        ++checks;

        // The packet: the whole state, adopted as-is, turn handed over.
        device[1 - holder] = device[holder];
        ++packets;
        if (memcmp(&device[0], &device[1], sizeof(Game)) != 0) {
          printf("FAIL: the two devices disagree\n");
          abort();
        }
        ++checks;
        holder = 1 - holder;
      }

      if (device[0].matchWinner() < 0) {
        printf("FAIL: match %d never finished over the link\n", match);
        abort();
      }
      if (device[0].currentPhase() != Phase::GameOver) {
        printf("FAIL: the link match stopped in phase %d\n", static_cast<int>(device[0].currentPhase()));
        abort();
      }
      checks += 2;
    }
    printf("link       %d checks, 0 failed  (%d matches, %d packets, %d passes, %d deals)\n", checks, matches, packets,
           passes, deals);
  }

  // --- 7: the running strip -----------------------------------------------
  //
  // The board header is two numbers, and they have to be the same measurement
  // taken twice. It shipped as score() for your column and goods tokens alone
  // for theirs, so the 5 rupee camel token was credited to you and never to
  // them: at a fresh deal where they held the only camel the header read
  // "YOU 0 THEM 0" over a table standing 0-5.
  {
    int checks = 0;

    // Turn zero, the three ways the herds can sit. Nothing is banked yet, so
    // the camel token is the only thing either number can contain, and the
    // tie is the case neither column may claim it.
    static const uint8_t kHerds[3][2] = {{3, 0}, {0, 3}, {2, 2}};
    for (int shape = 0; shape < 3; ++shape) {
      Game g;
      g.newGame(rnd(), 0);
      g.herd[0] = kHerds[shape][0];
      g.herd[1] = kHerds[shape][1];
      for (int viewer = 0; viewer < kSeats; ++viewer) {
        for (int seat = 0; seat < kSeats; ++seat) {
          const int expected = g.herd[seat] > g.herd[1 - seat] ? kCamelTokenValue : 0;
          if (g.visibleScore(viewer, seat) != expected) {
            printf("FAIL: herds %d-%d, seat %d reads %d for seat %d, expected %d\n", g.herd[0], g.herd[1], viewer,
                   g.visibleScore(viewer, seat), seat, expected);
            abort();
          }
          ++checks;
        }
      }
    }

    // And the moment it changes hands, mid-game, with tokens already banked:
    // the two numbers must move by -5 and +5 together. One column dropping 5
    // on its own is what a player sees as their score falling with no sale.
    for (int match = 0; match < 200; ++match) {
      Game g;
      g.newGame(rnd(), static_cast<uint8_t>(rnd() & 1));
      for (int step = 0; step < 24 && g.currentPhase() == Phase::Playing; ++step) {
        const Observation obs = observe(g, g.turn);
        uint32_t r = rnd();
        g.apply(chooseMove(obs, Skill::Merchant, r));
      }
      // Same position, the herd lead handed from one seat to the other. Only
      // the camel token can move, so nothing else in either number may.
      const uint8_t held[2] = {g.herd[0], g.herd[1]};
      for (int viewer = 0; viewer < kSeats; ++viewer) {
        g.herd[0] = 4;
        g.herd[1] = 1;
        const int zeroAhead[2] = {g.visibleScore(viewer, 0), g.visibleScore(viewer, 1)};
        g.herd[0] = 1;
        g.herd[1] = 4;
        const int oneAhead[2] = {g.visibleScore(viewer, 0), g.visibleScore(viewer, 1)};
        if (zeroAhead[0] - oneAhead[0] != kCamelTokenValue || oneAhead[1] - zeroAhead[1] != kCamelTokenValue) {
          printf("FAIL: seat %d watches the lead change and reads %d/%d then %d/%d\n", viewer, zeroAhead[0],
                 zeroAhead[1], oneAhead[0], oneAhead[1]);
          abort();
        }
        // A tie takes it off the table for both, which is the third case a
        // two-sided test would otherwise never reach.
        g.herd[0] = 2;
        g.herd[1] = 2;
        if (g.visibleScore(viewer, 0) != oneAhead[0] || g.visibleScore(viewer, 1) != zeroAhead[1]) {
          printf("FAIL: seat %d reads a tied herd as %d/%d\n", viewer, g.visibleScore(viewer, 0),
                 g.visibleScore(viewer, 1));
          abort();
        }
        checks += 3;
      }
      g.herd[0] = held[0];
      g.herd[1] = held[1];

      // Theirs still hides exactly one thing: the value on the back of their
      // bonus tokens. The strip prints that as a "+N?" count beside the number.
      for (int viewer = 0; viewer < kSeats; ++viewer) {
        const int them = 1 - viewer;
        if (g.visibleScore(viewer, them) + g.bonusRupees(them) != g.score(them)) {
          printf("FAIL: seat %d reads %d for a seat worth %d with %d in bonuses\n", viewer,
                 g.visibleScore(viewer, them), g.score(them), g.bonusRupees(them));
          abort();
        }
        ++checks;
      }
    }

    printf("strip      %d checks, 0 failed  (camel token in both columns, tie in neither)\n", checks);
  }

  return 0;
}

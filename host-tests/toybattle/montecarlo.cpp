// Does playing the game out beat evaluating it?
//
// Mario's call, 2026-08-11, after beating GENERAL three in a row: hand-tuned
// heuristics are the wrong shape for this, and the mechanical answer is to
// compute a great many scenarios and look at the outcomes. That is Monte Carlo,
// and this measures whether the simplest possible version of it -- FLAT, no
// tree, no tuning -- already beats the heuristic brain.
//
// Why it fits this game better than the evaluation does:
//
//   * There is nothing to tune. The value of a position is how often you win
//     from it, which is measured rather than asserted. Four evaluation terms
//     have been tried here and all four were deleted.
//   * NINE BOARDS WITH DIFFERENT SPECIALS. A heuristic needs a human to decide
//     what a Nullify base is worth against a gated pier. A rollout never asks:
//     it plays the real rules on the real board, so each board's character
//     falls out of the win counts for free. This is the argument that matters,
//     and it was Mario's.
//
// It does NOT cheat. Rollouts start from `observe(g, seat)` -- the same
// reconstructed view chooseMove gets, where the enemy rack is inferred from
// what has been seen rather than read off the real state. That is
// determinization, and using the true state instead would measure a player we
// could never ship.
//
//   host-tests/toybattle/montecarlo.sh [games] [playouts-per-move]

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "ToyBattleBrain.h"
#include "ToyBattleCore.h"

using namespace toybattle;

namespace {

// Cheap and deterministic. Not std::rand: it is slow, global, and this is the
// hottest loop in the program by a wide margin.
struct Rng {
  uint32_t s;
  uint32_t next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }
  int below(int n) { return n > 0 ? static_cast<int>(next() % static_cast<uint32_t>(n)) : 0; }
};

constexpr int kPlayoutCap = 300;  // a game that has not ended by here is a draw

// One random game to the end. Returns the winning seat, or kNoSeat.
int rollout(Game g, Rng& rng) {
  const Terrain& b = g.board();
  Step steps[96];
  int guard = 0;
  while (g.currentPhase() == Phase::Playing && guard++ < kPlayoutCap) {
    const int seat = g.turn;
    const int n = g.legalPlacements(seat, steps, 96);
    bool moved = false;
    if (n > 0) {
      // HEAVY playout, not random. 69% of purely random games on Castle Field
      // end STUCK -- both sides spend their troops on placements that go
      // nowhere and nobody reaches the objective -- so the outcome says almost
      // nothing about who stood better, which is the precondition Monte Carlo
      // needs. Two rules are enough to make a rollout finish like a game: take
      // an H.Q. if one is on offer, and finish a region if one is one move from
      // done. Everything else stays random, which is what keeps it cheap.
      int pick = -1;
      for (int j = 0; j < n && pick < 0; ++j) {
        if (!b.isBase(steps[j].slot)) pick = j;  // an H.Q. ends it outright
      }
      if (pick < 0) {
        const uint32_t mine = g.occupiedBy(seat);
        for (int j = 0; j < n && pick < 0; ++j) {
          const uint32_t bit = uint32_t{1} << steps[j].slot;
          for (int r = 0; r < b.regionCount; ++r) {
            if (g.regionsTaken & (1u << r)) continue;
            const uint32_t need = b.regions[r].bases;
            if ((need & bit) && (need & ~(mine | bit)) == 0) {
              pick = j;
              break;
            }
          }
        }
      }
      for (int attempt = 0; attempt < 3 && !moved; ++attempt) {
        const Step& s = steps[pick >= 0 ? pick : rng.below(n)];
        moved = g.apply(Move::place(s.slot, static_cast<Troop>(s.kind), s.useEffect));
        pick = -1;  // if the preferred one was refused, fall back to random
      }
    }
    if (!moved && !g.apply(Move::draw())) break;
  }
  return g.winner;
}

// For each candidate move, play it and roll out from there; keep the one with
// the best win rate. No tree, no exploration policy, no tuning.
Move flatMonteCarlo(const Observation& obs, int playouts, Rng& rng, uint32_t& rolloutsDone) {
  static Move options[512];
  const int n = detail::candidates(obs, options, 512);
  if (n <= 0) return Move::draw();
  if (n == 1) return options[0];

  const int seat = obs.seat;
  // PER CANDIDATE. Dividing a fixed budget by the branching factor is how
  // the first run of this gave every move 2 rollouts and measured noise.
  const int each = playouts;

  int bestScore = -1;
  int best = 0;
  for (int i = 0; i < n; ++i) {
    Game after = obs.view;
    if (!after.apply(options[i])) continue;
    int wins = 0;
    for (int p = 0; p < each; ++p) {
      const int w = rollout(after, rng);
      ++rolloutsDone;
      if (w == seat)
        wins += 2;
      else if (w == kNoSeat)
        wins += 1;  // a draw is half a win, as in the tournament
    }
    if (wins > bestScore) {
      bestScore = wins;
      best = i;
    }
  }
  return options[best];
}

struct Tally {
  int mcWins = 0, brainWins = 0, draws = 0, games = 0;
  double points() const { return games ? (mcWins + draws * 0.5) / games : 0.0; }
};

}  // namespace

// What does a random playout actually look like? If they all end the same way
// in a handful of moves, the win rate they report is noise about that one
// accident rather than information about the position, and no amount of
// compute fixes it. Run with games=0.
void diagnoseRollouts(int n) {
  Rng rng{0xD1A6u};
  int byHq = 0, byMedals = 0, byStuck = 0, unfinished = 0;
  long totalLen = 0;
  int shortest = 1 << 30;
  for (int i = 0; i < n; ++i) {
    Game g;
    g.newGame(0xA5A5u + static_cast<uint32_t>(i) * 2654435761u, static_cast<int>(TerrainId::CastleField), i & 1, true);
    const Terrain& b = g.board();
    Step steps[96];
    int moves = 0;
    while (g.currentPhase() == Phase::Playing && moves < kPlayoutCap) {
      const int seat = g.turn;
      const int cnt = g.legalPlacements(seat, steps, 96);
      bool moved = false;
      if (cnt > 0) {
        int pick = -1;
        for (int j = 0; j < cnt && pick < 0; ++j) {
          if (!b.isBase(steps[j].slot)) pick = j;
        }
        if (pick < 0) {
          const uint32_t mine = g.occupiedBy(seat);
          for (int j = 0; j < cnt && pick < 0; ++j) {
            const uint32_t bit = uint32_t{1} << steps[j].slot;
            for (int r = 0; r < b.regionCount; ++r) {
              if (g.regionsTaken & (1u << r)) continue;
              const uint32_t need = b.regions[r].bases;
              if ((need & bit) && (need & ~(mine | bit)) == 0) {
                pick = j;
                break;
              }
            }
          }
        }
        for (int a = 0; a < 3 && !moved; ++a) {
          const Step& st = steps[pick >= 0 ? pick : rng.below(cnt)];
          moved = g.apply(Move::place(st.slot, static_cast<Troop>(st.kind), st.useEffect));
          pick = -1;
        }
      }
      if (!moved && !g.apply(Move::draw())) break;
      ++moves;
    }
    totalLen += moves;
    if (moves < shortest) shortest = moves;
    if (g.currentPhase() != Phase::GameOver)
      ++unfinished;
    else if (g.ending == static_cast<uint8_t>(Ending::HqCaptured))
      ++byHq;
    else if (g.ending == static_cast<uint8_t>(Ending::MedalsObjective))
      ++byMedals;
    else
      ++byStuck;
  }
  printf("%d random playouts on CASTLE FIELD, bases on:\n", n);
  printf("  ended by H.Q. capture   %6.1f%%\n", 100.0 * byHq / n);
  printf("  ended by medals         %6.1f%%\n", 100.0 * byMedals / n);
  printf("  ended stuck             %6.1f%%\n", 100.0 * byStuck / n);
  printf("  never finished          %6.1f%%\n", 100.0 * unfinished / n);
  printf("  average length          %6.1f moves   (shortest %d)\n", double(totalLen) / n, shortest);
}

int main(int argc, char** argv) {
  const int games = argc > 1 ? std::atoi(argv[1]) : 40;
  if (games == 0) {
    diagnoseRollouts(argc > 2 ? std::atoi(argv[2]) : 20000);
    return 0;
  }
  const int playouts = argc > 2 ? std::atoi(argv[2]) : 400;
  const int onlyBoard = argc > 3 ? std::atoi(argv[3]) : -1;

  printf("FLAT MONTE CARLO (no tree, no evaluation, no tuning)\n");
  printf("against GENERAL, %d games a board, %d playouts PER CANDIDATE, seats alternating\n\n", games, playouts);
  printf("%-18s %-6s %8s %8s %8s %10s\n", "board", "bases", "MC%%", "MC", "brain", "ms/move");

  double worst = 1.0;
  const char* worstBoard = "";
  uint32_t seedCounter = 0x1234567u;

  for (int nth = 0; nth < kPlayableTerrainCount; ++nth) {
    const int t = playableTerrainAt(nth);
    if (onlyBoard >= 0 && t != onlyBoard) continue;
    for (int s = 0; s < 2; ++s) {
      Tally tally;
      double totalMs = 0.0;
      int moves = 0;
      for (int i = 0; i < games; ++i) {
        Rng rng{0xC0FFEEu + seedCounter++};
        Game g;
        g.newGame(0xB0A2D000u + seedCounter * 2654435761u, t, static_cast<int>(seedCounter & 1u), s != 0);
        const int mcSeat = i & 1;
        int turns = 0;
        while (g.currentPhase() == Phase::Playing && turns < 400) {
          const int seat = g.turn;
          const Observation obs = observe(g, seat);
          Move m;
          if (seat == mcSeat) {
            uint32_t done = 0;
            const auto t0 = std::chrono::steady_clock::now();
            m = flatMonteCarlo(obs, playouts, rng, done);
            totalMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            ++moves;
          } else {
            m = chooseMove(obs, Skill::General);
          }
          if (!g.apply(m) && !g.apply(Move::draw())) break;
          ++turns;
        }
        if (g.winner == kNoSeat)
          ++tally.draws;
        else if (g.winner == mcSeat)
          ++tally.mcWins;
        else
          ++tally.brainWins;
        ++tally.games;
      }
      printf("%-18s %-6s %7.1f%% %8d %8d %10.1f\n", terrainAt(t).name, s ? "on" : "off", 100.0 * tally.points(),
             tally.mcWins, tally.brainWins, moves ? totalMs / moves : 0.0);
      if (tally.points() < worst) {
        worst = tally.points();
        worstBoard = terrainAt(t).name;
      }
    }
  }

  printf("\nworst board for Monte Carlo: %.1f%% on %s\n", 100.0 * worst, worstBoard);
  printf("Above 50%% everywhere means playing the game out beats evaluating it,\n");
  printf("with nothing hand-tuned and nothing written per board.\n");
  return 0;
}

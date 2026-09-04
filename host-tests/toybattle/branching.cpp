// How wide does this game actually get?
//
// `kMaxCandidates` is 384, and the comment sizing it records a measured worst of
// 363 over 400 games. That measurement was taken when the only boards were
// Castle Field and PROVING GROUND. There are nine real boards now, two of them
// shapes nothing was tuned on: Station Metal-X is a web with two hubs of degree
// seven, and La Croisette is the only real board carrying four special kinds at
// once, each of which offers a use-it-or-decline choice that multiplies moves.
//
// Overflow here does not crash. `candidates()` fills to `max` and stops, so the
// brain silently searches a subset and plays on. A suite of millions of
// assertions stays green while the opponent quietly gets worse on exactly the
// boards Mario just added. That is why this is measured against a buffer far
// larger than the shipped one rather than asserted against the shipped one.
//
// Not part of check.sh: it is a measurement, like tournament.sh.
//
//   host-tests/toybattle/branching.sh [games-per-condition]

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "ToyBattleBrain.h"
#include "ToyBattleCore.h"

using namespace toybattle;

namespace {

// Far above anything the game could produce, so the count that comes back is
// the true one and not the buffer's size reported as a fact.
constexpr int kProbe = 8192;
Move probe[kProbe];

struct Result {
  const char* board;
  bool specials;
  int worstCandidates = 0;
  uint32_t worstPositions = 0;
  double worstMoveMs = 0.0;
  uint32_t ceilingHits = 0;
  int games = 0;
  int turns = 0;
};

void measure(Result& r, int terrain, bool specials, int games, uint32_t seedBase) {
  const Policy general = policyFor(Skill::General);
  for (int i = 0; i < games; ++i) {
    Game g;
    g.newGame(seedBase + static_cast<uint32_t>(i) * 2654435761u, terrain, i & 1, specials);
    int turns = 0;
    while (g.currentPhase() == Phase::Playing && turns < 400) {
      const Observation obs = observe(g, g.turn);
      const int n = detail::candidates(obs, probe, kProbe);
      if (n > r.worstCandidates) r.worstCandidates = n;
      if (n >= kProbe) {
        printf("FAIL: the probe buffer itself overflowed; raise kProbe\n");
        abort();
      }
      detail::resetCost();
      const auto t0 = std::chrono::steady_clock::now();
      const Move m = chooseMove(obs, general);
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      if (ms > r.worstMoveMs) r.worstMoveMs = ms;
      if (detail::cost.worstPerMove > r.worstPositions) r.worstPositions = detail::cost.worstPerMove;
      // What the SHIPPED buffer did, as opposed to what the probe measured.
      r.ceilingHits += detail::cost.ceilingHits;
      if (!g.apply(m)) {
        printf("FAIL: the brain produced a move the game rejects on %s\n", r.board);
        abort();
      }
      ++turns;
    }
    r.turns += turns;
    ++r.games;
  }
}

}  // namespace

int main(int argc, char** argv) {
  const int games = argc > 1 ? std::atoi(argv[1]) : 40;

  printf("branching, %d games per condition, General, probe buffer %d\n", games, kProbe);
  printf("shipped kMaxCandidates = %d\n\n", detail::kMaxCandidatesShipped);
  printf("%-18s %-6s %11s %10s %9s %7s %8s\n", "board", "bases", "candidates", "positions", "worst ms", "turns",
         "ceiling");

  int worstOverall = 0;
  uint32_t hits = 0;
  const char* worstBoard = "";
  // playableTerrainAt, not a bare offset: the hidden board is terrain 1,
  // with Castle Field at 0. This said kFirstPlayableTerrain and stopped
  // compiling when that was removed, which nothing noticed because these
  // measurements are deliberately outside check.sh.
  for (int nth = 0; nth < kPlayableTerrainCount; ++nth) {
    const int t = playableTerrainAt(nth);
    for (int s = 0; s < 2; ++s) {
      Result r;
      r.board = terrainAt(t).name;
      r.specials = s != 0;
      measure(r, t, r.specials, games, 0xB0A2D000u + static_cast<uint32_t>(t) * 131u + static_cast<uint32_t>(s));
      printf("%-18s %-6s %11d %10u %9.1f %7d %8u\n", r.board, r.specials ? "on" : "off", r.worstCandidates,
             r.worstPositions, r.worstMoveMs, r.turns, r.ceilingHits);
      hits += r.ceilingHits;
      if (r.worstCandidates > worstOverall) {
        worstOverall = r.worstCandidates;
        worstBoard = r.board;
      }
    }
  }

  const int headroom = detail::kMaxCandidatesShipped - worstOverall;
  printf("\nworst: %d candidates on %s\n", worstOverall, worstBoard);
  printf("shipped buffer: %d, headroom: %d (%.0f%%)\n", detail::kMaxCandidatesShipped, headroom,
         100.0 * headroom / detail::kMaxCandidatesShipped);
  printf("buffer filled during real play: %u times\n", hits);
  if (worstOverall > detail::kMaxCandidatesShipped || hits > 0) {
    printf("\nOVERFLOWS. The shipped brain is silently dropping moves.\n");
    return 1;
  }
  printf("\nfits, and the ceiling was never reached.\n");
  return 0;
}

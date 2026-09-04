// Self-play data for FITTING the evaluation instead of guessing it.
//
// Mario, 2026-08-11, after beating GENERAL three games running and asking why
// chess engines learn without human games: the mechanical answer is to play a
// great many games and let the outcomes decide. AlphaZero is the extreme of
// that; this is the version that fits on an ESP32, and it is standard practice
// in chess engines -- play, record every position with how its game ended, and
// solve for the weights that best predict the result.
//
// Why this and not a deeper search: `evaluate` is seven numbers -- 400 a medal,
// 25 a base, 8 a reachable slot, 6 a rack troop -- and I chose all of them by
// eye. Nothing has ever fitted them. The tournament could only ever RANK
// variants that shared them, so it could rank depth and was structurally
// unable to say the goal was wrong.
//
// Nothing here runs on the device. The output is a table of integers; the
// firmware keeps the same search and the same cost.
//
// Emits one line per position: label then features, from the MOVER's view.
//
//   host-tests/toybattle/tune.sh <games> [board] > positions.tsv

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "ToyBattleBrain.h"
#include "ToyBattleCore.h"

using namespace toybattle;

namespace {

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

int popcount32(uint32_t v) {
  int n = 0;
  while (v) {
    v &= v - 1;
    ++n;
  }
  return n;
}
int popcount64(uint64_t v) {
  int n = 0;
  while (v) {
    v &= v - 1;
    ++n;
  }
  return n;
}

// DISTANCE TO WINNING, not a description of the board.
//
// Mario, 2026-08-11, on watching the first fitted brain rush his H.Q.: if a
// medal one move away wins the game, take it; there is no sense charging the
// H.Q. then. It should decide from the situation.
//
// He is right, and the first feature set could not do that by construction. A
// flat `hq_pressure` weight says threatening the H.Q. is worth three and a
// half medals ALWAYS -- whether the brain is one placement from winning the
// medal race or nine. A linear evaluation cannot express "it depends" unless
// the FEATURES carry the situation, so these do: how far each side is from
// each of the two ways to win. Then closeness is what gets weighed, and the
// two routes are compared on one scale instead of one being a constant.
constexpr int kFeatures = 10;
const char* kFeatureNames[kFeatures] = {
    "medals_to_go",  // medals I still need, negated: bigger is closer
    "their_medals_togo",
    "regions_ready",  // regions one placement from mine, and from theirs
    "their_regions_ready",
    "hq_takeable_now",  // a legal move THIS turn captures their H.Q.
    "my_hq_takeable",   // one captures mine
    "hq_reach",         // their H.Q. is connected to me, but not takeable yet
    "bases_diff",
    "reach_diff",
    "rack_diff",
};

// Is there a legal placement, right now, that lands on `hqSlot`?
bool canTakeHq(const Game& g, int seat, int hqSlot) {
  static Step steps[128];
  const int n = g.legalPlacements(seat, steps, 128);
  for (int i = 0; i < n; ++i) {
    if (steps[i].slot == hqSlot) return true;
  }
  return false;
}

void features(const Game& g, int seat, int* out) {
  const Terrain& b = g.board();
  const int foe = seat ^ 1;
  const uint32_t mine = g.occupiedBy(seat), theirs = g.occupiedBy(foe);
  const uint64_t myReach = g.reachable(seat), theirReach = g.reachable(foe);

  // Negated so that every feature points the same way: bigger is better for
  // the mover. A fit can find a sign, but a table nobody can read by eye is
  // how a wrong sign survives.
  out[0] = -(b.medalsObjective - g.medals[seat]);
  out[1] = b.medalsObjective - g.medals[foe];

  int ready = 0, theirReady = 0;
  for (int r = 0; r < b.regionCount; ++r) {
    if (g.regionsTaken & (1u << r)) continue;
    const uint32_t need = b.regions[r].bases;
    if (popcount32(need & ~mine) == 1) ready += b.regions[r].medals;
    if (popcount32(need & ~theirs) == 1) theirReady += b.regions[r].medals;
  }
  out[2] = ready;
  out[3] = -theirReady;

  int takeable = 0, exposed = 0, reachOnly = 0;
  for (int slot = b.baseCount; slot < b.slotCount(); ++slot) {
    const bool connected = (myReach & (uint64_t{1} << slot)) != 0;
    if (b.hqOwner(slot) == foe) {
      if (canTakeHq(g, seat, slot))
        takeable = 1;
      else if (connected)
        reachOnly = 1;
    }
    if (b.hqOwner(slot) == seat && (theirReach & (uint64_t{1} << slot))) exposed = 1;
  }
  out[4] = takeable;
  out[5] = -exposed;
  out[6] = reachOnly;

  out[7] = popcount32(mine) - popcount32(theirs);
  out[8] = popcount64(myReach) - popcount64(theirReach);
  out[9] = g.rackSize(seat) - g.rackSize(foe);
}

}  // namespace

int main(int argc, char** argv) {
  const int games = argc > 1 ? std::atoi(argv[1]) : 200;
  const int onlyBoard = argc > 2 ? std::atoi(argv[2]) : -1;

  std::printf("# label");
  for (int f = 0; f < kFeatures; ++f) std::printf("\t%s", kFeatureNames[f]);
  std::printf("\tboard\n");

  Rng rng{0xF17u};
  int emitted = 0;

  for (int nth = 0; nth < kPlayableTerrainCount; ++nth) {
    const int t = playableTerrainAt(nth);
    if (onlyBoard >= 0 && t != onlyBoard) continue;
    for (int i = 0; i < games; ++i) {
      Game g;
      g.newGame(rng.next() | 1u, t, static_cast<int>(rng.next() & 1u), (rng.next() & 1u) != 0);

      // Positions and whose turn it was, replayed against the result at the end.
      static int rows[400][kFeatures];
      static int movers[400];
      int n = 0, turns = 0;

      while (g.currentPhase() == Phase::Playing && turns < 400) {
        const int seat = g.turn;
        if (n < 400) {
          features(g, seat, rows[n]);
          movers[n] = seat;
          ++n;
        }
        // Mostly the real brain, sometimes not. A fit over positions that only
        // ONE policy ever reaches learns that policy's blind spots as facts;
        // the noise is what makes the sample cover the board.
        Move m;
        if (rng.below(100) < 15) {
          Step steps[96];
          const int c = g.legalPlacements(seat, steps, 96);
          if (c > 0) {
            const Step& s = steps[rng.below(c)];
            m = Move::place(s.slot, static_cast<Troop>(s.kind), s.useEffect);
          } else {
            m = Move::draw();
          }
        } else {
          m = chooseMove(observe(g, seat), Skill::General);
        }
        if (!g.apply(m) && !g.apply(Move::draw())) break;
        ++turns;
      }

      // Label every position with how the game it came from actually ended.
      // Draws are dropped rather than called half a win: this game's draws are
      // mutual gridlock, which says little about who stood better.
      if (g.winner == kNoSeat) continue;
      for (int r = 0; r < n; ++r) {
        const int label = g.winner == movers[r] ? 1 : 0;
        std::printf("%d", label);
        for (int f = 0; f < kFeatures; ++f) std::printf("\t%d", rows[r][f]);
        std::printf("\t%d\n", t);
        ++emitted;
      }
    }
  }
  std::fprintf(stderr, "%d positions\n", emitted);
  return 0;
}

// The Toy Battle opponent tournament. Not a test: it prints a table and asserts
// almost nothing, because its job is to decide which policy is strongest rather
// than to defend one that already shipped.
//
// The method is Mario's: build the candidates several ways, play them against
// each other, and only believe a tier list if it is transitive. A cycle (A beats
// B beats C beats A) means the differences are noise wearing a percentage, which
// is exactly how an earlier version of this brain "measured" a difficulty
// setting that did not exist.
//
// Three rules it follows so the numbers mean something:
//
//   * Seats alternate. Otherwise the result is the first-player advantage in a
//     costume.
//   * Every pairing sees the SAME seeds. The comparison is paired, so a lucky
//     shuffle helps both sides and cancels instead of adding noise.
//   * Every pairing is played on both boards, with special bases on and off.
//     More maps are coming and they carry different special bases, so a policy
//     that only wins on Castle Field with wells has not won anything.
//
//   host-tests/toybattle/tournament.sh [games-per-condition]

#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "ToyBattleBrain.h"
#include "ToyBattleCore.h"

using namespace toybattle;

namespace {

uint32_t rngState = 0x9E3779B9u;
uint32_t rnd() {
  rngState ^= rngState << 13;
  rngState ^= rngState >> 17;
  rngState ^= rngState << 5;
  return rngState;
}

struct Variant {
  const char* name;
  Policy policy;
};

// The knobs, one at a time and then combined, so a combination that wins can be
// read against the parts it is made of. `greedy` is what ships today.
Policy make(uint8_t beam) {
  Policy p;
  p.beam = beam;
  p.material = true;
  return p;
}

Policy deep(uint8_t beam) {
  Policy p = make(beam);
  p.depth = 3;
  return p;
}

const Variant kVariants[] = {
    // What ships, measured as what ships: `policyFor` rather than a copy of it,
    // so this cannot quietly drift from the brain the game actually uses.
    {"SHIPPED", policyFor(Skill::General)},
    {"recruit", policyFor(Skill::Recruit)},
    {"greedy", make(0)},  // what shipped before the search, kept as the yardstick
    {"d3b4", deep(4)},
    {"d3b12", deep(12)},
};
constexpr int kVariantCount = static_cast<int>(sizeof(kVariants) / sizeof(kVariants[0]));

struct Condition {
  int terrain;
  bool specials;
  const char* name;
};
const Condition kConditions[] = {
    // Boards people actually play, chosen for spread rather than convenience:
    // Castle Field is the default and a regular lattice, Station Metal-X is the
    // only web (two hubs of degree seven, so the widest replies), and La
    // Croisette is the only real board carrying all four special kinds at once.
    // PROVING GROUND was half of this list until 2026-08-11; it is ours, it is
    // hidden from the picker, and a tier list measured on it was partly a tier
    // list for a board nobody can select.
    {static_cast<int>(TerrainId::CastleField), true, "castle+bases"},
    {static_cast<int>(TerrainId::CastleField), false, "castle"},
    {static_cast<int>(TerrainId::StationMetalX), true, "metalx+bases"},
    {static_cast<int>(TerrainId::LaCroisette), true, "croisette+bases"},
    {static_cast<int>(TerrainId::LaCroisette), false, "croisette"},
};
constexpr int kConditionCount = static_cast<int>(sizeof(kConditions) / sizeof(kConditions[0]));

// Returns the winning seat, or kNoSeat for a draw.
int playMatch(const Policy& p0, const Policy& p1, uint32_t seed, const Condition& c) {
  Game g;
  g.newGame(seed, c.terrain, static_cast<int>(seed & 1u), c.specials);
  int turns = 0;
  while (g.currentPhase() == Phase::Playing) {
    const int seat = g.turn;
    const Observation obs = observe(g, seat);
    const Move m = chooseMove(obs, seat == 0 ? p0 : p1);
    if (!g.apply(m)) {
      printf("FAIL: a policy produced a move the real game rejects\n");
      abort();
    }
    if (++turns > 400) return kNoSeat;
  }
  return g.winner;
}

struct Score {
  int wins = 0;
  int draws = 0;
  int games = 0;
  // Draws are half a win, so a policy cannot farm a high score by stalling.
  double points() const { return games ? (wins + draws * 0.5) / games : 0.0; }
};

// `a` against `b` over the same seeds, alternating seats.
Score faceOff(const Policy& a, const Policy& b, int games, const Condition& c, uint32_t seedBase) {
  Score s;
  for (int i = 0; i < games; ++i) {
    // Paired: the same seed is played by both arrangements, so the shuffle is
    // not part of what is being measured.
    const uint32_t seed = seedBase + static_cast<uint32_t>(i) * 2654435761u;
    const int aSeat = i & 1;
    const int winner = aSeat == 0 ? playMatch(a, b, seed, c) : playMatch(b, a, seed, c);
    if (winner == kNoSeat) {
      ++s.draws;
    } else if (winner == aSeat) {
      ++s.wins;
    }
    ++s.games;
  }
  return s;
}

}  // namespace

int main(int argc, char** argv) {
  const int games = argc > 1 ? atoi(argv[1]) : 40;
  const uint32_t seedBase = rnd();

  printf("Toy Battle opponent tournament\n");
  printf("%d games per pairing per condition, %d conditions, %d variants\n\n", games, kConditionCount, kVariantCount);

  // matrix[i][j] = points i scored against j, pooled over every condition.
  static double matrix[kVariantCount][kVariantCount] = {};
  static int played[kVariantCount][kVariantCount] = {};
  static double perCondition[kVariantCount][kConditionCount] = {};
  static int perConditionGames[kVariantCount][kConditionCount] = {};

  for (int i = 0; i < kVariantCount; ++i) {
    for (int j = i + 1; j < kVariantCount; ++j) {
      for (int c = 0; c < kConditionCount; ++c) {
        const Score s = faceOff(kVariants[i].policy, kVariants[j].policy, games, kConditions[c], seedBase);
        matrix[i][j] += s.wins + s.draws * 0.5;
        matrix[j][i] += (s.games - s.wins - s.draws) + s.draws * 0.5;
        played[i][j] += s.games;
        played[j][i] += s.games;
        perCondition[i][c] += s.wins + s.draws * 0.5;
        perCondition[j][c] += (s.games - s.wins - s.draws) + s.draws * 0.5;
        perConditionGames[i][c] += s.games;
        perConditionGames[j][c] += s.games;
      }
      printf("  %-10s vs %-10s %5.1f%%\n", kVariants[i].name, kVariants[j].name,
             100.0 * matrix[i][j] / (played[i][j] ? played[i][j] : 1));
      fflush(stdout);
    }
  }

  // --- the table ------------------------------------------------------------
  printf("\n%-11s", "");
  for (int j = 0; j < kVariantCount; ++j) printf("%10s", kVariants[j].name);
  printf("%9s%8s\n", "overall", "+/-");

  static double overall[kVariantCount];
  static int overallGames[kVariantCount];
  for (int i = 0; i < kVariantCount; ++i) {
    overall[i] = 0;
    overallGames[i] = 0;
    for (int j = 0; j < kVariantCount; ++j) {
      overall[i] += matrix[i][j];
      overallGames[i] += played[i][j];
    }
  }
  for (int i = 0; i < kVariantCount; ++i) {
    printf("%-11s", kVariants[i].name);
    for (int j = 0; j < kVariantCount; ++j) {
      if (i == j) {
        printf("%10s", "--");
      } else {
        printf("%9.1f%%", 100.0 * matrix[i][j] / (played[i][j] ? played[i][j] : 1));
      }
    }
    const double p = overall[i] / overallGames[i];
    // One standard error of a proportion. Anything inside this band of 50% is a
    // coin toss, whatever the decimal says.
    const double se = 100.0 * (overallGames[i] ? __builtin_sqrt(p * (1.0 - p) / overallGames[i]) : 0.0);
    printf("%8.1f%%%7.1f\n", 100.0 * p, se);
  }

  // --- is it a tier list, or a cycle? --------------------------------------
  static int order[kVariantCount];
  for (int i = 0; i < kVariantCount; ++i) order[i] = i;
  for (int i = 0; i < kVariantCount; ++i) {
    for (int j = i + 1; j < kVariantCount; ++j) {
      const double a = overall[order[i]] / overallGames[order[i]];
      const double b = overall[order[j]] / overallGames[order[j]];
      if (b > a) {
        const int t = order[i];
        order[i] = order[j];
        order[j] = t;
      }
    }
  }
  printf("\nranking: ");
  for (int i = 0; i < kVariantCount; ++i) printf("%s%s", i ? " > " : "", kVariants[order[i]].name);
  printf("\n");

  // A tie is not a cycle. Two policies half a point apart get force-ranked by
  // the sort, and flagging that as non-transitivity cries wolf on every pairing
  // that is genuinely level -- which is what the first run of this did. An
  // inversion counts only when the head-to-head is below 50% by more than two
  // standard errors, i.e. when the ranking and the result actually disagree.
  int cycles = 0;
  for (int i = 0; i < kVariantCount; ++i) {
    for (int j = i + 1; j < kVariantCount; ++j) {
      const int a = order[i], b = order[j];
      const double head = matrix[a][b] / played[a][b];
      const double se = __builtin_sqrt(head * (1.0 - head) / played[a][b]);
      if (head < 0.5 - 2.0 * se) {
        printf("  CYCLE: %s ranks above %s but loses to it head to head (%.1f%% +/- %.1f)\n", kVariants[a].name,
               kVariants[b].name, 100.0 * head, 100.0 * se);
        ++cycles;
      }
    }
  }
  printf(cycles ? "  not a tier list: %d inversion(s)\n" : "  transitive: the ranking is a tier list\n", cycles);

  // --- does it hold on every board and with bases off? ---------------------
  printf("\nby condition (overall points against the field)\n%-11s", "");
  for (int c = 0; c < kConditionCount; ++c) printf("%15s", kConditions[c].name);
  printf("\n");
  for (int i = 0; i < kVariantCount; ++i) {
    printf("%-11s", kVariants[i].name);
    for (int c = 0; c < kConditionCount; ++c) {
      printf("%14.1f%%", 100.0 * perCondition[i][c] / (perConditionGames[i][c] ? perConditionGames[i][c] : 1));
    }
    printf("\n");
  }

  // --- what it costs -------------------------------------------------------
  //
  // The number that decides whether a policy can ship. Positions applied per
  // move is the honest unit: it is what the ESP32-S3 will actually spend, and
  // unlike wall time here it does not flatter itself on a laptop.
  printf("\ncost per move (positions applied, and host time)\n");
  for (int i = 0; i < kVariantCount; ++i) {
    detail::resetCost();
    int moves = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (int k = 0; k < 6; ++k) {
      Game g;
      g.newGame(seedBase + static_cast<uint32_t>(k) * 7919u, static_cast<int>(TerrainId::CastleField), k & 1, true);
      while (g.currentPhase() == Phase::Playing && moves < 100000) {
        const Observation obs = observe(g, g.turn);
        const Move m = chooseMove(obs, kVariants[i].policy);
        if (!g.apply(m)) break;
        ++moves;
      }
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    printf("  %-11s mean %7u   worst %7u   %6.3f ms/move (host)\n", kVariants[i].name,
           moves ? detail::cost.positions / static_cast<uint32_t>(moves) : 0, detail::cost.worstPerMove,
           moves ? ms / moves : 0.0);
  }
  return 0;
}

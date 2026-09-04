// The Toy Battle opponent. Three things are proved and one is only measured,
// and the output says which.
//
// Proved: every move it returns is legal in the *real* game, not merely in the
// reconstruction it reasons over; it takes a win when one is on the table; and
// it never leaves its own H.Q. exposed when a move existed that would not.
//
// Measured: strength, as bands. The number against a random baseline says the
// baseline is weak. The head-to-head is the one that would catch an evaluation
// regression.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "ToyBattleBrain.h"
#include "ToyBattleCore.h"

using namespace toybattle;

// These exercise the rules, not a board Repos printed, so they run on the
// lattice. Terrain 0 is Castle Field.
static constexpr int kPG = static_cast<int>(TerrainId::ProvingGround);

static int checks = 0;

static void check(bool ok, const char* what) {
  ++checks;
  if (!ok) {
    printf("FAIL: %s\n", what);
    abort();
  }
}

static uint32_t rngState = 0xC0FFEEu;
static uint32_t rnd() {
  rngState ^= rngState << 13;
  rngState ^= rngState >> 17;
  rngState ^= rngState << 5;
  return rngState;
}

// --- players ---------------------------------------------------------------

// The weak baseline: any legal move at all. Deliberately not "greedy", because
// a greedy player is already making a judgement and would flatter nothing.
static Move randomLegalMove(const Game& g) {
  std::vector<Move> options;
  if (g.isLegal(Move::draw())) options.push_back(Move::draw());
  Step steps[kTroopKinds * kMaxSlots];
  const int n = g.legalPlacements(g.turn, steps, static_cast<int>(sizeof(steps) / sizeof(steps[0])));
  for (int i = 0; i < n; ++i) {
    Move m = Move::place(steps[i].slot, static_cast<Troop>(steps[i].kind), steps[i].useEffect);
    if (g.isLegal(m)) options.push_back(m);
  }
  if (options.empty()) return Move::draw();
  return options[rnd() % options.size()];
}

enum class Player { Random, Recruit, Sergeant, General };

static Move moveFor(const Game& g, Player p) {
  if (p == Player::Random) return randomLegalMove(g);
  const Observation obs = observe(g, g.turn);
  const Skill skill =
      p == Player::Recruit ? Skill::Recruit : (p == Player::Sergeant ? Skill::Sergeant : Skill::General);
  return chooseMove(obs, skill);
}

// Plays one game and returns the winner, checking the brain's promises on every
// move it makes.
static int playMatch(Player seat0, Player seat1, uint32_t seed, bool specialBases, bool verifyPromises,
                     int terrain = kPG) {
  Game g;
  g.newGame(seed, terrain, static_cast<int>(seed & 1u), specialBases);

  int turns = 0;
  while (g.currentPhase() == Phase::Playing) {
    const int seat = g.turn;
    const Player who = seat == 0 ? seat0 : seat1;
    const Move m = moveFor(g, who);

    if (who != Player::Random) {
      // The brain reasons over a view whose enemy rack is a reconstruction. A
      // move that is legal there and not here would be the whole design
      // failing, so it is checked on every single move of every match.
      check(g.isLegal(m), "the brain's move is legal in the real game");

      if (verifyPromises) {
        const Observation obs = observe(g, seat);
        Move options[512];
        const int n = detail::candidates(obs, options, 512);

        // If any move leaves the H.Q. safe, the one played must too.
        bool safeExists = false;
        for (int i = 0; i < n && !safeExists; ++i) {
          Game after = obs.view;
          if (!after.apply(options[i])) continue;
          if (after.currentPhase() == Phase::GameOver && after.winner == seat)
            safeExists = true;
          else if (after.currentPhase() == Phase::Playing &&
                   !detail::hqIsExposed(after, seat, obs.unseen, obs.opponentRackSize))
            safeExists = true;
        }
        if (safeExists) {
          Game after = obs.view;
          check(after.apply(m), "the chosen move applies to the view it was chosen from");
          const bool won = after.currentPhase() == Phase::GameOver && after.winner == seat;
          const bool safe = won || (after.currentPhase() == Phase::Playing &&
                                    !detail::hqIsExposed(after, seat, obs.unseen, obs.opponentRackSize));
          check(safe, "with a safe move available, the brain does not hang its H.Q.");
        }

        // A win on the table is taken.
        bool winExists = false;
        for (int i = 0; i < n && !winExists; ++i) {
          Game after = obs.view;
          if (after.apply(options[i]) && after.currentPhase() == Phase::GameOver && after.winner == seat) {
            winExists = true;
          }
        }
        if (winExists) {
          Game after = obs.view;
          check(after.apply(m) && after.currentPhase() == Phase::GameOver && after.winner == seat,
                "with a win on the table, the brain takes it");
        }
      }
    }

    check(g.apply(m), "every move applies");
    if (++turns > 400) {
      printf("FAIL: a brain match ran past 400 turns\n");
      abort();
    }
  }
  return g.winner;
}

// --- targeted tests --------------------------------------------------------

static void put(Game& g, int seat, int base, Troop kind) {
  g.placeSlot[g.placementCount] = static_cast<uint8_t>(base);
  g.placeTile[g.placementCount] = static_cast<uint8_t>((seat << 3) | static_cast<int>(kind));
  ++g.placementCount;
}

static void testObservationCarriesNoSecret() {
  Game g;
  g.newGame(12345u, kPG, 0);
  const Observation obs = observe(g, 0);

  check(obs.opponentRackSize == g.rackSize(1), "the enemy rack size is public and carried across");
  check(obs.view.seed == 0, "the seed is cleared, so neither reserve can be read off it");

  // The reconstruction is a guess. It has the right size and draws only from
  // what is actually unaccounted for -- and it is allowed to be wrong, which is
  // the point: if it were always right the brain would be cheating.
  int rebuilt = 0;
  for (int k = 0; k < kTroopKinds; ++k) {
    rebuilt += obs.view.rack[1][k];
    check(obs.view.rack[1][k] <= obs.unseen[k], "the reconstruction stays inside the public multiset");
  }
  check(rebuilt == obs.opponentRackSize, "and has exactly the right number of troops in it");

  // Two games differing only in the enemy's hidden rack must observe the same,
  // or something private is leaking through.
  Game h = g;
  for (int k = 0; k < kTroopKinds; ++k) h.rack[1][k] = 0;
  h.rack[1][static_cast<int>(Troop::Roxy)] = static_cast<uint8_t>(g.rackSize(1));
  const Observation obs2 = observe(h, 0);
  check(memcmp(&obs.view, &obs2.view, sizeof(Game)) == 0, "reshuffling the hidden rack changes nothing observable");
  check(memcmp(obs.unseen, obs2.unseen, sizeof(obs.unseen)) == 0, "including the belief");
}

static void testTakesTheWinInFrontOfIt() {
  // A line to the enemy H.Q. and a troop in hand: there is a win and it must be
  // taken rather than improved upon.
  Game g;
  g.newGame(5u, kPG, 0);
  for (int k = 0; k < kTroopKinds; ++k) g.rack[0][k] = 0;
  g.rack[0][static_cast<int>(Troop::Skully)] = 1;
  g.reserveTaken[0] = 1;
  g.placementCount = 0;
  for (int base = 10; base <= 14; ++base) put(g, 0, base, Troop::Roxy);

  for (Skill s : {Skill::Recruit, Skill::Sergeant, Skill::General}) {
    const Observation obs = observe(g, 0);
    const Move m = chooseMove(obs, s);
    Game after = g;
    check(after.apply(m), "the move applies");
    check(after.currentPhase() == Phase::GameOver && after.winner == 0, "the brain captures the H.Q. when it can");
  }
}

static void testRefusesToHangItsHq() {
  // Seat 1 has walked the bottom row from their own H.Q. to base 10, which
  // touches seat 0's. That is a real exposure -- and note what it takes: a
  // troop merely standing next to an H.Q. is nothing, because their line has
  // to trace back to their own H.Q. through bases they hold.
  //
  // Seat 0 holds a 7, so covering base 10 breaks the line. Every other move
  // leaves the game lost next turn.
  Game g;
  g.newGame(9u, kPG, 0);
  for (int k = 0; k < kTroopKinds; ++k) {
    g.rack[0][k] = 0;
    g.rack[1][k] = 0;
  }
  g.rack[0][static_cast<int>(Troop::Roxy)] = 2;
  g.rack[1][static_cast<int>(Troop::Roxy)] = 1;
  g.reserveTaken[0] = 2;
  g.reserveTaken[1] = 1;
  g.placementCount = 0;
  g.turn = 0;
  put(g, 1, 14, Troop::Skully);
  put(g, 1, 13, Troop::Skully);
  put(g, 1, 12, Troop::Capn);
  put(g, 1, 11, Troop::Capn);
  put(g, 1, 10, Troop::Jumbo);

  const Observation obs = observe(g, 0);
  check(detail::hqIsExposed(g, 0, obs.unseen, obs.opponentRackSize), "the H.Q. is exposed to start with");

  for (Skill s : {Skill::Recruit, Skill::Sergeant, Skill::General}) {
    const Move m = chooseMove(observe(g, 0), s);
    Game after = g;
    check(after.apply(m), "the move applies");
    const Observation post = observe(after, 0);
    check(!detail::hqIsExposed(after, 0, post.unseen, post.opponentRackSize),
          "and it closes the hole rather than ignoring it");
  }
}

static void testHookCannotBeImaginedOntoAnHq() {
  // The rule every summary got wrong, asked of the brain rather than the rules:
  // a Hook across the map is not a threat to an H.Q., so a position that is
  // safe must not be read as exposed.
  Game g;
  g.newGame(3u, kPG, 0);
  for (int k = 0; k < kTroopKinds; ++k) g.rack[1][k] = 0;
  g.rack[1][static_cast<int>(Troop::Hook)] = 3;
  g.reserveTaken[1] = 3;
  g.placementCount = 0;

  const Observation obs = observe(g, 0);
  check(!detail::hqIsExposed(g, 0, obs.unseen, obs.opponentRackSize),
        "an enemy holding only Hooks does not threaten a disconnected H.Q.");
}

static void testDeterminism() {
  Game g;
  g.newGame(777u, kPG, 1);
  const Observation obs = observe(g, g.turn);
  const Move a = chooseMove(obs, Skill::General);
  const Move b = chooseMove(obs, Skill::General);
  check(memcmp(&a, &b, sizeof(Move)) == 0, "the same observation always produces the same move");
}

// --- strength --------------------------------------------------------------

struct Record {
  int wins = 0;
  int games = 0;
  int percent() const { return games ? (wins * 100) / games : 0; }
};

static Record faceOff(Player challenger, Player defender, int games, bool specialBases, bool verifyPromises,
                      int terrain = kPG) {
  Record r;
  for (int i = 0; i < games; ++i) {
    // Alternate who starts, so the result is not the first-player advantage
    // wearing a costume.
    const uint32_t seed = rnd();
    const int winner = (i & 1) ? playMatch(challenger, defender, seed, specialBases, verifyPromises, terrain)
                               : playMatch(defender, challenger, seed, specialBases, verifyPromises, terrain);
    const int challengerSeat = (i & 1) ? 0 : 1;
    if (winner == challengerSeat) ++r.wins;
    ++r.games;
  }
  return r;
}

int main() {
  testObservationCarriesNoSecret();
  testTakesTheWinInFrontOfIt();
  testRefusesToHangItsHq();
  testHookCannotBeImaginedOntoAnHq();
  testDeterminism();

  // The brain's promises are verified on every move of the matchups flagged
  // below, which is what makes them slow and what makes them worth running.
  const Record recruitVsRandom = faceOff(Player::Recruit, Player::Random, 300, true, true);
  const Record generalVsRandom = faceOff(Player::General, Player::Random, 100, true, true);
  const Record generalNoBases = faceOff(Player::General, Player::Random, 60, false, true);
  // The number that means something. Recruit has the same two reflexes and no
  // opinions, so this is the evaluation's own contribution and nothing else.
  // 600 games because at 60 the noise is about +/- 6%, and any claim inside
  // that band is a coin toss wearing a percentage -- which is how an earlier
  // version of this file "measured" a difference that did not exist.
  const Record generalVsRecruit = faceOff(Player::General, Player::Recruit, 600, true, false);
  // And on the board people will actually play, with the promises verified on
  // every move: a brain proved only on the lattice is a brain proved on a board
  // nobody owns.
  const Record castle =
      faceOff(Player::General, Player::Recruit, 200, true, true, static_cast<int>(TerrainId::CastleField));
  // The difficulty setting offers three rungs, so the three rungs have to BE a
  // ladder rather than three names over the same opponent. Each step is checked
  // against the one below it; if a change ever makes Sergeant as strong as
  // General, this is what says so instead of a player noticing.
  const Record generalVsSergeant = faceOff(Player::General, Player::Sergeant, 400, true, false);
  const Record sergeantVsRecruit = faceOff(Player::Sergeant, Player::Recruit, 300, true, false);

  // Bands, deliberately well below what was measured (77%, 100%, 100%, 100%),
  // so that a real regression trips them and ordinary variance does not. A
  // bound set to the number that was just observed is a bound that passes by
  // construction.
  check(recruitVsRandom.percent() >= 65, "reflexes alone beat a random player");
  check(generalVsRandom.percent() >= 90, "and an evaluation on top of them beats it nearly always");
  check(generalNoBases.percent() >= 90, "with special bases switched off as well");
  check(generalVsRecruit.percent() >= 90, "General beats Recruit, which is the evaluation earning its place");
  check(castle.percent() >= 90, "and the same on Castle Field");
  // The rungs shifted up one on 2026-08-11, so both bands below were re-derived
  // from the tournament rather than left where they were. GENERAL is now the
  // fitted evaluation, SERGEANT is the beam-8 depth-3 search that used to be
  // GENERAL, and RECRUIT is the greedy evaluation that used to be SERGEANT.
  //
  // Tournament, 600 games a pairing: fitted beats the search 84.0%. The bands
  // sit about ten points under what this suite actually measures -- 81% and
  // 80% -- which is the headroom the old ones had.
  //
  // The lower band was first set to 55 from the tournament's 64.7% for search
  // against greedy, and that was the wrong number to derive from: the
  // tournament's `greedy` variant is not policyFor(Recruit), and the real
  // pairing measures 80%. Deriving a bound from a nearby-but-different
  // measurement is its own way of being wrong, so it is stated here rather
  // than left looking deliberate.
  //
  // The second one USED to be >= 90, and that was right when it compared an
  // evaluation against no evaluation at all. It now compares search against
  // greedy, which is a much narrower gap: 90 would have been a bound that
  // could only be met by the pairing it was written for.
  check(generalVsSergeant.percent() >= 70, "General out-evaluates Sergeant, so the top rung is a real rung");
  check(sergeantVsRecruit.percent() >= 65, "and Sergeant out-searches Recruit, so the bottom one is too");

  printf("brain      %d checks, 0 failed\n", checks);
  printf("           vs random: recruit %d%% (%d/%d), general %d%% (%d%% with special bases off)\n",
         recruitVsRandom.percent(), recruitVsRandom.wins, recruitVsRandom.games, generalVsRandom.percent(),
         generalNoBases.percent());
  printf("           general %d%% (%d/%d) vs recruit  <- the one that would catch a regression\n",
         generalVsRecruit.percent(), generalVsRecruit.wins, generalVsRecruit.games);
  printf("           general %d%% vs recruit on CASTLE FIELD over %d games\n", castle.percent(), castle.games);
  printf("           the ladder: general %d%% vs sergeant, sergeant %d%% vs recruit\n", generalVsSergeant.percent(),
         sergeantVsRecruit.percent());
  return 0;
}

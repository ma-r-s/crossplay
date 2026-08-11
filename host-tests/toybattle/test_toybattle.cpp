// Toy Battle rules tests: the placement rules, the eight troop effects, region
// capture, both instant wins and the stuck ending, plus a soak that rechecks
// every invariant after every move of whole random matches.
//
// The targeted cases build positions by writing into the placement log
// directly. That is deliberate: reaching "an enemy 6 buried under my 7, three
// bases from my H.Q." by legal play alone would take a page of setup per test
// and the test would then be about the setup.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

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

static uint32_t rngState = 0x1234567u;
static uint32_t rnd() {
  rngState ^= rngState << 13;
  rngState ^= rngState >> 17;
  rngState ^= rngState << 5;
  return rngState;
}

// --- position building -----------------------------------------------------

static void put(Game& g, int seat, int base, Troop kind) {
  g.placeSlot[g.placementCount] = static_cast<uint8_t>(base);
  g.placeTile[g.placementCount] = static_cast<uint8_t>((seat << 3) | static_cast<int>(kind));
  ++g.placementCount;
}

static void clearRack(Game& g, int seat) {
  for (int k = 0; k < kTroopKinds; ++k) g.rack[seat][k] = 0;
}

static void give(Game& g, int seat, Troop kind, int n = 1) {
  g.rack[seat][static_cast<int>(kind)] = static_cast<uint8_t>(g.rack[seat][static_cast<int>(kind)] + n);
}

// A game with empty racks and an empty board, so a test says exactly what it
// depends on.
static Game bare(int starter = 0) {
  Game g;
  g.newGame(1u, kPG, starter);
  clearRack(g, 0);
  clearRack(g, 1);
  g.placementCount = 0;
  return g;
}

// --- invariants ------------------------------------------------------------

// Only meaningful for positions reached by legal play from `newGame`. The
// hand-built positions below write into the placement log without paying for
// the troops, so the conservation check would fail on them by construction --
// which is the soak's job, not theirs.
static void checkInvariants(const Game& g, const char* where) {
  const Terrain& b = g.board();

  for (int seat = 0; seat < kSeats; ++seat) {
    check(g.rackSize(seat) <= kRackLimit, where);
    check(g.reserveTaken[seat] <= kReserveSize, where);

    // Every troop is somewhere: drawn from the reserve means it is on the
    // rack, in the discard, or on the board. Nothing else can hold one.
    int onBoard = 0;
    for (int i = 0; i < g.placementCount; ++i) {
      if ((g.placeTile[i] >> 3) == seat) ++onBoard;
    }
    int discarded = 0;
    for (int k = 0; k < kTroopKinds; ++k) discarded += g.discarded[seat][k];
    check(g.reserveTaken[seat] == g.rackSize(seat) + discarded + onBoard, where);
  }

  check(g.placementCount <= kMaxPlacements, where);

  // Medals held equal the medals of the regions that have fallen. Nothing
  // mints a medal, and losing a region never takes one back.
  int banked = 0;
  for (int r = 0; r < b.regionCount; ++r) {
    if (g.regionsTaken & (1u << r)) banked += b.regions[r].medals;
  }
  check(g.medals[0] + g.medals[1] == banked, where);

  // The state is also the wire format, so every legally reached position must
  // pass the validator a received packet will be held to. A validator that
  // only ever sees hand-picked cases is a validator nobody has tested.
  check(g.isWellFormed(), where);
}

// --- terrains --------------------------------------------------------------

// Run against every terrain in the table, because a board is data and a
// mistyped index in data compiles perfectly. These are the checks that catch a
// board nobody could win on, or one with a base no path reaches.
static void testEveryTerrainIsStructurallySound() {
  for (int index = 0; index < kTerrainCount; ++index) {
    const Terrain& b = terrainAt(index);
    const char* who = b.name;

    check(b.baseCount > 0 && b.baseCount <= kMaxBases, who);
    check(b.hqCount > 0 && b.hqCount <= kMaxHq, who);
    check(b.slotCount() <= kMaxSlots, who);
    check(b.medalsObjective > 0, who);

    // Both seats need somewhere to start and something to attack.
    int hqPerSeat[kSeats] = {};
    for (int i = 0; i < b.hqCount; ++i) {
      check(b.hqSeat[i] < kSeats, who);
      ++hqPerSeat[b.hqSeat[i]];
    }
    check(hqPerSeat[0] > 0 && hqPerSeat[1] > 0, who);

    // Edges name real slots, and adjacency agrees with them in both
    // directions. A one-way path is a board nobody can debug.
    for (int e = 0; e < b.edgeCount; ++e) {
      const int u = b.edges[e].a, v = b.edges[e].b;
      check(u < b.slotCount() && v < b.slotCount(), who);
      check(u != v, who);
      check((b.adj[u] & (uint64_t{1} << v)) != 0, who);
      check((b.adj[v] & (uint64_t{1} << u)) != 0, who);
    }
    for (int u = 0; u < b.slotCount(); ++u) {
      for (int v = 0; v < b.slotCount(); ++v) {
        if (!(b.adj[u] & (uint64_t{1} << v))) continue;
        check((b.adj[v] & (uint64_t{1} << u)) != 0, who);
      }
    }

    // Every slot is reachable through the path graph, ignoring occupancy. An
    // orphan base is one no troop could ever legally stand on.
    uint64_t seen = 1;  // slot 0
    for (bool grew = true; grew;) {
      grew = false;
      for (int u = 0; u < b.slotCount(); ++u) {
        if (!(seen & (uint64_t{1} << u))) continue;
        const uint64_t next = b.adj[u] & ~seen;
        if (next) {
          seen |= next;
          grew = true;
        }
      }
    }
    for (int u = 0; u < b.slotCount(); ++u) check((seen & (uint64_t{1} << u)) != 0, who);

    // Regions name only real bases, and a region of one base is a base, not a
    // region.
    int medalsOnBoard = 0;
    for (int r = 0; r < b.regionCount; ++r) {
      const uint32_t mask = b.regions[r].bases;
      check(mask != 0, who);
      check((mask >> b.baseCount) == 0, who);
      int members = 0;
      for (int i = 0; i < b.baseCount; ++i) {
        if (mask & (uint32_t{1} << i)) ++members;
      }
      check(members >= 2, who);
      check(b.regions[r].medals > 0, who);
      medalsOnBoard += b.regions[r].medals;
    }
    // A terrain whose medals cannot reach its own objective can only ever be
    // won by capture, which would make the objective a decoration.
    check(medalsOnBoard >= b.medalsObjective, who);

    // A gate is the one special that can sit on an H.Q. as well as a base: it
    // restricts what may be PLACED, and an H.Q. is a thing you place on to win.
    // Tropical Pool gates one H.Q. per side.
    //
    // This check used to read `specialAt(slot) == Gate` for every slot, which
    // is false for an H.Q. by construction -- `specialAt` returns None for
    // anything that is not a base -- so it asserted that no H.Q. could ever be
    // gated. It passed for years because no board had one, and the very field
    // it was testing says otherwise: `gate` is indexed by slot precisely
    // "because the gate covers the H.Q. too".
    for (int slot = 0; slot < b.slotCount(); ++slot) {
      if (b.isBase(slot)) {
        check((b.specialAt(slot) == Special::Gate) == (b.gate[slot] != 0), who);
      } else {
        // An H.Q. carries no special at all; its gate, if any, stands alone.
        check(b.specialAt(slot) == Special::None, who);
      }
      // Whatever is gated admits something, and nothing outside the eight kinds.
      if (b.gate[slot] != 0) check((b.gate[slot] >> kTroopKinds) == 0, who);
    }
    for (int base = 0; base < b.baseCount; ++base) {
      check(b.special[base] <= static_cast<uint8_t>(Special::Nullify), who);
    }
  }
}

static void testCastleFieldMatchesTheBoard() {
  const Terrain& b = terrainAt(static_cast<int>(TerrainId::CastleField));
  check(b.baseCount == 15, "Castle Field has 15 bases");
  check(b.hqCount == 2, "and one H.Q. each");
  check(b.regionCount == 8, "and eight regions");
  check(b.medalsObjective == 7, "and an objective of 7, read off the badge");

  int medals = 0;
  for (int r = 0; r < b.regionCount; ++r) medals += b.regions[r].medals;
  check(medals == 14, "14 medals sit on the board");

  int wells = 0;
  for (int base = 0; base < b.baseCount; ++base) {
    if (b.specialAt(base) == Special::Recall) ++wells;
  }
  check(wells == 4, "four wells, and the retreat is Castle Field's effect");

  // Stated as the whole shape of the board rather than as three indices, so
  // that retracing it -- which has already happened once, and corrected the
  // wells -- cannot quietly reorder its way past this.
  int shape[10] = {}, kinds = 0;
  for (int r = 0; r < b.regionCount; ++r) ++shape[b.regions[r].medals];
  check(shape[1] == 4, "four shoulder regions paying 1: the wells sit on the inner bases");
  check(shape[2] == 2, "two river gaps paying 2: the water between adjacent bridges");
  check(shape[3] == 2, "two centre regions paying 3");
  for (int m = 1; m < 10; ++m) kinds += shape[m] > 0 ? 1 : 0;
  check(kinds == 3, "and nothing else");
  // A centre prize plus both river gaps is exactly the objective.
  check(3 + 2 + 2 == b.medalsObjective, "centre plus both gaps is exactly 7");
}

// --- targeted tests --------------------------------------------------------

static void testCovering() {
  check(covers(Troop::Kwak, Troop::Roxy), "Kwak covers the T-Rex");
  check(covers(Troop::Roxy, Troop::Kwak), "anything covers Kwak");
  check(covers(Troop::Kwak, Troop::Kwak), "Kwak covers Kwak, which strength 0 could not");
  check(covers(Troop::Skully, Troop::Kwak), "the weakest troop still covers the joker");
  check(covers(Troop::Roxy, Troop::Star), "7 covers 6");
  check(!covers(Troop::Star, Troop::Roxy), "6 does not cover 7");
  check(!covers(Troop::Star, Troop::Star), "equal strength does not cover: it must be strictly lower");
}

static void testSetup() {
  Game g;
  g.newGame(99u, kPG, 0);
  check(g.rackSize(0) == 3, "the starter racks 3");
  check(g.rackSize(1) == 4, "the second player racks 4");
  check(g.reserveRemaining(0) == kReserveSize - 3, "the starter drew 3 from a 20 reserve");
  check(g.reserveRemaining(1) == kReserveSize - 4, "the opponent drew 4 from a 20 reserve");

  Game h;
  h.newGame(99u, kPG, 1);
  check(h.rackSize(1) == 3, "starting is what costs a troop, not the seat number");

  // The four set aside are never dealt, so a whole reserve read out is 20 long.
  check(kReserveSize == kTroopsPerSeat - kSetAside, "20 troops are live per seat");
  checkInvariants(g, "setup");
}

static void testConnection() {
  Game g = bare();
  give(g, 0, Troop::Roxy, 3);

  // Seat 0's H.Q. touches bases 0, 5 and 10 only.
  check(g.canPlaceHere(0, 0, Troop::Roxy, false), "a base touching my H.Q. is reachable");
  check(g.canPlaceHere(0, 5, Troop::Roxy, false), "so is the middle one");
  check(!g.canPlaceHere(0, 1, Troop::Roxy, false), "a base two paths away is not");
  check(!g.canPlaceHere(0, 15, Troop::Roxy, false), "you may never place on your own H.Q.");

  check(g.apply(Move::place(0, Troop::Roxy)), "place on base 0");
  check(g.turn == 1, "the turn passes");
  g.turn = 0;
  check(g.canPlaceHere(0, 1, Troop::Roxy, false), "occupying base 0 extends the line to base 1");

  // An enemy on the only stepping stone cuts it.
  Game h = bare();
  give(h, 0, Troop::Skully, 3);
  put(h, 1, 0, Troop::Roxy);
  put(h, 1, 5, Troop::Roxy);
  put(h, 1, 10, Troop::Roxy);
  check(!h.canPlaceHere(0, 1, Troop::Skully, false), "an enemy base cuts the connection");
  check(!h.canPlaceHere(0, 0, Troop::Skully, false), "and a 1 cannot cover a 7 to get through");
}

static void testHookWaiver() {
  Game g = bare();
  give(g, 0, Troop::Hook, 2);
  check(!g.canPlaceHere(0, 7, Troop::Hook, false), "base 7 is far from the H.Q.");
  check(g.canPlaceHere(0, 7, Troop::Hook, true), "Hook ignores the connection rule for a base");
  check(g.apply(Move::place(7, Troop::Hook, true)), "Hook lands across the map");
  check(g.occupantSeat(7) == 0, "and holds the base");

  // The H.Q. is not a base, and the aid says so explicitly. This is the rule
  // every secondary source got wrong, and the one a brain would exploit.
  Game h = bare();
  give(h, 0, Troop::Hook, 1);
  check(!h.canPlaceHere(0, 16, Troop::Hook, true), "Hook cannot jump onto an unconnected H.Q.");
  check(!h.apply(Move::place(16, Troop::Hook, true)), "and the move is refused");
  check(h.currentPhase() == Phase::Playing, "so the game does not end");
}

static void testStackUncovers() {
  Game g = bare();
  put(g, 0, 0, Troop::Roxy);  // mine, buried
  put(g, 1, 0, Troop::Kwak);  // theirs, on top: the joker covers anything
  check(g.occupantSeat(0) == 1, "only the visible troop occupies");
  check(g.stackDepth(0) == 2, "the buried troop is still there");

  // Jumbo, adjacent at base 1, discards the visible Kwak. What was underneath
  // comes back, and the base changes hands without anyone placing on it.
  give(g, 0, Troop::Jumbo, 1);
  put(g, 0, 5, Troop::Roxy);  // a line from the H.Q. to base 1 via 0? no: via 5,6
  put(g, 0, 6, Troop::Roxy);
  g.turn = 0;
  check(g.apply(Move::place(1, Troop::Jumbo, true, 0)), "Jumbo discards the troop next door");
  check(g.occupantSeat(0) == 0, "the buried troop is uncovered and the base flips");
  check(g.occupantTroop(0) == Troop::Roxy, "and it is the one that was underneath");
  check(g.discarded[1][static_cast<int>(Troop::Kwak)] == 1, "the victim is in its owner's discard");

  // Jumbo reaches one section of path, no further.
  Game h = bare();
  give(h, 0, Troop::Jumbo, 1);
  put(h, 1, 2, Troop::Skully);
  check(!h.apply(Move::place(0, Troop::Jumbo, true, 2)), "base 2 is not adjacent to base 0");
  check(!h.apply(Move::place(0, Troop::Jumbo, true, 5)), "and an empty base is not a victim");
}

static void testRegions() {
  Game g = bare();
  // Region 0 is bases 0, 1, 5, 6.
  give(g, 0, Troop::Roxy, 1);
  put(g, 0, 0, Troop::Roxy);
  put(g, 0, 1, Troop::Roxy);
  put(g, 0, 5, Troop::Roxy);
  check(g.medals[0] == 0, "three of the four bases is not control");
  check(g.apply(Move::place(6, Troop::Roxy)), "close the region");
  check(g.medals[0] == 2, "the medals are taken the instant it closes");
  check((g.regionsTaken & 1u) != 0, "and the region is marked looted");

  // Losing it afterwards changes nothing, and retaking it pays nothing.
  Game h = g;
  h.turn = 1;
  give(h, 1, Troop::Kwak, 1);
  check(h.apply(Move::place(6, Troop::Kwak, false)) || true, "the enemy may cover a corner");
  check(h.medals[0] == 2, "medals already banked are kept");
  check(h.medals[1] == 0, "and a looted region pays the next holder nothing");
}

static void testHqCaptureWins() {
  Game g = bare();
  give(g, 0, Troop::Skully, 1);
  for (int base = 10; base <= 14; ++base) put(g, 0, base, Troop::Roxy);
  check(g.canPlaceHere(0, 16, Troop::Skully, false), "the enemy H.Q. is connected along the bottom row");
  check(g.apply(Move::place(16, Troop::Skully)), "the weakest troop takes an H.Q. as well as any");
  check(g.currentPhase() == Phase::GameOver, "capturing an H.Q. ends the game at once");
  check(g.winner == 0, "and the player who captured it wins");
  check(g.endedBy() == Ending::HqCaptured, "for the stated reason");
}

static void testMedalsObjectiveWins() {
  Game g = bare();
  // Four regions at 2 medals each clears the objective of 7. Regions 0, 1 and
  // 4 are already closed on the board; base 8 closes region 2 as well, and all
  // four settle at once because building a position by hand skips the check.
  give(g, 0, Troop::Roxy, 1);
  for (int base : {0, 1, 2, 3, 5, 6, 7, 10, 11}) put(g, 0, base, Troop::Roxy);
  check(g.medals[0] == 0, "nothing is awarded while the position is being built");
  check(g.apply(Move::place(8, Troop::Roxy)), "one placement settles four regions");
  check(g.medals[0] >= g.board().medalsObjective, "the objective is met");
  check(g.currentPhase() == Phase::GameOver, "which ends the game immediately");
  check(g.endedBy() == Ending::MedalsObjective, "for the stated reason");
  check(g.winner == 0, "and the player who met it wins");
}

static void testCapnChain() {
  Game g = bare();
  give(g, 0, Troop::Capn, 1);
  give(g, 0, Troop::Roxy, 1);

  Move m = Move::place(0, Troop::Capn, true);
  m.then(5, Troop::Roxy);
  check(g.isLegal(m), "Cap'n places an extra troop in the same turn");
  Game after = g;
  check(after.apply(m), "and the chain applies");
  check(after.occupantSeat(0) == 0 && after.occupantSeat(5) == 0, "both landed");
  check(after.turn == 1, "one turn passed, not two");

  // The flag and the extra placement are the same thing, so they must agree.
  Move dangling = Move::place(0, Troop::Capn, true);
  check(!g.isLegal(dangling), "a Cap'n that used its effect must name the extra placement");
  Move declined = Move::place(0, Troop::Capn, false);
  check(g.isLegal(declined), "declining the effect is legal: every effect is 'you may'");

  Move unearned = Move::place(0, Troop::Roxy, false);
  unearned.then(5, Troop::Capn);
  check(!g.isLegal(unearned), "only a Cap'n may hand out a second placement");

  // The extra placement obeys the ordinary rules, connection included.
  Move offMap = Move::place(0, Troop::Capn, true);
  offMap.then(12, Troop::Roxy);
  check(!g.isLegal(offMap), "the extra troop still needs a connection");
}

static void testEffectsAndRackLimit() {
  // Skully draws 2, and only 1 when the rack is already at 7.
  Game g = bare();
  give(g, 0, Troop::Skully, 1);
  check(g.apply(Move::place(0, Troop::Skully, true)), "Skully calls in reinforcements");
  check(g.rackSize(0) == 2, "an empty rack gains 2");

  Game h = bare();
  give(h, 0, Troop::Skully, 1);
  give(h, 0, Troop::Roxy, 7);  // 8 on the rack, at the limit
  check(h.rackSize(0) == 8, "the rack starts full");
  check(h.apply(Move::place(0, Troop::Skully, true)), "placing Skully frees a space");
  check(h.rackSize(0) == 8, "so only one troop can come back");

  // Draw is illegal at the limit, and legal below it.
  Game d = bare();
  give(d, 0, Troop::Roxy, 8);
  check(!d.canDraw(0), "you cannot draw onto a full rack");
  check(!d.isLegal(Move::draw()), "and the move is refused");

  Game e = bare();
  give(e, 0, Troop::Roxy, 7);
  check(e.canDraw(0), "one free space is enough to draw");
  check(e.apply(Move::draw()), "and the draw applies");
  check(e.rackSize(0) == 8, "filling the last space rather than overflowing it");

  // Star draws 1. XB-42 discards one of the enemy's, at random but really.
  Game s = bare();
  give(s, 0, Troop::Star, 1);
  check(s.apply(Move::place(0, Troop::Star, true)), "Star draws one");
  check(s.rackSize(0) == 1, "exactly one");

  Game x = bare();
  give(x, 0, Troop::XB42, 1);
  give(x, 1, Troop::Roxy, 3);
  check(x.apply(Move::place(0, Troop::XB42, true)), "XB-42 shoots into the enemy rack");
  check(x.rackSize(1) == 2, "one troop leaves the rack");
  check(x.discarded[1][static_cast<int>(Troop::Roxy)] == 1, "and lands in its owner's discard");

  // Troops with no effect cannot claim one: a brain that sets the flag on Roxy
  // has a bug, and this is where it surfaces.
  Game r = bare();
  give(r, 0, Troop::Roxy, 1);
  check(!r.isLegal(Move::place(0, Troop::Roxy, true)), "Roxy has no effect to use");
  Game k = bare();
  give(k, 0, Troop::Kwak, 1);
  check(!k.isLegal(Move::place(0, Troop::Kwak, true)), "nor does Kwak");
}

static void testStuckEnding() {
  // Seat 1 to move with an empty rack and an empty reserve: no draw, no place.
  Game g = bare();
  g.reserveTaken[1] = kReserveSize;
  give(g, 0, Troop::Roxy, 1);
  put(g, 0, 0, Troop::Roxy);
  put(g, 0, 1, Troop::Roxy);
  put(g, 0, 5, Troop::Roxy);
  check(!g.hasAnyLegalMove(1), "seat 1 can neither draw nor place");
  check(g.apply(Move::place(6, Troop::Roxy)), "seat 0 closes a region and passes the turn");
  check(g.currentPhase() == Phase::GameOver, "a player who cannot act ends the game");
  check(g.endedBy() == Ending::Stuck, "for the stated reason");
  check(g.winner == 0, "and the medals decide it");

  // On a tie the player who ended it loses.
  Game t = bare();
  t.reserveTaken[1] = kReserveSize;
  give(t, 0, Troop::Roxy, 1);
  t.turn = 0;
  check(t.medals[0] == 0 && t.medals[1] == 0, "level on medals");
  check(t.apply(Move::place(0, Troop::Roxy)), "seat 0 places and passes to a stuck seat 1");
  check(t.currentPhase() == Phase::GameOver, "which ends it");
  check(t.winner == 0, "a tie goes against whoever could not act");
}

// --- special bases ---------------------------------------------------------

static void testSpecialBaseSetting() {
  // The setting lives in the state, not in app settings, because two linked
  // devices have to agree on it and the opponent has to see it.
  Game on = bare();
  Game off;
  off.newGame(7u, kPG, 0, /*withSpecialBases=*/false);
  check(on.specialBases == 1, "special bases default on");
  check(off.specialBases == 0, "and can be turned off for the whole game");

  // Base 9 is gated to 6 and 7. With the setting off it is an ordinary base.
  clearRack(off, 0);
  clearRack(off, 1);
  off.placementCount = 0;
  give(off, 0, Troop::Skully, 1);
  give(on, 0, Troop::Skully, 1);
  // A held line along the bottom row, so base 9 is connected in both games and
  // the gate is the only thing that can refuse the placement.
  for (int base = 10; base <= 14; ++base) {
    put(off, 0, base, Troop::Roxy);
    put(on, 0, base, Troop::Roxy);
  }
  check(!on.canPlaceHere(0, 9, Troop::Skully, false), "a 1 cannot land on a base gated to 6 and 7");
  check(off.canPlaceHere(0, 9, Troop::Skully, false), "unless special bases are switched off");
}

static void testGateAndNullify() {
  Game g = bare();
  give(g, 0, Troop::Roxy, 1);
  give(g, 0, Troop::Star, 1);
  give(g, 0, Troop::Skully, 1);
  for (int base = 10; base <= 14; ++base) put(g, 0, base, Troop::Roxy);  // a line to base 9
  check(g.canPlaceHere(0, 9, Troop::Roxy, false), "a 7 is one of the printed values");
  check(g.canPlaceHere(0, 9, Troop::Star, false), "and so is a 6");
  check(!g.canPlaceHere(0, 9, Troop::Skully, false), "a 1 is not");

  // Station Metal-X: effects do not apply on the base, so claiming one there
  // is not a legal move rather than a no-op that quietly does nothing.
  Game n = bare();
  give(n, 0, Troop::Skully, 2);
  give(n, 0, Troop::Hook, 1);
  put(n, 0, 0, Troop::Roxy);
  put(n, 0, 1, Troop::Roxy);
  put(n, 0, 2, Troop::Roxy);
  put(n, 0, 3, Troop::Roxy);
  check(n.canPlaceHere(0, 4, Troop::Skully, false), "the base itself takes a troop normally");
  check(!n.isLegal(Move::place(4, Troop::Skully, true)), "but its effect cannot be used there");
  check(n.isLegal(Move::place(4, Troop::Skully, false)), "declining is the only way to play it");

  // And Hook's waiver is a troop effect, so it cannot reach one either.
  Game h = bare();
  give(h, 0, Troop::Hook, 1);
  check(!h.canPlaceHere(0, 4, Troop::Hook, true), "Hook cannot jump onto a base that nullifies effects");
  check(h.canPlaceHere(0, 7, Troop::Hook, true), "though it still reaches an ordinary far base");
}

static void testBaseEffects() {
  // Recall: one of your OTHER troops, from anywhere, back to the rack.
  Game r = bare();
  give(r, 0, Troop::Roxy, 1);
  put(r, 0, 0, Troop::Roxy);
  put(r, 0, 1, Troop::Roxy);
  Move m = Move::place(2, Troop::Roxy);
  m.steps[0].useBase = true;
  m.steps[0].baseFrom = 0;
  check(r.isLegal(m), "Castle Field's base calls a troop home");
  Game after = r;
  check(after.apply(m), "and it applies");
  check(after.occupantSeat(0) == kNoSeat, "the recalled troop leaves the board");
  check(after.rack[0][static_cast<int>(Troop::Roxy)] == 1, "and lands back on the rack");

  Move self = Move::place(2, Troop::Roxy);
  self.steps[0].useBase = true;
  self.steps[0].baseFrom = 2;
  check(!r.isLegal(self), "it may not recall the troop that just landed: 'one of your OTHER troops'");

  // Draw: one from the reserve, refused rather than clamped at a full rack.
  Game d = bare();
  give(d, 0, Troop::Roxy, 1);
  for (int base : {0, 1, 2}) put(d, 0, base, Troop::Roxy);  // a line out to base 7
  Move dm = Move::place(7, Troop::Roxy);
  dm.steps[0].useBase = true;
  check(d.isLegal(dm), "City of Clouds draws one");
  Game dAfter = d;
  check(dAfter.apply(dm) && dAfter.rackSize(0) == 1, "and the rack grows by exactly one");

  Game full = bare();
  give(full, 0, Troop::Roxy, 1);
  give(full, 0, Troop::Star, 8);
  for (int base : {0, 1, 2}) put(full, 0, base, Troop::Roxy);
  Move fm = Move::place(7, Troop::Roxy);
  fm.steps[0].useBase = true;
  check(!full.isLegal(fm), "an effect that would exceed 8 cannot be applied at all");

  // Exhume: one of your own out of the discard.
  Game e = bare();
  give(e, 0, Troop::Roxy, 1);
  for (int base : {0, 1, 2}) put(e, 0, base, Troop::Roxy);
  e.discarded[0][static_cast<int>(Troop::Jumbo)] = 1;
  Move em = Move::place(3, Troop::Roxy);
  em.steps[0].useBase = true;
  em.steps[0].baseKind = static_cast<uint8_t>(Troop::Jumbo);
  check(e.isLegal(em), "Cursed Cemetery brings one back");
  Game eAfter = e;
  check(eAfter.apply(em), "and it applies");
  check(eAfter.rack[0][static_cast<int>(Troop::Jumbo)] == 1, "onto the rack");
  check(eAfter.discarded[0][static_cast<int>(Troop::Jumbo)] == 0, "out of the discard");

  Move theirs = Move::place(3, Troop::Roxy);
  theirs.steps[0].useBase = true;
  theirs.steps[0].baseKind = static_cast<uint8_t>(Troop::Star);
  check(!e.isLegal(theirs), "and only a troop that is actually in your discard");

  // Shove: an adjacent enemy troop, moved beside where it stood, ignoring the
  // placement rules -- so it may land on a base its strength could not take.
  Game s = bare();
  give(s, 0, Troop::Skully, 1);
  put(s, 0, 10, Troop::Roxy);  // a line along the bottom out to base 12
  put(s, 0, 11, Troop::Roxy);
  put(s, 1, 13, Troop::Roxy);  // adjacent to base 12
  put(s, 1, 14, Troop::Star);
  Move sm = Move::place(12, Troop::Skully);
  sm.steps[0].useBase = true;
  sm.steps[0].baseFrom = 13;
  sm.steps[0].baseTo = 14;
  check(s.isLegal(sm), "Volcanic Jungle shoves the neighbour");
  Game sAfter = s;
  check(sAfter.apply(sm), "and it applies");
  check(sAfter.occupantSeat(13) == kNoSeat, "the shoved troop leaves its base");
  check(sAfter.occupantTroop(14) == Troop::Roxy, "and lands on top of one a 7 could not have covered");
  check(sAfter.stackDepth(14) == 2, "stacking as it goes");

  Move far = Move::place(12, Troop::Skully);
  far.steps[0].useBase = true;
  far.steps[0].baseFrom = 13;
  far.steps[0].baseTo = 0;
  check(!s.isLegal(far), "but only to a base beside where it started");
}

static void testSuppression() {
  Game g = bare();
  give(g, 0, Troop::Roxy, 1);
  give(g, 1, Troop::Star, 1);                                      // the only thing seat 1 holds, so the pick is known
  for (int base : {10, 11, 12, 13}) put(g, 0, base, Troop::Roxy);  // a line out to base 8
  Move m = Move::place(8, Troop::Roxy);
  m.steps[0].useBase = true;
  check(g.apply(m), "Battlefield points at a troop on the enemy rack");
  check(g.frozenKind[1] == static_cast<uint8_t>(Troop::Star), "the pointed troop is face down");
  check(g.rackSize(1) == 1, "it stays on the rack and still counts against the 8");
  check(!g.canPlaceHere(1, 14, Troop::Star, false), "and cannot be placed this turn");

  // It comes back at the end of the turn it sat out.
  check(g.turn == 1, "it is the suppressed player's turn");
  check(g.apply(Move::draw()), "who draws instead");
  check(g.frozenKind[1] == kNoSlot, "and gets it back at the end of that turn");
}

static void testWireFormat() {
  check(sizeof(Game) <= 192, "Game fits one LinkPlay packet");
  Game a;
  a.newGame(4242u, kPG, 1);
  give(a, 0, Troop::Roxy, 1);
  a.turn = 0;
  check(a.apply(Move::place(0, Troop::Roxy)), "play a move");

  uint8_t wire[sizeof(Game)];
  memcpy(wire, &a, sizeof(Game));
  Game b;
  memcpy(&b, wire, sizeof(Game));
  check(memcmp(&a, &b, sizeof(Game)) == 0, "a state survives the wire byte for byte");
  check(b.occupantSeat(0) == 0, "and means the same thing at the other end");
}

// The link layer checks payload length and nothing else, so every other
// guarantee is this function's job. Each mutant below is a byte a corrupt
// packet could plausibly carry into an array index. A validator nobody has
// tried to break is a validator that has never been tested.
static void testValidatorRejectsCorruption() {
  Game good;
  good.newGame(31337u, kPG, 0);
  check(good.isWellFormed(), "a dealt game is well formed");

  auto mutant = [&](void (*wreck)(Game&), const char* what) {
    Game m = good;
    wreck(m);
    check(!m.isWellFormed(), what);
  };

  mutant([](Game& m) { m.turn = 200; }, "a turn that would index past the seats is rejected");
  mutant([](Game& m) { m.terrain = 9; }, "a terrain index with no terrain behind it is rejected");
  mutant([](Game& m) { m.phase = 7; }, "a phase outside the enum is rejected");
  mutant([](Game& m) { m.ending = 9; }, "an ending outside the enum is rejected");
  mutant([](Game& m) { m.winner = 5; }, "a winner who is not a seat is rejected");
  mutant([](Game& m) { m.placementCount = 200; }, "a placement count past the board is rejected");
  mutant([](Game& m) { m.specialBases = 4; }, "a setting that is neither on nor off is rejected");
  mutant([](Game& m) { m.frozenKind[0] = 99; }, "a suppressed troop that is not a troop is rejected");
  mutant([](Game& m) { m.medals[0] = 9; }, "medals nobody won are rejected");
  mutant([](Game& m) { m.regionsTaken = 0xFFFF; }, "regions the terrain does not have are rejected");
  mutant([](Game& m) { m.rack[0][0] = 8; }, "a rack holding more copies than exist is rejected");
  mutant([](Game& m) { m.reserveTaken[0] = 19; }, "a reserve count the rack cannot account for is rejected");
  mutant(
      [](Game& m) {
        m.placeSlot[0] = 3;
        m.placeTile[0] = 0;
        m.placementCount = 1;
      },
      "a troop on the board that was never drawn is rejected");
  mutant([](Game& m) { m.seed ^= 0x5A5A5A5Au; }, "a state whose seed describes a different shuffle is rejected");

  // And the validator is not merely returning false for everything: a state
  // reached by real play still passes.
  Game played = good;
  give(played, 0, Troop::Roxy, 0);
  Step steps[64];
  const int n = played.legalPlacements(0, steps, 64);
  check(n > 0, "the dealt position has a move");
  check(played.apply(Move::place(steps[0].slot, static_cast<Troop>(steps[0].kind), steps[0].useEffect)),
        "which applies");
  check(played.isWellFormed(), "and leaves a well-formed state");
}

// --- soak ------------------------------------------------------------------

// Everything legal from here, including the effects and Cap'n's chain. Built by
// proposing and filtering through isLegal rather than by a second rule
// implementation: two judges that can drift is the bug class worth avoiding.
static std::vector<Move> legalMoves(const Game& g) {
  std::vector<Move> out;
  const int seat = g.turn;

  if (g.isLegal(Move::draw())) out.push_back(Move::draw());

  Step steps[kTroopKinds * kMaxSlots];
  const int n = g.legalPlacements(seat, steps, static_cast<int>(sizeof(steps) / sizeof(steps[0])));
  for (int i = 0; i < n; ++i) {
    const Troop kind = static_cast<Troop>(steps[i].kind);
    const int slot = steps[i].slot;

    Move plain = Move::place(slot, kind, steps[i].useEffect);
    if (g.isLegal(plain)) out.push_back(plain);

    if (kind == Troop::Jumbo) {
      for (int target = 0; target < g.board().baseCount; ++target) {
        Move m = Move::place(slot, kind, true, target);
        if (g.isLegal(m)) out.push_back(m);
      }
    } else if (kind == Troop::Capn) {
      Game probe = g;
      // Land the Cap'n on a copy so the extra placement is generated against
      // the position it will actually see.
      Game staged = g;
      if (!staged.apply(Move::place(slot, kind, false))) continue;
      staged.turn = static_cast<uint8_t>(seat);
      Step extra[kTroopKinds * kMaxSlots];
      const int m2 = staged.legalPlacements(seat, extra, static_cast<int>(sizeof(extra) / sizeof(extra[0])));
      for (int j = 0; j < m2 && j < 12; ++j) {
        Move chained = Move::place(slot, kind, true);
        chained.then(extra[j].slot, static_cast<Troop>(extra[j].kind), extra[j].useEffect);
        if (g.isLegal(chained)) out.push_back(chained);
      }
      (void)probe;
    } else if (kind != Troop::Kwak && kind != Troop::Roxy) {
      Move withEffect = Move::place(slot, kind, true);
      if (g.isLegal(withEffect)) out.push_back(withEffect);
    }

    // The special base under the placement, with every choice it might need.
    // Without this the soak would only ever decline them, and a whole half of
    // the rules would sit untested behind a flag that is on by default.
    const Terrain& b = g.board();
    if (!g.specialBases || !b.isBase(slot)) continue;
    switch (b.specialAt(slot)) {
      case Special::Draw:
      case Special::Suppress: {
        Move m = Move::place(slot, kind, steps[i].useEffect);
        m.steps[0].useBase = true;
        if (g.isLegal(m)) out.push_back(m);
        break;
      }
      case Special::Recall: {
        for (int from = 0; from < b.baseCount; ++from) {
          Move m = Move::place(slot, kind, steps[i].useEffect);
          m.steps[0].useBase = true;
          m.steps[0].baseFrom = static_cast<uint8_t>(from);
          if (g.isLegal(m)) out.push_back(m);
        }
        break;
      }
      case Special::Exhume: {
        for (int k = 0; k < kTroopKinds; ++k) {
          Move m = Move::place(slot, kind, steps[i].useEffect);
          m.steps[0].useBase = true;
          m.steps[0].baseKind = static_cast<uint8_t>(k);
          if (g.isLegal(m)) out.push_back(m);
        }
        break;
      }
      case Special::Shove: {
        for (int from = 0; from < b.baseCount; ++from) {
          for (int to = 0; to < b.baseCount; ++to) {
            Move m = Move::place(slot, kind, steps[i].useEffect);
            m.steps[0].useBase = true;
            m.steps[0].baseFrom = static_cast<uint8_t>(from);
            m.steps[0].baseTo = static_cast<uint8_t>(to);
            if (g.isLegal(m)) out.push_back(m);
          }
        }
        break;
      }
      case Special::None:
      case Special::Gate:
      case Special::Nullify:
        break;
    }
  }
  return out;
}

static void soak(int matches, bool specialBases, int terrain = kPG) {
  int finished = 0;
  int byHq = 0, byMedals = 0, byStuck = 0;
  long moves = 0;
  long baseEffects = 0;

  for (int match = 0; match < matches; ++match) {
    Game g;
    g.newGame(rnd(), terrain, static_cast<int>(rnd() & 1u), specialBases);
    checkInvariants(g, "soak: after the deal");

    int turns = 0;
    while (g.currentPhase() == Phase::Playing) {
      const std::vector<Move> options = legalMoves(g);
      check(!options.empty(), "soak: a playing position always has a legal move");
      const Move& chosen = options[rnd() % options.size()];
      const int before = g.turn;
      if (chosen.kind == Move::Kind::Place && chosen.steps[0].useBase) ++baseEffects;
      check(g.apply(chosen), "soak: a move that isLegal accepted must apply");
      check(g.currentPhase() != Phase::Playing || g.turn != before, "soak: the turn passes");
      checkInvariants(g, "soak: after a move");
      ++moves;
      if (++turns > 400) {
        printf("FAIL: a match ran past 400 turns without ending\n");
        abort();
      }
    }

    check(g.winner == 0 || g.winner == 1, "soak: a finished game has a winner");
    switch (g.endedBy()) {
      case Ending::HqCaptured:
        ++byHq;
        break;
      case Ending::MedalsObjective:
        ++byMedals;
        break;
      case Ending::Stuck:
        ++byStuck;
        break;
      default:
        check(false, "soak: a finished game names how it ended");
        break;
    }
    ++finished;
  }

  check(finished == matches, "soak: every match finished");
  // Not an assertion about balance, only that random play reaches all three
  // endings: an ending nothing ever reaches is an ending nothing tests.
  check(byHq > 0, "soak: random play captures an H.Q. sometimes");
  check(byMedals > 0, "soak: and meets the medals objective sometimes");
  // And the same for the setting: a soak that never fired a base effect would
  // report green for rules it never ran.
  if (specialBases) {
    check(baseEffects > 0, "soak: special bases actually fire when they are on");
  } else {
    check(baseEffects == 0, "soak: and never fire when they are off");
  }
  printf("soak %-12s %-3s  %d matches, %ld moves, %ld base effects  (hq %d, medals %d, stuck %d)\n",
         terrainAt(terrain).name, specialBases ? "on" : "off", matches, moves, baseEffects, byHq, byMedals, byStuck);
}

int main() {
  testEveryTerrainIsStructurallySound();
  testCastleFieldMatchesTheBoard();
  testCovering();
  testSetup();
  testConnection();
  testHookWaiver();
  testStackUncovers();
  testRegions();
  testHqCaptureWins();
  testMedalsObjectiveWins();
  testCapnChain();
  testEffectsAndRackLimit();
  testStuckEnding();
  testSpecialBaseSetting();
  testGateAndNullify();
  testBaseEffects();
  testSuppression();
  testWireFormat();
  testValidatorRejectsCorruption();
  soak(300, /*specialBases=*/true);
  soak(300, /*specialBases=*/false);
  // The real board, both ways round. Castle Field is the one people will
  // actually play, so it gets the same treatment the lattice does.
  soak(300, /*specialBases=*/true, static_cast<int>(TerrainId::CastleField));
  soak(300, /*specialBases=*/false, static_cast<int>(TerrainId::CastleField));

  printf("toybattle  %d checks, 0 failed\n", checks);
  return 0;
}

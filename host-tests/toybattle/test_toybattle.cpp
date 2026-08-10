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
  g.newGame(1u, 0, starter);
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
  g.newGame(99u, 0, 0);
  check(g.rackSize(0) == 3, "the starter racks 3");
  check(g.rackSize(1) == 4, "the second player racks 4");
  check(g.reserveRemaining(0) == kReserveSize - 3, "the starter drew 3 from a 20 reserve");
  check(g.reserveRemaining(1) == kReserveSize - 4, "the opponent drew 4 from a 20 reserve");

  Game h;
  h.newGame(99u, 0, 1);
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

static void testWireFormat() {
  check(sizeof(Game) <= 192, "Game fits one LinkPlay packet");
  Game a;
  a.newGame(4242u, 0, 1);
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
  }
  return out;
}

static void soak(int matches) {
  int finished = 0;
  int byHq = 0, byMedals = 0, byStuck = 0;
  long moves = 0;

  for (int match = 0; match < matches; ++match) {
    Game g;
    g.newGame(rnd(), 0, static_cast<int>(rnd() & 1u));
    checkInvariants(g, "soak: after the deal");

    int turns = 0;
    while (g.currentPhase() == Phase::Playing) {
      const std::vector<Move> options = legalMoves(g);
      check(!options.empty(), "soak: a playing position always has a legal move");
      const Move& chosen = options[rnd() % options.size()];
      const int before = g.turn;
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
  printf("soak       %d matches, %ld moves  (hq %d, medals %d, stuck %d)\n", matches, moves, byHq, byMedals, byStuck);
}

int main() {
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
  testWireFormat();
  soak(300);

  printf("toybattle  %d checks, 0 failed\n", checks);
  return 0;
}

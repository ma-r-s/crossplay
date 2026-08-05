// Battleship rules tests. Freestanding: no device, no PlatformIO.
//
// Chess verifies move generation with perft, against published node counts,
// because its bugs live in combinations rather than in single rules. This game
// has no published anything, so the equivalent is exhaustion: every legal
// placement of every ship is enumerated and counted against a formula, and
// every ordered pair of placements is checked against an independently built
// set of cells. `canPlace` cannot be subtly wrong at an edge and survive that.
//
// The gunner gets the other kind of proof. It is stateless -- everything it
// knows it reads off the board -- which is exactly the design that can get
// stuck in a loop, so it plays out thousands of whole fleets and the suite
// asserts it never repeats a shot, never runs out of ideas, and always finishes.

#include <cstdio>
#include <cstring>

#include "../../src/apps_local/battleship/BattleshipCore.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  checksRun++;
  if (condition) return;
  checksFailed++;
  std::printf("FAIL test_battleship.cpp:%d  %s\n", line, what);
}

#define CHECK(expr) check((expr), #expr, __LINE__)

using namespace bship;

// The cells a ship covers, built here rather than by the code under test, so
// the exhaustive checks below are comparing two independent answers.
struct Cells {
  int cell[8] = {};
  int count = 0;

  bool holds(const int value) const {
    for (int i = 0; i < count; ++i) {
      if (cell[i] == value) return true;
    }
    return false;
  }
};

Cells spell(const Ship& ship, const int length) {
  Cells cells;
  for (int i = 0; i < length; ++i) {
    const int row = ship.horizontal != 0 ? rowOf(ship.bow) : rowOf(ship.bow) + i;
    const int col = ship.horizontal != 0 ? colOf(ship.bow) + i : colOf(ship.bow);
    cells.cell[cells.count++] = row * kSize + col;
  }
  return cells;
}

bool onBoard(const Cells& cells) {
  for (int i = 0; i < cells.count; ++i) {
    if (cells.cell[i] < 0 || cells.cell[i] >= kCells) return false;
  }
  // A ship that ran off the right edge wraps to the next row rather than
  // leaving the board, so bounds alone would pass it. Rows and columns have to
  // stay in one line.
  const int firstRow = rowOf(cells.cell[0]);
  const int firstCol = colOf(cells.cell[0]);
  bool sameRow = true;
  bool sameCol = true;
  for (int i = 0; i < cells.count; ++i) {
    if (rowOf(cells.cell[i]) != firstRow) sameRow = false;
    if (colOf(cells.cell[i]) != firstCol) sameCol = false;
  }
  return sameRow || sameCol;
}

// Parks a fleet's other four ships off in a corner-free arrangement that the
// ship under test cannot reach, so a placement test is about one ship.
Fleet lonelyFleet(const int shipIndex, const Ship& ship) {
  Fleet fleet;
  // Everything else stacked along the bottom rows, which the tests below avoid.
  int row = kSize - 1;
  for (int i = 0; i < kShipCount; ++i) {
    if (i == shipIndex) {
      fleet.ships[i] = ship;
      continue;
    }
    fleet.ships[i].bow = static_cast<uint8_t>(cellOf(row, 0));
    fleet.ships[i].horizontal = 1;
    --row;
  }
  return fleet;
}

void testGeometry() {
  CHECK(cellOf(0, 0) == 0);
  CHECK(cellOf(9, 9) == 99);
  CHECK(rowOf(34) == 3);
  CHECK(colOf(34) == 4);

  Ship across;
  across.bow = static_cast<uint8_t>(cellOf(2, 3));
  across.horizontal = 1;
  CHECK(shipCell(across, 0) == cellOf(2, 3));
  CHECK(shipCell(across, 2) == cellOf(2, 5));

  Ship down;
  down.bow = static_cast<uint8_t>(cellOf(2, 3));
  down.horizontal = 0;
  CHECK(shipCell(down, 2) == cellOf(4, 3));

  char name[4] = {};
  cellName(cellOf(0, 0), name);
  CHECK(std::strcmp(name, "A1") == 0);
  cellName(cellOf(9, 9), name);
  CHECK(std::strcmp(name, "J10") == 0);
  cellName(cellOf(3, 2), name);
  CHECK(std::strcmp(name, "C4") == 0);
  cellName(-1, name);
  CHECK(name[0] == '\0');
}

// Every legal placement of every ship, counted two ways. A ship of length L
// has 10 rows x (10 - L + 1) columns lying across, and the same again lying
// down, and nothing else fits.
void testEveryLegalPlacement() {
  for (int shipIndex = 0; shipIndex < kShipCount; ++shipIndex) {
    const int length = kShipLength[shipIndex];
    int legal = 0;
    for (int bow = 0; bow < kCells; ++bow) {
      for (int horizontal = 0; horizontal < 2; ++horizontal) {
        Ship ship;
        ship.bow = static_cast<uint8_t>(bow);
        ship.horizontal = static_cast<uint8_t>(horizontal);
        const Fleet fleet = lonelyFleet(shipIndex, ship);
        const bool allowed = canPlace(fleet, shipIndex, ship);
        // The independent answer: spell the ship out and look at where it lands.
        const Cells cells = spell(ship, length);
        bool clear = onBoard(cells);
        if (clear) {
          for (int other = 0; other < kShipCount && clear; ++other) {
            if (other == shipIndex) continue;
            const Cells theirs = spell(fleet.ships[other], kShipLength[other]);
            for (int i = 0; i < theirs.count && clear; ++i) {
              if (cells.holds(theirs.cell[i])) clear = false;
            }
          }
        }
        CHECK(allowed == clear);
        if (allowed) ++legal;
      }
    }
    // The parked ships occupy the bottom rows, so subtract nothing: those rows
    // are excluded by the collision check, which the count above already ran.
    int expected = 0;
    for (int bow = 0; bow < kCells; ++bow) {
      for (int horizontal = 0; horizontal < 2; ++horizontal) {
        Ship ship;
        ship.bow = static_cast<uint8_t>(bow);
        ship.horizontal = static_cast<uint8_t>(horizontal);
        const Fleet fleet = lonelyFleet(shipIndex, ship);
        const Cells cells = spell(ship, length);
        if (!onBoard(cells)) continue;
        bool clear = true;
        for (int other = 0; other < kShipCount && clear; ++other) {
          if (other == shipIndex) continue;
          const Cells theirs = spell(fleet.ships[other], kShipLength[other]);
          for (int i = 0; i < theirs.count && clear; ++i) {
            if (cells.holds(theirs.cell[i])) clear = false;
          }
        }
        if (clear) ++expected;
      }
    }
    CHECK(legal == expected);
  }

  // And on a board with nothing else on it, the formula itself.
  for (int shipIndex = 0; shipIndex < kShipCount; ++shipIndex) {
    const int length = kShipLength[shipIndex];
    int legal = 0;
    for (int bow = 0; bow < kCells; ++bow) {
      for (int horizontal = 0; horizontal < 2; ++horizontal) {
        Ship ship;
        ship.bow = static_cast<uint8_t>(bow);
        ship.horizontal = static_cast<uint8_t>(horizontal);
        // A fleet of five copies of the same ship would collide with itself, so
        // this asks the geometry directly.
        const Cells cells = spell(ship, length);
        if (onBoard(cells)) ++legal;
      }
    }
    CHECK(legal == 2 * kSize * (kSize - length + 1));
  }
}

// Every ordered pair of placements of two ships, against an independently
// built intersection. This is the exhaustive one, and an off-by-one at an edge
// cannot hide in it.
//
// A fleet always holds five ships, which would normally make "just these two"
// impossible to ask. The trick is that ships 1, 2 and 3 are shorter than the
// carrier, so parking them on the carrier's own bow and orientation makes their
// cells a subset of its cells: the fleet then occupies exactly the carrier's
// five squares plus the destroyer's two, and the question really is about a
// pair.
void testEveryPairOfPlacements() {
  const int carrier = 0;
  const int destroyer = 4;
  int pairs = 0;
  for (int bowA = 0; bowA < kCells; ++bowA) {
    for (int horizontalA = 0; horizontalA < 2; ++horizontalA) {
      Ship shipA;
      shipA.bow = static_cast<uint8_t>(bowA);
      shipA.horizontal = static_cast<uint8_t>(horizontalA);
      const Cells cellsA = spell(shipA, kShipLength[carrier]);
      if (!onBoard(cellsA)) continue;

      Fleet fleet;
      for (int i = 0; i < kShipCount - 1; ++i) fleet.ships[i] = shipA;

      for (int bowB = 0; bowB < kCells; ++bowB) {
        for (int horizontalB = 0; horizontalB < 2; ++horizontalB) {
          Ship shipB;
          shipB.bow = static_cast<uint8_t>(bowB);
          shipB.horizontal = static_cast<uint8_t>(horizontalB);
          const Cells cellsB = spell(shipB, kShipLength[destroyer]);

          bool overlap = false;
          for (int i = 0; i < cellsB.count; ++i) {
            if (cellsA.holds(cellsB.cell[i])) overlap = true;
          }
          const bool legal = onBoard(cellsB) && !overlap;

          fleet.ships[destroyer] = shipB;
          CHECK(canPlace(fleet, destroyer, shipB) == legal);
          ++pairs;
        }
      }
    }
  }
  std::printf("  placements: %d ordered pairs checked\n", pairs);
  CHECK(pairs > 15000);
}

void testCanPlaceRejectsOverlap() {
  Fleet fleet;
  for (int i = 0; i < kShipCount; ++i) {
    fleet.ships[i].bow = static_cast<uint8_t>(cellOf(i, 0));
    fleet.ships[i].horizontal = 1;
  }
  // Row 1 already holds the battleship, which is four long.
  Ship clash;
  clash.bow = static_cast<uint8_t>(cellOf(1, 2));
  clash.horizontal = 0;
  CHECK(!canPlace(fleet, 0, clash));
  Ship clear;
  clear.bow = static_cast<uint8_t>(cellOf(5, 5));
  clear.horizontal = 0;
  CHECK(canPlace(fleet, 0, clear));
  // Replacing a ship with itself is legal: the slot being placed is skipped.
  CHECK(canPlace(fleet, 2, fleet.ships[2]));
  CHECK(!canPlace(fleet, -1, clear));
  CHECK(!canPlace(fleet, kShipCount, clear));
}

void testRandomFleetIsAlwaysLegal() {
  uint32_t seed = 12345;
  bool everCovered[kCells] = {};
  for (int trial = 0; trial < 20000; ++trial) {
    Fleet fleet;
    randomFleet(fleet, seed);
    int covered = 0;
    bool used[kCells] = {};
    for (int i = 0; i < kShipCount; ++i) {
      CHECK(canPlace(fleet, i, fleet.ships[i]));
      for (int j = 0; j < kShipLength[i]; ++j) {
        const int cell = shipCell(fleet.ships[i], j);
        if (cell < 0 || cell >= kCells) {
          CHECK(false);
          continue;
        }
        CHECK(!used[cell]);
        used[cell] = true;
        everCovered[cell] = true;
        ++covered;
      }
    }
    CHECK(covered == kFleetCells);
  }
  // A generator stuck in a corner would still pass every check above. Every
  // cell has to be reachable.
  for (int cell = 0; cell < kCells; ++cell) CHECK(everCovered[cell]);
}

void testSeedIsStable() {
  uint32_t a = 777;
  uint32_t b = 777;
  Fleet first;
  Fleet second;
  randomFleet(first, a);
  randomFleet(second, b);
  CHECK(std::memcmp(&first, &second, sizeof(Fleet)) == 0);
  uint32_t c = 778;
  Fleet third;
  randomFleet(third, c);
  CHECK(std::memcmp(&first, &third, sizeof(Fleet)) != 0);
}

// Two fleets placed and the turn back with the first player.
Game readyGame(uint32_t& seed) {
  Game game;
  reset(game);
  Fleet zero;
  Fleet one;
  randomFleet(zero, seed);
  randomFleet(one, seed);
  const bool placedZero = place(game, 0, zero);
  const bool placedOne = place(game, 1, one);
  check(placedZero && placedOne, "both fleets placed", __LINE__);
  return game;
}

void testPlacementIsTwoTurns() {
  Game game;
  reset(game);
  CHECK(game.turn == 0);
  CHECK(!bothPlaced(game));
  uint32_t seed = 99;
  Fleet fleet;
  randomFleet(fleet, seed);

  // Out of turn, and out of order.
  CHECK(!place(game, 1, fleet));
  CHECK(!fire(game, 0));
  CHECK(place(game, 0, fleet));
  CHECK(game.turn == 1);
  // Nobody places twice.
  CHECK(!place(game, 0, fleet));
  CHECK(!fire(game, 0));
  Fleet other;
  randomFleet(other, seed);
  CHECK(place(game, 1, other));
  // And the turn comes back to whoever went first, who now fires first.
  CHECK(game.turn == 0);
  CHECK(bothPlaced(game));
  CHECK(fire(game, 0));

  // An illegal fleet is refused rather than stored.
  Game fresh;
  reset(fresh);
  Fleet bad = fleet;
  bad.ships[0] = bad.ships[1];
  CHECK(!place(fresh, 0, bad));
  CHECK(fresh.side[0].placed == 0);
}

void testFiringAlternatesStrictly() {
  uint32_t seed = 4242;
  Game game = readyGame(seed);
  int expectedTurn = 0;
  for (int shot = 0; shot < 40; ++shot) {
    CHECK(game.turn == expectedTurn);
    CHECK(fire(game, shot));
    expectedTurn ^= 1;
    CHECK(game.turn == expectedTurn);
    // A hit does not buy a second shot, and a cell already fired at is refused.
    // The turn has just passed, so the side now holding it last fired at
    // `shot - 1`: that is the cell it is not allowed to fire at again. (`shot`
    // itself is on the other board and is still open, which is the trap this
    // check walked into first time round.)
    if (shot >= 1) CHECK(!fire(game, shot - 1));
    CHECK(game.turn == expectedTurn);
  }
  CHECK(!fire(game, -1));
  CHECK(!fire(game, kCells));
}

// Fires for a named side, asserting it was actually that side's turn. Every
// test below reads as the game would be played rather than as bookkeeping.
void fireFor(Game& game, const int side, const int cell, const int line) {
  check((game.turn & 1) == side, "the named side had the turn", line);
  check(fire(game, cell), "the shot was accepted", line);
}

#define FIRE(game, side, cell) fireFor((game), (side), (cell), __LINE__)

// A fleet lying in five rows, so an assertion can name the cell it means.
Fleet rowFleet(const int firstRow) {
  Fleet fleet;
  for (int i = 0; i < kShipCount; ++i) {
    fleet.ships[i] = Ship{static_cast<uint8_t>(cellOf(firstRow + i * 2, 0)), 1};
  }
  return fleet;
}

void testHitsAndSinking() {
  Game game;
  reset(game);
  const Fleet mine = rowFleet(0);  // destroyer at A9-B9
  const Fleet theirs = rowFleet(1);
  CHECK(place(game, 0, mine));
  CHECK(place(game, 1, theirs));

  // A miss says so, and the far column belongs to nobody.
  FIRE(game, 0, cellOf(0, 9));
  CHECK(!lastShotHit(game));
  CHECK(lastShotSank(game) == -1);

  FIRE(game, 1, cellOf(8, 0));
  CHECK(lastShotHit(game));
  CHECK(lastShotSank(game) == -1);
  CHECK(!sunk(game.side[0], 4));

  FIRE(game, 0, cellOf(1, 9));
  FIRE(game, 1, cellOf(8, 1));
  CHECK(lastShotHit(game));
  CHECK(lastShotSank(game) == 4);
  CHECK(sunk(game.side[0], 4));
  CHECK(sunkCount(game.side[0]) == 1);
  CHECK(!defeated(game.side[0]));
  CHECK(hitsTaken(game.side[0]) == 2);
  CHECK(shotsTaken(game.side[0]) == 2);
  CHECK(shotsTaken(game.side[1]) == 2);
  CHECK(hitsTaken(game.side[1]) == 0);
}

void testWinningStopsTheGame() {
  Game game;
  reset(game);
  const Fleet fleet = rowFleet(0);
  CHECK(place(game, 0, fleet));
  CHECK(place(game, 1, rowFleet(1)));

  // Side 1 sinks everything side 0 has, one shot each turn, with side 0 firing
  // harmlessly at the far column in between.
  int filler = cellOf(0, 9);
  for (int i = 0; i < kShipCount; ++i) {
    for (int j = 0; j < kShipLength[i]; ++j) {
      FIRE(game, 0, filler);
      filler += kSize;
      if (filler >= kCells) filler = cellOf(0, 8);
      FIRE(game, 1, shipCell(fleet.ships[i], j));
    }
    CHECK(sunk(game.side[0], i));
  }
  CHECK(defeated(game.side[0]));
  CHECK(over(game));
  CHECK(winner(game) == 1);
  // Nothing moves after that.
  const Game frozen = game;
  CHECK(!fire(game, cellOf(5, 5)));
  CHECK(std::memcmp(&frozen, &game, sizeof(Game)) == 0);
}

// The gunner's proof. It is stateless, so the failure mode is a board it can
// look at forever without deciding anything; thousands of whole fleets, with
// every shot checked for repetition, is what rules that out.
void testGunnerAlwaysFinishes() {
  uint32_t seed = 20240804;
  int totalShots = 0;
  int worst = 0;
  const int trials = 3000;
  for (int trial = 0; trial < trials; ++trial) {
    Side side;
    randomFleet(side.fleet, seed);
    side.placed = 1;
    int shots = 0;
    while (!defeated(side)) {
      const int cell = chooseShot(side, seed);
      CHECK(cell >= 0 && cell < kCells);
      if (cell < 0 || cell >= kCells) break;
      CHECK(!shotAt(side, cell));
      markShot(side, cell);
      ++shots;
      CHECK(shots <= kCells);
      if (shots > kCells) break;
    }
    totalShots += shots;
    if (shots > worst) worst = shots;
  }
  const double average = static_cast<double>(totalShots) / trials;
  std::printf("  gunner: %.1f shots on average, %d at worst, over %d fleets\n", average, worst, trials);
  // The published figures for this board and fleet: shooting at random takes
  // about 96, hunt-and-target about 65, hunt-and-target on a parity lattice
  // about 60, and a full probability-density search about 42. This gunner sits
  // around 51, so the bound below is not a formality -- it is between the
  // strategy this is and the one it replaced, and losing the targeting would
  // fail it while every fleet still sank.
  CHECK(average < 56.0);
  CHECK(worst <= kCells);
}

// Two gunners playing each other to the end, which is also the shape a match
// takes. Exercises fire() and the derived winner rather than the AI.
void testTwoGunnersPlayAWholeGame() {
  uint32_t seed = 31337;
  for (int trial = 0; trial < 500; ++trial) {
    Game game = readyGame(seed);
    int moves = 0;
    while (!over(game) && moves < 2 * kCells) {
      const int shooter = game.turn & 1;
      const int cell = chooseShot(game.side[shooter ^ 1], seed);
      CHECK(cell >= 0);
      if (cell < 0) break;
      CHECK(fire(game, cell));
      ++moves;
      // Strict alternation is what the link layer requires of this game, so it
      // is asserted here rather than assumed.
      CHECK((game.turn & 1) == (shooter ^ 1));
    }
    CHECK(over(game));
    CHECK(winner(game) == 0 || winner(game) == 1);
    // The loser has lost everything and the winner has not.
    const int loser = winner(game) ^ 1;
    CHECK(defeated(game.side[loser]));
    CHECK(!defeated(game.side[winner(game)]));
    CHECK(hitsTaken(game.side[loser]) == kFleetCells);
  }
}

// The gunner finishes a wounded ship instead of wandering off. Cheap to state,
// and it is the difference between an opponent and a random number generator.
void testGunnerFinishesWhatItStarts() {
  Side side;
  side.placed = 1;
  side.fleet.ships[0] = Ship{static_cast<uint8_t>(cellOf(4, 2)), 1};  // carrier C5-G5
  side.fleet.ships[1] = Ship{static_cast<uint8_t>(cellOf(0, 0)), 1};
  side.fleet.ships[2] = Ship{static_cast<uint8_t>(cellOf(2, 0)), 1};
  side.fleet.ships[3] = Ship{static_cast<uint8_t>(cellOf(6, 0)), 1};
  side.fleet.ships[4] = Ship{static_cast<uint8_t>(cellOf(8, 0)), 1};

  // One hit in the middle of the carrier: every next shot must touch it.
  markShot(side, cellOf(4, 4));
  uint32_t seed = 8;
  for (int trial = 0; trial < 200; ++trial) {
    Side probe = side;
    const int cell = chooseShot(probe, seed);
    const bool adjacent =
        (cell == cellOf(3, 4)) || (cell == cellOf(5, 4)) || (cell == cellOf(4, 3)) || (cell == cellOf(4, 5));
    CHECK(adjacent);
  }

  // Two hits in a row: it now knows the direction and must shoot along it.
  markShot(side, cellOf(4, 5));
  for (int trial = 0; trial < 200; ++trial) {
    Side probe = side;
    const int cell = chooseShot(probe, seed);
    CHECK(cell == cellOf(4, 3) || cell == cellOf(4, 6));
  }

  // A run that reaches the board's edge only has one end left.
  Side edge;
  edge.placed = 1;
  edge.fleet.ships[0] = Ship{static_cast<uint8_t>(cellOf(0, 0)), 1};
  edge.fleet.ships[1] = Ship{static_cast<uint8_t>(cellOf(2, 0)), 1};
  edge.fleet.ships[2] = Ship{static_cast<uint8_t>(cellOf(4, 0)), 1};
  edge.fleet.ships[3] = Ship{static_cast<uint8_t>(cellOf(6, 0)), 1};
  edge.fleet.ships[4] = Ship{static_cast<uint8_t>(cellOf(8, 0)), 1};
  markShot(edge, cellOf(0, 0));
  markShot(edge, cellOf(0, 1));
  for (int trial = 0; trial < 100; ++trial) {
    Side probe = edge;
    const int cell = chooseShot(probe, seed);
    CHECK(cell == cellOf(0, 2));
  }

  // And a sunk ship is finished business: with the destroyer sunk and nothing
  // else touched, the gunner goes back to hunting -- on a lattice that has just
  // become coarser, because the shortest ship left afloat is now three long.
  Side done;
  done.placed = 1;
  done.fleet.ships[0] = Ship{static_cast<uint8_t>(cellOf(0, 0)), 1};
  done.fleet.ships[1] = Ship{static_cast<uint8_t>(cellOf(2, 0)), 1};
  done.fleet.ships[2] = Ship{static_cast<uint8_t>(cellOf(4, 0)), 1};
  done.fleet.ships[3] = Ship{static_cast<uint8_t>(cellOf(6, 0)), 1};
  done.fleet.ships[4] = Ship{static_cast<uint8_t>(cellOf(8, 0)), 1};
  markShot(done, cellOf(8, 0));
  markShot(done, cellOf(8, 1));
  CHECK(sunk(done, 4));
  for (int trial = 0; trial < 100; ++trial) {
    Side probe = done;
    const int cell = chooseShot(probe, seed);
    CHECK((rowOf(cell) + colOf(cell)) % 3 == 0);
    CHECK(!shotAt(done, cell));
  }
}

void testStateFitsOnePacket() {
  // The link layer's packet is 192 bytes and this state is what travels, so a
  // field added carelessly should fail here rather than at pairing time.
  CHECK(sizeof(Game) <= 192);
  std::printf("  wire: sizeof(Game) = %d bytes\n", static_cast<int>(sizeof(Game)));
}

}  // namespace

int main() {
  testGeometry();
  testEveryLegalPlacement();
  testEveryPairOfPlacements();
  testCanPlaceRejectsOverlap();
  testRandomFleetIsAlwaysLegal();
  testSeedIsStable();
  testPlacementIsTwoTurns();
  testFiringAlternatesStrictly();
  testHitsAndSinking();
  testWinningStopsTheGame();
  testGunnerAlwaysFinishes();
  testTwoGunnersPlayAWholeGame();
  testGunnerFinishesWhatItStarts();
  testStateFitsOnePacket();

  std::printf("%d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}

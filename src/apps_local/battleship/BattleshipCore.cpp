#include "BattleshipCore.h"

namespace bship {

namespace {

// Two ships share a cell. Twenty-five comparisons in the worst case, which is
// cheaper than any cleverness would be and cannot get the edges wrong.
bool collide(const Ship& a, const int lengthA, const Ship& b, const int lengthB) {
  for (int i = 0; i < lengthA; ++i) {
    const int cellA = shipCell(a, i);
    for (int j = 0; j < lengthB; ++j) {
      if (cellA == shipCell(b, j)) return true;
    }
  }
  return false;
}

bool fitsBoard(const Ship& ship, const int length) {
  if (ship.bow >= kCells) return false;
  if (ship.horizontal != 0) return colOf(ship.bow) + length <= kSize;
  return rowOf(ship.bow) + length <= kSize;
}

// Only against the first `count` ships. randomFleet builds a fleet one ship at
// a time, and the slots it has not reached yet still hold whatever they were
// initialised to -- checking against those would reject legal placements for
// colliding with a ship that does not exist yet.
bool clearOfFirst(const Fleet& fleet, const int count, const Ship& candidate, const int length) {
  for (int i = 0; i < count; ++i) {
    if (collide(candidate, length, fleet.ships[i], kShipLength[i])) return false;
  }
  return true;
}

bool unresolvedHit(const Side& side, const int cell) {
  if (!shotAt(side, cell)) return false;
  const int ship = shipAt(side.fleet, cell);
  if (ship < 0) return false;
  return !sunk(side, ship);
}

// A step that stays on the board and does not wrap a row. The wrap is the whole
// reason this exists: cell + 1 from column 9 lands on column 0 of the next row,
// which is not a neighbour and would have the gunner shooting round corners.
bool step(const int cell, const int deltaRow, const int deltaCol, int& out) {
  const int row = rowOf(cell) + deltaRow;
  const int col = colOf(cell) + deltaCol;
  if (row < 0 || row >= kSize || col < 0 || col >= kSize) return false;
  out = cellOf(row, col);
  return true;
}

// Walks from `cell` in one direction across a run of unresolved hits and
// returns the first cell past the end, if it is on the board and unshot. That
// is where the rest of a wounded ship has to be.
bool endOfRun(const Side& side, const int cell, const int deltaRow, const int deltaCol, int& out) {
  int at = cell;
  for (int guard = 0; guard < kSize; ++guard) {
    int next = 0;
    if (!step(at, deltaRow, deltaCol, next)) return false;
    if (!shotAt(side, next)) {
      out = next;
      return true;
    }
    if (!unresolvedHit(side, next)) return false;
    at = next;
  }
  return false;
}

constexpr int kDirRow[4] = {-1, 1, 0, 0};
constexpr int kDirCol[4] = {0, 0, -1, 1};

// The shortest ship still afloat, which is how coarse the hunt lattice may be.
// Never less than one, so the stride is always a legal divisor.
int smallestAfloat(const Side& side) {
  int shortest = kSize;
  for (int i = 0; i < kShipCount; ++i) {
    if (sunk(side, i)) continue;
    if (kShipLength[i] < shortest) shortest = kShipLength[i];
  }
  return shortest < 1 ? 1 : shortest;
}

}  // namespace

const char* shipName(const int index) {
  switch (index) {
    case 0:
      return "CARRIER";
    case 1:
      return "BATTLESHIP";
    case 2:
      return "CRUISER";
    case 3:
      return "SUBMARINE";
    case 4:
      return "DESTROYER";
    default:
      return "";
  }
}

void cellName(const int cell, char* out) {
  if (out == nullptr) return;
  if (cell < 0 || cell >= kCells) {
    out[0] = '\0';
    return;
  }
  // Columns are letters and rows are numbers, the way the box has always done
  // it, so a shot can be said out loud across a table.
  out[0] = static_cast<char>('A' + colOf(cell));
  const int number = rowOf(cell) + 1;
  if (number < 10) {
    out[1] = static_cast<char>('0' + number);
    out[2] = '\0';
    return;
  }
  out[1] = '1';
  out[2] = '0';
  out[3] = '\0';
}

uint32_t nextRandom(uint32_t& seed) {
  // Any non-zero state will do; zero is the one value xorshift cannot leave.
  if (seed == 0) seed = 0x9E3779B9u;
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  return seed;
}

int shipCell(const Ship& ship, const int index) {
  return ship.horizontal != 0 ? ship.bow + index : ship.bow + index * kSize;
}

bool canPlace(const Fleet& fleet, const int shipIndex, const Ship& candidate) {
  if (shipIndex < 0 || shipIndex >= kShipCount) return false;
  const int length = kShipLength[shipIndex];
  if (!fitsBoard(candidate, length)) return false;
  for (int i = 0; i < kShipCount; ++i) {
    if (i == shipIndex) continue;
    if (collide(candidate, length, fleet.ships[i], kShipLength[i])) return false;
  }
  return true;
}

int shipAt(const Fleet& fleet, const int cell) {
  if (cell < 0 || cell >= kCells) return -1;
  for (int i = 0; i < kShipCount; ++i) {
    for (int j = 0; j < kShipLength[i]; ++j) {
      if (shipCell(fleet.ships[i], j) == cell) return i;
    }
  }
  return -1;
}

void randomFleet(Fleet& fleet, uint32_t& seed) {
  for (int i = 0; i < kShipCount; ++i) {
    const int length = kShipLength[i];
    // Bounded rather than open: 17 cells of fleet on a 100-cell board collide
    // rarely, but a loop that can spin forever has no place in a firmware.
    for (int attempt = 0; attempt < 512; ++attempt) {
      Ship candidate;
      candidate.horizontal = static_cast<uint8_t>(nextRandom(seed) & 1u);
      const int span = kSize - length + 1;
      const int row =
          static_cast<int>(nextRandom(seed) % static_cast<uint32_t>(candidate.horizontal != 0 ? kSize : span));
      const int col =
          static_cast<int>(nextRandom(seed) % static_cast<uint32_t>(candidate.horizontal != 0 ? span : kSize));
      candidate.bow = static_cast<uint8_t>(cellOf(row, col));
      if (!clearOfFirst(fleet, i, candidate, length)) continue;
      fleet.ships[i] = candidate;
      break;
    }
  }
}

bool shotAt(const Side& side, const int cell) {
  if (cell < 0 || cell >= kCells) return false;
  return (side.shots[cell >> 3] >> (cell & 7) & 1u) != 0;
}

void markShot(Side& side, const int cell) {
  if (cell < 0 || cell >= kCells) return;
  side.shots[cell >> 3] |= static_cast<uint8_t>(1u << (cell & 7));
}

bool sunk(const Side& side, const int shipIndex) {
  if (shipIndex < 0 || shipIndex >= kShipCount) return false;
  for (int i = 0; i < kShipLength[shipIndex]; ++i) {
    if (!shotAt(side, shipCell(side.fleet.ships[shipIndex], i))) return false;
  }
  return true;
}

int sunkCount(const Side& side) {
  int count = 0;
  for (int i = 0; i < kShipCount; ++i) {
    if (sunk(side, i)) ++count;
  }
  return count;
}

bool defeated(const Side& side) { return side.placed != 0 && sunkCount(side) == kShipCount; }

int shotsTaken(const Side& side) {
  int count = 0;
  for (int cell = 0; cell < kCells; ++cell) {
    if (shotAt(side, cell)) ++count;
  }
  return count;
}

int hitsTaken(const Side& side) {
  int count = 0;
  for (int cell = 0; cell < kCells; ++cell) {
    if (shotAt(side, cell) && shipAt(side.fleet, cell) >= 0) ++count;
  }
  return count;
}

void reset(Game& game) { game = Game{}; }

bool bothPlaced(const Game& game) { return game.side[0].placed != 0 && game.side[1].placed != 0; }

bool over(const Game& game) { return defeated(game.side[0]) || defeated(game.side[1]); }

int winner(const Game& game) {
  if (defeated(game.side[0])) return 1;
  if (defeated(game.side[1])) return 0;
  return -1;
}

bool place(Game& game, const int side, const Fleet& fleet) {
  if (side < 0 || side > 1) return false;
  if (game.turn != side) return false;
  if (game.side[side].placed != 0) return false;
  for (int i = 0; i < kShipCount; ++i) {
    if (!canPlace(fleet, i, fleet.ships[i])) return false;
  }
  game.side[side].fleet = fleet;
  game.side[side].placed = 1;
  // Handing a fleet over passes the turn exactly as firing does. That is what
  // lets a phase the box plays simultaneously travel down a link that only
  // knows how to alternate: setting up is two moves, then the shooting starts.
  game.turn = static_cast<uint8_t>(side ^ 1);
  game.lastShot = 0;
  return true;
}

bool fire(Game& game, const int cell) {
  if (cell < 0 || cell >= kCells) return false;
  if (!bothPlaced(game)) return false;
  if (over(game)) return false;
  const int shooter = game.turn & 1;
  Side& target = game.side[shooter ^ 1];
  if (shotAt(target, cell)) return false;
  markShot(target, cell);
  game.lastShot = static_cast<uint8_t>(cell + 1);
  // A hit does not buy another shot. Standard rules, and it is also what keeps
  // turns strictly alternating -- the one thing the link layer asks of a game.
  game.turn = static_cast<uint8_t>(shooter ^ 1);
  return true;
}

bool lastShotHit(const Game& game) {
  if (game.lastShot == 0) return false;
  // The turn has already passed, so the side that was shot at is the one whose
  // turn it now is.
  const Side& target = game.side[game.turn & 1];
  return shipAt(target.fleet, game.lastShot - 1) >= 0;
}

int lastShotSank(const Game& game) {
  if (!lastShotHit(game)) return -1;
  const Side& target = game.side[game.turn & 1];
  const int ship = shipAt(target.fleet, game.lastShot - 1);
  return sunk(target, ship) ? ship : -1;
}

int chooseShot(const Side& target, uint32_t& seed) {
  int candidates[kCells];
  int count = 0;

  // Finish a ship you have already found, before looking for another one.
  // A run of two or more hits gives a direction, and the rest of that ship can
  // only be off one of its two ends.
  for (int cell = 0; cell < kCells && count < kCells; ++cell) {
    if (!unresolvedHit(target, cell)) continue;
    for (int axis = 0; axis < 2; ++axis) {
      const int deltaRow = axis == 0 ? 0 : 1;
      const int deltaCol = axis == 0 ? 1 : 0;
      int neighbour = 0;
      if (!step(cell, deltaRow, deltaCol, neighbour)) continue;
      if (!unresolvedHit(target, neighbour)) continue;
      int end = 0;
      if (endOfRun(target, cell, -deltaRow, -deltaCol, end)) candidates[count++] = end;
      if (endOfRun(target, neighbour, deltaRow, deltaCol, end)) candidates[count++] = end;
    }
  }

  // A lone hit: no direction yet, so try its four neighbours.
  if (count == 0) {
    for (int cell = 0; cell < kCells && count < kCells; ++cell) {
      if (!unresolvedHit(target, cell)) continue;
      for (int dir = 0; dir < 4; ++dir) {
        int neighbour = 0;
        if (!step(cell, kDirRow[dir], kDirCol[dir], neighbour)) continue;
        if (shotAt(target, neighbour)) continue;
        candidates[count++] = neighbour;
      }
    }
  }

  // Hunting, on a lattice as coarse as the shortest ship still afloat. A ship
  // of length L covers L cells in a line, so (row + col) takes L consecutive
  // values along it and exactly one of them is divisible by L: searching every
  // Lth cell cannot miss it. That is half the board while the destroyer is
  // alive and a third of it once it is sunk, which is most of the difference
  // between this gunner and one that shoots at random.
  if (count == 0) {
    const int stride = smallestAfloat(target);
    for (int cell = 0; cell < kCells; ++cell) {
      if (shotAt(target, cell)) continue;
      if ((rowOf(cell) + colOf(cell)) % stride != 0) continue;
      candidates[count++] = cell;
    }
  }

  // The lattice is spent, which means every remaining ship is already sunk or
  // the board is nearly full. Anything unshot will do.
  if (count == 0) {
    for (int cell = 0; cell < kCells; ++cell) {
      if (!shotAt(target, cell)) candidates[count++] = cell;
    }
  }

  if (count == 0) return -1;
  return candidates[nextRandom(seed) % static_cast<uint32_t>(count)];
}

}  // namespace bship

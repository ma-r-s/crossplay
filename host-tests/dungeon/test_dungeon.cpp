// Host tests for the dungeon rules and the puzzle bank. No device, no
// PlatformIO: DungeonCore is freestanding C++17 precisely so this runs on a
// laptop. See run.sh.
//
// The centrepiece is not the board tests, it is validateBank(): an independent
// implementation of the five puzzle rules, run over all 65 stored solutions.
//
// That is this app's perft. DungeonCore deliberately does not know the rules --
// it compares a finished board against the stored answer -- which is only safe
// if the stored answer really is a legal solution to the clues beside it. The
// generator asserts that in Python at build time; this asserts it in C++
// against the header that actually ships, so a hand-edit to the generated file
// or a bad merge cannot slip a wrong answer through. Two implementations, one
// in each language, agreeing on 65 boards.

#include <cstdio>
#include <set>
#include <vector>

#include "../../src/apps_local/dungeon/DungeonCore.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  ++checksRun;
  if (!condition) {
    ++checksFailed;
    std::printf("FAIL %s:%d  %s\n", "test_dungeon.cpp", line, what);
  }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

using Cell = std::pair<int, int>;

bool bitAt(const uint64_t mask, const int row, const int col) {
  return (mask & (uint64_t{1} << (row * 8 + col))) != 0;
}

// ---------------------------------------------------------------------------
// The oracle: the five rules, written out longhand.
// ---------------------------------------------------------------------------

// Rule 1: the number beside a row or column is how many walls it holds.
bool cluesMatch(const dungeon::Puzzle& p) {
  const int n = p.size;
  for (int r = 0; r < n; ++r) {
    int count = 0;
    for (int c = 0; c < n; ++c) count += bitAt(p.walls, r, c) ? 1 : 0;
    if (count != p.rowClues[r]) return false;
  }
  for (int c = 0; c < n; ++c) {
    int count = 0;
    for (int r = 0; r < n; ++r) count += bitAt(p.walls, r, c) ? 1 : 0;
    if (count != p.colClues[c]) return false;
  }
  return true;
}

int openNeighbours(const dungeon::Puzzle& p, const int row, const int col) {
  const int n = p.size;
  static const int kDr[4] = {1, -1, 0, 0};
  static const int kDc[4] = {0, 0, 1, -1};
  int open = 0;
  for (int i = 0; i < 4; ++i) {
    const int r = row + kDr[i];
    const int c = col + kDc[i];
    if (r < 0 || r >= n || c < 0 || c >= n) continue;
    if (!bitAt(p.walls, r, c)) ++open;
  }
  return open;
}

// Rule 2: every dead end holds a monster, and every monster is in a dead end.
bool deadEndsAreMonsters(const dungeon::Puzzle& p) {
  const int n = p.size;
  for (int r = 0; r < n; ++r) {
    for (int c = 0; c < n; ++c) {
      if (bitAt(p.walls, r, c)) {
        // Nothing stands in a wall.
        if (bitAt(p.monsters, r, c) || bitAt(p.chests, r, c)) return false;
        continue;
      }
      const bool deadEnd = openNeighbours(p, r, c) == 1;
      if (deadEnd != bitAt(p.monsters, r, c)) return false;
    }
  }
  return true;
}

// Rule 5: all floor is connected.
bool floorIsConnected(const dungeon::Puzzle& p) {
  const int n = p.size;
  std::vector<Cell> floor;
  for (int r = 0; r < n; ++r) {
    for (int c = 0; c < n; ++c) {
      if (!bitAt(p.walls, r, c)) floor.push_back({r, c});
    }
  }
  if (floor.empty()) return false;
  std::set<Cell> seen{floor.front()};
  std::vector<Cell> stack{floor.front()};
  static const int kDr[4] = {1, -1, 0, 0};
  static const int kDc[4] = {0, 0, 1, -1};
  while (!stack.empty()) {
    const Cell cell = stack.back();
    stack.pop_back();
    for (int i = 0; i < 4; ++i) {
      const int r = cell.first + kDr[i];
      const int c = cell.second + kDc[i];
      if (r < 0 || r >= n || c < 0 || c >= n) continue;
      if (bitAt(p.walls, r, c)) continue;
      const Cell next{r, c};
      if (seen.insert(next).second) stack.push_back(next);
    }
  }
  return seen.size() == floor.size();
}

// Rules 3 and 4: each chest sits in its own 3x3 room of floor with exactly one
// way in, and every 2x2 of floor is inside such a room.
bool treasureRoomsAndCorridors(const dungeon::Puzzle& p) {
  const int n = p.size;
  std::set<Cell> claimed;
  for (int cr = 0; cr < n; ++cr) {
    for (int cc = 0; cc < n; ++cc) {
      if (!bitAt(p.chests, cr, cc)) continue;
      bool placed = false;
      for (int tr = cr - 2; tr <= cr && !placed; ++tr) {
        for (int tc = cc - 2; tc <= cc && !placed; ++tc) {
          if (tr < 0 || tc < 0 || tr + 2 >= n || tc + 2 >= n) continue;
          std::set<Cell> room;
          bool allFloor = true;
          int chestsInside = 0;
          for (int i = 0; i < 3 && allFloor; ++i) {
            for (int j = 0; j < 3; ++j) {
              const int r = tr + i;
              const int c = tc + j;
              if (bitAt(p.walls, r, c) || bitAt(p.monsters, r, c)) {
                allFloor = false;
                break;
              }
              if (bitAt(p.chests, r, c)) ++chestsInside;
              room.insert({r, c});
            }
          }
          if (!allFloor || chestsInside != 1) continue;
          int doors = 0;
          static const int kDr[4] = {1, -1, 0, 0};
          static const int kDc[4] = {0, 0, 1, -1};
          for (const Cell& cell : room) {
            for (int i = 0; i < 4; ++i) {
              const int r = cell.first + kDr[i];
              const int c = cell.second + kDc[i];
              if (r < 0 || r >= n || c < 0 || c >= n) continue;
              if (room.count({r, c}) != 0) continue;
              if (!bitAt(p.walls, r, c)) ++doors;
            }
          }
          if (doors != 1) continue;
          for (const Cell& cell : room) {
            if (claimed.count(cell) != 0) return false;  // two chests, one room
            claimed.insert(cell);
          }
          placed = true;
        }
      }
      if (!placed) return false;
    }
  }
  for (int r = 0; r + 1 < n; ++r) {
    for (int c = 0; c + 1 < n; ++c) {
      const bool block = !bitAt(p.walls, r, c) && !bitAt(p.walls, r, c + 1) && !bitAt(p.walls, r + 1, c) &&
                         !bitAt(p.walls, r + 1, c + 1);
      if (!block) continue;
      const bool inRoom = claimed.count({r, c}) != 0 && claimed.count({r, c + 1}) != 0 &&
                          claimed.count({r + 1, c}) != 0 && claimed.count({r + 1, c + 1}) != 0;
      if (!inRoom) return false;  // a 2x2 hall is not one cell wide
    }
  }
  return true;
}

void validateBank() {
  for (int i = 0; i < dungeon::kPuzzleCount; ++i) {
    const dungeon::Puzzle& p = dungeon::kPuzzles[i];
    const bool clues = cluesMatch(p);
    const bool ends = deadEndsAreMonsters(p);
    const bool connected = floorIsConnected(p);
    const bool rooms = treasureRoomsAndCorridors(p);
    if (!clues || !ends || !connected || !rooms) {
      std::printf("FAIL %s: clues=%d deadEnds=%d connected=%d rooms=%d\n", p.name, clues, ends, connected, rooms);
    }
    check(clues, "row and column clues match the stored solution", __LINE__);
    check(ends, "dead ends and monsters correspond", __LINE__);
    check(connected, "all floor is connected", __LINE__);
    check(rooms, "treasure rooms are legal and corridors are one wide", __LINE__);

    // No bit may stray outside a puzzle's own square. The tutorial is 6x6 in
    // an 8-wide word, so a generator that padded wrongly would produce walls
    // the board can never reach and a puzzle that can never be finished.
    uint64_t outside = 0;
    for (int r = 0; r < 8; ++r) {
      for (int c = 0; c < 8; ++c) {
        if (r < p.size && c < p.size) continue;
        outside |= uint64_t{1} << (r * 8 + c);
      }
    }
    CHECK((p.walls & outside) == 0);
    CHECK((p.monsters & outside) == 0);
    CHECK((p.chests & outside) == 0);
    CHECK(p.name != nullptr && p.name[0] != '\0');
    CHECK(p.size == 6 || p.size == 8);
  }
}

// ---------------------------------------------------------------------------
// The board.
// ---------------------------------------------------------------------------

void testTapCycle() {
  dungeon::Board board;
  board.load(1);
  // Find a cell holding nothing.
  int row = -1;
  int col = -1;
  for (int r = 0; r < board.size() && row < 0; ++r) {
    for (int c = 0; c < board.size(); ++c) {
      if (!board.isMonster(r, c) && !board.isChest(r, c)) {
        row = r;
        col = c;
        break;
      }
    }
  }
  CHECK(row >= 0);
  CHECK(board.mark(row, col) == dungeon::Mark::Unknown);
  board.tap(row, col);
  CHECK(board.mark(row, col) == dungeon::Mark::Wall);
  board.tap(row, col);
  CHECK(board.mark(row, col) == dungeon::Mark::Floor);
  board.tap(row, col);
  CHECK(board.mark(row, col) == dungeon::Mark::Unknown);
  CHECK(!board.touched());

  // Off the board in every direction, including past the tutorial's 6x6 edge.
  board.tap(-1, 0);
  board.tap(0, -1);
  board.tap(board.size(), 0);
  board.tap(0, board.size());
  CHECK(!board.touched());
}

void testFixedCellsRefuse() {
  for (int i = 0; i < dungeon::kPuzzleCount; ++i) {
    dungeon::Board board;
    board.load(i);
    for (int r = 0; r < board.size(); ++r) {
      for (int c = 0; c < board.size(); ++c) {
        if (!board.isMonster(r, c) && !board.isChest(r, c)) continue;
        board.tap(r, c);
        board.tap(r, c);
        CHECK(board.mark(r, c) == dungeon::Mark::Unknown);
      }
    }
    CHECK(!board.touched());
  }
}

// Play every puzzle to its solution and confirm the board agrees, then confirm
// it does NOT agree one wall short. The second half is the one that matters:
// a solved() that always returned true would pass the first.
void testPlayEveryPuzzle() {
  for (int i = 0; i < dungeon::kPuzzleCount; ++i) {
    dungeon::Board board;
    board.load(i);
    CHECK(!board.solved());
    const dungeon::Puzzle& p = dungeon::kPuzzles[i];
    int lastRow = -1;
    int lastCol = -1;
    for (int r = 0; r < board.size(); ++r) {
      for (int c = 0; c < board.size(); ++c) {
        if (!bitAt(p.walls, r, c)) continue;
        board.tap(r, c);
        lastRow = r;
        lastCol = c;
      }
    }
    CHECK(board.solved());
    for (int r = 0; r < board.size(); ++r) {
      CHECK(board.rowSatisfied(r));
      CHECK(board.rowWalls(r) == p.rowClues[r]);
    }
    for (int c = 0; c < board.size(); ++c) {
      CHECK(board.colSatisfied(c));
      CHECK(board.colWalls(c) == p.colClues[c]);
    }
    // One wall short is not solved.
    board.tap(lastRow, lastCol);
    CHECK(!board.solved());
    CHECK(!board.rowSatisfied(lastRow));

    // Floor marks are notes, never part of the answer: marking every remaining
    // cell as floor must not change whether it is solved.
    board.tap(lastRow, lastCol);  // Floor -> Unknown
    board.tap(lastRow, lastCol);  // Unknown -> Wall
    CHECK(board.solved());
    for (int r = 0; r < board.size(); ++r) {
      for (int c = 0; c < board.size(); ++c) {
        if (board.mark(r, c) != dungeon::Mark::Unknown) continue;
        board.tap(r, c);
        board.tap(r, c);
      }
    }
    CHECK(board.solved());

    board.reset();
    CHECK(!board.solved());
    CHECK(!board.touched());
  }
}

// A clue is satisfied by EXACTLY its number of walls, never by "at least".
//
// This is what the filled chip on the board means, so it is worth pinning: an
// implementation that read >= would look correct on a solved board and would
// quietly tell the player a row was done while it held one wall too many.
// Checked from below and from above, on every row and column of every puzzle.
void testSatisfiedMeansExactly() {
  for (int i = 0; i < dungeon::kPuzzleCount; ++i) {
    dungeon::Board board;
    board.load(i);
    const dungeon::Puzzle& p = dungeon::kPuzzles[i];

    // Fill one row at a time, one wall at a time, and confirm the clue is
    // satisfied on exactly the step that reaches its number.
    for (int r = 0; r < board.size(); ++r) {
      board.reset();
      int placed = 0;
      for (int c = 0; c < board.size(); ++c) {
        if (board.isMonster(r, c) || board.isChest(r, c)) continue;
        board.tap(r, c);
        ++placed;
        CHECK(board.rowWalls(r) == placed);
        CHECK(board.rowSatisfied(r) == (placed == p.rowClues[r]));
      }
      // Every row of every puzzle has at least one cell it cannot fill, so a
      // row can always be overfilled past its clue -- and the moment it is,
      // the chip has to go out again.
      if (placed > p.rowClues[r]) CHECK(!board.rowSatisfied(r));
    }

    for (int c = 0; c < board.size(); ++c) {
      board.reset();
      int placed = 0;
      for (int r = 0; r < board.size(); ++r) {
        if (board.isMonster(r, c) || board.isChest(r, c)) continue;
        board.tap(r, c);
        ++placed;
        CHECK(board.colWalls(c) == placed);
        CHECK(board.colSatisfied(c) == (placed == p.colClues[c]));
      }
      if (placed > p.colClues[c]) CHECK(!board.colSatisfied(c));
    }
  }
}

void testRestoreRoundTrip() {
  for (int i = 0; i < dungeon::kPuzzleCount; ++i) {
    dungeon::Board board;
    board.load(i);
    // A scattered position: walls on every third free cell, floor on every
    // fifth, which reaches both masks without being the solution.
    int seen = 0;
    for (int r = 0; r < board.size(); ++r) {
      for (int c = 0; c < board.size(); ++c) {
        if (board.isMonster(r, c) || board.isChest(r, c)) continue;
        ++seen;
        if (seen % 3 == 0) {
          board.tap(r, c);
        } else if (seen % 5 == 0) {
          board.tap(r, c);
          board.tap(r, c);
        }
      }
    }
    dungeon::Board copy;
    copy.restore(board.index(), board.wallMask(), board.floorMask());
    CHECK(copy.index() == board.index());
    CHECK(copy.wallMask() == board.wallMask());
    CHECK(copy.floorMask() == board.floorMask());
    for (int r = 0; r < board.size(); ++r) {
      for (int c = 0; c < board.size(); ++c) CHECK(copy.mark(r, c) == board.mark(r, c));
    }
  }
}

// A save off the SD card is input, not truth. Feeding it nonsense must not
// leave a board that cannot be finished.
void testRestoreRejectsNonsense() {
  const uint64_t everything = ~uint64_t{0};
  for (int i = 0; i < dungeon::kPuzzleCount; ++i) {
    dungeon::Board board;
    board.restore(i, everything, everything);
    const dungeon::Puzzle& p = dungeon::kPuzzles[i];
    CHECK((board.wallMask() & (p.monsters | p.chests)) == 0);
    CHECK((board.floorMask() & (p.monsters | p.chests)) == 0);
    // No cell is both a wall and a floor.
    CHECK((board.wallMask() & board.floorMask()) == 0);
    for (int r = 0; r < board.size(); ++r) {
      for (int c = 0; c < board.size(); ++c) {
        if (board.isMonster(r, c) || board.isChest(r, c)) CHECK(board.mark(r, c) == dungeon::Mark::Unknown);
      }
    }
  }
  dungeon::Board board;
  board.restore(-5, 0, 0);
  CHECK(board.index() == 0);
  board.restore(dungeon::kPuzzleCount + 10, 0, 0);
  CHECK(board.index() == 0);
}

void testProgress() {
  dungeon::Progress progress;
  CHECK(progress.solvedCount() == 0);
  CHECK(progress.nextUnsolved() == 0);
  // Every index, including the ones past the 64-bit boundary the two words
  // exist to cover. An off-by-one there would silently lose the last puzzle.
  for (int i = 0; i < dungeon::kPuzzleCount; ++i) {
    CHECK(!progress.isSolved(i));
    progress.markSolved(i);
    CHECK(progress.isSolved(i));
    CHECK(progress.solvedCount() == i + 1);
    CHECK(progress.nextUnsolved() == (i + 1 < dungeon::kPuzzleCount ? i + 1 : dungeon::kPuzzleCount - 1));
  }
  progress.markSolved(-1);
  progress.markSolved(dungeon::kPuzzleCount);
  CHECK(progress.solvedCount() == dungeon::kPuzzleCount);
  CHECK(!progress.isSolved(-1));
  CHECK(!progress.isSolved(dungeon::kPuzzleCount));
}

void testTiers() {
  // The bank is stored in tier order and the menu depends on it.
  int total = 0;
  for (int tier = 0; tier <= 8; ++tier) {
    const int start = dungeon::tierStart(tier);
    const int count = dungeon::tierCount(tier);
    total += count;
    for (int i = 0; i < count; ++i) CHECK(dungeon::kPuzzles[start + i].tier == tier);
  }
  CHECK(total == dungeon::kPuzzleCount);
  CHECK(dungeon::tierCount(0) == 1);
  for (int tier = 1; tier <= 8; ++tier) CHECK(dungeon::tierCount(tier) == 8);
  for (int i = 1; i < dungeon::kPuzzleCount; ++i) CHECK(dungeon::kPuzzles[i - 1].tier <= dungeon::kPuzzles[i].tier);
}

}  // namespace

int main() {
  validateBank();
  testTapCycle();
  testFixedCellsRefuse();
  testPlayEveryPuzzle();
  testSatisfiedMeansExactly();
  testRestoreRoundTrip();
  testRestoreRejectsNonsense();
  testProgress();
  testTiers();
  std::printf("%d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}

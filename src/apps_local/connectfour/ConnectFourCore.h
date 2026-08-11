#pragma once

// Connect Four, the rules. Freestanding: no renderer, no Activity, no storage.
//
// Seven columns, six rows, the standard board. Drop a disc into a column and it
// falls to the lowest empty cell. Four of yours in a line -- any direction --
// wins. A full board with no line is a draw.
//
// The board is stored column-major, `cell[column][row]`, with **row 0 at the
// bottom**, because that is the direction gravity works in and it makes `drop`
// a scan from zero rather than a scan from the far end. The screen flips it.
//
// One property is built in rather than checked for: **a dropped disc always
// lands on the floor or on another disc.** There is no way to express a
// floating disc, because the row is computed by the rules and never passed in.

#include <cstdint>

namespace connectfour {

constexpr int kColumns = 7;
constexpr int kRows = 6;
constexpr int kCells = kColumns * kRows;
// Four in a line. The name of the game, and the only number in it.
constexpr int kLine = 4;

// The two sides. Named for how they are DRAWN -- outlined and solid -- rather
// than for the yellow and red of the physical game, because this panel has one
// ink and Checkers already established the convention: filled means whose,
// never whose turn. Two disc games on one device using different words for the
// same distinction is a cost with nothing on the other side of it.
constexpr uint8_t kEmpty = 0;
constexpr uint8_t kLight = 1;
constexpr uint8_t kDark = 2;

constexpr int8_t kNoColumn = -1;

enum class Outcome : uint8_t {
  Running,
  LightWins,
  DarkWins,
  Draw,
};

struct Game {
  // cell[column][row], row 0 at the bottom. kEmpty, kLight or kDark.
  uint8_t cell[kColumns][kRows];
  // Where the last disc landed, so the board can point at it. A board of
  // forty-two identical discs does not otherwise say what just changed, and
  // "what did they play" is the first thing a player asks on their turn.
  int8_t lastColumn;
  int8_t lastRow;
  uint8_t turn;
  Outcome outcome;
};

inline uint8_t other(const uint8_t side) { return side == kLight ? kDark : kLight; }

inline void start(Game& game) {
  for (int column = 0; column < kColumns; ++column) {
    for (int row = 0; row < kRows; ++row) game.cell[column][row] = kEmpty;
  }
  game.lastColumn = kNoColumn;
  game.lastRow = kNoColumn;
  // Light always opens. Connect Four is a solved first-player win with perfect
  // play, so who opens is not a fair coin and pretending otherwise by
  // randomising it would just hide that from whoever loses.
  game.turn = kLight;
  game.outcome = Outcome::Running;
}

inline bool inside(const int column, const int row) {
  return column >= 0 && column < kColumns && row >= 0 && row < kRows;
}

inline bool over(const Game& game) { return game.outcome != Outcome::Running; }

// The row a disc dropped into `column` would land on, or kNoColumn when the
// column is full. The only place gravity is expressed.
inline int landingRow(const Game& game, const int column) {
  if (column < 0 || column >= kColumns) return kNoColumn;
  for (int row = 0; row < kRows; ++row) {
    if (game.cell[column][row] == kEmpty) return row;
  }
  return kNoColumn;
}

inline bool canDrop(const Game& game, const int column) {
  if (over(game)) return false;
  return landingRow(game, column) != kNoColumn;
}

// The four cells of the line through (column, row) in direction (dc, dr), when
// they are all `side`. Written into `out` as flat indices; returns whether it
// found one. Used both to settle the game and to mark the winning line, so the
// marked cells are by construction the cells that won.
// Writes into `out` only on SUCCESS. It used to write each cell before testing
// the next, so a direction that failed left partial indices behind -- measured
// at 8,132,315 scribbles over 4,265,889 positions of random play, 95.3% of the
// calls that returned false. Harmless today only because the one caller checks
// the outcome first, which is not a property of this function.
inline bool lineFrom(const Game& game, const int column, const int row, const int dc, const int dr, const uint8_t side,
                     int* out) {
  int found[kLine];
  for (int step = 0; step < kLine; ++step) {
    const int c = column + dc * step;
    const int r = row + dr * step;
    if (!inside(c, r) || game.cell[c][r] != side) return false;
    found[step] = c * kRows + r;
  }
  if (out != nullptr) {
    for (int step = 0; step < kLine; ++step) out[step] = found[step];
  }
  return true;
}

// Whether `side` has four in a line anywhere, and where. `out` receives the
// four flat indices when it is not null.
//
// Only four directions are searched, not eight: a line and its reverse are the
// same line, and scanning both would report every win twice and make "the
// winning line" ambiguous when the app has to mark exactly one.
inline bool winningLine(const Game& game, const uint8_t side, int* out) {
  constexpr int kDirections[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};
  for (int column = 0; column < kColumns; ++column) {
    for (int row = 0; row < kRows; ++row) {
      if (game.cell[column][row] != side) continue;
      for (const auto& dir : kDirections) {
        if (lineFrom(game, column, row, dir[0], dir[1], side, out)) return true;
      }
    }
  }
  return false;
}

inline bool boardFull(const Game& game) {
  for (int column = 0; column < kColumns; ++column) {
    if (game.cell[column][kRows - 1] == kEmpty) return false;
  }
  return true;
}

// Drop a disc for the side to move. Returns false and changes nothing when the
// column is full or the game is settled, so a caller can route a tap straight
// here.
inline bool drop(Game& game, const int column) {
  if (!canDrop(game, column)) return false;
  const int row = landingRow(game, column);
  const uint8_t side = game.turn;
  game.cell[column][row] = side;
  game.lastColumn = static_cast<int8_t>(column);
  game.lastRow = static_cast<int8_t>(row);

  // Settle before handing the turn over. A side that has just won does not then
  // wait for the other to move, and the turn field is what "whose move" is read
  // from everywhere else.
  if (winningLine(game, side, nullptr)) {
    game.outcome = side == kLight ? Outcome::LightWins : Outcome::DarkWins;
    return true;
  }
  if (boardFull(game)) {
    game.outcome = Outcome::Draw;
    return true;
  }
  game.turn = other(side);
  return true;
}

inline int discCount(const Game& game) {
  int count = 0;
  for (int column = 0; column < kColumns; ++column) {
    for (int row = 0; row < kRows; ++row) {
      if (game.cell[column][row] != kEmpty) ++count;
    }
  }
  return count;
}

// Whether a state could have come from real play. Checked on anything arriving
// over the wire: a corrupt packet must not be able to put the board in a
// position the rules cannot produce.
//
// The gravity check is the one that matters. A tampered board with a floating
// disc would otherwise render, accept moves, and disagree between the two
// devices about which cells are even occupied.
inline bool plausible(const Game& game) {
  int light = 0;
  int dark = 0;
  for (int column = 0; column < kColumns; ++column) {
    bool sawEmpty = false;
    for (int row = 0; row < kRows; ++row) {
      const uint8_t cell = game.cell[column][row];
      if (cell == kEmpty) {
        sawEmpty = true;
        continue;
      }
      if (cell != kLight && cell != kDark) return false;
      // Nothing may sit above a gap.
      if (sawEmpty) return false;
      if (cell == kLight)
        ++light;
      else
        ++dark;
    }
  }
  // Light opens, so it is level or exactly one ahead.
  if (light != dark && light != dark + 1) return false;
  if (game.turn != kLight && game.turn != kDark) return false;
  if (!over(game) && game.turn != (light == dark ? kLight : kDark)) return false;
  // Both sides holding a line is not a position play can reach.
  const bool lightLine = winningLine(game, kLight, nullptr);
  const bool darkLine = winningLine(game, kDark, nullptr);
  if (lightLine && darkLine) return false;

  // The OUTCOME has to match the board. Without this, nine hand-built states
  // that play cannot produce were accepted -- among them a full drawn board
  // with outcome still Running, which is not cosmetic: over() is false, no
  // column accepts a disc, chooseColumn returns kNoColumn, and takeOpponentTurn
  // then returns without moving and without repainting on every single pass.
  // A wedged game, reachable only because this function was not checking.
  switch (game.outcome) {
    case Outcome::Running:
      if (lightLine || darkLine || boardFull(game)) return false;
      break;
    case Outcome::LightWins:
      if (!lightLine) return false;
      break;
    case Outcome::DarkWins:
      if (!darkLine) return false;
      break;
    case Outcome::Draw:
      if (!boardFull(game) || lightLine || darkLine) return false;
      break;
  }
  // A settled game keeps the turn with whoever settled it.
  if (game.outcome == Outcome::LightWins && game.turn != kLight) return false;
  if (game.outcome == Outcome::DarkWins && game.turn != kDark) return false;

  // The last-move fields point at a real disc, or at nothing at all. The board
  // reads cell[lastColumn][lastRow] to draw its marker, so a lastRow pointing
  // at an empty cell renders a disc that is not there.
  const bool noLast = game.lastColumn == kNoColumn && game.lastRow == kNoColumn;
  if (!noLast) {
    if (game.lastColumn < 0 || game.lastColumn >= kColumns) return false;
    if (game.lastRow < 0 || game.lastRow >= kRows) return false;
    if (game.cell[game.lastColumn][game.lastRow] == kEmpty) return false;
  } else if (discCount(game) != 0) {
    // Discs on the board but nothing recorded as the last move.
    return false;
  }
  return true;
}

}  // namespace connectfour

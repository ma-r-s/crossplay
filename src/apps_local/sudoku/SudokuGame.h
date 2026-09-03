#pragma once

// The game the player actually touches: the puzzle, their pencil, their undo
// and their clock. Freestanding, like the rules underneath it, and it is also
// the save file: `Game` is written to the card as-is.
//
// Two decisions here are worth reading before changing anything.
//
// **There is one way a cell can change, and it is `tapCell`.** Not one per
// situation: the same call handles writing, overwriting, clearing and picking a
// digit up off the board, because which of those happens is a fact about the
// cell rather than about a mode. This fork has a rule that touch and buttons
// must converge on a single function; the same argument applies to a grid where
// four different gestures would otherwise grow four different code paths that
// drift.
//
// **A note is never edited by anything but the player.** Placing a 5 does not
// strike 5 from the notes of its peers, because that would make undo a
// twenty-cell operation and pencil marks something the game quietly rewrites
// behind you. Instead `visibleNotes` hides the digits a cell can no longer
// take, computed at draw time. The stored marks stay exactly as pencilled, undo
// stays one cell wide, and un-doing a placement brings the marks back on its
// own with no bookkeeping at all.

#include "SudokuCore.h"

namespace sudoku {

// Deep enough to walk back a wrong line of reasoning, short enough to stay a
// fixed array in a struct that is also a save file.
constexpr int kUndoDepth = 24;

// "No cell", used by the hint and by the finger. kCells rather than -1 so it
// survives being a uint8_t in the save.
constexpr uint8_t kNoCell = kCells;

struct Change {
  uint8_t cell;
  uint8_t entry;  // what the cell held before
  Mask note;      // what was pencilled in it before
};

struct Game {
  Puzzle puzzle;

  uint8_t entry[kCells] = {};  // the player's digits, 0 for empty
  Mask note[kCells] = {};      // their pencil marks

  Change undo[kUndoDepth] = {};
  uint8_t undoCount = 0;  // how many entries are live, at most kUndoDepth
  uint8_t undoNext = 0;   // where the next change goes

  // The digit the pad is holding. Always valid: there is no unarmed state, so
  // a tap on the board can never be a tap that does nothing.
  uint8_t armed = 1;

  // The cell the last HINT named, or kNoCell. Cleared as soon as it is filled,
  // so a stale hint cannot sit on the board pointing at a solved cell.
  uint8_t hintCell = kNoCell;
  uint8_t hintDigit = 0;
  uint8_t hintTechnique = 0;  // a Technique, stored small
  uint8_t hintsUsed = 0;

  uint8_t solvedFlag = 0;
  uint8_t spare[3] = {0, 0, 0};

  uint32_t elapsedMs = 0;
};

// What a cell shows: its clue if it has one, otherwise the player's digit.
inline uint8_t valueAt(const Game& game, const int cell) {
  return game.puzzle.given[cell] != 0 ? game.puzzle.given[cell] : game.entry[cell];
}

inline bool isGiven(const Game& game, const int cell) { return game.puzzle.given[cell] != 0; }

// Which digits are already taken by a cell's peers. Not stored: it is a fact
// about the board, and storing a fact about the board is how two copies of it
// end up disagreeing.
inline Mask takenAround(const Game& game, const int cell) {
  Mask taken = 0;
  for (int other = 0; other < kCells; ++other) {
    if (!arePeers(cell, other)) continue;
    const uint8_t value = valueAt(game, other);
    if (value != 0) taken = static_cast<Mask>(taken | bitFor(value));
  }
  return taken;
}

// The marks worth drawing: what was pencilled, minus what a peer has since
// taken. See the header comment for why this is computed rather than stored.
inline Mask visibleNotes(const Game& game, const int cell) {
  if (valueAt(game, cell) != 0) return 0;
  return static_cast<Mask>(game.note[cell] & ~takenAround(game, cell));
}

// A digit that clashes with a peer. This is the whole of the game's mistake
// feedback: it is derivable from the board alone, it is what a person would
// notice on paper, and it never reveals the answer. A wrong digit that happens
// to clash with nothing yet stays on the board looking fine, which is correct.
inline bool isClashing(const Game& game, const int cell) {
  const uint8_t value = valueAt(game, cell);
  if (value == 0) return false;
  for (int other = 0; other < kCells; ++other) {
    if (!arePeers(cell, other)) continue;
    if (valueAt(game, other) == value) return true;
  }
  return false;
}

inline int emptyCount(const Game& game) {
  int count = 0;
  for (int cell = 0; cell < kCells; ++cell) {
    if (valueAt(game, cell) == 0) ++count;
  }
  return count;
}

// How many of one digit are on the board, clues included. Nine means that digit
// is finished, which is what dims its key on the pad.
inline int placedCount(const Game& game, const int digit) {
  int count = 0;
  for (int cell = 0; cell < kCells; ++cell) {
    if (valueAt(game, cell) == digit) ++count;
  }
  return count;
}

// The first cell whose digit disagrees with the answer, or kNoCell. Only the
// HINT door reads this: the board never marks it, because a game that tells you
// which digit is wrong the moment you write it is a game that solves itself.
inline int firstWrong(const Game& game) {
  for (int cell = 0; cell < kCells; ++cell) {
    if (game.entry[cell] == 0) continue;
    if (game.entry[cell] != game.puzzle.solution[cell]) return cell;
  }
  return kNoCell;
}

inline bool isSolved(const Game& game) {
  for (int cell = 0; cell < kCells; ++cell) {
    if (valueAt(game, cell) != game.puzzle.solution[cell]) return false;
  }
  return true;
}

inline void pushUndo(Game& game, const int cell) {
  game.undo[game.undoNext] = Change{static_cast<uint8_t>(cell), game.entry[cell], game.note[cell]};
  game.undoNext = static_cast<uint8_t>((game.undoNext + 1) % kUndoDepth);
  if (game.undoCount < kUndoDepth) ++game.undoCount;
}

inline bool canUndo(const Game& game) { return game.undoCount > 0; }

inline bool undoOnce(Game& game) {
  if (game.undoCount == 0) return false;
  game.undoNext = static_cast<uint8_t>((game.undoNext + kUndoDepth - 1) % kUndoDepth);
  const Change& change = game.undo[game.undoNext];
  game.entry[change.cell] = change.entry;
  game.note[change.cell] = change.note;
  --game.undoCount;
  game.solvedFlag = 0;
  return true;
}

inline void clearHintIfSpent(Game& game) {
  if (game.hintCell == kNoCell) return;
  if (valueAt(game, game.hintCell) != 0) {
    game.hintCell = kNoCell;
    game.hintDigit = 0;
  }
}

// A tap on a cell. Every case does something, which is the point: a dead tap on
// a panel with no press feedback is indistinguishable from a tap that missed.
//
//   a clue          -> pick that digit up (the pad arms it, the board lights it)
//   holds the armed -> clear it
//   holds another   -> overwrite with the armed digit
//   empty           -> write the armed digit
inline void tapCell(Game& game, const int cell) {
  if (isGiven(game, cell)) {
    game.armed = game.puzzle.given[cell];
    return;
  }
  pushUndo(game, cell);
  if (game.entry[cell] == game.armed) {
    game.entry[cell] = 0;
  } else {
    game.entry[cell] = game.armed;
  }
  game.note[cell] = 0;
  clearHintIfSpent(game);
  if (isSolved(game)) game.solvedFlag = 1;
}

// What the front door is offering, DERIVED from the save rather than latched
// beside it.
//
// `Puzzle` carries the level it was carved at, so "is the menu showing the
// level you were playing" is a fact with an answer at any moment. A flag set on
// every DIFFICULTY tap cannot represent COMING BACK: the row cycles
// `(level + 1) % 4`, so four taps returned the menu to where it started with
// the flag still set, and the door under a grid that was still on the screen
// silently became the one that overwrites it.
//
// One enum rather than two predicates because the button and the caption above
// it are two renderings of a single fact. Asking twice is how they disagree,
// and a door that does something other than what it says is the bug this whole
// file exists to have stopped having.
enum class MenuOffer : uint8_t {
  Fresh,       // nothing saved
  Resume,      // an unsolved game at the level the menu is showing
  Solved,      // the saved game is finished; the door replaces it
  OtherLevel,  // a saved game, but the menu is pointed somewhere else
};

inline MenuOffer menuOffer(const Game& game, const bool hasGame, const Level menuLevel) {
  if (!hasGame) return MenuOffer::Fresh;
  if (menuLevel != game.puzzle.level) return MenuOffer::OtherLevel;
  return game.solvedFlag != 0 ? MenuOffer::Solved : MenuOffer::Resume;
}

// Resume is the ONLY offer that opens the saved grid; every other one carves a
// new puzzle over it. Keeping that asymmetry in one line is the point.
inline bool canResume(const Game& game, const bool hasGame, const Level menuLevel) {
  return menuOffer(game, hasGame, menuLevel) == MenuOffer::Resume;
}

// A hold on a cell pencils the armed digit in, or rubs it out. Same split as
// Minesweeper's tap-to-dig and hold-to-flag, deliberately: it is the one
// two-gesture idiom this device already has, so it is the one a player has
// already met.
inline void holdCell(Game& game, const int cell) {
  if (isGiven(game, cell)) {
    game.armed = game.puzzle.given[cell];
    return;
  }
  pushUndo(game, cell);
  game.entry[cell] = 0;
  game.note[cell] = static_cast<Mask>(game.note[cell] ^ bitFor(game.armed));
  game.solvedFlag = 0;
}

inline void startGame(Game& game, const Puzzle& puzzle) {
  game = Game{};
  game.puzzle = puzzle;
  // The scarcest digit is the one worth starting on, and it costs nothing to
  // work out. Arming 1 every time would put the pad on whatever digit the
  // puzzle happens to have most of.
  int scarcest = 1;
  int fewest = kSize + 1;
  for (int digit = 1; digit <= kSize; ++digit) {
    const int placed = placedCount(game, digit);
    if (placed < fewest) {
      fewest = placed;
      scarcest = digit;
    }
  }
  game.armed = static_cast<uint8_t>(scarcest);
}

// What the player has finished, per level. Kept beside the game rather than in
// it: a record outlives the puzzle it was set on.
struct Record {
  uint16_t solved[kLevelCount] = {};
  uint32_t bestMs[kLevelCount] = {};
  uint16_t hintsTaken = 0;
  uint16_t spare = 0;
};

inline void recordSolve(Record& record, const Level level, const uint32_t elapsedMs, const int hintsUsed) {
  const int index = static_cast<int>(level);
  if (record.solved[index] < 0xFFFF) ++record.solved[index];
  // A solve leaning on hints does not set a best time. The clock is a claim
  // about you, and a hinted run is a claim about the solver.
  if (hintsUsed == 0 && (record.bestMs[index] == 0 || elapsedMs < record.bestMs[index])) {
    record.bestMs[index] = elapsedMs;
  }
  record.hintsTaken = static_cast<uint16_t>(record.hintsTaken + hintsUsed);
}

inline int totalSolved(const Record& record) {
  int total = 0;
  for (int i = 0; i < kLevelCount; ++i) total += record.solved[i];
  return total;
}

}  // namespace sudoku

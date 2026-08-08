#pragma once

// Checkers (English draughts), the rules. Freestanding: no renderer, no
// Activity, no storage.
//
// Eight by eight, dark squares only, twelve pieces a side. Men step diagonally
// forward one; kings step both ways. Reaching the far row promotes.
//
// **Captures are mandatory, and a multi-jump must be completed.** That single
// rule is what makes this checkers rather than a diagonal shuffle, and it is
// built into move generation rather than checked afterwards: `moves()` returns
// jumps ONLY when any jump exists, so an illegal quiet move is not a move this
// core can express. A validator that rejected it later would be the same result
// bolted on, and every caller would have to remember to ask.
//
// The board is the wire format: 64 bytes plus three, trivially copyable, so two
// devices share one description of a game and cannot drift.

#include <cstdint>

namespace checkers {

constexpr int kSize = 8;
constexpr int kCells = kSize * kSize;
// The most moves a position can offer. A king in open play has 4 steps; a
// multi-jump tree is bounded by the pieces it can take. 48 is comfortably above
// any real position and keeps the move list a fixed array.
constexpr int kMaxMoves = 48;

constexpr uint8_t kEmpty = 0;
constexpr uint8_t kPiece = 1 << 0;  // occupied
constexpr uint8_t kDark = 1 << 1;   // the dark side; light is the other
constexpr uint8_t kKing = 1 << 2;

// Seats. Light moves first, as in the printed rules.
constexpr uint8_t kLight = 0;
constexpr uint8_t kDarkSeat = 1;

struct Move {
  // Where it starts and ends. For a multi-jump the end is the FINAL square:
  // the whole sequence is one move, because a half-finished jump is not a legal
  // position and nothing should be able to represent one.
  uint8_t from;
  uint8_t to;
  // Squares captured along the way, and how many. A jump of three takes three.
  uint8_t taken[3];
  uint8_t takenCount;
};

struct Game {
  uint8_t cell[kCells];
  uint8_t turn;
  // Set when a side has no move at all, which is a loss for them. Derived on
  // demand rather than stored -- see winner().
  uint8_t pad0;
  uint8_t pad1;
};

inline bool inside(const int file, const int rank) { return file >= 0 && file < kSize && rank >= 0 && rank < kSize; }

inline int indexOf(const int file, const int rank) { return rank * kSize + file; }

// Only dark squares are played on, which is every square where file and rank
// have the same parity in this layout.
inline bool playable(const int file, const int rank) { return ((file + rank) & 1) == 1; }

inline bool occupied(const Game& game, const int index) { return (game.cell[index] & kPiece) != 0; }
inline bool isDark(const Game& game, const int index) { return (game.cell[index] & kDark) != 0; }
inline bool isKing(const Game& game, const int index) { return (game.cell[index] & kKing) != 0; }
inline uint8_t ownerOf(const Game& game, const int index) { return isDark(game, index) ? kDarkSeat : kLight; }

inline void start(Game& game) {
  for (int i = 0; i < kCells; ++i) game.cell[i] = kEmpty;
  for (int rank = 0; rank < kSize; ++rank) {
    for (int file = 0; file < kSize; ++file) {
      if (!playable(file, rank)) continue;
      // Dark occupies the top three rows, light the bottom three, so light
      // advances up the board and dark down it.
      if (rank < 3) game.cell[indexOf(file, rank)] = kPiece | kDark;
      if (rank > 4) game.cell[indexOf(file, rank)] = kPiece;
    }
  }
  game.turn = kLight;
  game.pad0 = 0;
  game.pad1 = 0;
}

// Which way a piece may step. A man moves toward the far side only; a king both.
inline void stepDirections(const bool king, const uint8_t owner, int (&dr)[4], int& count) {
  count = 0;
  const int forward = owner == kLight ? -1 : 1;
  dr[count++] = forward;
  if (king) dr[count++] = -forward;
}

// Collect every jump sequence starting at `index`, depth first.
//
// A multi-jump is ONE move ending on the final square, because a half-finished
// jump is not a legal position and nothing here should be able to represent
// one. The recursion walks a scratch copy with the captured men lifted, which
// is what makes a chain of jumps see the board the way a player does.
inline void collectJumps(const Game& game, const int index, const Move& sofar, Move* out, int& count) {
  const int file = index % kSize;
  const int rank = index / kSize;
  const uint8_t owner = ownerOf(game, index);
  int dr[4] = {};
  int dirs = 0;
  stepDirections(isKing(game, index), owner, dr, dirs);

  for (int d = 0; d < dirs; ++d) {
    for (int df = -1; df <= 1; df += 2) {
      const int overFile = file + df;
      const int overRank = rank + dr[d];
      const int landFile = file + df * 2;
      const int landRank = rank + dr[d] * 2;
      if (!inside(landFile, landRank)) continue;
      const int over = indexOf(overFile, overRank);
      const int land = indexOf(landFile, landRank);
      if (!occupied(game, over) || ownerOf(game, over) == owner) continue;
      if (occupied(game, land)) continue;
      // A man already taken cannot be taken twice in the same chain.
      bool already = false;
      for (int t = 0; t < sofar.takenCount; ++t) {
        if (sofar.taken[t] == over) already = true;
      }
      if (already || sofar.takenCount >= 3) continue;

      Game next = game;
      next.cell[land] = next.cell[index];
      next.cell[index] = kEmpty;
      next.cell[over] = kEmpty;

      Move chain = sofar;
      chain.to = static_cast<uint8_t>(land);
      chain.taken[chain.takenCount++] = static_cast<uint8_t>(over);

      // Promotion ends a chain -- the standard rule -- and it needs no branch
      // here, which is worth saying because the first version had one and a
      // mutation test proved it dead. A man promotes only on the last row, and
      // its forward direction from the last row is off the board, so the
      // recursion below finds nothing and stops anyway. The scratch board also
      // does not promote (play() does), so the piece never gains king powers
      // mid-chain. The rule is enforced by geometry rather than by a check that
      // could rot.
      const int before = count;
      collectJumps(next, land, chain, out, count);
      if (count == before && count < kMaxMoves) out[count++] = chain;
    }
  }
}

// Every legal move for the side to play.
//
// Jumps only, when any jump exists. Captures are mandatory in this game, and
// making that true HERE rather than in a validator means an illegal quiet move
// is not something this core can express -- no caller has to remember to ask.
inline int moves(const Game& game, Move* out) {
  int count = 0;

  for (int index = 0; index < kCells; ++index) {
    if (!occupied(game, index) || ownerOf(game, index) != game.turn) continue;
    Move seed{};
    seed.from = static_cast<uint8_t>(index);
    seed.to = static_cast<uint8_t>(index);
    seed.takenCount = 0;
    collectJumps(game, index, seed, out, count);
  }
  if (count > 0) return count;

  for (int index = 0; index < kCells; ++index) {
    if (!occupied(game, index) || ownerOf(game, index) != game.turn) continue;
    const int file = index % kSize;
    const int rank = index / kSize;
    int dr[4] = {};
    int dirs = 0;
    stepDirections(isKing(game, index), game.turn, dr, dirs);
    for (int d = 0; d < dirs; ++d) {
      for (int df = -1; df <= 1; df += 2) {
        const int toFile = file + df;
        const int toRank = rank + dr[d];
        if (!inside(toFile, toRank)) continue;
        const int to = indexOf(toFile, toRank);
        if (occupied(game, to)) continue;
        if (count >= kMaxMoves) continue;
        Move step{};
        step.from = static_cast<uint8_t>(index);
        step.to = static_cast<uint8_t>(to);
        step.takenCount = 0;
        out[count++] = step;
      }
    }
  }
  return count;
}

// Apply a move, promoting and handing over. Refuses anything `moves()` did not
// offer, so a stale tap cannot half-apply and desync a two-device match.
inline bool play(Game& game, const Move& move) {
  Move legal[kMaxMoves];
  const int count = moves(game, legal);
  for (int i = 0; i < count; ++i) {
    if (legal[i].from != move.from || legal[i].to != move.to) continue;

    const Move& chosen = legal[i];
    game.cell[chosen.to] = game.cell[chosen.from];
    game.cell[chosen.from] = kEmpty;
    for (int t = 0; t < chosen.takenCount; ++t) game.cell[chosen.taken[t]] = kEmpty;

    const int toRank = chosen.to / kSize;
    const bool light = ownerOf(game, chosen.to) == kLight;
    if ((light && toRank == 0) || (!light && toRank == kSize - 1)) game.cell[chosen.to] |= kKing;

    game.turn = game.turn == kLight ? kDarkSeat : kLight;
    return true;
  }
  return false;
}

inline int pieceCount(const Game& game, const uint8_t owner) {
  int count = 0;
  for (int index = 0; index < kCells; ++index) {
    if (occupied(game, index) && ownerOf(game, index) == owner) ++count;
  }
  return count;
}

// The side to play has no move, which is a loss for them -- by capture or by
// being blocked, which checkers treats the same way.
inline bool over(const Game& game) {
  Move scratch[kMaxMoves];
  return moves(game, scratch) == 0;
}

// The winner, or -1 while the game is running. Only meaningful once over().
inline int winner(const Game& game) {
  if (!over(game)) return -1;
  return game.turn == kLight ? kDarkSeat : kLight;
}

}  // namespace checkers

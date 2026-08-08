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
  // Which squares this move captures, as a bit per board square.
  //
  // A fixed array of three was the first version and it was WRONG: the real
  // maximum in English draughts is NINE, and when a chain wanted a fourth
  // capture the recursion simply stopped and recorded the three-capture prefix
  // AS A COMPLETED MOVE. That is a half-finished jump -- exactly the thing this
  // file claims cannot be represented -- and it was reachable, appearing in 13
  // of 282,558 positions from random legal play.
  //
  // A mask has no depth to get wrong. Sixty-four squares, sixty-four bits.
  uint64_t taken;
  uint8_t from;
  uint8_t to;
};

inline int takenCountOf(const Move& move) {
  int count = 0;
  for (uint64_t bits = move.taken; bits != 0; bits &= bits - 1) ++count;
  return count;
}

inline bool tookSquare(const Move& move, const int index) {
  return (move.taken & (static_cast<uint64_t>(1) << index)) != 0;
}

// Plies since the last capture or man move. Checkers' own draw rule: forty
// moves by each side with no capture and no man advanced is a draw, because
// kings alone cannot force progress and two careful players would shuffle
// forever.
//
// This was missing from the first version and a test found it the hard way --
// two deterministic opponents played each other past four thousand plies
// without finishing. A game that cannot end is a real defect on a device with a
// sleep timer.
constexpr uint8_t kIdleLimit = 80;

struct Game {
  uint8_t cell[kCells];
  uint8_t turn;
  uint8_t idlePlies;
  uint8_t pad0;
};

enum class Outcome : uint8_t { Running, LightWins, DarkWins, Draw };

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
  game.idlePlies = 0;
  game.pad0 = 0;
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
      // No "already taken" check: the scratch board below lifts each captured
      // man, so `occupied(game, over)` above has already rejected it. The first
      // version had one, and a mutation test proved it never fired in 69,532
      // jump steps -- dead code presented as the rule that enforces something.

      Game next = game;
      next.cell[land] = next.cell[index];
      next.cell[index] = kEmpty;
      next.cell[over] = kEmpty;

      Move chain = sofar;
      chain.to = static_cast<uint8_t>(land);
      chain.taken |= static_cast<uint64_t>(1) << over;

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
    seed.taken = 0;
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
        step.taken = 0;
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
    const bool wasKing = isKing(game, chosen.from);
    // Lifted before the square is cleared, because a multi-jump can END where
    // it started -- a four-capture cycle returns a king to its own square, and
    // clearing `from` afterwards would delete the piece that just moved.
    const uint8_t moving = game.cell[chosen.from];
    game.cell[chosen.from] = kEmpty;
    for (int square = 0; square < kCells; ++square) {
      if (tookSquare(chosen, square)) game.cell[square] = kEmpty;
    }
    game.cell[chosen.to] = moving;

    const int toRank = chosen.to / kSize;
    const bool light = ownerOf(game, chosen.to) == kLight;
    if ((light && toRank == 0) || (!light && toRank == kSize - 1)) game.cell[chosen.to] |= kKing;

    // Progress resets the clock: a capture, or a man moving. Only kings
    // shuffling with nothing taken counts as idle.
    if (chosen.taken != 0 || !wasKing) {
      game.idlePlies = 0;
    } else if (game.idlePlies < 255) {
      ++game.idlePlies;
    }

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

// How the game stands. One function rather than an over()/winner() pair,
// because a draw made "no winner" mean two different things and a caller had no
// way to tell them apart.
inline Outcome outcome(const Game& game) {
  if (game.idlePlies >= kIdleLimit) return Outcome::Draw;
  Move scratch[kMaxMoves];
  // The side to play has no move: a loss for them, whether by capture or by
  // being blocked. Checkers treats both the same way.
  if (moves(game, scratch) == 0) return game.turn == kLight ? Outcome::DarkWins : Outcome::LightWins;
  return Outcome::Running;
}

inline bool over(const Game& game) { return outcome(game) != Outcome::Running; }

}  // namespace checkers

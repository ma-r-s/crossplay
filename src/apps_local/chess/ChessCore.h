#pragma once

// Chess rules engine: board state, move generation, make/unmake.
//
// Freestanding C++17 on purpose. No Arduino, no ESP-IDF, no renderer, no heap.
// That is what lets the whole ruleset be verified on the host with plain c++
// (see test/run.sh), which matters because move generation is where chess bugs
// hide: castling through check, en passant, promotion, discovered pins.
//
// Allocation: none. Every structure here is a fixed-size value type, so the
// caller decides where it lives. MoveList is ~1KB, too big for a C3 stack
// frame, so search and UI code must hold it as a member or static, never as a
// local. See ChessEngine and ChessActivity.

#include <cstdint>

namespace chess {

// Squares are 0 = a1 .. 63 = h8. file = sq & 7, rank = sq >> 3, so white
// advances by +8. kNoSquare marks "none" (an absent en passant target).
using Square = int8_t;
constexpr Square kNoSquare = -1;

// A piece is a type in the low 3 bits plus a colour bit, so `Empty` is 0 and a
// single byte per square keeps the board at 64 bytes.
enum Piece : uint8_t {
  Empty = 0,
  Pawn = 1,
  Knight = 2,
  Bishop = 3,
  Rook = 4,
  Queen = 5,
  King = 6,
  BlackFlag = 8,
};

constexpr uint8_t pieceType(const uint8_t piece) { return piece & 7; }
constexpr bool isBlack(const uint8_t piece) { return (piece & BlackFlag) != 0; }
constexpr bool isWhitePiece(const uint8_t piece) { return piece != Empty && !isBlack(piece); }
constexpr bool isBlackPiece(const uint8_t piece) { return piece != Empty && isBlack(piece); }
constexpr bool isColour(const uint8_t piece, const bool white) { return piece != Empty && isBlack(piece) != white; }

enum CastleRight : uint8_t {
  WhiteKingSide = 1,
  WhiteQueenSide = 2,
  BlackKingSide = 4,
  BlackQueenSide = 8,
};

enum MoveFlag : uint8_t {
  FlagNone = 0,
  FlagCapture = 1,
  FlagDoublePush = 2,
  FlagEnPassant = 4,
  FlagCastle = 8,
  FlagPromotion = 16,
};

struct Move {
  uint8_t from = 0;
  uint8_t to = 0;
  uint8_t promotion = Empty;  // piece TYPE only (Queen/Rook/Bishop/Knight)
  uint8_t flags = FlagNone;

  bool operator==(const Move& other) const {
    return from == other.from && to == other.to && promotion == other.promotion;
  }
};

// Everything makeMove() destroys and unmakeMove() must put back. The board
// itself is restored by moving pieces the other way, so only the irreversible
// state lives here.
struct Undo {
  uint8_t captured = Empty;
  uint8_t castling = 0;
  Square epSquare = kNoSquare;
  uint8_t halfmoveClock = 0;
};

struct Position {
  uint8_t squares[64] = {};
  bool whiteToMove = true;
  uint8_t castling = 0;
  Square epSquare = kNoSquare;
  uint8_t halfmoveClock = 0;
  uint16_t fullmoveNumber = 1;
};

// 218 is the highest legal move count found in any real position; 256 leaves
// headroom and keeps the struct a round size.
constexpr int kMaxMoves = 256;

struct MoveList {
  Move moves[kMaxMoves];
  int count = 0;

  void add(const Move& move) {
    if (count < kMaxMoves) moves[count++] = move;
  }
  void clear() { count = 0; }
};

// Sets up the standard opening position.
void setStartPosition(Position& position);

// Parses Forsyth-Edwards Notation. Returns false and leaves `position`
// untouched if the record is malformed. Accepts records truncated after the
// en passant field (clocks default to 0 and 1).
bool parseFen(const char* fen, Position& position);

// Writes the position as FEN into `out`, which must hold at least 90 bytes.
// The inverse of parseFen, so a saved game round-trips.
void positionToFen(const Position& position, char* out);

// True when `square` is attacked by any piece of the given colour.
bool isSquareAttacked(const Position& position, Square square, bool byWhite);

// True when the side to move is in check.
bool isInCheck(const Position& position);

// Appends every move that is legal for the side to move. Pseudo-legal moves
// that leave the king attacked are filtered out, so callers never have to
// re-check legality.
void generateLegalMoves(const Position& position, MoveList& list);

void makeMove(Position& position, const Move& move, Undo& undo);
void unmakeMove(Position& position, const Move& move, const Undo& undo);

// Node count at the given depth. The standard correctness measure for a move
// generator: it exercises castling, en passant, promotion and pin handling in
// combination, which spot-checks never reach.
uint64_t perft(Position& position, int depth);

// Writes long algebraic notation ("e2e4", "e7e8q") into `out`, which must hold
// at least 6 bytes. Used by tests and move logging.
void moveToString(const Move& move, char* out);

// Writes standard algebraic notation ("e4", "Nxf3", "O-O", "exd8=Q+") into
// `out`, which must hold at least 10 bytes. `position` is the position BEFORE
// the move: disambiguation depends on what else could have reached that square,
// and the check/mate suffix depends on what happens after.
void moveToSan(const Position& position, const Move& move, char* out);

// A 64-bit fingerprint of everything that makes two positions the same move
// for move: the pieces, the side to move, the castling rights and the en
// passant square. Repetition is the only caller, and it hashes once per ply,
// so there is no incremental update to be had -- which is why this is FNV-1a
// over the struct rather than a Zobrist table nothing would update in place.
//
// The en passant square is included as it stands. Strictly, two positions are
// the same when the same moves are available, so an en passant square no pawn
// can actually capture towards should not tell them apart. Including it raw
// can therefore MISS a repetition; it can never invent one, and a draw that is
// not there is the only error worth avoiding here.
uint64_t positionKey(const Position& position);

// How many times the last key in `keys` already appears, itself included,
// looking back at most `window` entries. Threefold repetition is this
// returning 3 or more.
//
// `window` is the halfmove clock: a pawn move or a capture is irreversible, so
// no position before the last one can ever come back, and the clock counts
// exactly those plies. That is a correctness bound rather than an
// optimisation. Without it a position from before a capture -- unreachable
// now, and reached by a different game -- would be counted as a repetition.
int repetitionCount(const uint64_t* keys, int count, int window);

}  // namespace chess

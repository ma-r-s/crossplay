#include "ChessCore.h"

#include <initializer_list>

namespace chess {
namespace {

constexpr int fileOf(const int square) { return square & 7; }
constexpr int rankOf(const int square) { return square >> 3; }
constexpr bool onBoard(const int file, const int rank) { return file >= 0 && file < 8 && rank >= 0 && rank < 8; }
constexpr int squareAt(const int file, const int rank) { return rank * 8 + file; }

struct Delta {
  int8_t file;
  int8_t rank;
};

constexpr Delta kKnightDeltas[8] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
constexpr Delta kKingDeltas[8] = {{0, 1}, {1, 1}, {1, 0}, {1, -1}, {0, -1}, {-1, -1}, {-1, 0}, {-1, 1}};
constexpr Delta kRookDeltas[4] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};
constexpr Delta kBishopDeltas[4] = {{1, 1}, {1, -1}, {-1, -1}, {-1, 1}};

Square findKing(const Position& position, const bool white) {
  const uint8_t target = static_cast<uint8_t>(King | (white ? 0 : BlackFlag));
  for (int square = 0; square < 64; ++square) {
    if (position.squares[square] == target) return static_cast<Square>(square);
  }
  return kNoSquare;  // only reachable from a hand-built position with no king
}

bool hasPieceAt(const Position& position, const int file, const int rank, const uint8_t type, const bool white) {
  if (!onBoard(file, rank)) return false;
  const uint8_t piece = position.squares[squareAt(file, rank)];
  return piece != Empty && pieceType(piece) == type && isBlack(piece) != white;
}

// Walks outward until it hits something. A slider of the right kind on the
// first occupied square attacks; anything else blocks the ray.
bool attackedBySlider(const Position& position, const int file, const int rank, const Delta* deltas,
                      const uint8_t sliderType, const bool byWhite) {
  for (int direction = 0; direction < 4; ++direction) {
    int f = file + deltas[direction].file;
    int r = rank + deltas[direction].rank;
    while (onBoard(f, r)) {
      const uint8_t piece = position.squares[squareAt(f, r)];
      if (piece != Empty) {
        if (isBlack(piece) != byWhite) {
          const uint8_t type = pieceType(piece);
          if (type == sliderType || type == Queen) return true;
        }
        break;
      }
      f += deltas[direction].file;
      r += deltas[direction].rank;
    }
  }
  return false;
}

void addPawnMoves(MoveList& list, const int from, const int to, const uint8_t flags, const bool promoting) {
  if (!promoting) {
    list.add(Move{static_cast<uint8_t>(from), static_cast<uint8_t>(to), Empty, flags});
    return;
  }
  const uint8_t promotionFlags = static_cast<uint8_t>(flags | FlagPromotion);
  for (const uint8_t type : {Queen, Rook, Bishop, Knight}) {
    list.add(Move{static_cast<uint8_t>(from), static_cast<uint8_t>(to), type, promotionFlags});
  }
}

void generatePseudoLegal(const Position& position, MoveList& list) {
  const bool white = position.whiteToMove;
  const int forward = white ? 1 : -1;
  const int startRank = white ? 1 : 6;
  const int promotionRank = white ? 7 : 0;

  for (int from = 0; from < 64; ++from) {
    const uint8_t piece = position.squares[from];
    if (!isColour(piece, white)) continue;
    const int file = fileOf(from);
    const int rank = rankOf(from);

    switch (pieceType(piece)) {
      case Pawn: {
        const int oneRank = rank + forward;
        if (onBoard(file, oneRank) && position.squares[squareAt(file, oneRank)] == Empty) {
          addPawnMoves(list, from, squareAt(file, oneRank), FlagNone, oneRank == promotionRank);
          const int twoRank = rank + 2 * forward;
          if (rank == startRank && position.squares[squareAt(file, twoRank)] == Empty) {
            list.add(
                Move{static_cast<uint8_t>(from), static_cast<uint8_t>(squareAt(file, twoRank)), Empty, FlagDoublePush});
          }
        }
        for (const int captureFile : {file - 1, file + 1}) {
          if (!onBoard(captureFile, oneRank)) continue;
          const int to = squareAt(captureFile, oneRank);
          const uint8_t target = position.squares[to];
          if (target != Empty && isBlack(target) == white) {
            addPawnMoves(list, from, to, FlagCapture, oneRank == promotionRank);
          } else if (target == Empty && position.epSquare != kNoSquare && to == position.epSquare) {
            list.add(Move{static_cast<uint8_t>(from), static_cast<uint8_t>(to), Empty,
                          static_cast<uint8_t>(FlagCapture | FlagEnPassant)});
          }
        }
        break;
      }
      case Knight:
      case King: {
        const Delta* deltas = pieceType(piece) == Knight ? kKnightDeltas : kKingDeltas;
        for (int i = 0; i < 8; ++i) {
          const int f = file + deltas[i].file;
          const int r = rank + deltas[i].rank;
          if (!onBoard(f, r)) continue;
          const uint8_t target = position.squares[squareAt(f, r)];
          if (isColour(target, white)) continue;
          list.add(Move{static_cast<uint8_t>(from), static_cast<uint8_t>(squareAt(f, r)), Empty,
                        static_cast<uint8_t>(target == Empty ? FlagNone : FlagCapture)});
        }
        break;
      }
      case Bishop:
      case Rook:
      case Queen: {
        const uint8_t type = pieceType(piece);
        for (int set = 0; set < 2; ++set) {
          if (set == 0 && type == Bishop) continue;
          if (set == 1 && type == Rook) continue;
          const Delta* deltas = set == 0 ? kRookDeltas : kBishopDeltas;
          for (int direction = 0; direction < 4; ++direction) {
            int f = file + deltas[direction].file;
            int r = rank + deltas[direction].rank;
            while (onBoard(f, r)) {
              const uint8_t target = position.squares[squareAt(f, r)];
              if (isColour(target, white)) break;
              list.add(Move{static_cast<uint8_t>(from), static_cast<uint8_t>(squareAt(f, r)), Empty,
                            static_cast<uint8_t>(target == Empty ? FlagNone : FlagCapture)});
              if (target != Empty) break;
              f += deltas[direction].file;
              r += deltas[direction].rank;
            }
          }
        }
        break;
      }
      default:
        break;
    }
  }

  // Castling. Rights alone are not enough: the squares between must be empty,
  // and the king may neither start in check nor pass through an attacked
  // square (landing square included).
  const int homeRank = white ? 0 : 7;
  const int kingSquare = squareAt(4, homeRank);
  const uint8_t kingSideRight = white ? WhiteKingSide : BlackKingSide;
  const uint8_t queenSideRight = white ? WhiteQueenSide : BlackQueenSide;
  if (position.squares[kingSquare] == static_cast<uint8_t>(King | (white ? 0 : BlackFlag)) &&
      !isSquareAttacked(position, static_cast<Square>(kingSquare), !white)) {
    if ((position.castling & kingSideRight) != 0 && position.squares[squareAt(5, homeRank)] == Empty &&
        position.squares[squareAt(6, homeRank)] == Empty &&
        !isSquareAttacked(position, static_cast<Square>(squareAt(5, homeRank)), !white) &&
        !isSquareAttacked(position, static_cast<Square>(squareAt(6, homeRank)), !white)) {
      list.add(Move{static_cast<uint8_t>(kingSquare), static_cast<uint8_t>(squareAt(6, homeRank)), Empty, FlagCastle});
    }
    if ((position.castling & queenSideRight) != 0 && position.squares[squareAt(3, homeRank)] == Empty &&
        position.squares[squareAt(2, homeRank)] == Empty && position.squares[squareAt(1, homeRank)] == Empty &&
        !isSquareAttacked(position, static_cast<Square>(squareAt(3, homeRank)), !white) &&
        !isSquareAttacked(position, static_cast<Square>(squareAt(2, homeRank)), !white)) {
      list.add(Move{static_cast<uint8_t>(kingSquare), static_cast<uint8_t>(squareAt(2, homeRank)), Empty, FlagCastle});
    }
  }
}

// Losing a rook, by moving it or having it captured, forfeits that side's
// right on that flank. Keyed by the rook's home square.
uint8_t castlingMaskFor(const int square) {
  switch (square) {
    case 0:
      return WhiteQueenSide;
    case 7:
      return WhiteKingSide;
    case 56:
      return BlackQueenSide;
    case 63:
      return BlackKingSide;
    default:
      return 0;
  }
}

}  // namespace

bool isSquareAttacked(const Position& position, const Square square, const bool byWhite) {
  const int file = fileOf(square);
  const int rank = rankOf(square);

  // A pawn of the attacking colour sits one rank "behind" the target from its
  // own point of view, on either adjacent file.
  const int pawnRank = byWhite ? rank - 1 : rank + 1;
  if (hasPieceAt(position, file - 1, pawnRank, Pawn, byWhite)) return true;
  if (hasPieceAt(position, file + 1, pawnRank, Pawn, byWhite)) return true;

  for (int i = 0; i < 8; ++i) {
    if (hasPieceAt(position, file + kKnightDeltas[i].file, rank + kKnightDeltas[i].rank, Knight, byWhite)) return true;
    if (hasPieceAt(position, file + kKingDeltas[i].file, rank + kKingDeltas[i].rank, King, byWhite)) return true;
  }

  if (attackedBySlider(position, file, rank, kRookDeltas, Rook, byWhite)) return true;
  if (attackedBySlider(position, file, rank, kBishopDeltas, Bishop, byWhite)) return true;
  return false;
}

bool isInCheck(const Position& position) {
  const Square king = findKing(position, position.whiteToMove);
  if (king == kNoSquare) return false;
  return isSquareAttacked(position, king, !position.whiteToMove);
}

void generateLegalMoves(const Position& position, MoveList& list) {
  list.clear();
  MoveList pseudo;
  generatePseudoLegal(position, pseudo);

  Position working = position;
  for (int i = 0; i < pseudo.count; ++i) {
    Undo undo;
    const bool white = working.whiteToMove;
    makeMove(working, pseudo.moves[i], undo);
    const Square king = findKing(working, white);
    // Legal exactly when the mover's king is not attacked afterwards. This is
    // what filters out moving into check, moving a pinned piece, and the en
    // passant capture that exposes the king along the fifth rank.
    if (king == kNoSquare || !isSquareAttacked(working, king, !white)) {
      list.add(pseudo.moves[i]);
    }
    unmakeMove(working, pseudo.moves[i], undo);
  }
}

void makeMove(Position& position, const Move& move, Undo& undo) {
  undo.captured = position.squares[move.to];
  undo.castling = position.castling;
  undo.epSquare = position.epSquare;
  undo.halfmoveClock = position.halfmoveClock;

  const uint8_t piece = position.squares[move.from];
  const bool white = position.whiteToMove;
  const uint8_t colourFlag = white ? 0 : BlackFlag;

  position.squares[move.from] = Empty;
  position.squares[move.to] = piece;

  if ((move.flags & FlagEnPassant) != 0) {
    // The captured pawn is beside the destination, not on it.
    const int capturedSquare = squareAt(fileOf(move.to), rankOf(move.from));
    undo.captured = position.squares[capturedSquare];
    position.squares[capturedSquare] = Empty;
  } else if ((move.flags & FlagPromotion) != 0) {
    position.squares[move.to] = static_cast<uint8_t>(move.promotion | colourFlag);
  } else if ((move.flags & FlagCastle) != 0) {
    const int homeRank = rankOf(move.from);
    const bool kingSide = fileOf(move.to) == 6;
    const int rookFrom = squareAt(kingSide ? 7 : 0, homeRank);
    const int rookTo = squareAt(kingSide ? 5 : 3, homeRank);
    position.squares[rookTo] = position.squares[rookFrom];
    position.squares[rookFrom] = Empty;
  }

  if (pieceType(piece) == King) {
    position.castling &=
        static_cast<uint8_t>(~(white ? (WhiteKingSide | WhiteQueenSide) : (BlackKingSide | BlackQueenSide)));
  }
  position.castling &= static_cast<uint8_t>(~castlingMaskFor(move.from));
  position.castling &= static_cast<uint8_t>(~castlingMaskFor(move.to));

  position.epSquare = (move.flags & FlagDoublePush) != 0
                          ? static_cast<Square>(squareAt(fileOf(move.from), (rankOf(move.from) + rankOf(move.to)) / 2))
                          : kNoSquare;

  if (pieceType(piece) == Pawn || undo.captured != Empty) {
    position.halfmoveClock = 0;
  } else if (position.halfmoveClock < 255) {
    ++position.halfmoveClock;
  }
  if (!white) ++position.fullmoveNumber;
  position.whiteToMove = !white;
}

void unmakeMove(Position& position, const Move& move, const Undo& undo) {
  position.whiteToMove = !position.whiteToMove;
  const bool white = position.whiteToMove;
  if (!white) --position.fullmoveNumber;

  const uint8_t moved = position.squares[move.to];
  position.squares[move.from] =
      (move.flags & FlagPromotion) != 0 ? static_cast<uint8_t>(Pawn | (white ? 0 : BlackFlag)) : moved;
  position.squares[move.to] = Empty;

  if ((move.flags & FlagEnPassant) != 0) {
    position.squares[squareAt(fileOf(move.to), rankOf(move.from))] = undo.captured;
  } else {
    position.squares[move.to] = undo.captured;
  }

  if ((move.flags & FlagCastle) != 0) {
    const int homeRank = rankOf(move.from);
    const bool kingSide = fileOf(move.to) == 6;
    const int rookFrom = squareAt(kingSide ? 7 : 0, homeRank);
    const int rookTo = squareAt(kingSide ? 5 : 3, homeRank);
    position.squares[rookFrom] = position.squares[rookTo];
    position.squares[rookTo] = Empty;
  }

  position.castling = undo.castling;
  position.epSquare = undo.epSquare;
  position.halfmoveClock = undo.halfmoveClock;
}

uint64_t perft(Position& position, const int depth) {
  if (depth <= 0) return 1;
  MoveList list;
  generateLegalMoves(position, list);
  if (depth == 1) return static_cast<uint64_t>(list.count);

  uint64_t nodes = 0;
  for (int i = 0; i < list.count; ++i) {
    Undo undo;
    makeMove(position, list.moves[i], undo);
    nodes += perft(position, depth - 1);
    unmakeMove(position, list.moves[i], undo);
  }
  return nodes;
}

void setStartPosition(Position& position) {
  parseFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", position);
}

void positionToFen(const Position& position, char* out) {
  int n = 0;
  for (int rank = 7; rank >= 0; --rank) {
    int empty = 0;
    for (int file = 0; file < 8; ++file) {
      const uint8_t piece = position.squares[squareAt(file, rank)];
      if (piece == Empty) {
        ++empty;
        continue;
      }
      if (empty > 0) {
        out[n++] = static_cast<char>('0' + empty);
        empty = 0;
      }
      const char symbol = ".pnbrqk"[pieceType(piece)];
      out[n++] = isBlack(piece) ? symbol : static_cast<char>(symbol - 32);
    }
    if (empty > 0) out[n++] = static_cast<char>('0' + empty);
    if (rank > 0) out[n++] = '/';
  }

  out[n++] = ' ';
  out[n++] = position.whiteToMove ? 'w' : 'b';
  out[n++] = ' ';
  if (position.castling == 0) {
    out[n++] = '-';
  } else {
    if ((position.castling & WhiteKingSide) != 0) out[n++] = 'K';
    if ((position.castling & WhiteQueenSide) != 0) out[n++] = 'Q';
    if ((position.castling & BlackKingSide) != 0) out[n++] = 'k';
    if ((position.castling & BlackQueenSide) != 0) out[n++] = 'q';
  }
  out[n++] = ' ';
  if (position.epSquare == kNoSquare) {
    out[n++] = '-';
  } else {
    out[n++] = static_cast<char>('a' + fileOf(position.epSquare));
    out[n++] = static_cast<char>('1' + rankOf(position.epSquare));
  }

  // Clocks, written by hand rather than snprintf to keep this freestanding.
  out[n++] = ' ';
  const auto writeNumber = [&out, &n](unsigned value) {
    char digits[6];
    int count = 0;
    do {
      digits[count++] = static_cast<char>('0' + value % 10);
      value /= 10;
    } while (value > 0 && count < 6);
    while (count > 0) out[n++] = digits[--count];
  };
  writeNumber(position.halfmoveClock);
  out[n++] = ' ';
  writeNumber(position.fullmoveNumber);
  out[n] = '\0';
}

bool parseFen(const char* fen, Position& position) {
  if (fen == nullptr) return false;
  Position parsed;
  const char* cursor = fen;

  int rank = 7;
  int file = 0;
  for (; *cursor != '\0' && *cursor != ' '; ++cursor) {
    const char c = *cursor;
    if (c == '/') {
      if (file != 8) return false;
      --rank;
      file = 0;
      if (rank < 0) return false;
      continue;
    }
    if (c >= '1' && c <= '8') {
      file += c - '0';
      if (file > 8) return false;
      continue;
    }
    uint8_t piece = Empty;
    switch (c) {
      case 'p':
      case 'P':
        piece = Pawn;
        break;
      case 'n':
      case 'N':
        piece = Knight;
        break;
      case 'b':
      case 'B':
        piece = Bishop;
        break;
      case 'r':
      case 'R':
        piece = Rook;
        break;
      case 'q':
      case 'Q':
        piece = Queen;
        break;
      case 'k':
      case 'K':
        piece = King;
        break;
      default:
        return false;
    }
    if (c >= 'a' && c <= 'z') piece = static_cast<uint8_t>(piece | BlackFlag);
    if (file > 7 || rank < 0) return false;
    parsed.squares[squareAt(file, rank)] = piece;
    ++file;
  }
  if (rank != 0 || file != 8) return false;

  while (*cursor == ' ') ++cursor;
  if (*cursor == 'w') {
    parsed.whiteToMove = true;
  } else if (*cursor == 'b') {
    parsed.whiteToMove = false;
  } else {
    return false;
  }
  ++cursor;

  while (*cursor == ' ') ++cursor;
  if (*cursor == '-') {
    ++cursor;
  } else {
    for (; *cursor != '\0' && *cursor != ' '; ++cursor) {
      switch (*cursor) {
        case 'K':
          parsed.castling |= WhiteKingSide;
          break;
        case 'Q':
          parsed.castling |= WhiteQueenSide;
          break;
        case 'k':
          parsed.castling |= BlackKingSide;
          break;
        case 'q':
          parsed.castling |= BlackQueenSide;
          break;
        default:
          return false;
      }
    }
  }

  while (*cursor == ' ') ++cursor;
  if (*cursor == '-') {
    ++cursor;
  } else if (*cursor >= 'a' && *cursor <= 'h') {
    const int epFile = *cursor - 'a';
    ++cursor;
    if (*cursor < '1' || *cursor > '8') return false;
    parsed.epSquare = static_cast<Square>(squareAt(epFile, *cursor - '1'));
    ++cursor;
  } else if (*cursor != '\0') {
    return false;
  }

  // Clocks are optional: plenty of test records stop after the ep field.
  while (*cursor == ' ') ++cursor;
  if (*cursor >= '0' && *cursor <= '9') {
    int value = 0;
    for (; *cursor >= '0' && *cursor <= '9'; ++cursor) value = value * 10 + (*cursor - '0');
    parsed.halfmoveClock = static_cast<uint8_t>(value > 255 ? 255 : value);
    while (*cursor == ' ') ++cursor;
    if (*cursor >= '0' && *cursor <= '9') {
      value = 0;
      for (; *cursor >= '0' && *cursor <= '9'; ++cursor) value = value * 10 + (*cursor - '0');
      parsed.fullmoveNumber = static_cast<uint16_t>(value);
    }
  }

  position = parsed;
  return true;
}

void moveToSan(const Position& position, const Move& move, char* out) {
  int n = 0;
  const uint8_t piece = position.squares[move.from];
  const uint8_t type = pieceType(piece);

  if ((move.flags & FlagCastle) != 0) {
    // Queen-side is the long one; the king lands on the c-file.
    const char* text = fileOf(move.to) == 2 ? "O-O-O" : "O-O";
    while (*text != '\0') out[n++] = *text++;
  } else if (type == Pawn) {
    if ((move.flags & FlagCapture) != 0) {
      out[n++] = static_cast<char>('a' + fileOf(move.from));
      out[n++] = 'x';
    }
    out[n++] = static_cast<char>('a' + fileOf(move.to));
    out[n++] = static_cast<char>('1' + rankOf(move.to));
    if ((move.flags & FlagPromotion) != 0) {
      out[n++] = '=';
      out[n++] = "..NBRQ"[move.promotion];
    }
  } else {
    out[n++] = "..NBRQK"[type];
    // Disambiguation: only needed when another piece of the same type could
    // legally reach the same square. File first, rank if the files also match,
    // both if neither separates them.
    MoveList others;
    generateLegalMoves(position, others);
    bool sameFile = false;
    bool sameRank = false;
    bool ambiguous = false;
    for (int i = 0; i < others.count; ++i) {
      const Move& other = others.moves[i];
      if (other.to != move.to || other.from == move.from) continue;
      if (pieceType(position.squares[other.from]) != type) continue;
      ambiguous = true;
      if (fileOf(other.from) == fileOf(move.from)) sameFile = true;
      if (rankOf(other.from) == rankOf(move.from)) sameRank = true;
    }
    if (ambiguous) {
      if (!sameFile) {
        out[n++] = static_cast<char>('a' + fileOf(move.from));
      } else if (!sameRank) {
        out[n++] = static_cast<char>('1' + rankOf(move.from));
      } else {
        out[n++] = static_cast<char>('a' + fileOf(move.from));
        out[n++] = static_cast<char>('1' + rankOf(move.from));
      }
    }
    if ((move.flags & FlagCapture) != 0) out[n++] = 'x';
    out[n++] = static_cast<char>('a' + fileOf(move.to));
    out[n++] = static_cast<char>('1' + rankOf(move.to));
  }

  // Suffix from the resulting position: '#' when the reply has no legal moves
  // and the king is attacked, '+' when it is attacked but escapable.
  Position after = position;
  Undo undo;
  makeMove(after, move, undo);
  if (isInCheck(after)) {
    MoveList replies;
    generateLegalMoves(after, replies);
    out[n++] = replies.count == 0 ? '#' : '+';
  }
  out[n] = '\0';
}

void moveToString(const Move& move, char* out) {
  out[0] = static_cast<char>('a' + fileOf(move.from));
  out[1] = static_cast<char>('1' + rankOf(move.from));
  out[2] = static_cast<char>('a' + fileOf(move.to));
  out[3] = static_cast<char>('1' + rankOf(move.to));
  int length = 4;
  if ((move.flags & FlagPromotion) != 0) {
    char symbol = 'q';
    switch (move.promotion) {
      case Rook:
        symbol = 'r';
        break;
      case Bishop:
        symbol = 'b';
        break;
      case Knight:
        symbol = 'n';
        break;
      default:
        symbol = 'q';
        break;
    }
    out[length++] = symbol;
  }
  out[length] = '\0';
}

uint64_t positionKey(const Position& position) {
  // FNV-1a. Chosen over a hand-rolled mix because it is one line to verify
  // against the published constants, and over Zobrist because there is nothing
  // here to update incrementally.
  constexpr uint64_t kOffsetBasis = 14695981039346656037ULL;
  constexpr uint64_t kPrime = 1099511628211ULL;
  uint64_t hash = kOffsetBasis;
  const auto mix = [&hash](const uint8_t byte) {
    hash ^= byte;
    hash *= kPrime;
  };
  for (int square = 0; square < 64; ++square) mix(position.squares[square]);
  mix(position.whiteToMove ? 1 : 0);
  mix(position.castling);
  // +1 so kNoSquare (-1) becomes 0 and every real square stays distinct.
  mix(static_cast<uint8_t>(position.epSquare + 1));
  return hash;
}

int repetitionCount(const uint64_t* keys, const int count, const int window) {
  if (keys == nullptr || count <= 0) return 0;
  const uint64_t current = keys[count - 1];
  int seen = 1;  // the position on the board is its own first occurrence
  for (int back = 1; back <= window; ++back) {
    const int index = count - 1 - back;
    if (index < 0) break;
    if (keys[index] == current) ++seen;
  }
  return seen;
}

}  // namespace chess

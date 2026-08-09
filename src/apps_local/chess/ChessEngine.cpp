#include "ChessEngine.h"

namespace chess {
namespace {

// Centipawns. The king's value only has to dominate every other term; mate is
// scored separately, so its exact size does not matter.
constexpr int kPieceValue[7] = {0, 100, 320, 330, 500, 900, 20000};

// Piece-square tables, written from White's point of view with rank 1 first, so
// index == square index. They are what stop the engine playing aimlessly at
// this depth: without them it shuffles, because material is equal in most
// early positions.
// clang-format off
constexpr int8_t kPawnTable[64] = {
   0,  0,  0,  0,  0,  0,  0,  0,
   5, 10, 10,-20,-20, 10, 10,  5,
   5, -5,-10,  0,  0,-10, -5,  5,
   0,  0,  0, 20, 20,  0,  0,  0,
   5,  5, 10, 25, 25, 10,  5,  5,
  10, 10, 20, 30, 30, 20, 10, 10,
  50, 50, 50, 50, 50, 50, 50, 50,
   0,  0,  0,  0,  0,  0,  0,  0,
};
constexpr int8_t kKnightTable[64] = {
 -50,-40,-30,-30,-30,-30,-40,-50,
 -40,-20,  0,  5,  5,  0,-20,-40,
 -30,  5, 10, 15, 15, 10,  5,-30,
 -30,  0, 15, 20, 20, 15,  0,-30,
 -30,  5, 15, 20, 20, 15,  5,-30,
 -30,  0, 10, 15, 15, 10,  0,-30,
 -40,-20,  0,  0,  0,  0,-20,-40,
 -50,-40,-30,-30,-30,-30,-40,-50,
};
constexpr int8_t kBishopTable[64] = {
 -20,-10,-10,-10,-10,-10,-10,-20,
 -10,  5,  0,  0,  0,  0,  5,-10,
 -10, 10, 10, 10, 10, 10, 10,-10,
 -10,  0, 10, 10, 10, 10,  0,-10,
 -10,  5,  5, 10, 10,  5,  5,-10,
 -10,  0,  5, 10, 10,  5,  0,-10,
 -10,  0,  0,  0,  0,  0,  0,-10,
 -20,-10,-10,-10,-10,-10,-10,-20,
};
constexpr int8_t kRookTable[64] = {
   0,  0,  0,  5,  5,  0,  0,  0,
  -5,  0,  0,  0,  0,  0,  0, -5,
  -5,  0,  0,  0,  0,  0,  0, -5,
  -5,  0,  0,  0,  0,  0,  0, -5,
  -5,  0,  0,  0,  0,  0,  0, -5,
  -5,  0,  0,  0,  0,  0,  0, -5,
   5, 10, 10, 10, 10, 10, 10,  5,
   0,  0,  0,  0,  0,  0,  0,  0,
};
constexpr int8_t kQueenTable[64] = {
 -20,-10,-10, -5, -5,-10,-10,-20,
 -10,  0,  5,  0,  0,  0,  0,-10,
 -10,  5,  5,  5,  5,  5,  0,-10,
   0,  0,  5,  5,  5,  5,  0, -5,
  -5,  0,  5,  5,  5,  5,  0, -5,
 -10,  0,  5,  5,  5,  5,  0,-10,
 -10,  0,  0,  0,  0,  0,  0,-10,
 -20,-10,-10, -5, -5,-10,-10,-20,
};
// Midgame king table: rewards castling and staying behind pawns. An endgame
// table would want the opposite, but at this depth it is not worth the phase
// detection.
constexpr int8_t kKingTable[64] = {
  20, 30, 10,  0,  0, 10, 30, 20,
  20, 20,  0,  0,  0,  0, 20, 20,
 -10,-20,-20,-20,-20,-20,-20,-10,
 -20,-30,-30,-40,-40,-30,-30,-20,
 -30,-40,-40,-50,-50,-40,-40,-30,
 -30,-40,-40,-50,-50,-40,-40,-30,
 -30,-40,-40,-50,-50,-40,-40,-30,
 -30,-40,-40,-50,-50,-40,-40,-30,
};
// clang-format on

const int8_t* tableFor(const uint8_t type) {
  switch (type) {
    case Pawn:
      return kPawnTable;
    case Knight:
      return kKnightTable;
    case Bishop:
      return kBishopTable;
    case Rook:
      return kRookTable;
    case Queen:
      return kQueenTable;
    case King:
      return kKingTable;
    default:
      return nullptr;
  }
}

// Most Valuable Victim minus Least Valuable Attacker. Trying PxQ before QxP
// prunes far more of the tree, which matters much more than the ordering being
// clever: alpha-beta's whole benefit depends on searching good moves first.
int moveScore(const Position& position, const Move& move) {
  int score = 0;
  if ((move.flags & FlagCapture) != 0) {
    // static_cast keeps both arms uint8_t: Piece is an enum and pieceType()
    // returns the plain type, which -Wextra flags as a mixed conditional.
    const uint8_t victim =
        (move.flags & FlagEnPassant) != 0 ? static_cast<uint8_t>(Pawn) : pieceType(position.squares[move.to]);
    const uint8_t attacker = pieceType(position.squares[move.from]);
    score += 10000 + kPieceValue[victim] * 10 - kPieceValue[attacker];
  }
  if ((move.flags & FlagPromotion) != 0) score += 9000 + kPieceValue[move.promotion];
  return score;
}

// Selection sort of the single best remaining move. Cheaper than sorting the
// whole list, because a beta cutoff usually happens in the first few moves.
void pickBestMoveTo(const Position& position, MoveList& list, const int index) {
  int bestIndex = index;
  int bestScore = moveScore(position, list.moves[index]);
  for (int i = index + 1; i < list.count; ++i) {
    const int score = moveScore(position, list.moves[i]);
    if (score > bestScore) {
      bestScore = score;
      bestIndex = i;
    }
  }
  if (bestIndex != index) {
    const Move temp = list.moves[index];
    list.moves[index] = list.moves[bestIndex];
    list.moves[bestIndex] = temp;
  }
}

struct SearchContext {
  SearchBuffers* buffers;
  uint32_t nodes;
  uint32_t nodeBudget;
  bool budgetExhausted;
};

int negamax(Position& position, const int depth, int alpha, const int beta, const int ply, SearchContext& context) {
  if (context.nodeBudget != 0 && context.nodes >= context.nodeBudget) {
    context.budgetExhausted = true;
    return evaluate(position);
  }
  ++context.nodes;

  if (depth <= 0) return evaluate(position);

  MoveList& list = context.buffers->plies[ply];
  generateLegalMoves(position, list);
  if (list.count == 0) {
    // Mate scores shrink with distance so the search prefers the quicker mate,
    // and prefers the slower one when it is getting mated.
    return isInCheck(position) ? -kMateScore + ply : 0;
  }

  int best = -kMateScore * 2;
  const int count = list.count;
  for (int i = 0; i < count; ++i) {
    pickBestMoveTo(position, list, i);
    // The move must be copied out: the recursive call reuses deeper plies, but
    // this ply's list stays put, and re-reading it after unmake is fine only
    // because nothing below touches index `i`.
    const Move move = list.moves[i];
    Undo undo;
    makeMove(position, move, undo);
    const int score = -negamax(position, depth - 1, -beta, -alpha, ply + 1, context);
    unmakeMove(position, move, undo);

    if (score > best) best = score;
    if (best > alpha) alpha = best;
    if (alpha >= beta) break;  // opponent would avoid this line entirely
    if (context.budgetExhausted) break;
  }
  return best;
}

}  // namespace

int evaluate(const Position& position) {
  int score = 0;
  for (int square = 0; square < 64; ++square) {
    const uint8_t piece = position.squares[square];
    if (piece == Empty) continue;
    const uint8_t type = pieceType(piece);
    const bool white = !isBlack(piece);
    // Black reads the same table from the mirrored square.
    const int tableIndex = white ? square : ((7 - (square >> 3)) * 8 + (square & 7));
    const int8_t* table = tableFor(type);
    const int value = kPieceValue[type] + (table != nullptr ? table[tableIndex] : 0);
    score += white ? value : -value;
  }
  return position.whiteToMove ? score : -score;
}

SearchResult search(Position& position, int depth, SearchBuffers& buffers, const uint32_t nodeBudget) {
  SearchResult result{};
  if (depth < 1) depth = 1;
  if (depth > kMaxSearchDepth) depth = kMaxSearchDepth;

  MoveList& root = buffers.plies[0];
  generateLegalMoves(position, root);
  if (root.count == 0) return result;

  SearchContext context{&buffers, 0, nodeBudget, false};
  int alpha = -kMateScore * 2;
  const int beta = kMateScore * 2;
  const int count = root.count;

  for (int i = 0; i < count; ++i) {
    pickBestMoveTo(position, root, i);
    const Move move = root.moves[i];
    Undo undo;
    makeMove(position, move, undo);
    const int score = -negamax(position, depth - 1, -beta, -alpha, 1, context);
    unmakeMove(position, move, undo);

    if (!result.hasMove || score > result.score) {
      result.best = move;
      result.score = score;
      result.hasMove = true;
      if (score > alpha) alpha = score;
    }
    if (context.budgetExhausted) break;
  }

  result.nodes = context.nodes;
  result.budgetExhausted = context.budgetExhausted;
  return result;
}

}  // namespace chess

// Host tests for the chess rules engine. No device, no PlatformIO: ChessCore is
// freestanding C++17 precisely so this can run on a laptop. See run.sh.
//
// The bulk of this is perft: node counts at fixed depth from positions whose
// correct values are published and widely cross-checked. Perft is the standard
// measure because it catches the combinations that spot-checks miss, such as an
// en passant capture that is illegal only because it uncovers a rank check.

#include <cstdio>
#include <cstring>

#include "../../src/apps_local/chess/ChessCore.h"
#include "../../src/apps_local/chess/ChessEngine.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  ++checksRun;
  if (!condition) {
    ++checksFailed;
    std::printf("FAIL %s:%d  %s\n", "test_chess.cpp", line, what);
  }
}

void checkPerft(const char* fen, const int depth, const uint64_t expected, const char* name, const int line) {
  chess::Position position;
  ++checksRun;
  if (!chess::parseFen(fen, position)) {
    ++checksFailed;
    std::printf("FAIL %s:%d  %s: could not parse FEN\n", "test_chess.cpp", line, name);
    return;
  }
  const uint64_t actual = chess::perft(position, depth);
  if (actual != expected) {
    ++checksFailed;
    std::printf("FAIL %s:%d  %s perft(%d) = %llu, expected %llu\n", "test_chess.cpp", line, name, depth,
                static_cast<unsigned long long>(actual), static_cast<unsigned long long>(expected));
  }
}

#define CHECK(cond) check((cond), #cond, __LINE__)
#define CHECK_PERFT(fen, depth, expected, name) checkPerft((fen), (depth), (expected), (name), __LINE__)

bool hasMove(const chess::Position& position, const char* algebraic) {
  chess::MoveList list;
  chess::generateLegalMoves(position, list);
  char buffer[6];
  for (int i = 0; i < list.count; ++i) {
    chess::moveToString(list.moves[i], buffer);
    if (std::strcmp(buffer, algebraic) == 0) return true;
  }
  return false;
}

// Plays the move written in long algebraic ("g1f3"). Returns false if it is
// not legal here, so a typo in a sequence fails the test rather than silently
// playing nothing.
bool play(chess::Position& position, const char* algebraic) {
  chess::MoveList list;
  chess::generateLegalMoves(position, list);
  char buffer[6];
  for (int i = 0; i < list.count; ++i) {
    chess::moveToString(list.moves[i], buffer);
    if (std::strcmp(buffer, algebraic) == 0) {
      chess::Undo undo;
      chess::makeMove(position, list.moves[i], undo);
      return true;
    }
  }
  return false;
}

int legalMoveCount(const char* fen) {
  chess::Position position;
  if (!chess::parseFen(fen, position)) return -1;
  chess::MoveList list;
  chess::generateLegalMoves(position, list);
  return list.count;
}

void testFenRoundTrip() {
  chess::Position position;
  CHECK(chess::parseFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", position));
  CHECK(position.whiteToMove);
  CHECK(position.castling ==
        (chess::WhiteKingSide | chess::WhiteQueenSide | chess::BlackKingSide | chess::BlackQueenSide));
  CHECK(position.epSquare == chess::kNoSquare);
  CHECK(position.squares[0] == chess::Rook);                        // a1
  CHECK(position.squares[4] == chess::King);                        // e1
  CHECK(position.squares[60] == (chess::King | chess::BlackFlag));  // e8
  CHECK(position.squares[63] == (chess::Rook | chess::BlackFlag));  // h8
  CHECK(position.squares[32] == chess::Empty);                      // a5

  // Records truncated after the en passant field are legal input.
  CHECK(chess::parseFen("8/8/8/8/8/8/8/K6k w - -", position));
  CHECK(!chess::parseFen("this is not a fen", position));
  CHECK(!chess::parseFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP w KQkq - 0 1", position));  // too few ranks
}

void testFenRoundTrip2() {
  // A saved game is stored as FEN, so the writer must reproduce exactly what the
  // reader accepts, including castling rights, the en passant square and both
  // clocks. Round-trip every fixture we have.
  const char* fens[] = {
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
      "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
      "rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3",
      "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
      "6k1/5ppp/8/8/8/8/8/4R2K b - - 12 34",
  };
  for (const char* fen : fens) {
    chess::Position position;
    CHECK(chess::parseFen(fen, position));
    char out[90];
    chess::positionToFen(position, out);
    CHECK(std::strcmp(out, fen) == 0);
    if (std::strcmp(out, fen) != 0) std::printf("  got %s\n  want %s\n", out, fen);

    // And the round trip must produce an identical Position, not merely an
    // identical string.
    chess::Position again;
    CHECK(chess::parseFen(out, again));
    CHECK(std::memcmp(&position, &again, sizeof(chess::Position)) == 0);
  }
}

void testBasicMovement() {
  CHECK(legalMoveCount("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") == 20);
  // A lone king on an empty board: 8 from the middle, 3 from a corner.
  CHECK(legalMoveCount("8/8/8/3K4/8/8/8/7k w - -") == 8);
  CHECK(legalMoveCount("8/8/8/8/8/8/8/K6k w - -") == 3);
}

void testCheckEvasion() {
  // Black king on e8 faces a rook on e1: it may only leave the file.
  chess::Position position;
  CHECK(chess::parseFen("4k3/8/8/8/8/8/8/4R2K b - -", position));
  CHECK(chess::isInCheck(position));
  CHECK(!hasMove(position, "e8e7"));
  CHECK(hasMove(position, "e8d7"));
  CHECK(hasMove(position, "e8f7"));

  // Absolute pin: the d2 knight cannot move, since it shields the king from
  // the d8 rook.
  CHECK(chess::parseFen("3r3k/8/8/8/8/8/3N4/3K4 w - -", position));
  CHECK(!chess::isInCheck(position));
  CHECK(!hasMove(position, "d2f3"));
  CHECK(!hasMove(position, "d2b3"));

  // Checkmate and stalemate both produce zero legal moves; isInCheck separates
  // them. Fool's mate:
  CHECK(legalMoveCount("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq -") == 0);
  CHECK(chess::parseFen("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq -", position));
  CHECK(chess::isInCheck(position));
  // Classic stalemate: black to move, not in check, no legal move.
  CHECK(legalMoveCount("7k/5Q2/6K1/8/8/8/8/8 b - -") == 0);
  CHECK(chess::parseFen("7k/5Q2/6K1/8/8/8/8/8 b - -", position));
  CHECK(!chess::isInCheck(position));
}

void testCastling() {
  chess::Position position;
  // Both sides available and unobstructed.
  CHECK(chess::parseFen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq -", position));
  CHECK(hasMove(position, "e1g1"));
  CHECK(hasMove(position, "e1c1"));

  // A rook on e8 attacks e1, so the king starts in check: no castling at all.
  CHECK(chess::parseFen("4r3/8/8/8/8/8/8/R3K2R w KQ -", position));
  CHECK(!hasMove(position, "e1g1"));
  CHECK(!hasMove(position, "e1c1"));

  // A rook on f8 attacks f1, the king-side transit square.
  CHECK(chess::parseFen("5r2/8/8/8/8/8/8/R3K2R w KQ -", position));
  CHECK(!hasMove(position, "e1g1"));
  CHECK(hasMove(position, "e1c1"));

  // Queen-side is legal even when b1 is attacked: the king never visits b1.
  CHECK(chess::parseFen("1r6/8/8/8/8/8/8/R3K2R w KQ -", position));
  CHECK(hasMove(position, "e1c1"));

  // Rights must be present.
  CHECK(chess::parseFen("r3k2r/8/8/8/8/8/8/R3K2R w - -", position));
  CHECK(!hasMove(position, "e1g1"));

  // Moving the king clears both rights; unmake must restore them.
  CHECK(chess::parseFen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq -", position));
  chess::MoveList list;
  chess::generateLegalMoves(position, list);
  for (int i = 0; i < list.count; ++i) {
    char buffer[6];
    chess::moveToString(list.moves[i], buffer);
    if (std::strcmp(buffer, "e1e2") != 0) continue;
    chess::Undo undo;
    chess::makeMove(position, list.moves[i], undo);
    CHECK((position.castling & (chess::WhiteKingSide | chess::WhiteQueenSide)) == 0);
    chess::unmakeMove(position, list.moves[i], undo);
    CHECK((position.castling & (chess::WhiteKingSide | chess::WhiteQueenSide)) ==
          (chess::WhiteKingSide | chess::WhiteQueenSide));
  }

  // Castling moves the rook too, and unmake puts it back.
  CHECK(chess::parseFen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq -", position));
  chess::generateLegalMoves(position, list);
  for (int i = 0; i < list.count; ++i) {
    char buffer[6];
    chess::moveToString(list.moves[i], buffer);
    if (std::strcmp(buffer, "e1g1") != 0) continue;
    chess::Undo undo;
    chess::makeMove(position, list.moves[i], undo);
    CHECK(position.squares[6] == chess::King);   // g1
    CHECK(position.squares[5] == chess::Rook);   // f1
    CHECK(position.squares[7] == chess::Empty);  // h1
    chess::unmakeMove(position, list.moves[i], undo);
    CHECK(position.squares[4] == chess::King);   // e1
    CHECK(position.squares[7] == chess::Rook);   // h1
    CHECK(position.squares[5] == chess::Empty);  // f1
  }
}

void testEnPassantAndPromotion() {
  chess::Position position;
  // White pawn on e5, black has just played d7d5.
  CHECK(chess::parseFen("rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6", position));
  CHECK(hasMove(position, "e5d6"));

  chess::MoveList list;
  chess::generateLegalMoves(position, list);
  for (int i = 0; i < list.count; ++i) {
    char buffer[6];
    chess::moveToString(list.moves[i], buffer);
    if (std::strcmp(buffer, "e5d6") != 0) continue;
    chess::Undo undo;
    chess::makeMove(position, list.moves[i], undo);
    CHECK(position.squares[43] == chess::Pawn);   // d6 now holds the white pawn
    CHECK(position.squares[35] == chess::Empty);  // d5 pawn removed, though nothing landed there
    chess::unmakeMove(position, list.moves[i], undo);
    CHECK(position.squares[35] == (chess::Pawn | chess::BlackFlag));  // restored
    CHECK(position.squares[36] == chess::Pawn);                       // e5 white pawn back
  }

  // The en passant capture that is illegal because it opens a rank check on
  // the white king. Both pawns leave the fifth rank at once, unmasking the
  // rook on a5. A generator that only checks the destination square misses it.
  chess::Position pinnedEnPassant;
  CHECK(chess::parseFen("8/8/8/r2pP2K/8/8/8/k7 w - d6", pinnedEnPassant));
  CHECK(!hasMove(pinnedEnPassant, "e5d6"));

  // Promotion generates four distinct moves, not one.
  CHECK(legalMoveCount("8/P6k/8/8/8/8/8/K7 w - -") == 4 + 3);  // 4 promotions + 3 king moves
  CHECK(chess::parseFen("8/P6k/8/8/8/8/8/K7 w - -", position));
  CHECK(hasMove(position, "a7a8q"));
  CHECK(hasMove(position, "a7a8r"));
  CHECK(hasMove(position, "a7a8b"));
  CHECK(hasMove(position, "a7a8n"));
}

// Published perft values. Positions 1-5 are the standard set used to validate
// move generators; each targets a different cluster of edge cases.
void testPerft() {
  const char* start = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
  CHECK_PERFT(start, 1, 20, "start");
  CHECK_PERFT(start, 2, 400, "start");
  CHECK_PERFT(start, 3, 8902, "start");
  CHECK_PERFT(start, 4, 197281, "start");
  CHECK_PERFT(start, 5, 4865609, "start");

  // Kiwipete: dense with castling, pins and en passant chances.
  const char* kiwipete = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -";
  CHECK_PERFT(kiwipete, 1, 48, "kiwipete");
  CHECK_PERFT(kiwipete, 2, 2039, "kiwipete");
  CHECK_PERFT(kiwipete, 3, 97862, "kiwipete");
  CHECK_PERFT(kiwipete, 4, 4085603, "kiwipete");

  // Position 3: sparse, but full of en passant and promotion races.
  const char* position3 = "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -";
  CHECK_PERFT(position3, 1, 14, "position3");
  CHECK_PERFT(position3, 2, 191, "position3");
  CHECK_PERFT(position3, 3, 2812, "position3");
  CHECK_PERFT(position3, 4, 43238, "position3");
  CHECK_PERFT(position3, 5, 674624, "position3");

  // Position 4: promotion-heavy, and asymmetric castling rights.
  const char* position4 = "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq -";
  CHECK_PERFT(position4, 1, 6, "position4");
  CHECK_PERFT(position4, 2, 264, "position4");
  CHECK_PERFT(position4, 3, 9467, "position4");
  CHECK_PERFT(position4, 4, 422333, "position4");

  // Position 5: known to break generators that mishandle promotion + check.
  const char* position5 = "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ -";
  CHECK_PERFT(position5, 1, 44, "position5");
  CHECK_PERFT(position5, 2, 1486, "position5");
  CHECK_PERFT(position5, 3, 62379, "position5");

  // Position 6: a quiet middlegame, catching everyday errors the sharp
  // positions above can mask.
  const char* position6 = "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10";
  CHECK_PERFT(position6, 1, 46, "position6");
  CHECK_PERFT(position6, 2, 2079, "position6");
  CHECK_PERFT(position6, 3, 89890, "position6");
}

// make/unmake must be exactly reversible, or a search corrupts the board as it
// backtracks. Walk every move at depth 2 and compare the raw bytes.
void testMakeUnmakeRestoresState() {
  const char* fens[] = {
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
      "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -",
      "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq -",
  };
  for (const char* fen : fens) {
    chess::Position position;
    CHECK(chess::parseFen(fen, position));
    const chess::Position original = position;

    chess::MoveList list;
    chess::generateLegalMoves(position, list);
    for (int i = 0; i < list.count; ++i) {
      chess::Undo undo;
      chess::makeMove(position, list.moves[i], undo);

      chess::MoveList replies;
      chess::generateLegalMoves(position, replies);
      const chess::Position afterMove = position;
      for (int j = 0; j < replies.count; ++j) {
        chess::Undo replyUndo;
        chess::makeMove(position, replies.moves[j], replyUndo);
        chess::unmakeMove(position, replies.moves[j], replyUndo);
        CHECK(std::memcmp(&position, &afterMove, sizeof(chess::Position)) == 0);
      }

      chess::unmakeMove(position, list.moves[i], undo);
      CHECK(std::memcmp(&position, &original, sizeof(chess::Position)) == 0);
    }
  }
}

// Plays a sequence of long-algebraic moves and returns the SAN of each.
void sanOf(const char* fen, const char* const* moves, const int count, char out[][10]) {
  chess::Position position;
  if (!chess::parseFen(fen, position)) return;
  for (int m = 0; m < count; ++m) {
    chess::MoveList list;
    chess::generateLegalMoves(position, list);
    char buffer[6];
    for (int i = 0; i < list.count; ++i) {
      chess::moveToString(list.moves[i], buffer);
      if (std::strcmp(buffer, moves[m]) != 0) continue;
      chess::moveToSan(position, list.moves[i], out[m]);
      chess::Undo undo;
      chess::makeMove(position, list.moves[i], undo);
      break;
    }
  }
}

void testSan() {
  const char* start = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
  const char* opening[] = {"e2e4", "e7e5", "g1f3", "b8c6", "f1b5"};
  char san[5][10] = {};
  sanOf(start, opening, 5, san);
  CHECK(std::strcmp(san[0], "e4") == 0);
  CHECK(std::strcmp(san[1], "e5") == 0);
  CHECK(std::strcmp(san[2], "Nf3") == 0);
  CHECK(std::strcmp(san[3], "Nc6") == 0);
  CHECK(std::strcmp(san[4], "Bb5") == 0);

  // Castling, both sides.
  chess::Position position;
  chess::MoveList list;
  char out[10];
  CHECK(chess::parseFen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq -", position));
  chess::generateLegalMoves(position, list);
  for (int i = 0; i < list.count; ++i) {
    char lan[6];
    chess::moveToString(list.moves[i], lan);
    if (std::strcmp(lan, "e1g1") == 0) {
      chess::moveToSan(position, list.moves[i], out);
      CHECK(std::strcmp(out, "O-O") == 0);
    }
    if (std::strcmp(lan, "e1c1") == 0) {
      chess::moveToSan(position, list.moves[i], out);
      CHECK(std::strcmp(out, "O-O-O") == 0);
    }
  }

  // Pawn capture carries its origin file; promotion carries the piece.
  const char* ep[] = {"e5d6"};
  char epSan[1][10] = {};
  sanOf("rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6", ep, 1, epSan);
  CHECK(std::strcmp(epSan[0], "exd6") == 0);

  const char* promo[] = {"a7a8q"};
  char promoSan[1][10] = {};
  sanOf("8/P6k/8/8/8/8/8/K7 w - -", promo, 1, promoSan);
  CHECK(std::strcmp(promoSan[0], "a8=Q") == 0);

  // Disambiguation: two knights on b1 and d1 both reach c3, so the file must
  // appear. This is the case a naive generator gets wrong.
  const char* ambig[] = {"b1c3"};
  char ambigSan[1][10] = {};
  sanOf("4k3/8/8/8/8/8/8/1N1N1K2 w - -", ambig, 1, ambigSan);
  CHECK(std::strcmp(ambigSan[0], "Nbc3") == 0);

  // Rank disambiguation when the files match: rooks on a1 and a5 both reach a3.
  const char* rankAmbig[] = {"a1a3"};
  char rankSan[1][10] = {};
  sanOf("4k3/8/8/R7/8/8/8/R4K2 w - -", rankAmbig, 1, rankSan);
  CHECK(std::strcmp(rankSan[0], "R1a3") == 0);

  // Check and mate suffixes.
  const char* mate[] = {"e1e8"};
  char mateSan[1][10] = {};
  sanOf("6k1/5ppp/8/8/8/8/8/4R2K w - -", mate, 1, mateSan);
  CHECK(std::strcmp(mateSan[0], "Re8#") == 0);

  const char* checking[] = {"e1e7"};
  char checkSan[1][10] = {};
  sanOf("4k3/8/8/8/8/8/8/4R2K w - -", checking, 1, checkSan);
  CHECK(std::strcmp(checkSan[0], "Re7+") == 0);
}

// SearchBuffers is ~7KB. Static here for the same reason the activity holds it
// as a member: it is far too big for a stack frame on the target.
chess::SearchBuffers searchBuffers;

// Runs a search and returns the chosen move in long algebraic notation.
// `out` must hold at least 6 bytes.
void bestMove(const char* fen, const int depth, char* out) {
  out[0] = '\0';
  chess::Position position;
  if (!chess::parseFen(fen, position)) return;
  const chess::SearchResult result = chess::search(position, depth, searchBuffers);
  if (result.hasMove) chess::moveToString(result.best, out);
}

void testEvaluation() {
  chess::Position position;
  // The opening position is symmetric, so it must evaluate to dead level.
  // A sign error in the piece-square mirroring shows up here immediately.
  CHECK(chess::parseFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", position));
  CHECK(chess::evaluate(position) == 0);
  CHECK(chess::parseFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1", position));
  CHECK(chess::evaluate(position) == 0);

  // Scores are from the side to move's point of view, so an extra white queen
  // reads positive for White and negative for Black.
  CHECK(chess::parseFen("4k3/8/8/8/8/8/8/3QK3 w - -", position));
  CHECK(chess::evaluate(position) > 500);
  CHECK(chess::parseFen("4k3/8/8/8/8/8/8/3QK3 b - -", position));
  CHECK(chess::evaluate(position) < -500);
}

void testSearchFindsMate() {
  // Back-rank mate: Re1-e8 is mate because f7/g7/h7 are the king's own pawns.
  char move[6];
  bestMove("6k1/5ppp/8/8/8/8/8/4R2K w - -", 3, move);
  CHECK(std::strcmp(move, "e1e8") == 0);

  chess::Position position;
  CHECK(chess::parseFen("6k1/5ppp/8/8/8/8/8/4R2K w - -", position));
  const chess::SearchResult result = chess::search(position, 3, searchBuffers);
  CHECK(result.hasMove);
  // Mate must outscore any material evaluation, or the engine trades it away.
  CHECK(result.score >= chess::kMateThreshold);
}

void testSearchTakesFreeMaterial() {
  // The black queen on h4 is undefended and the rook on h1 can take it.
  char move[6];
  bestMove("7k/8/8/8/7q/8/8/K6R w - -", 3, move);
  CHECK(std::strcmp(move, "h1h4") == 0);
}

void testSearchReportsNoMove() {
  chess::Position position;
  // Checkmate: no legal move at all.
  CHECK(chess::parseFen("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq -", position));
  CHECK(!chess::search(position, 3, searchBuffers).hasMove);
  // Stalemate: likewise, and the caller distinguishes them with isInCheck().
  CHECK(chess::parseFen("7k/5Q2/6K1/8/8/8/8/8 b - -", position));
  CHECK(!chess::search(position, 3, searchBuffers).hasMove);
}

void testSearchLeavesPositionUntouched() {
  // A search that corrupts the board as it backtracks would quietly ruin the
  // game state the UI is holding.
  const char* fens[] = {
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
      "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -",
  };
  for (const char* fen : fens) {
    chess::Position position;
    CHECK(chess::parseFen(fen, position));
    const chess::Position before = position;
    const chess::SearchResult result = chess::search(position, 3, searchBuffers);
    CHECK(result.hasMove);
    CHECK(std::memcmp(&position, &before, sizeof(chess::Position)) == 0);

    // Whatever it picked must be one of the legal moves.
    chess::MoveList legal;
    chess::generateLegalMoves(position, legal);
    bool found = false;
    for (int i = 0; i < legal.count; ++i) {
      if (legal.moves[i].from == result.best.from && legal.moves[i].to == result.best.to) found = true;
    }
    CHECK(found);
  }
}

void testNodeBudgetStopsSearch() {
  chess::Position position;
  CHECK(chess::parseFen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -", position));

  // A budget far below what depth 4 needs must trip, and must still hand back
  // a usable move rather than nothing: on the device this is what keeps a slow
  // search from stalling the UI.
  const chess::SearchResult limited = chess::search(position, 4, searchBuffers, 200);
  CHECK(limited.budgetExhausted);
  CHECK(limited.hasMove);
  CHECK(limited.nodes <= 400);  // the check is per-node, so a small overshoot is expected

  // Unbounded, the same search runs to completion.
  const chess::SearchResult full = chess::search(position, 4, searchBuffers, 0);
  CHECK(!full.budgetExhausted);
  CHECK(full.hasMove);
  CHECK(full.nodes > limited.nodes);
}

void testDeeperSearchIsNotWorse() {
  // Alpha-beta must not change the score a plain search would return. If move
  // ordering or a cutoff bound is wrong, the score at a given depth drifts.
  // Depth 1 from the opening is a pure one-ply evaluation, so it is checkable.
  chess::Position position;
  CHECK(chess::parseFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", position));
  const chess::SearchResult depth1 = chess::search(position, 1, searchBuffers);
  CHECK(depth1.hasMove);

  chess::MoveList legal;
  chess::generateLegalMoves(position, legal);
  int bestByHand = -chess::kMateScore;
  for (int i = 0; i < legal.count; ++i) {
    chess::Undo undo;
    chess::makeMove(position, legal.moves[i], undo);
    const int score = -chess::evaluate(position);
    chess::unmakeMove(position, legal.moves[i], undo);
    if (score > bestByHand) bestByHand = score;
  }
  CHECK(depth1.score == bestByHand);
}

// --- repetition ------------------------------------------------------------

void testPositionKeyDistinguishes() {
  chess::Position a;
  chess::Position b;
  CHECK(chess::parseFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", a));
  CHECK(chess::parseFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", b));
  CHECK(chess::positionKey(a) == chess::positionKey(b));

  // The move counters are NOT part of the identity: the same position reached
  // by a different route is the same position for repetition.
  CHECK(chess::parseFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 7 12", b));
  CHECK(chess::positionKey(a) == chess::positionKey(b));

  // Side to move is. This is the one that matters most: without it a shuffle
  // would count each half of the cycle as a repetition of the other.
  CHECK(chess::parseFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1", b));
  CHECK(chess::positionKey(a) != chess::positionKey(b));

  // So are the castling rights: a king that has lost the right to castle is in
  // a different position from one that has not, on an identical board.
  CHECK(chess::parseFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w Kkq - 0 1", b));
  CHECK(chess::positionKey(a) != chess::positionKey(b));

  // And the en passant square.
  chess::Position c;
  CHECK(chess::parseFen("rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq c6 0 2", a));
  CHECK(chess::parseFen("rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2", c));
  CHECK(chess::positionKey(a) != chess::positionKey(c));
}

void testRepetitionCountWindow() {
  const uint64_t a = 111;
  const uint64_t b = 222;
  const uint64_t keys[] = {a, b, a, b, a};

  // Five plies with nothing irreversible: the position on the board is its
  // third occurrence.
  CHECK(chess::repetitionCount(keys, 5, 4) == 3);

  // The window is the halfmove clock, and it is a correctness bound rather
  // than a speed one. A capture two plies ago means the identical key before
  // it belongs to a position this game can no longer reach.
  CHECK(chess::repetitionCount(keys, 5, 2) == 2);
  CHECK(chess::repetitionCount(keys, 5, 1) == 1);
  CHECK(chess::repetitionCount(keys, 5, 0) == 1);

  // Degenerate inputs answer rather than read off the end.
  CHECK(chess::repetitionCount(keys, 1, 99) == 1);
  CHECK(chess::repetitionCount(keys, 0, 99) == 0);
  CHECK(chess::repetitionCount(nullptr, 5, 4) == 0);

  // A window longer than the history stops at the start of it.
  CHECK(chess::repetitionCount(keys, 5, 400) == 3);
}

void testThreefoldFromKnightShuffle() {
  // The repetition every player actually meets: both sides walk a knight out
  // and back twice. The third time the starting position appears, it is a draw.
  chess::Position position;
  chess::setStartPosition(position);

  uint64_t keys[16];
  int count = 0;
  keys[count++] = chess::positionKey(position);

  const char* shuffle[] = {"g1f3", "b8c6", "f3g1", "c6b8", "g1f3", "b8c6", "f3g1", "c6b8"};
  for (int ply = 0; ply < 8; ++ply) {
    CHECK(play(position, shuffle[ply]));
    keys[count++] = chess::positionKey(position);

    // Knight moves are reversible, so the clock never resets and the window is
    // the whole game so far.
    CHECK(position.halfmoveClock == ply + 1);

    const int seen = chess::repetitionCount(keys, count, position.halfmoveClock);
    // The whole second lap is a repetition, not just the moment the start
    // position comes back: ply 5 repeats the position after ply 1, ply 6 the
    // one after ply 2, and so on. Writing this as "1 except at plies 4 and 8"
    // was the first version of this test, and it failed on exactly those three
    // plies -- the code was right and the expectation was not.
    if (ply < 3) {
      CHECK(seen == 1);
    } else if (ply == 7) {
      CHECK(seen == 3);
    } else {
      CHECK(seen == 2);
    }
  }
  // And the board really is back where it started, so the count above is about
  // the position it claims to be about.
  chess::Position start;
  chess::setStartPosition(start);
  CHECK(chess::positionKey(position) == chess::positionKey(start));
}

void testCaptureEndsTheRepetitionWindow() {
  // The window bound doing its job on a real board rather than on an array.
  // After a capture the clock is 0, so nothing before it can be a repetition
  // even if the key matched -- and here the shuffle around it is deliberate.
  chess::Position position;
  CHECK(chess::parseFen("4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1", position));
  CHECK(play(position, "e4d5"));  // capture
  CHECK(position.halfmoveClock == 0);

  uint64_t keys[8];
  int count = 0;
  keys[count++] = chess::positionKey(position);
  CHECK(chess::repetitionCount(keys, count, position.halfmoveClock) == 1);
}

}  // namespace

int main() {
  testFenRoundTrip();
  testFenRoundTrip2();
  testBasicMovement();
  testCheckEvasion();
  testCastling();
  testEnPassantAndPromotion();
  testMakeUnmakeRestoresState();
  testPerft();

  testSan();
  testEvaluation();
  testSearchFindsMate();
  testSearchTakesFreeMaterial();
  testSearchReportsNoMove();
  testSearchLeavesPositionUntouched();
  testNodeBudgetStopsSearch();
  testDeeperSearchIsNotWorse();

  testPositionKeyDistinguishes();
  testRepetitionCountWindow();
  testThreefoldFromKnightShuffle();
  testCaptureEndsTheRepetitionWindow();

  std::printf("%d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}

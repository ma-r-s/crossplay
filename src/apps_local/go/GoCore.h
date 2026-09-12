#pragma once

// Go on a nine by nine board: the rules. Freestanding -- no renderer, no
// Activity, no storage, no heap.
//
// Nine by nine only, and that is a product decision rather than a limitation.
// Nineteen lines on a 480px panel gives a 24px grid pitch with a 20px stone,
// which is below the fingertip this device is driven with; nine gives 48px and
// a game that finishes in twenty minutes on a train.
//
// **Area scoring (Chinese), positional superko, komi 7.5.** That combination is
// what every engine plays because it makes a finished position scorable by
// counting alone -- no prisoners to remember, no dame to haggle over, no seki
// exception. It is also the ruleset a beginner never has to learn: they put
// stones down and the machine counts.
//
// The one place this ruleset is user-visible is the END of the game, and it is
// handled in `GoFlow.h` rather than here: under area scoring the engine would
// happily fill every neutral point, which reads as an idiot to a human. The
// rules here stop at "two passes ends it"; who is dead is a separate agreement.
//
// This struct IS the wire format and IS the save format. 140 bytes, trivially
// copyable, so two devices share one description of a game and cannot drift.

#include <cstdint>

namespace go {

constexpr int kSize = 9;
constexpr int kPoints = kSize * kSize;

// The three things a point can be. Values are deliberate: `other()` is an xor.
constexpr uint8_t kEmpty = 0;
constexpr uint8_t kBlack = 1;
constexpr uint8_t kWhite = 2;

// A move is a point index, or one of these. `kPass` is a real move -- it ends
// the game when doubled and it is the only legal move in a filled position --
// so it lives in the same space as the points rather than in a flag beside
// them, where a caller could forget it.
constexpr uint8_t kPass = 81;
constexpr uint8_t kNoPoint = 255;

// Komi, in half points, always to White. 7.5 is what CGOS and every 9x9 engine
// tournament play, and the half point is not decoration: at a flat 7.0 a 44/37
// area split is an exact tie, which on this device would mean writing a draw
// screen for a result the ruleset exists to avoid. An odd number of half points
// against an even number cannot come out level.
//
// Held in halves so the core needs no floating point at all.
constexpr int16_t kKomiHalves = 15;

// How many recent positions the superko rule looks back over.
//
// Full positional superko wants every position the game has ever held, which is
// a few hundred hashes and does not fit in a packet. Eight covers simple ko
// (length 1), the triple ko that is the reason superko exists at all (length 3
// each way), and every cycle a human will produce. A cycle longer than this is
// not reachable by accident, and the double-pass rule ends any game that finds
// one.
constexpr int kHistory = 8;

// Whose turn it is not.
constexpr uint8_t other(const uint8_t colour) { return colour ^ 3; }

constexpr bool isStone(const uint8_t point) { return point == kBlack || point == kWhite; }

// Where a point sits. Row 0 is the top of the screen, column 0 the left.
constexpr int rowOf(const int point) { return point / kSize; }
constexpr int colOf(const int point) { return point % kSize; }
constexpr int pointAt(const int row, const int col) { return row * kSize + col; }
constexpr bool onBoard(const int row, const int col) {
  return row >= 0 && row < kSize && col >= 0 && col < kSize;
}

// The four orthogonal neighbours of a point, written into `out`, returning how
// many there were. Corners have two, edges three.
//
// Every rule in Go is built on this one function, so it is the one place the
// edge of the board is expressed. Callers that open-code `point - 9` wrap round
// the board and produce a game that is subtly not Go; there is exactly one
// such loop here and it is this.
int neighbours(int point, uint8_t out[4]);

// What a game is. This is the wire format and the save payload.
struct Game {
  // The position. One byte a point rather than two bits, because the packing
  // would save 60 bytes of a 192-byte budget that is not under pressure, and
  // cost every rule in this file a shift.
  uint8_t point[kPoints];

  // Stones the players have agreed are dead, as a bit a point. Meaningless
  // until `phase` is Scoring; see GoFlow.h for why this is an agreement rather
  // than a computation.
  uint8_t dead[(kPoints + 7) / 8];

  // Truncated position keys, most recent last. Only `recentCount` of them are
  // real; the ring never wraps within one game because it is shifted down.
  uint32_t recent[kHistory];

  uint16_t capturedBy[3];  // indexed by colour; [0] is unused

  uint16_t moveNumber;
  uint8_t toMove;
  uint8_t ko;        // the point simple ko forbids, or kNoPoint
  uint8_t passes;    // consecutive passes; two ends the game
  uint8_t lastMove;  // the move just played, for the marker on the board
  uint8_t recentCount;
  uint8_t phase;  // go::Phase, held as a byte so the struct stays trivially copyable
};

// Where a game is in its life. Not the same thing as whose turn it is.
enum class Phase : uint8_t {
  // Stones are going down.
  Playing,
  // Both players passed. Dead stones are being agreed, and only then is there
  // a score. This phase is the whole reason casual Go is hard to ship.
  Scoring,
  // Counted, and the result is fixed.
  Over,
};

enum class Outcome : uint8_t { Running, BlackWins, WhiteWins };

// A fresh game. Black moves first; there is no handicap on 9x9 here.
void reset(Game& game);

// Whether `colour` may play at `point` right now. Points are 0..80; `kPass` is
// always legal and answers true.
//
// This is the whole of Go's legality: the point must be empty, the move must
// not leave its own group without liberties (suicide), and it must not recreate
// a position the game has already held (ko, and superko above it).
bool legal(const Game& game, int point, uint8_t colour);

// Play `point` for the side to move. Returns false and changes nothing when the
// move is not legal, so a caller cannot half-play.
//
// `kPass` passes. Two passes in a row move the game to Scoring.
bool play(Game& game, int point);

// How many stones `point`'s group has, and how many liberties, written through
// the out parameters. `stones` may be null. Answers 0/0 on an empty point.
//
// `stones` receives a bit a point, the same shape as Game::dead.
void group(const Game& game, int point, uint8_t stones[(kPoints + 7) / 8], int& size, int& liberties);

// Whether `point` is an eye for `colour`: empty, orthogonally surrounded by
// `colour`, and with enough of its diagonals held that filling it would be
// self-destruction.
//
// This is not a rule of Go. It is the one heuristic the rules file owns,
// because both the opponent and the board's own "you probably did not mean
// that" hint need exactly the same answer, and two versions of it would drift.
bool isEye(const Game& game, int point, uint8_t colour);

// How many liberties `colour`'s group would have after playing at `point`,
// captures included. Zero means the move is suicide; one means it is self
// atari, which is legal, occasionally brilliant and usually a beginner throwing
// a stone away. Answers -1 when the point is not empty.
//
// The board's "are you sure" hint and the opponent's playout policy both ask
// this, which is why it is one function rather than two similar ones.
int libertiesAfter(const Game& game, int point, uint8_t colour);

// FNV-1a over the position and the side to move. The superko ring holds these.
uint32_t positionKey(const Game& game);

// Bitset helpers for `Game::dead` and the group masks.
inline bool marked(const uint8_t mask[(kPoints + 7) / 8], const int point) {
  return (mask[point / 8] & (1u << (point % 8))) != 0;
}
inline void mark(uint8_t mask[(kPoints + 7) / 8], const int point) { mask[point / 8] |= (1u << (point % 8)); }
inline void unmark(uint8_t mask[(kPoints + 7) / 8], const int point) {
  mask[point / 8] &= static_cast<uint8_t>(~(1u << (point % 8)));
}
inline void clearMask(uint8_t mask[(kPoints + 7) / 8]) {
  for (int i = 0; i < (kPoints + 7) / 8; ++i) mask[i] = 0;
}

// --- Scoring ---------------------------------------------------------------
//
// Area scoring: a player's score is their live stones plus the empty points
// only they surround. White adds komi. Neutral points -- reachable from both
// colours -- count for nobody, which is why nobody has to fill them.

// What one point counts as once the dead stones are lifted: kEmpty for neutral,
// kBlack or kWhite for a point that belongs to one of them.
//
// `owner` receives one byte a point. Dead stones read as territory for their
// captor, which is what makes agreeing them the whole endgame.
void territory(const Game& game, uint8_t owner[kPoints]);

struct Score {
  int16_t blackHalves;  // area in half points, so komi is expressible
  int16_t whiteHalves;
};

// The final count, in half points, with komi already added to White.
Score score(const Game& game);

// Who won. Running until the game is Over.
Outcome outcome(const Game& game);

// The margin in half points, always positive. Ask `outcome()` who it is for.
int16_t marginHalves(const Game& game);

}  // namespace go

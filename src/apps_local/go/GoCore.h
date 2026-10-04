#pragma once

// Go: the rules. Freestanding -- no renderer, no Activity, no storage, no heap.
//
// **Three board sizes, nine, thirteen and nineteen, chosen per game.** Nine
// gives a 49px grid pitch and a game that finishes in twenty minutes on a
// train; thirteen gives 33px; nineteen gives 22px, which is below the fingertip
// this device is driven with. Both larger boards are playable because a stone
// goes down in two taps rather than one and the first tap can be moved (see
// GoFlow.h). Nineteen is offered because it is the board real Go is played on
// (GitHub #282); it is a setting, never the default.
//
// The size is a FIELD of the game rather than a compile-time constant. It was a
// constant, and every rule in this file read it from the same place the screens
// did, so making it a setting meant threading it through: `game.points()`,
// `game.row(p)`, `neighbours(size, ...)`. The arrays are sized for the larger
// board and a nine by nine game simply uses the first 81 of them.
//
// **Area scoring (Chinese), situational superko, komi 7.5.** That combination is
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
// This struct IS the wire format and IS the save format: trivially copyable,
// inside the link layer's 192-byte packet, so two devices share one description
// of a game and cannot drift. A nineteen by nineteen position does not fit at
// one byte a point, which is why the board is held two bits a point and reached
// through `at()` and `put()` rather than indexed directly. At nineteen it is a
// tight fit, not a comfortable one.
//
// The exact size is deliberately NOT written here. It was, as 140, and adding
// one byte for `accepted` made every copy of that number wrong at once -- in
// this file, in GoEngine.cpp and twice in docs/apps/go.md. The suite asserts
// the ceiling, which is the only part that matters.

#include <cstdint>

namespace go {

// The sizes a game can be played at. Nine is the default; thirteen and
// nineteen are settings. Every array here is sized for nineteen.
constexpr int kSmallSize = 9;
constexpr int kLargeSize = 13;
constexpr int kFullSize = 19;
constexpr int kMaxSize = kFullSize;

// Whether `size` is one of the three boards. The one place that list is
// written: the save reader, reset() and the settings row all ask this.
constexpr bool isBoardSize(const int size) { return size == kSmallSize || size == kLargeSize || size == kFullSize; }

// The line the corner star points sit on, counted from the edge from zero: the
// 3-3 points on nine, the 4-4s on thirteen and nineteen. The handicap stones
// and the drawn stars both read it, so a stone can never land off a star.
constexpr int starLine(const int size) { return size == kSmallSize ? 2 : 3; }
constexpr int kMaxPoints = kMaxSize * kMaxSize;

// Bytes in a one-bit-a-point mask (Game::dead, the group masks).
constexpr int kMaskBytes = (kMaxPoints + 7) / 8;
// Bytes in a two-bit-a-point board.
constexpr int kCellBytes = (kMaxPoints * 2 + 7) / 8;

// The three things a point can be. Values are deliberate twice over: `other()`
// is an xor, and they fit the two bits a packed board gives them.
constexpr uint8_t kEmpty = 0;
constexpr uint8_t kBlack = 1;
constexpr uint8_t kWhite = 2;

// A point index, or one of the two values below. Two bytes, because nineteen by
// nineteen has 361 points and a byte holds 256: every stored point (the ko, the
// last move, a flood fill's stack, a neighbour list) is a Point and never a
// uint8_t, which would silently wrap past the 256th point.
using Point = uint16_t;

// A move is a point index, or one of these. `kPass` is a real move -- it ends
// the game when doubled and it is the only legal move in a filled position --
// so it lives in the same space as the points rather than in a flag beside
// them, where a caller could forget it.
//
// Both sit above the largest board rather than just above the current one: a
// value that means "pass" on nine and "the last point" on thirteen is a save
// file that changes meaning when a setting is toggled.
constexpr Point kPass = 0xFFFE;
constexpr Point kNoPoint = 0xFFFF;

// Komi, in half points, always to White. 7.5 is what CGOS and every 9x9 engine
// tournament play, and the half point is not decoration: at a flat 7.0 a 44/37
// area split is an exact tie, which on this device would mean writing a draw
// screen for a result the ruleset exists to avoid. An odd number of half points
// against an even number cannot come out level.
//
// Held in halves so the core needs no floating point at all. It is the DEFAULT
// rather than a constant: a handicap game reduces it.
constexpr int16_t kDefaultKomiHalves = 15;

// The komi a handicap implies. They are ONE decision: a handicap game is played
// at half a point, an even one at seven and a half. Both are odd in halves, so
// neither can tie -- see settlesEveryGame.
constexpr int16_t komiForHandicap(const int handicap) { return handicap > 0 ? 1 : kDefaultKomiHalves; }

// The most handicap stones either board is worth giving. Beyond five the
// opening is largely pre-placed and there is not much game left to play.
constexpr int kMaxHandicap = 5;

// Whether a komi can produce a tie on this board. Area is a whole number of
// points, so only an odd number of half points settles every game.
constexpr bool settlesEveryGame(const int16_t komiHalves) { return (komiHalves % 2) != 0; }

// The longest a game may run before it is counted whether or not anybody
// passed. **[house rule]**
//
// Chinese rules with FULL superko terminate on their own, but the ring below
// remembers eight positions rather than every one, so a long enough cycle is
// not forbidden -- and an opponent that refuses to pass while it is losing
// (which is the correct behaviour, see GoEngine.h) will happily play into one.
// A self-play game ran past four hundred moves during testing.
//
// Five times the board: 405 on nine, 845 on thirteen, 1805 on nineteen. A real game is forty to a
// hundred and fifty moves and a human cannot reach this by playing; it exists
// so that a game on a device with a sleep timer cannot fail to end. Checkers
// carries the same kind of rule for the same reason.
constexpr uint16_t moveLimit(const int size) { return static_cast<uint16_t>(size * size * 5); }

// How many recent positions the superko rule looks back over.
//
// Full superko wants every position the game has ever held, which is a few
// hundred hashes and does not fit in a packet. Eight covers simple ko
// (length 1), the triple ko that is the reason superko exists at all (length 3
// each way), and every cycle a human will produce. A cycle longer than this is
// not reachable by accident, and the double-pass rule ends any game that finds
// one.
constexpr int kHistory = 8;

// Whose turn it is not.
constexpr uint8_t other(const uint8_t colour) { return colour ^ 3; }

constexpr bool isStone(const uint8_t point) { return point == kBlack || point == kWhite; }

// Where a point sits, on a board of `size`. Row 0 is the top of the screen,
// column 0 the left.
constexpr int rowOf(const int size, const int point) { return point / size; }
constexpr int colOf(const int size, const int point) { return point % size; }
constexpr int pointAt(const int size, const int row, const int col) { return row * size + col; }
constexpr bool onBoard(const int size, const int row, const int col) {
  return row >= 0 && row < size && col >= 0 && col < size;
}

// The four orthogonal neighbours of a point, written into `out`, returning how
// many there were. Corners have two, edges three.
//
// Every rule in Go is built on this one function, so it is the one place the
// edge of the board is expressed. Callers that open-code `point - size` wrap
// round the board and produce a game that is subtly not Go; there is exactly
// one such loop here and it is this.
int neighbours(int size, int point, Point out[4]);

// What a game is. This is the wire format and the save payload.
//
// Field order is by width, so the struct has no interior padding on either the
// device or the host. The link layer copies it raw between two identical
// builds, so its layout is a protocol and `GameId::Go` carries its version.
struct Game {
  // Truncated position keys, most recent last. Only `recentCount` of them are
  // real; the ring never wraps within one game because it is shifted down.
  uint32_t recent[kHistory];

  uint16_t capturedBy[3];  // indexed by colour; [0] is unused

  // Komi in half points, to White. In the game rather than in a constant
  // because a handicap changes it and the result screen reads it back.
  int16_t komiHalves;

  uint16_t moveNumber;
  Point ko;        // the point simple ko forbids, or kNoPoint
  Point lastMove;  // the move just played, for the marker on the board

  // The position, two bits a point, row major. Reached through at()/put():
  // a byte a point would be 361 at nineteen, and the whole packet is 192.
  uint8_t cell[kCellBytes];

  // Stones the players have agreed are dead, as a bit a point. Meaningless
  // until `stage` is Scoring; see GoFlow.h for why this is an agreement rather
  // than a computation.
  uint8_t dead[kMaskBytes];

  // Nine, thirteen or nineteen. Fixed when the game is reset and never changed under a
  // game in progress: the setting takes effect on the next new game.
  uint8_t size;

  uint8_t toMove;
  // How many stones Black was given before the first move. Zero is an even
  // game. Kept so the board can say so and the result can be read honestly.
  uint8_t handicap;
  uint8_t passes;  // consecutive passes; two ends the game
  uint8_t recentCount;
  uint8_t stage;  // go::Stage, held as a byte so the struct stays trivially copyable

  // Who has agreed the count, as a bit per colour: `1 << kBlack`, `1 << kWhite`.
  //
  // It lives in the GAME rather than in the activity because it has to cross
  // the wire. Two devices counting the same board have to agree TWICE -- once
  // about which stones are dead and once that they are finished -- and an
  // agreement held only on the device that made it is not an agreement: the
  // first version ended the game the moment either seat pressed ACCEPT, while
  // the button it pressed said WAITING.
  uint8_t accepted;

  // How many points this board has. Every loop over the position bounds itself
  // with this rather than with kMaxPoints: a nine by nine game leaves the tail
  // of every array untouched, and a loop that reads it sees stones that are not
  // there.
  int points() const { return static_cast<int>(size) * static_cast<int>(size); }

  // What is on a point. Out of range answers kEmpty rather than reading past
  // the board, because the callers that ask are hit tests and playouts.
  uint8_t at(const int point) const {
    if (point < 0 || point >= kMaxPoints) return kEmpty;
    return static_cast<uint8_t>((cell[point >> 2] >> ((point & 3) * 2)) & 3u);
  }

  void put(const int point, const uint8_t colour) {
    if (point < 0 || point >= kMaxPoints) return;
    const int shift = (point & 3) * 2;
    cell[point >> 2] = static_cast<uint8_t>((cell[point >> 2] & ~(3u << shift)) | ((colour & 3u) << shift));
  }

  int row(const int point) const { return rowOf(size, point); }
  int col(const int point) const { return colOf(size, point); }
  int point(const int r, const int c) const { return pointAt(size, r, c); }
  bool on(const int r, const int c) const { return onBoard(size, r, c); }
};

// Where a game is in its life. Not the same thing as whose turn it is, which
// is GoFlow.h's Phase: this one is Playing / Scoring / Over and changes three
// times a game, that one is Yours / Theirs / Finished and changes every move.
enum class Stage : uint8_t {
  // Stones are going down.
  Playing,
  // Both players passed. Dead stones are being agreed, and only then is there
  // a score. This stage is the whole reason casual Go is hard to ship.
  Scoring,
  // Counted, and the result is fixed.
  Over,
};

enum class Outcome : uint8_t { Running, BlackWins, WhiteWins };

// A fresh game on a board of `size`. With no handicap Black moves first; with
// one, Black's stones are already on the board and WHITE moves first, which is
// what a handicap is.
//
// `handicap` is 0 or 2..kMaxHandicap. One stone is not a handicap, it is Black
// playing first, which is the even game.
void reset(Game& game, int size = kSmallSize, int handicap = 0, int16_t komiHalves = kDefaultKomiHalves);

// Where the handicap stones go, in the order they are added. The corner star
// points and the centre, which is where every handicap on these boards is set.
int handicapPoints(int size, int handicap, Point out[kMaxHandicap]);

// Whether `colour` may play at `point` right now. Points are 0..points()-1;
// `kPass` is always legal and answers true.
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
void group(const Game& game, int point, uint8_t stones[kMaskBytes], int& size, int& liberties);

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
//
// **Every mask is kMaskBytes long, whatever board is being played.** The array
// parameter decays to a pointer, so a caller that sizes one for nine by nine
// compiles and then has eleven bytes of its own stack cleared by clearMask().
// That happened in a test, and it presented as a compiler bug: identical code,
// green under clang and red under g++.
inline bool marked(const uint8_t mask[kMaskBytes], const int point) {
  return (mask[point / 8] & (1u << (point % 8))) != 0;
}
inline void mark(uint8_t mask[kMaskBytes], const int point) { mask[point / 8] |= (1u << (point % 8)); }
inline void unmark(uint8_t mask[kMaskBytes], const int point) {
  mask[point / 8] &= static_cast<uint8_t>(~(1u << (point % 8)));
}
// Whether two masks mark exactly the same points.
inline bool sameMask(const uint8_t a[kMaskBytes], const uint8_t b[kMaskBytes]) {
  for (int i = 0; i < kMaskBytes; ++i) {
    if (a[i] != b[i]) return false;
  }
  return true;
}
inline void clearMask(uint8_t mask[kMaskBytes]) {
  for (int i = 0; i < kMaskBytes; ++i) mask[i] = 0;
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
void territory(const Game& game, uint8_t owner[kMaxPoints]);

struct Score {
  int16_t blackHalves;  // area in half points, so komi is expressible
  int16_t whiteHalves;
};

// The final count, in half points, with komi already added to White.
Score score(const Game& game);

// Whether `colour` has agreed the count as it stands.
constexpr bool hasAccepted(const Game& game, const uint8_t colour) { return (game.accepted & (1u << colour)) != 0; }

// Record that `colour` agrees. Returns true when BOTH now do, which is the only
// thing that ends a counted game.
inline bool accept(Game& game, const uint8_t colour) {
  game.accepted = static_cast<uint8_t>(game.accepted | (1u << colour));
  return hasAccepted(game, kBlack) && hasAccepted(game, kWhite);
}

// Nobody agrees any more. Called whenever a dead-stone mark changes, because a
// count that moved is a count nobody has read.
inline void withdrawAcceptance(Game& game) { game.accepted = 0; }

// Who won. Running until the game is Over.
Outcome outcome(const Game& game);

// The margin in half points, always positive. Ask `outcome()` who it is for.
int16_t marginHalves(const Game& game);

}  // namespace go

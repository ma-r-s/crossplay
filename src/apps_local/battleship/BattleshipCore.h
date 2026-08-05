#pragma once

// Battleship rules and a gunner, freestanding.
//
// No Arduino, no renderer, no heap, no allocation of any kind: the whole game
// is 52 bytes of plain struct, so host-tests/battleship/ runs the rules on a
// laptop in milliseconds and host-tests/link/ can play whole games between two
// simulated devices. Only BattleshipActivity draws.
//
// ---------------------------------------------------------------------------
// The state is the wire format, deliberately.
//
// Chess sends a FEN because its position has a text form older than computers.
// Battleship has no such thing, so `Game` itself is what travels: it is
// trivially copyable, it is 52 bytes against the link's 192-byte packet, and
// making it the wire format means there is exactly one description of a game in
// this app instead of two that can drift.
//
// Everything that can be derived is derived rather than stored -- who has won,
// whether a ship is sunk, whether the fleets are placed. A stored winner is a
// field two devices can disagree about; a computed one cannot be.
//
// `Game` holds BOTH fleets, including the one its owner must not see. That is
// the only shape that keeps whole-state-not-moves (and with it, desync being
// structurally impossible), and it is safe for the same reason the rest of this
// works: a match is two X4 Pros in the same room running the same build. The
// secret is kept by the drawing code, which never asks the opposing fleet where
// it is until the game is over. See BattleshipActivity::drawTargetGrid.
// ---------------------------------------------------------------------------

#include <cstdint>

namespace bship {

constexpr int kSize = 10;
constexpr int kCells = kSize * kSize;
constexpr int kShipCount = 5;
// Milton Bradley's fleet: carrier, battleship, cruiser, submarine, destroyer.
constexpr uint8_t kShipLength[kShipCount] = {5, 4, 3, 3, 2};
constexpr int kFleetCells = 17;
// One bit per cell, and 100 bits do not divide by 8.
constexpr int kShotBytes = (kCells + 7) / 8;

constexpr int cellOf(const int row, const int col) { return row * kSize + col; }
constexpr int rowOf(const int cell) { return cell / kSize; }
constexpr int colOf(const int cell) { return cell % kSize; }

// A ship is where its bow is and which way it lies, not a list of cells: two
// bytes instead of five, and an illegal shape is unrepresentable rather than
// checked for.
struct Ship {
  uint8_t bow = 0;
  uint8_t horizontal = 1;
};

struct Fleet {
  Ship ships[kShipCount] = {};
};

// One player's half of the game: their fleet, and every cell the other player
// has fired at it. Hits and misses are the same bit -- whether a shot hit is
// answered by asking the fleet, which is what stops the two from disagreeing.
struct Side {
  Fleet fleet;
  uint8_t shots[kShotBytes] = {};
  uint8_t placed = 0;
};

struct Game {
  Side side[2] = {};
  // Who acts next: places their fleet if they have not, otherwise fires.
  uint8_t turn = 0;
  // The shot that produced this state, as cell + 1, so zero means "none yet".
  // It rides along only so the receiving device can say what just happened;
  // a board says nothing about how it was reached.
  uint8_t lastShot = 0;
};

// The names are the fleet's, not decoration: "YOU SANK MY CRUISER" is the only
// moment in this game with any drama in it.
const char* shipName(int index);

// "C4" for the status line. `out` needs 4 bytes.
void cellName(int cell, char* out);

// A 32-bit xorshift, in the source rather than from <random> because this
// header is freestanding and because a test that cannot reproduce a game is
// worth very little. Every generator here takes its seed by reference.
uint32_t nextRandom(uint32_t& seed);

// --- one fleet --------------------------------------------------------------

// The `index`-th cell of a ship, counting from the bow. No bounds check: the
// caller owns the length, and every caller here has it.
int shipCell(const Ship& ship, int index);

// In bounds and clear of every other ship. `shipIndex` names the slot being
// placed, so a ship never collides with the version of itself it is replacing.
bool canPlace(const Fleet& fleet, int shipIndex, const Ship& candidate);

// Which ship occupies a cell, or -1. This is the fleet answering, which is why
// no hit/miss bit is ever stored.
int shipAt(const Fleet& fleet, int cell);

// A legal random layout. Rejection sampling: the board is 100 cells for 17 of
// fleet, so a placement almost never collides and the loop is bounded anyway.
void randomFleet(Fleet& fleet, uint32_t& seed);

// --- one side ---------------------------------------------------------------

bool shotAt(const Side& side, int cell);
void markShot(Side& side, int cell);
bool sunk(const Side& side, int shipIndex);
int sunkCount(const Side& side);
bool defeated(const Side& side);
int shotsTaken(const Side& side);
int hitsTaken(const Side& side);

// --- the game ---------------------------------------------------------------

void reset(Game& game);
bool bothPlaced(const Game& game);
bool over(const Game& game);
// 0 or 1, or -1 while the game is running.
int winner(const Game& game);

// Hands a fleet in and passes the turn. Refused unless it is that side's turn
// and they have not already placed one.
bool place(Game& game, int side, const Fleet& fleet);

// The only other mutator. The side to move fires at `cell` on the other side,
// then the turn passes -- a hit does not buy another shot. That is the standard
// rule and it is also what keeps turns strictly alternating, which is the one
// thing the link layer requires of a game.
//
// Refused (false, nothing changed) for an out-of-range cell, a cell already
// fired at, a fleet not yet placed, or a game already won. Every refusal is a
// state the UI should not have offered, so callers may treat false as a bug.
bool fire(Game& game, int cell);

// True when the shot that produced this state hit. Derived, like everything.
bool lastShotHit(const Game& game);
// The ship that shot sank, or -1. Named separately because sinking is the only
// thing worth interrupting the player for.
int lastShotSank(const Game& game);

// --- the gunner -------------------------------------------------------------

// The computer's next shot at `target`, as a cell. Hunt and target: it walks a
// wounded ship's line before going back to searching, and it searches on a
// parity lattice because no ship is shorter than two cells, which halves the
// board it has to cover.
//
// Stateless on purpose. Everything it knows is on the board it can see, so
// there is no gunner state to save, to restore, or to keep in step with a
// position that arrived from another device -- the last of which is what makes
// this the same function in single player and in a match.
//
// The one thing it knows that a person would have to work out: a hit belonging
// to an already-sunk ship is finished business. A human is told "you sank my
// cruiser" and usually deduces which run of hits that was; the gunner reads it
// off the fleet. That is a real advantage in the ambiguous cases and it is the
// only one it has.
int chooseShot(const Side& target, uint32_t& seed);

}  // namespace bship

#pragma once

// Hex: the rules. Freestanding -- no renderer, no Activity, no storage, no heap,
// no clock, no Arduino.
//
// Hein 1942, Nash 1948, public domain. Two players take turns placing a stone on
// an empty cell of an 11x11 rhombus of hexagons. Black joins the top edge to the
// bottom, White the left edge to the right. There are no captures, nothing ever
// moves, and **there are no draws**: a full board always contains exactly one
// winning connection, which is the Hex theorem. Everything below leans on it.
//
// **One board size and no setting for it.** Eleven is the size Hex is played at
// and the size the first-player advantage was measured on; offering nine as
// well would mean a second set of geometry constants, a saved field and a wire
// field, for a variant nobody asked for. See docs/apps/hex.md.
//
// **No swap rule.** The opening move is not stealable. That is a decided trade
// rather than an oversight: the swap is two more states to draw (offered,
// taken), a turn that belongs to neither player, and an explanation on the
// front door -- against a game whose whole appeal here is that the rules fit in
// one sentence.
//
// **Axial coordinates**, `idx = r * kSize + c`, and the six neighbours are
// `(r,c+1) (r+1,c) (r+1,c-1) (r,c-1) (r-1,c) (r-1,c+1)`. That is a cycle: each
// neighbour shares an edge with the next one round, which is what makes the
// bridge table below derivable rather than transcribed, and it is the same
// order HexScreens draws a hexagon's six edges in.
//
// **Win detection is union-find**, over the 121 cells plus four virtual nodes,
// one for each border. Placing a stone unions it with its same-coloured
// neighbours and with the border node when it sits on that player's edge;
// Black has won when TOP and BOTTOM are the same root. It is incremental, it is
// 125 bytes, and it costs nothing per move -- the alternative is a flood fill
// on every placement, on a device where the search wants every cycle.
//
// The union-find lives INSIDE `Game`, so it crosses the wire and lands in the
// save file with the position it describes. That is deliberate: a receiver that
// had to rebuild it would be a second implementation of the same fact, and the
// two would only have to disagree once. `find()` is bounded rather than
// trusting the forest it walks, because a packet is bytes from another device.

#include <cstdint>

namespace hex {

// The board. Not a setting -- see the header note.
constexpr int kSize = 11;
constexpr int kCells = kSize * kSize;

// Two bits a cell, so the whole game fits a link packet with room to spare.
constexpr int kCellBytes = (kCells * 2 + 7) / 8;
// One bit a cell, for the winning-chain mask the result screen draws.
constexpr int kMaskBytes = (kCells + 7) / 8;

// The four virtual border nodes, past the cells in the same index space.
constexpr int kTopNode = kCells;
constexpr int kBottomNode = kCells + 1;
constexpr int kLeftNode = kCells + 2;
constexpr int kRightNode = kCells + 3;
constexpr int kNodes = kCells + 4;

constexpr uint8_t kEmpty = 0;
// Black joins top to bottom, White left to right. Black and White rather than
// Red and Blue because the panel has two colours and Go's stones are already
// drawn: a Hex stone is a Go stone, so the shelf reads as one box of pieces.
constexpr uint8_t kBlack = 1;
constexpr uint8_t kWhite = 2;

// No cell. Above every real index, so it cannot be mistaken for one.
constexpr uint8_t kNoCell = 255;

constexpr uint8_t other(const uint8_t colour) { return colour == kBlack ? kWhite : kBlack; }
constexpr bool isStone(const uint8_t value) { return value == kBlack || value == kWhite; }

// The six neighbours, in cycle order: each shares an edge with the next.
constexpr int8_t kNeighbourRow[6] = {0, 1, 1, 0, -1, -1};
constexpr int8_t kNeighbourCol[6] = {1, 0, -1, -1, 0, 1};

constexpr int rowOf(const int cell) { return cell / kSize; }
constexpr int colOf(const int cell) { return cell % kSize; }
constexpr bool onBoard(const int row, const int col) { return row >= 0 && row < kSize && col >= 0 && col < kSize; }
constexpr int cellAt(const int row, const int col) { return row * kSize + col; }
constexpr bool validCell(const int cell) { return cell >= 0 && cell < kCells; }

// The neighbour in direction `dir`, or kNoCell off the board.
constexpr int neighbour(const int cell, const int dir) {
  const int row = rowOf(cell) + kNeighbourRow[dir];
  const int col = colOf(cell) + kNeighbourCol[dir];
  return onBoard(row, col) ? cellAt(row, col) : static_cast<int>(kNoCell);
}

// One game. Trivially copyable, and this IS the wire format and the save
// payload: two devices share one description of a position and cannot drift.
struct Game {
  // Two bits a cell.
  uint8_t cell[kCellBytes];
  // The union-find forest over kNodes. A node is its own parent when it is a
  // root; a cell that holds no stone is its own root and joins nothing.
  uint8_t parent[kNodes];
  uint8_t toMove;
  // kEmpty until a placement joins a player's two borders.
  uint8_t winner;
  // The stone just played, so the board can mark it. kNoCell before the first.
  uint8_t lastMove;
  // Named rather than left to the compiler, and zeroed by reset(). `moveNumber`
  // wants two-byte alignment, so without this the byte before it is PADDING --
  // which reset() never writes, which travels on the wire, and which makes two
  // games that are identical in every field compare unequal under memcmp. The
  // link layer copies this struct as bytes; a byte nobody owns is a byte that
  // eventually differs.
  uint8_t reserved;
  uint16_t moveNumber;

  uint8_t at(const int index) const {
    const int bit = index * 2;
    return static_cast<uint8_t>((cell[bit / 8] >> (bit % 8)) & 0x3u);
  }
  void put(const int index, const uint8_t value) {
    const int bit = index * 2;
    const uint8_t mask = static_cast<uint8_t>(0x3u << (bit % 8));
    cell[bit / 8] = static_cast<uint8_t>((cell[bit / 8] & ~mask) | ((value & 0x3u) << (bit % 8)));
  }
};

// An empty board with Black to move.
void reset(Game& game);

// The root of `node`, walking without compressing so a const position can be
// asked. Bounded by kNodes steps: the forest arrives from another device or off
// a card, and a cycle in it would otherwise hang the render task.
int find(const Game& game, int node);

// Whether two nodes are in the same component. `connected(game, kTopNode,
// kBottomNode)` is "Black has won".
bool connected(const Game& game, int a, int b);

// Whether a stone may be placed here by whoever is to move.
bool legal(const Game& game, int cell);

// Places the stone for `game.toMove`, unions it, sets the winner if that
// completed a connection, and hands the turn over. False (and nothing changed)
// when the cell is not legal.
bool play(Game& game, int cell);

// kEmpty while the game is running. Held in the game rather than recomputed,
// because it is what the whole union-find exists to answer.
constexpr uint8_t winner(const Game& game) { return game.winner; }
constexpr bool over(const Game& game) { return game.winner != kEmpty; }

// Every cell holds a stone. By the Hex theorem this implies a winner, and
// host-tests/hex asserts exactly that over random fills.
bool full(const Game& game);

// Whether placing at `cell` would win for `colour` outright. Used by the brain's
// root check, so the machine can never miss a mate in one or fail to block one.
bool winsImmediately(const Game& game, int cell, uint8_t colour);

// The winner's chain, as a bit a cell: the shortest connection from their first
// border to their second, found by breadth-first search over their stones. The
// whole connected group would also be true and is not what a player wants to
// look at -- most of a finished Hex group is scaffolding, and marking all of it
// hides the line that actually won.
//
// Clears `out` and returns false when nobody has won.
bool winningChain(const Game& game, uint8_t out[kMaskBytes]);

constexpr bool marked(const uint8_t mask[kMaskBytes], const int cell) {
  return (mask[cell / 8] & (1u << (cell % 8))) != 0;
}

// ---------------------------------------------------------------------------
// Bridges.
//
// Two same-coloured stones two apart with exactly two empty cells between them
// are BRIDGED: whichever of the two the opponent takes, the owner takes the
// other, so the pair is connected whatever happens. It is the one pattern that
// makes a Hex playout look like Hex rather than like noise, and Cazenave and
// Saffidine measured it at about +105 Elo in the playout policy alone.
//
// Derived rather than transcribed. A cell `m` is a carrier exactly when two of
// its own neighbours, two apart in the cycle above, are the bridge's ends -- and
// then the third neighbour, the one between them, is the other carrier. Six
// patterns a cell, which is the table below.
// ---------------------------------------------------------------------------

struct Bridge {
  // The two ends of the bridge this cell carries, and the OTHER cell carrying
  // it. kNoCell in `end` when the pattern runs off the board.
  uint8_t end[2];
  uint8_t partner;
};

struct BridgeTable {
  Bridge at[kCells][6];
};

constexpr BridgeTable makeBridgeTable() {
  BridgeTable table{};
  for (int cell = 0; cell < kCells; ++cell) {
    for (int dir = 0; dir < 6; ++dir) {
      const int first = neighbour(cell, dir);
      const int between = neighbour(cell, (dir + 1) % 6);
      const int second = neighbour(cell, (dir + 2) % 6);
      const bool whole = first != kNoCell && between != kNoCell && second != kNoCell;
      table.at[cell][dir].end[0] = whole ? static_cast<uint8_t>(first) : kNoCell;
      table.at[cell][dir].end[1] = whole ? static_cast<uint8_t>(second) : kNoCell;
      table.at[cell][dir].partner = whole ? static_cast<uint8_t>(between) : kNoCell;
    }
  }
  return table;
}

// About two kilobytes of flash, built at compile time, so a playout's bridge
// response is three array reads rather than six neighbour computations.
inline constexpr BridgeTable kBridges = makeBridgeTable();

}  // namespace hex

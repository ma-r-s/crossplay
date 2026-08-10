#pragma once

// Toy Battle, the rules. Freestanding: no renderer, no Activity, no storage.
//
// See docs/toybattle.md for the rulebook this implements, taken from Repos'
// own English rulebook and player aid rather than from summaries. The shape:
//
//   * `Game` is the whole shared state AND the LinkPlay wire format. One
//     description of a game, so two devices cannot drift.
//   * The board is the placement log, in order. The top of a base is the last
//     entry naming it. Every removal in this game pops a *visible* troop, so a
//     log only ever grows at the end or loses a base's last entry -- and an
//     uncovered tile can flip who holds a base, which is why the buried ones
//     have to be kept at all.
//   * Everything derivable is derived: connection, legal moves, region
//     control, the winner. No stored field can contradict a computed one.

#include <cstdint>
#include <type_traits>

namespace toybattle {

// ---------------------------------------------------------------------------
// Troops
// ---------------------------------------------------------------------------

enum class Troop : uint8_t { Kwak = 0, Skully, Capn, Jumbo, Hook, XB42, Star, Roxy };

constexpr int kTroopKinds = 8;
constexpr int kCopiesEach = 3;
constexpr int kTroopsPerSeat = kTroopKinds * kCopiesEach;  // 24
constexpr int kSetAside = 4;                               // removed unseen at setup
constexpr int kReserveSize = kTroopsPerSeat - kSetAside;   // 20
constexpr int kRackLimit = 8;
constexpr int kSeats = 2;

// Kwak has no printed strength: it is a joker. The 0 here is a placeholder that
// `covers` never consults, because both joker branches short-circuit first.
// Reading it as "the weakest troop" would be wrong -- a 0 could not cover
// another 0, and Kwak can cover anything, including another Kwak.
constexpr uint8_t kStrength[kTroopKinds] = {0, 1, 2, 3, 4, 5, 6, 7};

constexpr bool isJoker(Troop t) { return t == Troop::Kwak; }
constexpr int strengthOf(Troop t) { return kStrength[static_cast<int>(t)]; }

// May `attacker` be placed on top of an enemy `defender`?
constexpr bool covers(Troop attacker, Troop defender) {
  return isJoker(attacker) || isJoker(defender) || strengthOf(attacker) > strengthOf(defender);
}

// ---------------------------------------------------------------------------
// Terrain
// ---------------------------------------------------------------------------

constexpr int kMaxBases = 32;  // region masks are uint32_t over bases
constexpr int kMaxHq = 4;      // Caribbean Sea is 2 blue against 1 red
constexpr int kMaxSlots = kMaxBases + kMaxHq;
constexpr int kMaxRegions = 16;
constexpr int kMaxEdges = 72;

constexpr uint8_t kNoSlot = 0xFF;

struct Edge {
  uint8_t a = 0;
  uint8_t b = 0;
};

// A closed zone fenced by paths and bases. Held when one seat occupies every
// base in `bases`; the medals are then banked for good.
struct Region {
  uint32_t bases = 0;
  uint8_t medals = 0;
};

// Slots are numbered bases first, then H.Q. A base is a slot below
// `baseCount`; an H.Q. is not a base and is never a stepping stone.
struct Terrain {
  const char* name = "";
  uint8_t baseCount = 0;
  uint8_t hqCount = 0;
  uint8_t hqSeat[kMaxHq] = {};  // which seat owns H.Q. slot i
  uint8_t medalsObjective = 0;
  uint8_t regionCount = 0;
  Region regions[kMaxRegions] = {};
  uint8_t edgeCount = 0;
  Edge edges[kMaxEdges] = {};
  // Normalised 0..1000, so the renderer and hit-testing share one geometry
  // rather than computing it twice. Slot i, bases then H.Q.
  uint16_t x[kMaxSlots] = {};
  uint16_t y[kMaxSlots] = {};
  // Derived from `edges` at compile time by `withAdjacency`.
  uint64_t adj[kMaxSlots] = {};

  constexpr int slotCount() const { return baseCount + hqCount; }
  constexpr bool isBase(int slot) const { return slot < baseCount; }
  constexpr bool isHq(int slot) const { return slot >= baseCount && slot < slotCount(); }
  constexpr int hqOwner(int slot) const { return hqSeat[slot - baseCount]; }
};

// Fills `adj` from `edges`. Authoring an edge list twice, once per direction,
// is how a one-way path gets into a board nobody can debug.
constexpr Terrain withAdjacency(Terrain t) {
  for (int i = 0; i < t.edgeCount; ++i) {
    t.adj[t.edges[i].a] |= uint64_t{1} << t.edges[i].b;
    t.adj[t.edges[i].b] |= uint64_t{1} << t.edges[i].a;
  }
  return t;
}

// PROVING GROUND is ours, not Repos'. The eight real terrains are irregular and
// only Castle Field has ever been photographed straight on, so authoring them
// waits on a source (docs/toybattle.md, "Open items"). This one is a 5x3
// lattice: every rule has something to bite on -- two H.Q. at opposite ends,
// eight regions, and enough width that connection can actually be cut -- and it
// is named so that nobody mistakes it for a board Repos printed.
extern const Terrain kProvingGround;

const Terrain& terrainAt(int index);
constexpr int kTerrainCount = 1;

// ---------------------------------------------------------------------------
// Moves
// ---------------------------------------------------------------------------

// Cap'n places an extra troop, and that troop may be another Cap'n. Four is
// past what a rack plus the reserve can sustain in one turn.
constexpr int kMaxChain = 4;

struct Step {
  uint8_t slot = kNoSlot;
  uint8_t kind = 0;  // Troop
  // Every troop effect is printed "You may", so declining is a legal choice and
  // has to be representable. For Hook this is the connection waiver itself, not
  // an after-effect.
  bool useEffect = false;
  // Jumbo: the base whose visible enemy troop is discarded. Unused otherwise.
  uint8_t target = kNoSlot;
};

struct Move {
  enum class Kind : uint8_t { Draw = 0, Place } kind = Kind::Draw;
  uint8_t stepCount = 0;
  Step steps[kMaxChain] = {};

  static Move draw();
  static Move place(int slot, Troop kind, bool useEffect = false, int target = kNoSlot);
  // Appends a Cap'n's extra placement. Only legal when the previous step is a
  // Cap'n that used its effect.
  Move& then(int slot, Troop kind, bool useEffect = false, int target = kNoSlot);
};

enum class Phase : uint8_t { Playing = 0, GameOver };

// Why the game ended, so a screen can say it rather than infer it.
enum class Ending : uint8_t { None = 0, HqCaptured, MedalsObjective, Stuck };

// ---------------------------------------------------------------------------
// The game
// ---------------------------------------------------------------------------

constexpr int kMaxPlacements = kTroopsPerSeat * kSeats;  // 48
constexpr uint8_t kNoSeat = 0xFF;

// Trivially copyable, so LinkPlay ships it as raw bytes between two identical
// builds.
struct Game {
  uint32_t seed = 0;  // both reserves derive from this; both devices rebuild them
  uint8_t terrain = 0;
  uint8_t turn = 0;
  uint8_t phase = static_cast<uint8_t>(Phase::Playing);
  uint8_t ending = static_cast<uint8_t>(Ending::None);
  uint8_t winner = kNoSeat;

  uint8_t reserveTaken[kSeats] = {};
  uint8_t rack[kSeats][kTroopKinds] = {};       // a multiset; rack order is not a rule
  uint8_t discarded[kSeats][kTroopKinds] = {};  // face up, so both players know it
  uint8_t medals[kSeats] = {};
  uint16_t regionsTaken = 0;

  // XB-42 steals at random. The tick advances with every random draw so both
  // devices produce the same victim from the same seed.
  uint8_t rngTick = 0;

  // The board, in placement order. `placeTile` packs seat<<3 | kind.
  uint8_t placementCount = 0;
  uint8_t placeSlot[kMaxPlacements] = {};
  uint8_t placeTile[kMaxPlacements] = {};

  // --- set-up -------------------------------------------------------------

  // The rulebook's order: shuffle 24, set 4 aside unseen, starter racks 3 and
  // the opponent racks 4.
  void newGame(uint32_t gameSeed, int terrainIndex, int starter);

  // --- playing ------------------------------------------------------------

  bool isLegal(const Move& move) const;

  // Applies a legal move and passes the turn. Returns false and changes
  // nothing if the move is not legal: it works on a copy and commits only on
  // success, so a half-applied chain cannot be left behind.
  //
  // Wins are checked the instant they happen, which for a Cap'n chain means
  // mid-chain: reaching the medals objective with the first placement ends the
  // game and the second placement never happens.
  bool apply(const Move& move);

  // --- derived, never stored ----------------------------------------------

  const Terrain& board() const { return terrainAt(terrain); }
  Phase currentPhase() const { return static_cast<Phase>(phase); }
  Ending endedBy() const { return static_cast<Ending>(ending); }

  int rackSize(int seat) const;
  int reserveRemaining(int seat) const { return kReserveSize - reserveTaken[seat]; }

  // The seat holding `base`, or kNoSeat. Only the visible top tile occupies.
  int occupantSeat(int base) const;
  Troop occupantTroop(int base) const;  // meaningless when unoccupied
  int stackDepth(int base) const;
  uint32_t occupiedBy(int seat) const;

  // Slots this seat may legally reach: adjacent to one of their H.Q., or
  // adjacent to a base of theirs that is itself connected. The H.Q. is the
  // starting point only, never a step.
  uint64_t reachable(int seat) const;

  bool canPlaceHere(int seat, int slot, Troop kind, bool hookWaiver) const;
  // The same test with `reachable(seat)` hoisted out, for callers that test
  // many slots against one position.
  bool placeableWith(int seat, int slot, Troop kind, bool hookWaiver, uint64_t reach) const;
  bool canDraw(int seat) const;

  // Single placements only. Chains are the caller's business, because only a
  // brain needs them and enumerating them here would cost the UI nothing it
  // uses. Returns how many were written.
  int legalPlacements(int seat, Step* out, int max) const;
  bool hasAnyLegalMove(int seat) const;

  // The i-th troop of a seat's shuffled 24. Indices below kSetAside are the
  // four nobody ever sees; drawing reads from kSetAside upward.
  Troop reserveAt(int seat, int index) const;
};

static_assert(std::is_trivially_copyable<Game>::value, "Game travels as raw bytes");
static_assert(sizeof(Game) <= 192, "Game must fit one LinkPlay packet");

}  // namespace toybattle

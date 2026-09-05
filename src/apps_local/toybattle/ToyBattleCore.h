#pragma once

// Toy Battle, the rules. Freestanding: no renderer, no Activity, no storage.
//
// See docs/apps/toybattle.md for the rulebook this implements, taken from Repos'
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
  // Where the medals are drawn, normalised 0..1000 like every other coordinate
  // here. The centre of the fence bases is the obvious answer and it is wrong
  // the same way for every thin region: a triangle of two column bases and one
  // centre base puts its centroid a third of the way across, hard against the
  // column. This is the roomiest point inside the region instead, worked out by
  // to_cpp.py at authoring time because it never changes once a board exists.
  uint16_t x = 0;
  uint16_t y = 0;
};

// What a special base does, one kind per real terrain. Named for the mechanic
// rather than the terrain, because the terrain is only where Repos put it.
//
// Caribbean Sea has no entry: it has no special bases at all, and is asymmetric
// instead (2 H.Q. against 1), which the terrain already expresses.
//
// The last two are *placement restrictions*: the aid is explicit that they
// happen before the troop is placed, not after.
enum class Special : uint8_t {
  None = 0,
  Recall,    // Castle Field: return one of your OTHER troops, anywhere, to your rack
  Draw,      // City of Clouds: draw 1 from your reserve
  Shove,     // Volcanic Jungle: move an adjacent enemy troop to a base beside its start
  Exhume,    // Cursed Cemetery: take one of your own troops out of the discard
  Suppress,  // Battlefield: an enemy rack troop, picked blind, sits out their next turn
  Gate,      // Tropical Pool: only the printed values may be placed here
  Nullify,   // Station Metal-X: troop effects do not apply on this base
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
  // Per base. A real terrain uses one kind throughout; PROVING GROUND mixes
  // them because it is the board the tests live on.
  uint8_t special[kMaxBases] = {};
  // Special::Gate only: which troop kinds this slot admits, as a bitmask over
  // Troop. 0 means ungated, which is every slot on every terrain but Tropical
  // Pool's. Indexed by slot, not base, because the gate covers the H.Q. too.
  uint8_t gate[kMaxSlots] = {};
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
  constexpr Special specialAt(int slot) const {
    return isBase(slot) ? static_cast<Special>(special[slot]) : Special::None;
  }
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

// CASTLE FIELD, the rulebook's own first terrain. Topology is Repos'; the
// layout is ours. The printed board's coordinates are not copied -- what
// matters is which bases exist, what joins them, and which of them fence each
// region, and all of that survives being redrawn for a 480x800 panel. See
// docs/apps/toybattle.md for how it was read and which parts were inferred.
extern const Terrain kCastleField;

// PROVING GROUND is ours and is named so. A 5x3 lattice carrying every special
// base kind at once, which no printed terrain does; it exists so the rules
// tests have somewhere to exercise all of them.
extern const Terrain kProvingGround;

// CITY OF CLOUDS, traced by Mario 2026-08-11. Fourteen bases in two columns
// with four Draw bases down the middle, each joined to two on either side, so
// every face of the board is a triangle or a quad and there are thirteen of
// them. Sixteen medals, objective 8 -- the same half-the-board ratio Castle
// Field uses.
extern const Terrain kCityOfClouds;

// VOLCANIC JUNGLE, traced by Mario 2026-08-11. A complete 3x5 lattice with two
// Shove bases and two diagonal paths, and the first board with 180-degree
// rotational symmetry rather than mirror symmetry: every slot has a partner
// under a half turn, including the two H.Q. and the two Shove bases, and both
// diagonals are each other's partner. Six regions, two of them pentagons that
// close across a diagonal. Fourteen medals, objective 8.
extern const Terrain kVolcanicJungle;

// CURSED CEMETERY, traced by Mario 2026-08-11. Fifteen bases in an irregular
// arrangement with four Exhume graves, and the first board that is
// point-symmetric WITHOUT being a grid -- which is why the generator grew a
// rotational symmetry mode: mirroring the rows and columns separately is only
// equivalent to a half turn when every slot sits on a row-and-column crossing.
// Ten regions, fourteen medals, objective 7.
extern const Terrain kCursedCemetery;

// BATTLEFIELD, traced by Mario 2026-08-11. Fourteen bases either side of a
// central spine, four Suppress bases, nine regions including two six-base ones
// fenced partly by an H.Q. Sixteen medals, objective 8. Symmetric across both
// midlines.
extern const Terrain kBattlefield;

// CARIBBEAN SEA, traced by Mario 2026-08-11. The lopsided one: twelve bases, no
// special bases at all, and THREE H.Q. -- two for seat 0, one for seat 1, which
// is what kMaxHq was sized for. No symmetry of any kind, so the only tidying it
// gets is its top and bottom rows levelled and its left and right edges made
// plumb. Six regions, eleven medals, objective 5.
extern const Terrain kCaribbeanSea;

// TROPICAL POOL, traced by Mario 2026-08-11. The gated one, and the only board
// where a special restricts what may be PLACED rather than firing after a troop
// lands. Five gated bases and FOUR H.Q. -- two per player, one of each pair
// gated to 6 and 7 -- which is what `gate` being indexed by slot was always for.
// Thirteen bases, eight regions, twelve medals, objective 6. Point-symmetric
// under a half turn, gates and all.
extern const Terrain kTropicalPool;

// STATION METAL-X, traced by Mario 2026-08-11, and the eighth printed terrain --
// the last one. Three Nullify bases across the middle, and the only board that
// is a web rather than a lattice: bases 3 and 8 are hubs of degree seven, and
// its fourteen regions are almost all triangles because of it. Fourteen medals,
// objective 7. Point-symmetric under a half turn.
extern const Terrain kStationMetalX;

// LA CROISETTE, traced by Mario 2026-08-11 -- the 2026 expansion, and the only
// board here that is not from the base game. A beach down the left side and a
// town down the right: three piers (two gated 4-7-or-joker, one that nullifies),
// two ice cream vans that draw, two police boxes that suppress. All four special
// kinds on one board, which no printed base-game terrain does. Eleven medals,
// objective 5 -- the floor of half, as on Caribbean Sea. Mirror-symmetric about
// the horizontal midline, which is "vertical" here because that mirrors y.
extern const Terrain kLaCroisette;

// Index into the table `terrainAt` walks. Stored in `Game::terrain`, so the
// order is part of the save and wire format: append, never reorder.
enum class TerrainId : uint8_t {
  CastleField = 0,
  ProvingGround,
  CityOfClouds,
  VolcanicJungle,
  CursedCemetery,
  Battlefield,
  CaribbeanSea,
  TropicalPool,
  StationMetalX,
  LaCroisette
};
constexpr int kTerrainCount = 10;

// PROVING GROUND is ours, not a board Repos printed, and it exists so the rules
// suite has somewhere to run that carries every special kind at once and never
// changes underneath it. Players must never see it in the map list beside nine
// real boards. It stays where it is rather than being renumbered out of the
// way, because the index is in every save file and every link packet.
//
// Note WHERE it is: terrain 1, with Castle Field at 0. It is in the MIDDLE of
// the range, not at the start, so hiding it is a skip and not an offset. The
// first version of this was written as `kFirstPlayableTerrain = 1` on the
// assumption that Proving Ground was terrain 0; that hid CASTLE FIELD, left
// Proving Ground at the top of the picker, and defaulted new games to it. Both
// UI assertions passed, because they were written from the same wrong
// assumption and checked `terrainAt(0)` -- which is Castle Field. It was caught
// by looking at the emulator, and nothing else would have caught it.
constexpr TerrainId kHiddenTerrain = TerrainId::ProvingGround;
constexpr int kPlayableTerrainCount = kTerrainCount - 1;

// The nth board a player can choose, in picker order, skipping the hidden one.
constexpr int playableTerrainAt(const int nth) { return nth < static_cast<int>(kHiddenTerrain) ? nth : nth + 1; }

const Terrain& terrainAt(int index);

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

  // The special base under this placement, if any, is a second decision with
  // its own choices. Declining is legal here too, and a base whose effect the
  // player declines is the same as a base with no effect.
  bool useBase = false;
  uint8_t baseFrom = kNoSlot;  // Recall: my troop's base. Shove: the victim's base.
  uint8_t baseTo = kNoSlot;    // Shove: where the victim goes.
  uint8_t baseKind = kNoSlot;  // Exhume: which of my discarded troops to take back.
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
  // Field order is layout, not taste. Every byte of this struct is memcmp'd,
  // saved and put on the wire, so a hole no field owns ships whatever the stack
  // left there -- two identical games would compare unequal and a packet would
  // carry four bytes of noise. The two wide fields lead, the bytes follow, and
  // the static_assert at the bottom is what keeps it that way.
  uint32_t seed = 0;  // both reserves derive from this; both devices rebuild them
  uint16_t regionsTaken = 0;

  uint8_t terrain = 0;
  uint8_t turn = 0;
  uint8_t phase = static_cast<uint8_t>(Phase::Playing);
  uint8_t ending = static_cast<uint8_t>(Ending::None);
  uint8_t winner = kNoSeat;

  uint8_t reserveTaken[kSeats] = {};
  uint8_t rack[kSeats][kTroopKinds] = {};       // a multiset; rack order is not a rule
  uint8_t discarded[kSeats][kTroopKinds] = {};  // face up, so both players know it
  uint8_t medals[kSeats] = {};

  // Whether this game plays with special base effects. It lives in the state
  // rather than in app settings on purpose: two linked devices must agree on
  // it, and the opponent has to see it to play correctly. The app setting
  // chooses the default and `newGame` bakes it in. The rulebook itself
  // sanctions playing without ("treat all special bases like bases with no
  // effect"), so off is a real way to play, not a crippled one.
  uint8_t specialBases = 1;

  // Battlefield's suppression: a troop the enemy pointed at blind, which sits
  // out this seat's next turn and returns at the end of it. It stays on the
  // rack throughout and still counts against the 8. kNoSlot when nothing is
  // suppressed.
  uint8_t frozenKind[kSeats] = {kNoSlot, kNoSlot};

  // XB-42 steals at random. The tick advances with every random draw so both
  // devices produce the same victim from the same seed.
  uint8_t rngTick = 0;

  // The board, in placement order. `placeTile` packs seat<<3 | kind.
  uint8_t placementCount = 0;
  uint8_t placeSlot[kMaxPlacements] = {};
  uint8_t placeTile[kMaxPlacements] = {};

  // --- set-up -------------------------------------------------------------

  // The rulebook's order: shuffle 24, set 4 aside unseen, starter racks 3 and
  // the opponent racks 4. `withSpecialBases` is baked into the state here and
  // never changes mid-game.
  void newGame(uint32_t gameSeed, int terrainIndex, int starter, bool withSpecialBases = true);

  // A state that arrived over the radio is not trusted. The link layer checks
  // the payload length and nothing else, so a corrupt packet reaches here as a
  // `turn` of 200 indexing a two-seat array. Everything indexed or iterated is
  // range-checked; call this before letting a received state anywhere near a
  // screen or a brain.
  bool isWellFormed() const;

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
// Exact, because every byte is compared, saved and transmitted. This is the sum
// of the fields: 4 + 2 + 142. If it fails, the fields have grown a hole that no
// field owns, and those bytes are whatever the stack left there -- which is a
// state that does not memcmp equal to itself and a packet with noise in it.
// Reorder to close the hole rather than relaxing this, and bump the save
// version and linkplay::GameId when the layout genuinely changes.
static_assert(sizeof(Game) == 148, "Game must have no padding: see the comment above");

}  // namespace toybattle

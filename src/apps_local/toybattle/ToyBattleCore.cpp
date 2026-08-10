#include "ToyBattleCore.h"

#include <cstring>

namespace toybattle {
namespace {

constexpr int other(int seat) { return seat ^ 1; }

uint32_t mix32(uint32_t x) {
  x += 0x9E3779B9u;
  x = (x ^ (x >> 16)) * 0x85EBCA6Bu;
  x = (x ^ (x >> 13)) * 0xC2B2AE35u;
  return x ^ (x >> 16);
}

// Counter-based rather than a stream: the i-th value of a seat's shuffle is a
// pure function of the seed, so a device can rebuild any part of it without
// having replayed the rest.
uint32_t shuffleBit(uint32_t seed, int seat, int i) {
  return mix32(seed ^ (static_cast<uint32_t>(seat) * 0x9E3779B9u) ^ (static_cast<uint32_t>(i) * 0x85EBCA6Bu));
}

uint8_t packTile(int seat, Troop kind) { return static_cast<uint8_t>((seat << 3) | static_cast<int>(kind)); }
constexpr int tileSeat(uint8_t packed) { return packed >> 3; }
constexpr Troop tileKind(uint8_t packed) { return static_cast<Troop>(packed & 0x07); }

bool hasEffect(Troop kind) { return kind != Troop::Kwak && kind != Troop::Roxy; }

}  // namespace

// ---------------------------------------------------------------------------
// Terrain
// ---------------------------------------------------------------------------

namespace {

// 15 bases in a 5x3 lattice, an H.Q. at each end, and the eight cells of the
// lattice as regions. See the header for why this is ours and not Repos'.
constexpr Terrain buildProvingGround() {
  Terrain t{};
  t.name = "PROVING GROUND";
  t.baseCount = 15;
  t.hqCount = 2;
  t.hqSeat[0] = 0;
  t.hqSeat[1] = 1;
  t.medalsObjective = 7;

  // Rows of 5, left to right, top row first.
  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < 5; ++col) {
      const int b = row * 5 + col;
      t.x[b] = static_cast<uint16_t>(200 + col * 150);
      t.y[b] = static_cast<uint16_t>(200 + row * 300);
    }
  }
  t.x[15] = 60;
  t.y[15] = 500;  // seat 0's H.Q., left
  t.x[16] = 940;
  t.y[16] = 500;  // seat 1's H.Q., right

  int e = 0;
  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < 4; ++col) {
      t.edges[e++] = Edge{static_cast<uint8_t>(row * 5 + col), static_cast<uint8_t>(row * 5 + col + 1)};
    }
  }
  for (int row = 0; row < 2; ++row) {
    for (int col = 0; col < 5; ++col) {
      t.edges[e++] = Edge{static_cast<uint8_t>(row * 5 + col), static_cast<uint8_t>((row + 1) * 5 + col)};
    }
  }
  t.edges[e++] = Edge{15, 0};
  t.edges[e++] = Edge{15, 5};
  t.edges[e++] = Edge{15, 10};
  t.edges[e++] = Edge{16, 4};
  t.edges[e++] = Edge{16, 9};
  t.edges[e++] = Edge{16, 14};
  t.edgeCount = static_cast<uint8_t>(e);

  int r = 0;
  for (int row = 0; row < 2; ++row) {
    for (int col = 0; col < 4; ++col) {
      const int tl = row * 5 + col;
      t.regions[r].bases =
          (uint32_t{1} << tl) | (uint32_t{1} << (tl + 1)) | (uint32_t{1} << (tl + 5)) | (uint32_t{1} << (tl + 6));
      t.regions[r].medals = 2;
      ++r;
    }
  }
  t.regionCount = static_cast<uint8_t>(r);
  return t;
}

}  // namespace

const Terrain kProvingGround = withAdjacency(buildProvingGround());

const Terrain& terrainAt(int) { return kProvingGround; }

// ---------------------------------------------------------------------------
// Moves
// ---------------------------------------------------------------------------

Move Move::draw() {
  Move m;
  m.kind = Kind::Draw;
  m.stepCount = 0;
  return m;
}

Move Move::place(int slot, Troop kind, bool useEffect, int target) {
  Move m;
  m.kind = Kind::Place;
  m.stepCount = 1;
  m.steps[0].slot = static_cast<uint8_t>(slot);
  m.steps[0].kind = static_cast<uint8_t>(kind);
  m.steps[0].useEffect = useEffect;
  m.steps[0].target = static_cast<uint8_t>(target);
  return m;
}

Move& Move::then(int slot, Troop kind, bool useEffect, int target) {
  if (stepCount < kMaxChain) {
    steps[stepCount].slot = static_cast<uint8_t>(slot);
    steps[stepCount].kind = static_cast<uint8_t>(kind);
    steps[stepCount].useEffect = useEffect;
    steps[stepCount].target = static_cast<uint8_t>(target);
    ++stepCount;
  }
  return *this;
}

// ---------------------------------------------------------------------------
// Set-up
// ---------------------------------------------------------------------------

Troop Game::reserveAt(int seat, int index) const {
  uint8_t deck[kTroopsPerSeat];
  for (int i = 0; i < kTroopsPerSeat; ++i) deck[i] = static_cast<uint8_t>(i / kCopiesEach);
  for (int i = kTroopsPerSeat - 1; i > 0; --i) {
    const int j = static_cast<int>(shuffleBit(seed, seat, i) % static_cast<uint32_t>(i + 1));
    const uint8_t tmp = deck[i];
    deck[i] = deck[j];
    deck[j] = tmp;
  }
  return static_cast<Troop>(deck[index]);
}

void Game::newGame(uint32_t gameSeed, int terrainIndex, int starter) {
  *this = Game{};
  seed = gameSeed;
  terrain = static_cast<uint8_t>(terrainIndex);
  turn = static_cast<uint8_t>(starter);

  // The starter racks 3 and the opponent 4: the second player's compensation
  // for moving second, and the only asymmetry in the set-up.
  const int opening[kSeats] = {starter == 0 ? 3 : 4, starter == 0 ? 4 : 3};
  for (int seat = 0; seat < kSeats; ++seat) {
    for (int i = 0; i < opening[seat]; ++i) {
      const Troop drawn = reserveAt(seat, kSetAside + reserveTaken[seat]);
      ++reserveTaken[seat];
      ++rack[seat][static_cast<int>(drawn)];
    }
  }
}

// ---------------------------------------------------------------------------
// Reading the board
// ---------------------------------------------------------------------------

int Game::rackSize(int seat) const {
  int n = 0;
  for (int k = 0; k < kTroopKinds; ++k) n += rack[seat][k];
  return n;
}

int Game::occupantSeat(int base) const {
  for (int i = placementCount - 1; i >= 0; --i) {
    if (placeSlot[i] == base) return tileSeat(placeTile[i]);
  }
  return kNoSeat;
}

Troop Game::occupantTroop(int base) const {
  for (int i = placementCount - 1; i >= 0; --i) {
    if (placeSlot[i] == base) return tileKind(placeTile[i]);
  }
  return Troop::Kwak;
}

int Game::stackDepth(int base) const {
  int n = 0;
  for (int i = 0; i < placementCount; ++i) {
    if (placeSlot[i] == base) ++n;
  }
  return n;
}

uint32_t Game::occupiedBy(int seat) const {
  // One forward pass, last write wins. Asking `occupantSeat` per base instead
  // would rescan the whole log for every base, and this runs inside move
  // generation.
  uint8_t top[kMaxBases];
  memset(top, kNoSeat, sizeof(top));
  for (int i = 0; i < placementCount; ++i) {
    if (placeSlot[i] < kMaxBases) top[placeSlot[i]] = static_cast<uint8_t>(tileSeat(placeTile[i]));
  }
  const Terrain& b = board();
  uint32_t mask = 0;
  for (int base = 0; base < b.baseCount; ++base) {
    if (top[base] == seat) mask |= uint32_t{1} << base;
  }
  return mask;
}

uint64_t Game::reachable(int seat) const {
  const Terrain& b = board();
  const uint32_t mine = occupiedBy(seat);

  uint64_t out = 0;
  for (int i = 0; i < b.hqCount; ++i) {
    if (b.hqSeat[i] == seat) out |= b.adj[b.baseCount + i];
  }
  // Grow through bases I hold. The H.Q. is the starting point only: it never
  // appears in `mine`, so it can never be walked through.
  for (bool grew = true; grew;) {
    grew = false;
    for (int base = 0; base < b.baseCount; ++base) {
      const uint64_t bit = uint64_t{1} << base;
      if (!(out & bit)) continue;
      if (!(mine & (uint32_t{1} << base))) continue;
      const uint64_t next = b.adj[base] & ~out;
      if (next) {
        out |= next;
        grew = true;
      }
    }
  }
  return out;
}

bool Game::canPlaceHere(int seat, int slot, Troop kind, bool hookWaiver) const {
  return placeableWith(seat, slot, kind, hookWaiver, reachable(seat));
}

// The same test with the reachable set hoisted out. Move generation asks this
// 8 kinds x 17 slots times per position, and recomputing connectivity inside
// that loop is a two-orders-of-magnitude mistake in the brain's inner loop.
bool Game::placeableWith(int seat, int slot, Troop kind, bool hookWaiver, uint64_t reach) const {
  const Terrain& b = board();
  if (slot < 0 || slot >= b.slotCount()) return false;
  if (rack[seat][static_cast<int>(kind)] == 0) return false;

  const bool connected = (reach & (uint64_t{1} << slot)) != 0;

  if (b.isHq(slot)) {
    // Never your own, and Hook's waiver does not reach here: the aid is
    // explicit that the H.Q. is not a base.
    if (b.hqOwner(slot) == seat) return false;
    return connected;
  }

  const int holder = occupantSeat(slot);
  if (holder != kNoSeat && holder != seat && !covers(kind, occupantTroop(slot))) return false;

  if (connected) return true;
  return hookWaiver && kind == Troop::Hook;
}

bool Game::canDraw(int seat) const { return rackSize(seat) < kRackLimit && reserveRemaining(seat) > 0; }

int Game::legalPlacements(int seat, Step* out, int max) const {
  const Terrain& b = board();
  const uint64_t reach = reachable(seat);
  int n = 0;
  for (int k = 0; k < kTroopKinds && n < max; ++k) {
    if (rack[seat][k] == 0) continue;
    const Troop kind = static_cast<Troop>(k);
    for (int slot = 0; slot < b.slotCount() && n < max; ++slot) {
      if (!placeableWith(seat, slot, kind, /*hookWaiver=*/true, reach)) continue;
      Step s;
      s.slot = static_cast<uint8_t>(slot);
      s.kind = static_cast<uint8_t>(k);
      // Minimally committed: the effect is declined except where the
      // placement itself only stands up because of Hook's waiver. Choosing
      // effects, and Jumbo's target, is the caller's business.
      s.useEffect = !placeableWith(seat, slot, kind, /*hookWaiver=*/false, reach);
      out[n++] = s;
    }
  }
  return n;
}

bool Game::hasAnyLegalMove(int seat) const {
  if (canDraw(seat)) return true;
  Step scratch[1];
  return legalPlacements(seat, scratch, 1) > 0;
}

// ---------------------------------------------------------------------------
// Applying a move
// ---------------------------------------------------------------------------

namespace {

void drawTroops(Game& g, int seat, int wanted) {
  const int space = kRackLimit - g.rackSize(seat);
  int n = wanted < space ? wanted : space;
  if (n > g.reserveRemaining(seat)) n = g.reserveRemaining(seat);
  for (int i = 0; i < n; ++i) {
    const Troop drawn = g.reserveAt(seat, kSetAside + g.reserveTaken[seat]);
    ++g.reserveTaken[seat];
    ++g.rack[seat][static_cast<int>(drawn)];
  }
}

// Pops the visible troop off a base. Every removal in this game is of a
// visible troop, so this is the only shape a removal ever takes.
bool popVisible(Game& g, int base, int* outSeat, Troop* outKind) {
  for (int i = g.placementCount - 1; i >= 0; --i) {
    if (g.placeSlot[i] != base) continue;
    *outSeat = tileSeat(g.placeTile[i]);
    *outKind = tileKind(g.placeTile[i]);
    const int tail = g.placementCount - i - 1;
    if (tail > 0) {
      memmove(&g.placeSlot[i], &g.placeSlot[i + 1], static_cast<size_t>(tail));
      memmove(&g.placeTile[i], &g.placeTile[i + 1], static_cast<size_t>(tail));
    }
    --g.placementCount;
    return true;
  }
  return false;
}

void endGame(Game& g, int winnerSeat, Ending why) {
  g.phase = static_cast<uint8_t>(Phase::GameOver);
  g.winner = static_cast<uint8_t>(winnerSeat);
  g.ending = static_cast<uint8_t>(why);
}

// Regions fall the instant their last base is held, for either seat, after any
// change to the board -- including one that merely uncovers a buried tile.
void settleRegions(Game& g) {
  const Terrain& b = g.board();
  for (int seat = 0; seat < kSeats; ++seat) {
    const uint32_t mine = g.occupiedBy(seat);
    for (int r = 0; r < b.regionCount; ++r) {
      const uint16_t bit = static_cast<uint16_t>(1u << r);
      if (g.regionsTaken & bit) continue;
      if ((mine & b.regions[r].bases) != b.regions[r].bases) continue;
      g.regionsTaken = static_cast<uint16_t>(g.regionsTaken | bit);
      g.medals[seat] = static_cast<uint8_t>(g.medals[seat] + b.regions[r].medals);
      if (g.medals[seat] >= b.medalsObjective && g.currentPhase() == Phase::Playing) {
        endGame(g, seat, Ending::MedalsObjective);
      }
    }
  }
}

bool applyStep(Game& g, int seat, const Step& s, bool chainContinues) {
  const Terrain& b = g.board();
  const Troop kind = static_cast<Troop>(s.kind);
  if (s.kind >= kTroopKinds) return false;

  // A Cap'n's effect *is* the extra placement, so the flag and the presence of
  // a following step have to agree. Anything else is a move that means two
  // things at once.
  if (chainContinues && !(kind == Troop::Capn && s.useEffect)) return false;
  if (!chainContinues && kind == Troop::Capn && s.useEffect) return false;
  if (s.useEffect && !hasEffect(kind)) return false;

  const bool waiver = s.useEffect && kind == Troop::Hook;
  if (!g.canPlaceHere(seat, s.slot, kind, waiver)) return false;

  --g.rack[seat][s.kind];
  g.placeSlot[g.placementCount] = s.slot;
  g.placeTile[g.placementCount] = packTile(seat, kind);
  ++g.placementCount;

  if (b.isHq(s.slot)) {
    endGame(g, seat, Ending::HqCaptured);
    return true;
  }

  settleRegions(g);
  if (g.currentPhase() != Phase::Playing) return true;

  if (!s.useEffect) return true;

  switch (kind) {
    case Troop::Skully:
      drawTroops(g, seat, 2);
      break;
    case Troop::Star:
      drawTroops(g, seat, 1);
      break;
    case Troop::Hook:
      break;  // the waiver was the effect, and it was spent on the placement
    case Troop::Capn:
      break;  // the extra placement is the next step
    case Troop::Jumbo: {
      // One section of path, and only a visible enemy troop.
      if (s.target == kNoSlot) return false;
      if (s.target >= b.baseCount) return false;
      if (!(b.adj[s.slot] & (uint64_t{1} << s.target))) return false;
      if (g.occupantSeat(s.target) != other(seat)) return false;
      int victimSeat = 0;
      Troop victimKind = Troop::Kwak;
      if (!popVisible(g, s.target, &victimSeat, &victimKind)) return false;
      ++g.discarded[victimSeat][static_cast<int>(victimKind)];
      settleRegions(g);
      break;
    }
    case Troop::XB42: {
      const int foe = other(seat);
      const int held = g.rackSize(foe);
      if (held > 0) {
        int pick = static_cast<int>(mix32(g.seed ^ (uint32_t{0xB5} << 8) ^ g.rngTick) % static_cast<uint32_t>(held));
        ++g.rngTick;
        for (int k = 0; k < kTroopKinds; ++k) {
          if (pick < g.rack[foe][k]) {
            --g.rack[foe][k];
            ++g.discarded[foe][k];
            break;
          }
          pick -= g.rack[foe][k];
        }
      }
      break;
    }
    case Troop::Kwak:
    case Troop::Roxy:
      break;  // rejected above; no effect to apply
  }
  return true;
}

bool tryApply(Game& g, const Move& move) {
  if (g.currentPhase() != Phase::Playing) return false;
  const int seat = g.turn;

  if (move.kind == Move::Kind::Draw) {
    if (move.stepCount != 0) return false;
    if (!g.canDraw(seat)) return false;
    drawTroops(g, seat, 2);
  } else {
    if (move.stepCount < 1 || move.stepCount > kMaxChain) return false;
    for (int i = 0; i < move.stepCount; ++i) {
      const bool chainContinues = i + 1 < move.stepCount;
      if (!applyStep(g, seat, move.steps[i], chainContinues)) return false;
      // A win mid-chain ends the game there: the rest of the Cap'n's chain
      // never happens.
      if (g.currentPhase() != Phase::Playing) return true;
    }
  }

  g.turn = static_cast<uint8_t>(other(seat));
  if (!g.hasAnyLegalMove(g.turn)) {
    const int stuck = g.turn;
    const int foe = other(stuck);
    // Most medals, and a tie goes against whoever could not act.
    const int winnerSeat = g.medals[stuck] > g.medals[foe] ? stuck : foe;
    endGame(g, winnerSeat, Ending::Stuck);
  }
  return true;
}

}  // namespace

bool Game::isLegal(const Move& move) const {
  Game trial = *this;
  return tryApply(trial, move);
}

bool Game::apply(const Move& move) {
  Game trial = *this;
  if (!tryApply(trial, move)) return false;
  *this = trial;
  return true;
}

}  // namespace toybattle

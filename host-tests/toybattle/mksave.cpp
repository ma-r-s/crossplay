// A save parked one tap from the Cursed Cemetery question, so the fix can be
// LOOKED at. isWellFormed checks per-kind conservation against the SEED's own
// shuffle, so the position is dealt from that shuffle rather than invented.
#include <cstdio>

#include "ToyBattleCore.h"
#include "ToyBattleFlow.h"
using namespace toybattle;
int main(int, char** argv) {
  for (uint32_t seed = 1; seed < 200000; ++seed) {
    Game g;
    g.newGame(seed, static_cast<int>(TerrainId::CursedCemetery), 0, true);
    const Terrain& b = g.board();

    int hq = -1;
    for (int s = b.baseCount; s < b.slotCount(); ++s)
      if (b.hqSeat[s - b.baseCount] == 0) hq = s;
    int parent[kMaxSlots];
    for (int i = 0; i < kMaxSlots; ++i) parent[i] = -2;
    int q[kMaxSlots], head = 0, tail = 0;
    q[tail++] = hq;
    parent[hq] = -1;
    int grave = -1;
    while (head < tail && grave < 0) {
      const int at = q[head++];
      for (int n = 0; n < b.baseCount; ++n) {
        if (parent[n] != -2 || !(b.adj[at] & (uint64_t{1} << n))) continue;
        parent[n] = at;
        q[tail++] = n;
        if (b.specialAt(n) == Special::Exhume) {
          grave = n;
          break;
        }
      }
    }
    int path[kMaxSlots], len = 0;
    for (int at = parent[grave]; at >= 0 && at < b.baseCount; at = parent[at]) path[len++] = at;

    // Deal exactly what the shuffle gives: path bases, then two to the discard,
    // then one to hold. The one held must be Roxy, the only troop with no
    // effect of its own, or the flow asks about the troop before the base.
    const int take = len + 3;
    if (take > kReserveSize) continue;
    Troop drawn[kMaxSlots];
    for (int i = 0; i < take; ++i) drawn[i] = g.reserveAt(0, kSetAside + i);
    if (drawn[take - 1] != Troop::Roxy) continue;

    for (int i = 0; i < len; ++i) {
      g.placeSlot[g.placementCount] = static_cast<uint8_t>(path[i]);
      g.placeTile[g.placementCount] = static_cast<uint8_t>((0 << 3) | static_cast<int>(drawn[i]));
      ++g.placementCount;
    }
    for (int k = 0; k < kTroopKinds; ++k) {
      g.rack[0][k] = 0;
      g.discarded[0][k] = 0;
    }
    ++g.discarded[0][static_cast<int>(drawn[len])];
    ++g.discarded[0][static_cast<int>(drawn[len + 1])];
    ++g.rack[0][static_cast<int>(drawn[take - 1])];
    g.reserveTaken[0] = static_cast<uint8_t>(take);
    g.turn = 0;
    if (!g.isWellFormed()) continue;

    // And it must actually open the question.
    Draft d{};
    if (!answerTroop(g, d, Troop::Roxy)) continue;
    if (!answerSlot(g, d, grave)) continue;
    if (pending(g, d) != Ask::ExhumeKind) continue;

    Saved saved;
    saved.seat = 0;
    saved.options.terrain = static_cast<uint8_t>(TerrainId::CursedCemetery);
    saved.options.specialBases = true;
    saved.game = g;
    uint8_t bytes[kSaveBytes];
    const int n = encodeSave(saved, bytes);
    FILE* f = fopen(argv[1], "wb");
    fwrite(bytes, 1, static_cast<size_t>(n), f);
    fclose(f);
    printf("seed %u, grave slot %d, path %d long, well-formed=1\n", seed, grave, len);
    printf("discard holds %d kinds\n",
           (g.discarded[0][static_cast<int>(drawn[len])] > 0) + (drawn[len] != drawn[len + 1]));
    printf("TAPS: rack Roxy then grave at %d,%d\n", 46 + b.x[grave] * 388 / 1000, 142 + b.y[grave] * 458 / 1000);
    return 0;
  }
  printf("no seed found\n");
  return 1;
}

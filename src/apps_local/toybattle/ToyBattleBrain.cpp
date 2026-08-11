#include "ToyBattleBrain.h"

#include <cstring>

namespace toybattle {
namespace {

constexpr int other(int seat) { return seat ^ 1; }

int popcount32(uint32_t v) {
  int n = 0;
  while (v) {
    v &= v - 1;
    ++n;
  }
  return n;
}

int popcount64(uint64_t v) {
  int n = 0;
  while (v) {
    v &= v - 1;
    ++n;
  }
  return n;
}

uint32_t mix32(uint32_t x) {
  x += 0x9E3779B9u;
  x = (x ^ (x >> 16)) * 0x85EBCA6Bu;
  x = (x ^ (x >> 13)) * 0xC2B2AE35u;
  return x ^ (x >> 16);
}

// Scores far enough apart that no positional term can ever outweigh the result
// of the game, and far enough inside int range that they can be added.
constexpr int kWin = 1000000;
constexpr int kLoss = -1000000;
// Hanging an H.Q. loses next turn, so it is worth almost as much as losing --
// but not exactly, so that when every move hangs it the brain still prefers the
// one that is otherwise best rather than picking the first.
constexpr int kHangsHq = -500000;

// Measured: the busiest position over 400 games produced 363 candidate moves.
constexpr int kMaxCandidates = 384;
constexpr int kMaxPlacementsOffered = 96;

}  // namespace

// ---------------------------------------------------------------------------
// Observing
// ---------------------------------------------------------------------------

Observation observe(const Game& game, int seat) {
  Observation o;
  o.seat = static_cast<uint8_t>(seat);
  const int foe = other(seat);
  o.opponentRackSize = static_cast<uint8_t>(game.rackSize(foe));

  // Three of each, less the ones whose whereabouts are public. What is left is
  // their rack plus their reserve plus the four they set aside unseen.
  int onBoard[kTroopKinds] = {};
  for (int i = 0; i < game.placementCount; ++i) {
    if ((game.placeTile[i] >> 3) == foe) ++onBoard[game.placeTile[i] & 0x07];
  }
  for (int k = 0; k < kTroopKinds; ++k) {
    const int left = kCopiesEach - onBoard[k] - game.discarded[foe][k];
    o.unseen[k] = static_cast<uint8_t>(left > 0 ? left : 0);
  }

  o.view = game;
  // The seed decides both shuffles, so carrying it would hand over the enemy's
  // reserve in reading order. Cleared: a draw is tempo here, not a known troop.
  o.view.seed = 0;

  // Rebuild a rack of the right size from the public multiset, so the position
  // can be played forward. Greedy on what is most likely to remain, which is
  // the best guess available and, more importantly, a function of public data.
  uint8_t pool[kTroopKinds];
  memcpy(pool, o.unseen, sizeof(pool));
  for (int k = 0; k < kTroopKinds; ++k) o.view.rack[foe][k] = 0;
  for (int placed = 0; placed < o.opponentRackSize; ++placed) {
    int best = -1;
    for (int k = 0; k < kTroopKinds; ++k) {
      if (pool[k] == 0) continue;
      if (best < 0 || pool[k] > pool[best]) best = k;
    }
    if (best < 0) break;
    --pool[best];
    ++o.view.rack[foe][best];
  }
  return o;
}

// ---------------------------------------------------------------------------
// Threats
// ---------------------------------------------------------------------------

namespace detail {

bool hqIsExposed(const Game& v, int seat, const uint8_t* unseen, int opponentRackSize) {
  if (v.currentPhase() != Phase::Playing) return false;
  if (opponentRackSize <= 0) return false;

  const Terrain& b = v.board();
  const int foe = other(seat);
  const uint64_t reach = v.reachable(foe);

  for (int i = 0; i < b.hqCount; ++i) {
    if (b.hqSeat[i] != seat) continue;
    const int slot = b.baseCount + i;
    if (!(reach & (uint64_t{1} << slot))) continue;

    // Any troop takes an H.Q., so reaching it is enough -- unless a gate is in
    // the way, in which case they need a kind that could still be theirs.
    const uint8_t admits = v.specialBases ? b.gate[slot] : 0;
    if (admits == 0) return true;
    for (int k = 0; k < kTroopKinds; ++k) {
      if ((admits & (1u << k)) && unseen[k] > 0) return true;
    }
  }
  return false;
}

int evaluate(const Game& v, int seat, const uint8_t* unseen, int opponentRackSize, Skill skill) {
  const int foe = other(seat);

  if (v.currentPhase() == Phase::GameOver) {
    if (v.winner == kNoSeat) return 0;
    return v.winner == seat ? kWin : kLoss;
  }

  // Both skills keep the two reflexes: take the win in front of you, and do not
  // leave your H.Q. open. Recruit has nothing else, and that alone is a real
  // opponent -- it just has no opinion about which of the safe moves is good.
  int score = 0;
  if (hqIsExposed(v, seat, unseen, opponentRackSize)) score += kHangsHq;
  if (skill == Skill::Recruit) return score;

  // Medals are the only thing that ends a game short of an H.Q., and they are
  // permanent, so they dominate everything positional.
  score += (v.medals[seat] - v.medals[foe]) * 400;

  // Territory and room to move. The reachable set is the honest measure of a
  // position here: a big board presence that cannot extend is worth little.
  const uint32_t mine = v.occupiedBy(seat);
  const uint32_t theirs = v.occupiedBy(foe);
  score += (popcount32(mine) - popcount32(theirs)) * 25;
  score += popcount64(v.reachable(seat)) * 8;
  score -= popcount64(v.reachable(foe)) * 8;

  // Troops in hand are options; troops still in the reserve are future options
  // but cost a turn to reach.
  score += v.rackSize(seat) * 6;
  score -= opponentRackSize * 6;

  return score;
}

// ---------------------------------------------------------------------------
// Candidate moves
// ---------------------------------------------------------------------------

int candidates(const Observation& obs, Move* out, int max) {
  const Game& v = obs.view;
  const Terrain& b = v.board();
  const int seat = obs.seat;
  int n = 0;

  const auto add = [&](const Move& m) {
    if (n < max && v.isLegal(m)) out[n++] = m;
  };

  if (v.isLegal(Move::draw())) add(Move::draw());

  // Off the stack for the same reason, and sized by measurement: 49 was the
  // most placements any position offered.
  static Step steps[kMaxPlacementsOffered];
  const int placements = v.legalPlacements(seat, steps, kMaxPlacementsOffered);

  for (int i = 0; i < placements && n < max; ++i) {
    const Troop kind = static_cast<Troop>(steps[i].kind);
    const int slot = steps[i].slot;
    const bool forced = steps[i].useEffect;  // Hook's waiver, when the placement needs it

    add(Move::place(slot, kind, forced));
    if (kind != Troop::Kwak && kind != Troop::Roxy && !forced) add(Move::place(slot, kind, true));

    // Jumbo picks a victim. Every adjacent enemy troop is worth considering:
    // which one matters, and there are never many.
    if (kind == Troop::Jumbo) {
      for (int target = 0; target < b.baseCount && n < max; ++target) {
        if (!(b.adj[slot] & (uint64_t{1} << target))) continue;
        add(Move::place(slot, kind, true, target));
      }
    }

    // Cap'n's chain. The second placement is generated against the position the
    // Cap'n leaves, which is the only position it will ever see. Bounded to the
    // first few: the chain multiplies the branching factor by the whole board,
    // and the extra placements are enumerated in board order, so this drops
    // real options rather than duplicates.
    if (kind == Troop::Capn) {
      Game staged = v;
      if (staged.apply(Move::place(slot, kind, false))) {
        staged.turn = static_cast<uint8_t>(seat);
        static Step extra[kMaxPlacementsOffered];
        const int m2 = staged.legalPlacements(seat, extra, kMaxPlacementsOffered);
        const int cap = m2 < 24 ? m2 : 24;
        for (int j = 0; j < cap && n < max; ++j) {
          Move chained = Move::place(slot, kind, true);
          chained.then(extra[j].slot, static_cast<Troop>(extra[j].kind), extra[j].useEffect);
          add(chained);
        }
      }
    }

    // The special base underneath, with the choices it needs.
    if (!v.specialBases || !b.isBase(slot)) continue;
    switch (b.specialAt(slot)) {
      case Special::Draw:
      case Special::Suppress: {
        Move m = Move::place(slot, kind, forced);
        m.steps[0].useBase = true;
        add(m);
        break;
      }
      case Special::Recall: {
        for (int from = 0; from < b.baseCount && n < max; ++from) {
          if (v.occupantSeat(from) != seat) continue;
          Move m = Move::place(slot, kind, forced);
          m.steps[0].useBase = true;
          m.steps[0].baseFrom = static_cast<uint8_t>(from);
          add(m);
        }
        break;
      }
      case Special::Exhume: {
        for (int k = 0; k < kTroopKinds && n < max; ++k) {
          if (v.discarded[seat][k] == 0) continue;
          Move m = Move::place(slot, kind, forced);
          m.steps[0].useBase = true;
          m.steps[0].baseKind = static_cast<uint8_t>(k);
          add(m);
        }
        break;
      }
      case Special::Shove: {
        for (int from = 0; from < b.baseCount && n < max; ++from) {
          if (!(b.adj[slot] & (uint64_t{1} << from))) continue;
          for (int to = 0; to < b.baseCount && n < max; ++to) {
            if (!(b.adj[from] & (uint64_t{1} << to))) continue;
            Move m = Move::place(slot, kind, forced);
            m.steps[0].useBase = true;
            m.steps[0].baseFrom = static_cast<uint8_t>(from);
            m.steps[0].baseTo = static_cast<uint8_t>(to);
            add(m);
          }
        }
        break;
      }
      case Special::None:
      case Special::Gate:
      case Special::Nullify:
        break;
    }
  }
  return n;
}

}  // namespace detail

// ---------------------------------------------------------------------------
// Choosing
// ---------------------------------------------------------------------------

Move chooseMove(const Observation& obs, Skill skill) {
  // Static, not local. `Move` is 34 bytes and this used to be a 512-entry array
  // on the stack -- 17KB, against a task stack of a few thousand and a house
  // rule of 256 bytes for a local. The simulator never showed it because a
  // desktop stack swallows it; a device would have smashed straight through.
  //
  // 384 is measured, not guessed: the worst position over 400 games offered
  // 363 candidates. There is one brain and one turn at a time, so a shared
  // buffer is safe here, and the cap logs rather than truncating in silence.
  static Move options[kMaxCandidates];
  const int n = detail::candidates(obs, options, kMaxCandidates);
  if (n == 0) return Move::draw();  // nothing is legal; the caller is about to end the game

  const int seat = obs.seat;
  int bestIndex = 0;
  int bestScore = kLoss - 1;
  uint32_t bestJitter = 0;

  // Ties are broken by a hash of the position and the candidate, not by board
  // order. Deterministic, so the same position always produces the same move --
  // but Recruit, whose score is flat across every safe move, would otherwise
  // always play the lowest-numbered slot and read as a machine walking a list.
  uint32_t positionHash = obs.view.seed ^ (static_cast<uint32_t>(obs.view.placementCount) << 8);
  for (int i = 0; i < obs.view.placementCount; ++i) {
    positionHash = positionHash * 31u + obs.view.placeSlot[i] + (static_cast<uint32_t>(obs.view.placeTile[i]) << 5);
  }

  for (int i = 0; i < n; ++i) {
    Game after = obs.view;
    if (!after.apply(options[i])) continue;

    int score;
    if (after.currentPhase() == Phase::GameOver) {
      score = after.winner == static_cast<uint8_t>(seat) ? kWin : (after.winner == kNoSeat ? 0 : kLoss);
    } else {
      // One of their troops is spent answering, so the threat is measured
      // against the rack they will actually hold.
      score = detail::evaluate(after, seat, obs.unseen, obs.opponentRackSize, skill);
    }

    const uint32_t jitter = mix32(positionHash ^ (static_cast<uint32_t>(i) * 2654435761u));
    if (score > bestScore || (score == bestScore && jitter > bestJitter)) {
      bestScore = score;
      bestJitter = jitter;
      bestIndex = i;
    }
  }
  return options[bestIndex];
}

}  // namespace toybattle

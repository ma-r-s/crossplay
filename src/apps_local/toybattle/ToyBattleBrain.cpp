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
// A win now and a win in three plies must not score the same, or the tie-break
// picks between them by hash and the brain walks past a capture it could take
// this turn. Every terminal score is discounted by how deep it was found, which
// also makes it prefer losing later over losing now.
constexpr int kPlyPenalty = 4;

// Empirical, and re-measured 2026-08-11 by host-tests/toybattle/branching.sh
// across all ten boards with special bases on and off. 80 games a condition put
// the worst real board (Battlefield, bases on) at 337 and PROVING GROUND -- which
// carries all seven special kinds at once and is therefore an artificial worst
// case -- at 417, over the 384 this used to be. It had been sized from 400 games
// on two boards, and the tail kept growing with the sample, which is the tell
// that the number was never a bound.
//
// 512 clears the widest observed position by 23% and the widest REAL one by
// 52%, at 4.4KB more static. It is still not a proof, so `cost.ceilingHits`
// counts every time the buffer fills and branching.sh fails on any.
constexpr int kMaxCandidates = 512;
constexpr int kMaxPlacementsOffered = 96;
// The beam is tiny by design: move ordering means the answer is almost always
// inside the first few, and every extra one costs a whole board of replies.
constexpr int kMaxBeam = 24;

// A finished game, scored from `seat` and discounted by the ply it was found
// at. `ply` is 1 for the move being considered, 2 for the reply to it, 3 for
// the answer to that.
int terminalScore(const Game& g, int seat, int ply) {
  if (g.winner == kNoSeat) return 0;
  return g.winner == static_cast<uint8_t>(seat) ? kWin - ply * kPlyPenalty : kLoss + ply * kPlyPenalty;
}

}  // namespace

// The policy the search assumes the opponent is playing. Greedy on purpose: a
// model with a beam of its own would square the cost, for a reply that is only
// ever a guess about a rack this brain is not allowed to see.
static const Policy kModel = Policy{};

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

// Fitted by tools_local/toybattle/fit_eval.py over 28,823 self-play positions, logistic
// regression on win/loss, scaled so a medal stays at 400. Only the RATIOS are
// what the data decided.
//
// The features are DISTANCES TO WINNING rather than a description of the board,
// which is Mario's correction and the whole point. The first fit had a flat
// "their H.Q. is reachable" term worth three and a half medals, so the brain
// charged the H.Q. whatever else was on offer -- it won 84% but took 73% of
// those wins by decapitation, while BEHIND on medals. A constant cannot say
// "it depends".
//
// Splitting that one term in two is what fixed it:
//
//   hq_takeable_now  2161   a capture is available THIS turn
//   hq_reach          477   merely connected to it
//
// Five medals against one. So an actual chance gets taken and a distant one
// does not derail the medal race, which is the behaviour asked for.
constexpr int kFitMedalsToGo = 400;       // how close I am to the objective
constexpr int kFitTheirMedalsToGo = 378;  // and how far they still are
constexpr int kFitRegionsReady = 164;     // regions one placement from mine
constexpr int kFitTheirRegionsReady = 109;
constexpr int kFitHqTakeable = 2161;
constexpr int kFitMyHqTakeable = 536;
constexpr int kFitHqReach = 477;
constexpr int kFitBases = 300;
constexpr int kFitReach = 32;
constexpr int kFitRack = 347;

// Can `seat` capture an enemy H.Q. right now? Exact and cheap, the same shape
// as hqIsExposed: any troop takes an H.Q., so being connected is enough unless
// a gate stands in the way, and then it must be a kind actually in hand. Hook's
// waiver does not help here -- the aid is explicit that an H.Q. is not a base.
//
// Deliberately NOT legalPlacements(), which is what the offline feature used:
// this runs at every leaf of the search, and generating every move to answer
// one question would have cost more than the whole evaluation.
bool hqIsTakeable(const Game& v, int seat) {
  if (v.currentPhase() != Phase::Playing) return false;
  const Terrain& b = v.board();
  const uint64_t reach = v.reachable(seat);
  for (int i = 0; i < b.hqCount; ++i) {
    if (b.hqSeat[i] == seat) continue;
    const int slot = b.baseCount + i;
    if (!(reach & (uint64_t{1} << slot))) continue;
    const uint8_t admits = v.specialBases ? b.gate[slot] : 0;
    if (admits == 0) return v.rackSize(seat) > 0;
    for (int k = 0; k < kTroopKinds; ++k) {
      if ((admits & (1u << k)) && v.rack[seat][k] > 0) return true;
    }
  }
  return false;
}

int fittedScore(const Game& v, int seat, int opponentRackSize) {
  const Terrain& b = v.board();
  const int foe = other(seat);
  const uint32_t mine = v.occupiedBy(seat), theirs = v.occupiedBy(foe);
  const uint64_t myReach = v.reachable(seat), theirReach = v.reachable(foe);

  int score = -(b.medalsObjective - v.medals[seat]) * kFitMedalsToGo;
  score += (b.medalsObjective - v.medals[foe]) * kFitTheirMedalsToGo;

  int ready = 0, theirReady = 0;
  for (int r = 0; r < b.regionCount; ++r) {
    if (v.regionsTaken & (1u << r)) continue;
    const uint32_t need = b.regions[r].bases;
    if (popcount32(need & ~mine) == 1) ready += b.regions[r].medals;
    if (popcount32(need & ~theirs) == 1) theirReady += b.regions[r].medals;
  }
  score += ready * kFitRegionsReady;
  score -= theirReady * kFitTheirRegionsReady;

  if (hqIsTakeable(v, seat)) {
    score += kFitHqTakeable;
  } else {
    for (int i = 0; i < b.hqCount; ++i) {
      if (b.hqSeat[i] == seat) continue;
      if (myReach & (uint64_t{1} << (b.baseCount + i))) {
        score += kFitHqReach;
        break;
      }
    }
  }
  for (int i = 0; i < b.hqCount; ++i) {
    if (b.hqSeat[i] != seat) continue;
    if (theirReach & (uint64_t{1} << (b.baseCount + i))) {
      score -= kFitMyHqTakeable;
      break;
    }
  }

  score += (popcount32(mine) - popcount32(theirs)) * kFitBases;
  score += (popcount64(myReach) - popcount64(theirReach)) * kFitReach;
  score += (v.rackSize(seat) - opponentRackSize) * kFitRack;
  return score;
}

int evaluate(const Game& v, int seat, const uint8_t* unseen, int opponentRackSize, const Policy& policy) {
  const int foe = other(seat);

  if (v.currentPhase() == Phase::GameOver) {
    if (v.winner == kNoSeat) return 0;
    return v.winner == seat ? kWin : kLoss;
  }

  // Every policy keeps the two reflexes: take the win in front of you, and do
  // not leave your H.Q. open. With nothing else that is still a real opponent
  // -- it just has no opinion about which of the safe moves is good.
  int score = 0;
  if (hqIsExposed(v, seat, unseen, opponentRackSize)) score += kHangsHq;
  if (!policy.material) return score;

  if (policy.fitted) return score + fittedScore(v, seat, opponentRackSize);

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
  // Every early exit below tests `n < max`, so filling the buffer is the one
  // condition under which a real move can have been dropped. Counted rather
  // than asserted: the brain must keep playing on a device, just not silently.
  struct Ceiling {
    int& n;
    const int max;
    ~Ceiling() {
      if (n >= max) ++cost.ceilingHits;
    }
  } ceiling{n, max};

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

namespace detail {
Cost cost;
void resetCost() { cost = Cost{}; }

const int kMaxCandidatesShipped = kMaxCandidates;
const int kMaxPlacementsOfferedShipped = kMaxPlacementsOffered;
}  // namespace detail

// The three rungs, shifted up one on 2026-08-11 after Mario played the fitted
// brain and called the difficulty right. Every rung is now what the rung above
// it used to be:
//
//   GENERAL   the fitted evaluation      (new)
//   SERGEANT  beam 8 at depth 3          (was GENERAL)
//   RECRUIT   an evaluation, played greedily  (was SERGEANT)
//
// The old RECRUIT is gone. It had the two reflexes and no evaluation at all,
// and the tournament put it at 0.8% against the field -- it lost about
// ninety-nine games in a hundred to everything, including the greedy brain.
// A rung nobody can lose to is not a difficulty, it is a placeholder.
Policy policyFor(Skill skill) {
  Policy p;
  // An evaluation, played greedily: it scores the position its own move leaves
  // and never asks what happens next.
  if (skill == Skill::Recruit) return p;

  // Beam 8 at depth 3, which is where added width stopped paying: beam 12 and
  // beam 24 are level with it head to head and cost two and four times as much.
  // This is the brain Mario beat three games running, which is exactly what a
  // middle rung should feel like.
  Policy searching;
  searching.beam = 8;
  searching.depth = 3;
  if (skill == Skill::Sergeant) return searching;

  // Same search, weights FITTED to self-play outcomes rather than chosen by
  // eye. Beats the rung below it 84% of 600 games, on every board, for 0.288ms
  // a move. The rungs below keep the hand-tuned evaluation on purpose: rebuilt
  // on these numbers they would be one difficulty at three depths.
  searching.fitted = true;
  return searching;
}

Move chooseMove(const Observation& obs, Skill skill) { return chooseMove(obs, policyFor(skill)); }

Move chooseMove(const Observation& obs, const Policy& policy) {
  // Static, not local. `Move` is 34 bytes and this used to be a 512-entry array
  // on the stack -- 17KB, against a task stack of a few thousand and a house
  // rule of 256 bytes for a local. The simulator never showed it because a
  // desktop stack swallows it; a device would have smashed straight through.
  //
  // 384 is measured, not guessed: the worst position over 400 games offered
  // 363 candidates. There is one brain and one turn at a time, so a shared
  // buffer is safe here, and the cap logs rather than truncating in silence.
  // Every position the search holds is static for the same reason `options` is.
  // A Game is 148 bytes and an Observation 160, and the reply search wants six
  // of them live at once -- about 900 bytes of locals against this project's
  // 256-byte rule, on a device whose task stacks are a few thousand. There is
  // one brain and one turn at a time, so sharing them is safe; the last version
  // of this file put 17KB on the stack and the simulator never noticed.
  static Game after, worstReply, reply, counter;
  static Observation foeObs, back;
  static Move options[kMaxCandidates];
  const uint32_t startPositions = detail::cost.positions;
  const int n = detail::candidates(obs, options, kMaxCandidates);
  if (n == 0) return Move::draw();  // nothing is legal; the caller is about to end the game

  const int seat = obs.seat;
  int bestIndex = 0;
  Move chosen = options[0];
  int bestScore = kLoss - 1;
  uint32_t bestJitter = 0;
  static int scores[kMaxCandidates];

  // Ties are broken by a hash of the position and the candidate, not by board
  // order. Deterministic, so the same position always produces the same move --
  // but Recruit, whose score is flat across every safe move, would otherwise
  // always play the lowest-numbered slot and read as a machine walking a list.
  uint32_t positionHash = obs.view.seed ^ (static_cast<uint32_t>(obs.view.placementCount) << 8);
  for (int i = 0; i < obs.view.placementCount; ++i) {
    positionHash = positionHash * 31u + obs.view.placeSlot[i] + (static_cast<uint32_t>(obs.view.placeTile[i]) << 5);
  }

  for (int i = 0; i < n; ++i) {
    after = obs.view;
    if (!after.apply(options[i])) {
      scores[i] = kLoss - 1;
      continue;
    }
    ++detail::cost.positions;

    int score;
    if (after.currentPhase() == Phase::GameOver) {
      score = terminalScore(after, seat, 1);
    } else {
      // One of their troops is spent answering, so the threat is measured
      // against the rack they will actually hold.
      score = detail::evaluate(after, seat, obs.unseen, obs.opponentRackSize, policy);
    }
    scores[i] = score;

    const uint32_t jitter = mix32(positionHash ^ (static_cast<uint32_t>(i) * 2654435761u));
    if (score > bestScore || (score == bestScore && jitter > bestJitter)) {
      bestScore = score;
      bestJitter = jitter;
      bestIndex = i;
      chosen = options[i];
    }
  }

  if (policy.beam > 0 && n > 1) {
    // Look at the reply. Not for every move -- that is the whole board twice
    // over -- but for the handful that looked best without one, which is move
    // ordering doing the job a full width would do at thirty times the cost.
    //
    // The greedy score above is the ordering, so a move has to survive being
    // answered rather than merely look good before anyone answers.
    const int width = policy.beam < n ? policy.beam : n;
    static Move beam[kMaxBeam];
    static int beamOrder[kMaxBeam];
    int taken = 0;
    for (int slot = 0; slot < width; ++slot) {
      int pick = -1;
      for (int i = 0; i < n; ++i) {
        bool already = false;
        for (int j = 0; j < taken && !already; ++j) already = beamOrder[j] == i;
        if (already) continue;
        if (pick < 0 || scores[i] > scores[pick]) pick = i;
      }
      if (pick < 0) break;
      beamOrder[taken] = pick;
      beam[taken] = options[pick];
      ++taken;
    }

    bestScore = kLoss - 1;
    bestJitter = 0;
    for (int i = 0; i < taken; ++i) {
      after = obs.view;
      if (!after.apply(beam[i])) continue;

      int score;
      if (after.currentPhase() == Phase::GameOver) {
        score = terminalScore(after, seat, 1);
      } else {
        // Their turn, seen the way they would see it: `observe` from their
        // seat replaces this brain's own rack with the guess a player opposite
        // would have to make. The reply is therefore not an oracle reading our
        // hand -- it is the same kind of reasoning, pointed the other way.
        foeObs = observe(after, other(seat));
        const int m = detail::candidates(foeObs, options, kMaxCandidates);
        worstReply = after;
        int worstForThem = kLoss - 1;
        for (int j = 0; j < m; ++j) {
          reply = foeObs.view;
          if (!reply.apply(options[j])) continue;
          ++detail::cost.positions;
          const int theirs = reply.currentPhase() == Phase::GameOver
                                 ? terminalScore(reply, foeObs.seat, 1)
                                 : detail::evaluate(reply, foeObs.seat, foeObs.unseen, foeObs.opponentRackSize, kModel);
          if (theirs > worstForThem) {
            worstForThem = theirs;
            worstReply = reply;
          }
        }
        if (worstForThem == kLoss - 1) {
          // They have no reply at all, so the position stands as our move left it.
          score = detail::evaluate(after, seat, obs.unseen, obs.opponentRackSize, policy);
        } else if (worstReply.currentPhase() == Phase::GameOver) {
          score = terminalScore(worstReply, seat, 2);
        } else {
          back = observe(worstReply, seat);
          score = detail::evaluate(worstReply, seat, back.unseen, back.opponentRackSize, policy);
          if (policy.depth >= 3) {
            // My answer to their answer. This is the first depth at which
            // giving something up can pay, because it is the first one that can
            // see what it buys. Greedy here rather than another beam: widening
            // the last ply squares the cost for the ply that matters least.
            const int mine = detail::candidates(back, options, kMaxCandidates);
            int bestCounter = kLoss - 1;
            for (int j = 0; j < mine; ++j) {
              counter = back.view;
              if (!counter.apply(options[j])) continue;
              ++detail::cost.positions;
              const int v = counter.currentPhase() == Phase::GameOver
                                ? terminalScore(counter, seat, 3)
                                : detail::evaluate(counter, seat, back.unseen, back.opponentRackSize, policy);
              if (v > bestCounter) bestCounter = v;
            }
            if (bestCounter > kLoss - 1) score = bestCounter;
          }
        }
      }

      const uint32_t jitter = mix32(positionHash ^ (static_cast<uint32_t>(beamOrder[i]) * 2654435761u));
      if (score > bestScore || (score == bestScore && jitter > bestJitter)) {
        bestScore = score;
        bestJitter = jitter;
        chosen = beam[i];
      }
    }
  }

  const uint32_t spent = detail::cost.positions - startPositions;
  if (spent > detail::cost.worstPerMove) detail::cost.worstPerMove = spent;
  (void)bestIndex;
  return chosen;
}

}  // namespace toybattle

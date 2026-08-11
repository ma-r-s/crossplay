#include "ToyBattleFlow.h"

namespace toybattle {
namespace {

constexpr int other(int seat) { return seat ^ 1; }

Step& current(Draft& d) { return d.move.steps[d.step]; }
const Step& current(const Draft& d) { return d.move.steps[d.step]; }

// Which question a troop's effect asks, or Ask::Ready when it asks nothing.
// Hook is Ready because its effect is the connection waiver, spent on the
// placement itself: by the time the troop is down there is nothing to decide.
Ask effectAsk(Troop kind) {
  switch (kind) {
    case Troop::Skully:
    case Troop::Star:
      return Ask::DrawOffer;
    case Troop::XB42:
      return Ask::StealOffer;
    case Troop::Capn:
      return Ask::ChainOffer;
    case Troop::Jumbo:
      return Ask::JumboVictim;
    case Troop::Hook:
    case Troop::Kwak:
    case Troop::Roxy:
      return Ask::Ready;
  }
  return Ask::Ready;
}

// The position a draft has already brought about, so later questions are asked
// against the board as it will be rather than as it was. Each answered step is
// replayed with the decisions actually taken; a Cap'n's chain flag is dropped,
// because here the extra placement is the next step rather than a flag.
Game projected(const Game& game, const Draft& draft) {
  Game g = game;
  const int seat = game.turn;
  for (int i = 0; i <= draft.step && i < draft.move.stepCount; ++i) {
    const Step& s = draft.move.steps[i];
    const bool isCurrent = i == draft.step;
    if (isCurrent && !draft.slotChosen) break;

    const Troop kind = static_cast<Troop>(s.kind);
    Move one = Move::place(s.slot, kind, false);
    // Hook's waiver is part of the placement rather than an afterthought, so it
    // is carried whether or not an effect question has been answered.
    const bool settled = isCurrent ? draft.effectAnswered : true;
    if (s.useEffect && kind != Troop::Capn && (settled || kind == Troop::Hook)) {
      one.steps[0].useEffect = true;
      one.steps[0].target = s.target;
    }
    // Base effects are deliberately NOT replayed here. The engine defers every
    // one of them until after all the troop effects of the turn, so a Cap'n's
    // second placement is judged against a board where no base has fired yet.
    // Applying them early made the projection promise a placement that the
    // rules would then refuse, and stranded the chain.
    if (!g.apply(one)) return g;
    g.turn = static_cast<uint8_t>(seat);
  }
  return g;
}

// The answer functions without the completability gate. The public ones wrap
// these; `finishSomehow` uses them directly, because it is the thing that
// decides whether a finish exists.
bool takeTroop(const Game& game, Draft& draft, Troop kind);
bool takeSlot(const Game& game, Draft& draft, int slot);
bool takeOffer(const Game& game, Draft& draft, bool take);
bool takeTarget(const Game& game, Draft& draft, int slotOrKind);

// Finishes a draft the cheapest legal way: mandatory questions take the first
// candidate, optional ones are declined.
void finishSomehow(const Game& game, Draft& d) {
  for (int guard = 0; guard < 24; ++guard) {
    const Ask a = pending(game, d);
    if (a == Ask::Ready) return;

    if (a == Ask::Troop) {
      const uint8_t offer = candidateTroops(game, d);
      if (!offer) return;
      for (int k = 0; k < kTroopKinds; ++k) {
        if (!(offer & (1u << k))) continue;
        if (!takeTroop(game, d, static_cast<Troop>(k))) return;
        break;
      }
      continue;
    }
    if (a == Ask::Slot || a == Ask::ShoveTo) {
      const uint64_t mask = candidateSlots(game, d);
      if (!mask) return;
      for (int s = 0; s < kMaxSlots; ++s) {
        if (!(mask & (uint64_t{1} << s))) continue;
        if (a == Ask::Slot ? !takeSlot(game, d, s) : !takeTarget(game, d, s)) return;
        break;
      }
      continue;
    }
    // Everything else is optional, and declining is the cheapest finish.
    if (!takeOffer(game, d, false)) return;
  }
}

// The safety net the whole design hangs on: an answer is accepted only if the
// draft could still be finished into a move the rules take. That makes
// Ask::Ready legal by construction rather than by argument, so no sequence of
// taps can commit a move the engine would refuse.
bool completable(const Game& game, Draft probe) {
  finishSomehow(game, probe);
  return probe.move.stepCount > 0 && game.isLegal(probe.move);
}

bool takeTroop(const Game& game, Draft& draft, const Troop kind) {
  if (pending(game, draft) != Ask::Troop) return false;
  const Game g = projected(game, draft);
  if (g.rack[game.turn][static_cast<int>(kind)] == 0) return false;

  // A Cap'n chain advances to its next link before the troop lands on it.
  if (draft.move.stepCount > draft.step) {
    if (draft.step + 1 >= kMaxChain) return false;
    ++draft.step;
    draft.slotChosen = false;
    draft.effectAnswered = false;
    draft.baseAnswered = false;
  }
  draft.move.kind = Move::Kind::Place;
  if (draft.move.stepCount <= draft.step) draft.move.stepCount = static_cast<uint8_t>(draft.step + 1);
  current(draft) = Step{};
  current(draft).kind = static_cast<uint8_t>(kind);
  return true;
}

bool takeSlot(const Game& game, Draft& draft, const int slot) {
  if (pending(game, draft) != Ask::Slot) return false;
  if (!(candidateSlots(game, draft) & (uint64_t{1} << slot))) return false;
  const Game g = projected(game, draft);
  const Troop kind = static_cast<Troop>(current(draft).kind);
  current(draft).slot = static_cast<uint8_t>(slot);
  // Hook's waiver is not a separate choice: either the placement needed it or
  // it did not.
  const uint64_t reach = g.reachable(game.turn);
  current(draft).useEffect = kind == Troop::Hook && !g.placeableWith(game.turn, slot, kind, false, reach);
  draft.slotChosen = true;
  return true;
}

bool takeOffer(const Game& game, Draft& draft, const bool take) {
  switch (pending(game, draft)) {
    case Ask::DrawOffer:
    case Ask::StealOffer:
    case Ask::ChainOffer:
      current(draft).useEffect = take;
      draft.effectAnswered = true;
      return true;
    case Ask::JumboVictim:
      if (take) return false;  // taking it needs a target, not a yes
      current(draft).useEffect = false;
      draft.effectAnswered = true;
      return true;
    case Ask::BaseOffer:
      current(draft).useBase = take;
      draft.baseAnswered = true;
      return true;
    case Ask::RecallFrom:
    case Ask::ShoveFrom:
    case Ask::ShoveTo:
    case Ask::ExhumeKind:
      if (take) return false;
      current(draft).useBase = false;
      current(draft).baseFrom = kNoSlot;
      current(draft).baseTo = kNoSlot;
      current(draft).baseKind = kNoSlot;
      draft.baseAnswered = true;
      return true;
    default:
      return false;
  }
}

bool takeTarget(const Game& game, Draft& draft, const int slotOrKind) {
  const Ask a = pending(game, draft);
  switch (a) {
    case Ask::JumboVictim:
      if (!(candidateSlots(game, draft) & (uint64_t{1} << slotOrKind))) return false;
      current(draft).useEffect = true;
      current(draft).target = static_cast<uint8_t>(slotOrKind);
      draft.effectAnswered = true;
      return true;
    case Ask::RecallFrom:
    case Ask::ShoveFrom:
      if (!(candidateSlots(game, draft) & (uint64_t{1} << slotOrKind))) return false;
      current(draft).useBase = true;
      current(draft).baseFrom = static_cast<uint8_t>(slotOrKind);
      // A shove still owes a destination; a recall does not.
      if (a == Ask::RecallFrom) draft.baseAnswered = true;
      return true;
    case Ask::ShoveTo:
      if (!(candidateSlots(game, draft) & (uint64_t{1} << slotOrKind))) return false;
      current(draft).baseTo = static_cast<uint8_t>(slotOrKind);
      draft.baseAnswered = true;
      return true;
    case Ask::ExhumeKind:
      if (slotOrKind < 0 || slotOrKind >= kTroopKinds) return false;
      if (game.discarded[game.turn][slotOrKind] == 0) return false;
      current(draft).useBase = true;
      current(draft).baseKind = static_cast<uint8_t>(slotOrKind);
      draft.baseAnswered = true;
      return true;
    default:
      return false;
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Composing a move
// ---------------------------------------------------------------------------

Ask pending(const Game& game, const Draft& draft) {
  if (game.currentPhase() != Phase::Playing) return Ask::Ready;
  if (draft.move.stepCount <= draft.step) return Ask::Troop;
  if (!draft.slotChosen) return Ask::Slot;

  const Troop kind = static_cast<Troop>(current(draft).kind);
  const Terrain& b = game.board();
  const int seat = game.turn;
  const int slot = current(draft).slot;
  const Game g = projected(game, draft);

  // Station Metal-X is where troop effects do not happen, so there is no effect
  // question to ask on one. Asking anyway offered a steal the engine would
  // then refuse, which stranded the whole move.
  const bool nullified = game.specialBases && b.specialAt(slot) == Special::Nullify;

  if (!draft.effectAnswered && !nullified) {
    // A question with no possible answer is not a question: Jumbo beside
    // nobody, or a steal against an empty rack, is simply never asked.
    switch (effectAsk(kind)) {
      case Ask::JumboVictim:
        for (int t = 0; t < b.baseCount; ++t) {
          if ((b.adj[slot] & (uint64_t{1} << t)) && g.occupantSeat(t) == other(seat)) return Ask::JumboVictim;
        }
        break;
      case Ask::StealOffer:
        if (g.rackSize(other(seat)) > 0) return Ask::StealOffer;
        break;
      case Ask::DrawOffer:
        if (g.reserveRemaining(seat) > 0 && g.rackSize(seat) < kRackLimit) return Ask::DrawOffer;
        break;
      case Ask::ChainOffer: {
        if (draft.step + 1 >= kMaxChain) break;
        Step scratch[1];
        if (g.legalPlacements(seat, scratch, 1) > 0) return Ask::ChainOffer;
        break;
      }
      default:
        break;
    }
  }

  if (!draft.baseAnswered && game.specialBases) {
    switch (b.specialAt(slot)) {
      case Special::Draw:
        if (g.rackSize(seat) < kRackLimit && g.reserveRemaining(seat) > 0) return Ask::BaseOffer;
        break;
      case Special::Suppress:
        if (g.rackSize(other(seat)) > 0) return Ask::BaseOffer;
        break;
      case Special::Exhume:
        if (g.rackSize(seat) < kRackLimit) {
          for (int k = 0; k < kTroopKinds; ++k) {
            if (g.discarded[seat][k] > 0) return Ask::ExhumeKind;
          }
        }
        break;
      case Special::Recall:
        if (g.rackSize(seat) < kRackLimit) {
          for (int t = 0; t < b.baseCount; ++t) {
            if (t != slot && g.occupantSeat(t) == seat) return Ask::RecallFrom;
          }
        }
        break;
      case Special::Shove:
        // Two questions, and the second exists only once the first is
        // answered. Missing that left the machine unable to finish a shove.
        if (current(draft).useBase && current(draft).baseFrom != kNoSlot) return Ask::ShoveTo;
        for (int t = 0; t < b.baseCount; ++t) {
          if ((b.adj[slot] & (uint64_t{1} << t)) && g.occupantSeat(t) == other(seat)) return Ask::ShoveFrom;
        }
        break;
      case Special::None:
      case Special::Gate:
      case Special::Nullify:
        break;
    }
  }

  // A Cap'n that took its offer owes a second placement.
  if (kind == Troop::Capn && current(draft).useEffect && draft.step + 1 >= draft.move.stepCount) return Ask::Troop;

  return Ask::Ready;
}

// Each public answer is applied to a copy first and kept only if the draft
// could still be finished. A tap that would paint the player into a corner is
// refused at the moment it is made rather than at the moment they commit.
bool answerTroop(const Game& game, Draft& draft, const Troop kind) {
  Draft probe = draft;
  if (!takeTroop(game, probe, kind) || !completable(game, probe)) return false;
  draft = probe;
  return true;
}

bool answerSlot(const Game& game, Draft& draft, const int slot) {
  Draft probe = draft;
  if (!takeSlot(game, probe, slot) || !completable(game, probe)) return false;
  draft = probe;
  return true;
}

bool answerOffer(const Game& game, Draft& draft, const bool take) {
  Draft probe = draft;
  if (!takeOffer(game, probe, take) || !completable(game, probe)) return false;
  draft = probe;
  return true;
}

bool answerTarget(const Game& game, Draft& draft, const int slotOrKind) {
  Draft probe = draft;
  if (!takeTarget(game, probe, slotOrKind) || !completable(game, probe)) return false;
  draft = probe;
  return true;
}

// ---------------------------------------------------------------------------
// What the board lights up
// ---------------------------------------------------------------------------

uint64_t candidateSlots(const Game& game, const Draft& draft) {
  const Terrain& b = game.board();
  const int seat = game.turn;
  const Game g = projected(game, draft);
  uint64_t mask = 0;

  switch (pending(game, draft)) {
    case Ask::Slot: {
      const Troop kind = static_cast<Troop>(current(draft).kind);
      const uint64_t reach = g.reachable(seat);
      for (int s = 0; s < b.slotCount(); ++s) {
        if (g.placeableWith(seat, s, kind, true, reach)) mask |= uint64_t{1} << s;
      }
      break;
    }
    case Ask::JumboVictim:
    case Ask::ShoveFrom: {
      const int from = current(draft).slot;
      for (int t = 0; t < b.baseCount; ++t) {
        if ((b.adj[from] & (uint64_t{1} << t)) && g.occupantSeat(t) == other(seat)) mask |= uint64_t{1} << t;
      }
      break;
    }
    case Ask::ShoveTo: {
      const int from = current(draft).baseFrom;
      for (int t = 0; t < b.baseCount; ++t) {
        if (b.adj[from] & (uint64_t{1} << t)) mask |= uint64_t{1} << t;
      }
      break;
    }
    case Ask::RecallFrom: {
      for (int t = 0; t < b.baseCount; ++t) {
        if (t == current(draft).slot) continue;  // "one of your OTHER troops"
        if (g.occupantSeat(t) == seat) mask |= uint64_t{1} << t;
      }
      break;
    }
    default:
      break;
  }
  return mask;
}

uint8_t candidateTroops(const Game& game, const Draft& draft) {
  if (pending(game, draft) != Ask::Troop) return 0;
  const Game g = projected(game, draft);
  uint8_t mask = 0;
  Step steps[kTroopKinds * kMaxSlots];
  const int n = g.legalPlacements(game.turn, steps, static_cast<int>(sizeof(steps) / sizeof(steps[0])));
  for (int i = 0; i < n; ++i) mask = static_cast<uint8_t>(mask | (1u << steps[i].kind));
  return mask;
}

}  // namespace toybattle

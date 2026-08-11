#pragma once

// Who may act over a link, and how to tell the player what just happened.
// Freestanding: no renderer, no radio, no Activity, so a whole two-device
// match plays inside a host test.
//
// Sea Salt has the same two notions of "your turn" Jaipur documented -- the
// transport's, which alternates strictly, and the game's, which does not: a
// boat pair keeps the game turn while the transport waits, LAST CHANCE hands
// the game turn over mid-round, and the device that banks a round is not
// necessarily the one that should deal the next. The fix is the same Pass:
// a device holding the transport turn that the rules have nothing for sends
// the state back unchanged. This function is the only place that decision
// lives.

#include <cstdio>

#include "SeaSaltCore.h"

namespace seasalt {

enum class LinkAction : uint8_t {
  Wait,  // not your transport turn, or the match is over
  Move,  // both turns agree: play, then send
  Pass,  // yours to send, nothing to decide: send it back unchanged
  Deal,  // the round is banked; deal the next one and send it
};

inline LinkAction linkAction(const Game& game, const int seat, const bool yourTransportTurn) {
  if (!yourTransportTurn) return LinkAction::Wait;
  switch (game.currentPhase()) {
    case Phase::GameOver:
      return LinkAction::Wait;
    case Phase::RoundOver:
      // Exactly one device holds the transport turn, so exactly one deals.
      return LinkAction::Deal;
    case Phase::Playing:
    case Phase::LastChance:
      return game.turn == static_cast<uint8_t>(seat) ? LinkAction::Move : LinkAction::Pass;
  }
  return LinkAction::Wait;
}

// Their turn, reconstructed from the difference between the state you had and
// the one that arrived. The wire carries whole states and no move record, so
// the diff IS the account -- and every clause below reads a public fact, which
// is what makes this honest: nothing here could not be seen across a table.
//
// `names` is the display vocabulary (seasaltui::kindName's table), passed in
// because the rules layer does not know that TURTLE is what this device calls
// an octopus.
inline void describeTheirTurn(const Game& before, const Game& after, const int seat,
                              const char* const names[kKindCount], char* out, const size_t size) {
  const int them = seat ^ 1;

  // A new seed is a new deal: the packet is a fresh round, not a turn, and
  // "they took a card" would be a lie under a THEIR TURN pill.
  if (after.seed != before.seed) {
    std::snprintf(out, size, "ROUND %d. %s", after.round,
                  after.turn == static_cast<uint8_t>(seat) ? "YOU START." : "THEY START.");
    return;
  }

  if (after.currentPhase() == Phase::GameOver && after.mermaidsHeld(them) == kMermaidsToWin) {
    std::snprintf(out, size, "FOUR MERMAIDS. THEY WIN.");
    return;
  }
  if (after.ender == static_cast<uint8_t>(them) && before.ender == kNoSeat) {
    std::snprintf(out, size,
                  after.betWasLastChance ? "THEY BET LAST CHANCE. ONE MORE TURN, MAKE IT COUNT." : "THEY CALLED STOP.");
    return;
  }
  if (after.currentPhase() == Phase::RoundOver && after.ender == kNoSeat) {
    std::snprintf(out, size, "THE DECK RAN OUT. NOBODY SCORES.");
    return;
  }
  if (after.handSize(seat) < before.handSize(seat)) {
    std::snprintf(out, size, "SWIMMER AND SHARK. THEY STOLE FROM YOUR HAND.");
    return;
  }
  // A pair hitting their table is public; name the loudest one. Boats first:
  // the extra turns are the thing you most need explained.
  static constexpr Kind kLoudest[4] = {Kind::Boat, Kind::Crab, Kind::Fish, Kind::Swimmer};
  for (const Kind kind : kLoudest) {
    const int grew = after.countIn(tableOf(them), kind) - before.countIn(tableOf(them), kind);
    if (grew <= 0) continue;
    if (kind == Kind::Boat) {
      std::snprintf(out, size, "THEY PLAYED BOATS FOR EXTRA TURNS.");
    } else if (kind == Kind::Swimmer) {
      std::snprintf(out, size, "THEY PLAYED SWIMMER AND SHARK.");
    } else {
      std::snprintf(out, size, "THEY PLAYED TWO %sS.", names[static_cast<int>(kind)]);
    }
    return;
  }
  std::snprintf(out, size, "THEY TOOK A CARD. YOUR MOVE.");
}

}  // namespace seasalt

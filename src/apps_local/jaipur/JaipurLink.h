#pragma once

// Who may act, over a link. Freestanding: no renderer, no radio, no Activity,
// so a whole two-device match can be played inside a host test.
//
// Jaipur has two notions of "your turn" and they are not the same thing:
//
//   * the transport turn. LinkPlay alternates strictly -- send a state, hand
//     over the turn -- and refuses play() from the side that does not hold it.
//   * the game turn, `Game::turn`, which the rules move.
//
// They agree on every move of a round, because a move advances both. They come
// apart at a round boundary: **the loser starts the next round**, and that has
// nothing to do with who sent the last packet. Half the time the device that
// deals is also the one the rules want to move first, and then the transport is
// pointing the wrong way.
//
// The fix is a pass: a device holding the transport turn that the rules have
// nothing for sends the state back unchanged. Nothing in the game moves, the
// turn does. It needs no new LinkPlay primitive and no extra packet type, which
// is why it is this rather than a "turn hand-back" message.
//
// This function is the only place that decision is made, so the screen, the tap
// router and the test cannot each have their own version of it.

#include "JaipurCore.h"

namespace jaipur {

enum class LinkAction : uint8_t {
  Wait,  // not your turn, or the match is over and the chrome owns the screen
  Move,  // both turns agree: play, then send
  Pass,  // yours to send, nothing to decide: send it back unchanged
  Deal,  // the round is scored; deal the next one and send it
};

// `yourTransportTurn` is LinkPlay's phase, `seat` is which side this device
// plays. Single-player callers do not need this: there is no transport, so
// nothing can disagree.
inline LinkAction linkAction(const Game& game, const int seat, const bool yourTransportTurn) {
  if (!yourTransportTurn) return LinkAction::Wait;
  switch (game.currentPhase()) {
    case Phase::GameOver:
      // Nothing left to send. The link's own rematch screen takes it from here,
      // and a device that kept sending would be answering a question nobody
      // asked.
      return LinkAction::Wait;
    case Phase::RoundOver:
      // Exactly one device deals, and it is this one: the round ended on the
      // other player's move, which is what handed the turn over. Both are
      // looking at the same scores, only one of them is holding the deck.
      return LinkAction::Deal;
    case Phase::Playing:
      return game.turn == static_cast<uint8_t>(seat) ? LinkAction::Move : LinkAction::Pass;
  }
  return LinkAction::Wait;
}

}  // namespace jaipur

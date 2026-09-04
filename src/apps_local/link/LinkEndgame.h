#pragma once

// The end of a match: what happens between the last move and ANOTHER GAME?
//
// Two things went wrong at that instant, and it is the same instant, which is
// why they are one file.
//
// The loser never saw the move that beat them. driveLink() applied the winning
// state and put the rematch screen up in the same pass, and a repaint is
// deferred to the end of the loop, so the board carrying the win was never on
// the panel at all. Losing without seeing how is the worst version of losing.
//
// And a link match was never counted. Five of the nine games recorded their
// result inside gameLoop() -- checkers, connect four, knucklebones and yahtzee
// in an else-if arm after a guard that returns first in multiplayer, toy battle
// in a block gameLoop() simply stops reaching -- so the tally, the final board
// and the whole W/L/D record only ever existed for solo games. Nothing crashed
// and nothing logged: the record was simply never written.
//
// Ask what a Nintendo DS would do. It shows the winning move, lets it sit long
// enough to read, and only then offers another game -- and it counts the match
// while it is doing that. So both live here, driven by the layer, rather than
// in nine games that would each get it slightly differently.
//
// Freestanding on purpose -- <atomic> and <cstdint> and nothing else -- so
// host-tests/link can drive the whole sequence with no renderer, no device and
// no radio. Everything that has an ORDER to get wrong is inside update(),
// rather than spread across the caller where a test would have to reproduce it
// and could reproduce it wrongly. The host is a template parameter rather than
// a base class for the same reason a vtable is not free here; it must supply:
//
//   bool matchGameOver() const   has the game finished
//   void onMatchEnded()          count it, and show it: called exactly ONCE
//                                per finished match, and it is the only place
//                                a link match is ever recorded
//   void onEndgameChanged()      repaint
//
// On tasks: update(), reset() and skip() belong to the LOOP task, notePainted()
// to the RENDER task, and only the timestamp crosses between them, atomically.
// The stage byte is written by the loop and read by the render task through
// showingFinal(), the same way requested_ and rematch_ already are in
// LinkActivity: a single byte, and a stale read costs one frame.

#include <atomic>
#include <cstdint>

namespace linkplay {

class Endgame {
 public:
  // Long enough to read a board and see what happened. Counted from the paint
  // rather than from the move, because a repaint on this panel takes the better
  // part of a second and a hold measured from the move is mostly the repaint.
  static constexpr uint32_t kHoldMs = 2500;
  // And a ceiling, from the move, in case the paint is never reported. A player
  // stuck forever on a finished board with no way to ask for another game is a
  // worse bug than the one this file exists to fix.
  static constexpr uint32_t kUnpaintedHoldMs = 6000;

  enum class Stage : uint8_t {
    // Playing, or not in a match at all.
    Live,
    // The game has finished and its own final screen is up. The link layer
    // keeps its hands off the panel for the length of the hold.
    Final,
    // ANOTHER GAME?
    Offer,
  };

  Stage stage() const { return stage_; }
  // The game still owns the screen. Ask this before putting anything over it.
  bool showingFinal() const { return stage_ == Stage::Final; }
  bool offering() const { return stage_ == Stage::Offer; }

  // A fresh match, a rematch, leaving: anything after which the next end of a
  // game is a different end of a game.
  void reset() {
    stage_ = Stage::Live;
    painted_ = false;
    holdUntil_ = 0;
    pendingPaintMs_.store(0);
  }

  // One pass. `inMatch` is the link layer's own idea of being in a match, so a
  // finished SOLO game never reaches any of this.
  template <class Host>
  void update(Host& host, const bool inMatch, const uint32_t now) {
    // Consumed FIRST, and consumed whatever the stage is. A frame that reached
    // the panel while the game was still running is not the final board, and if
    // it were left pending it would be picked up by the transition below and
    // cut the hold to nothing -- which is the original bug wearing a timer.
    const uint32_t painted = pendingPaintMs_.exchange(0);
    if (painted != 0 && stage_ == Stage::Final && !painted_) {
      painted_ = true;
      holdUntil_ = painted + kHoldMs;
    }

    if (!inMatch) {
      if (stage_ != Stage::Live) reset();
      return;
    }
    if (!host.matchGameOver()) {
      // A rematch that began without anybody calling reset() is still a new
      // game, and the next end of it must be announced again.
      if (stage_ != Stage::Live) reset();
      return;
    }
    if (stage_ == Stage::Live) {
      stage_ = Stage::Final;
      painted_ = false;
      holdUntil_ = now + kUnpaintedHoldMs;
      // Counted here and nowhere else. This runs on both devices, on the pass
      // the game ends, whoever played the last move.
      host.onMatchEnded();
      host.onEndgameChanged();
      return;
    }
    if (stage_ == Stage::Final && expired(now)) {
      stage_ = Stage::Offer;
      host.onEndgameChanged();
    }
  }

  // A frame reached the panel at `now`. Called from the RENDER task, which is
  // not the task that runs update(), so the stamp is atomic and everything that
  // reads it happens on the next pass of the loop. Zero means nothing pending,
  // so a millis() of 0 -- the first millisecond after boot -- is stamped as 1.
  //
  // `stageAtBuild` is the stage as it was when this frame STARTED being drawn,
  // and it is the whole reason this takes an argument. requestUpdate() only
  // notifies the render task, so a repaint of the old board can already be in
  // flight when the match ends; reporting that one would start the hold from a
  // frame that never showed the final position. A frame that began before the
  // end is not the final board, however it ends up on the panel.
  void notePainted(const Stage stageAtBuild, const uint32_t now) {
    if (stageAtBuild != Stage::Final) return;
    pendingPaintMs_.store(now == 0 ? 1 : now);
  }

  // The player has asked for another game while the board was still up. They
  // have seen everything the hold exists to show them, so stop holding.
  void skip() {
    if (stage_ == Stage::Final) stage_ = Stage::Offer;
  }

 private:
  // Wrap-safe: millis() rolls over about every 49 days, and a difference
  // compared as signed survives it where `now >= holdUntil_` does not.
  bool expired(const uint32_t now) const { return static_cast<int32_t>(now - holdUntil_) >= 0; }

  Stage stage_ = Stage::Live;
  // The final board has been seen, so the hold is counted from that rather than
  // from the move.
  bool painted_ = false;
  uint32_t holdUntil_ = 0;
  std::atomic<uint32_t> pendingPaintMs_{0};
};

}  // namespace linkplay

#pragma once

// The half of multiplayer that is the same in every game.
//
// LinkPlay handles the radio and LinkScreens draws the two seats, and between
// them there was still a match's worth of behaviour that chess had written by
// hand: when to start looking, how a rematch is asked and answered, which
// screen owns the pass, what a seat says, when to keep the device awake. The
// second game was going to copy about two hundred lines of it, and a rematch
// conversation that two games implement separately is a rematch conversation
// that will eventually work differently in each.
//
// So this is where the DS's uniformity is enforced rather than hoped for. A
// game inherits, fills in what only it can know, and gets everything else --
// including every screen -- for free.
//
// What a game must supply:
//
//   linkState()        the Play<State> it owns
//   linkGameTitle()    "CHESS"
//   linkHeadline()     why the link screen is up, in the player's words
//   onMatchStart()     a fresh game, and which side you are
//   takeOpponentState()  adopt their state into yours, however that merges
//   onRematch()        both sides said yes
//   onLinkEnded()      back to solo: restore what was saved
//   matchGameOver()    whether the game has finished
//   onMatchEnded()     it just finished: count it and put the final board up
//   gameLoop() / gameRender()   what loop() and render() used to be
//
// What it must NOT write: the searching screen, the disconnect screen, the
// rematch screen, the note vocabulary, the seat states, the tick, or the sleep
// suppression.
//
// Note this is the one file under link/ that is not freestanding: it inherits
// from Activity and therefore reaches the renderer and the input manager. Keep
// it out of host-tests/link/run.sh, which compiles the rest with nothing but a
// C++17 toolchain on purpose.

#include "../../activities/Activity.h"
#include "../../components/UITheme.h"  // Rect
#include "../ui/ToyboxScreen.h"
#include "LinkEndgame.h"
#include "LinkPlay.h"
#include "LinkScreens.h"

namespace linkplay {

class LinkActivity : public Activity {
 public:
  LinkActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity(name, renderer, mappedInput) {}

  // Final, both of them, because tick placement is the thing a game gets wrong.
  // Chess's loop() has six early returns and the first is a settings overlay,
  // so a tick written anywhere but the first line kills the match whenever
  // somebody opens one for ten seconds. Here there is nowhere else to put it.
  void loop() final;
  void render(RenderLock&& lock) final;

  // A live match must keep the device awake: an opponent thinking for five
  // minutes is indistinguishable from an idle device, and the sleep timer would
  // take the second one.
  bool preventAutoSleep() override { return linkState().wantsAwake(); }

 protected:
  using Phase = PlayBase::Phase;

  // --- what the game supplies ---------------------------------------------

  virtual PlayBase& linkState() = 0;
  virtual const PlayBase& linkState() const = 0;
  virtual const char* linkGameTitle() const = 0;
  // The sentence at the top of the link screen: LOOKING FOR A PLAYER,
  // CHECKMATE, YOU WIN. The game owns it because only the game knows how its
  // own match ended -- but not when the match ended because somebody walked
  // away, which is the layer's news and is said in one voice for every game.
  virtual const char* linkHeadline() const = 0;
  // A match has just begun. Set up a fresh game; `goesFirst` is your side, and
  // it comes from a coin toss both devices compute, so there is nothing to
  // agree on and nothing to show.
  virtual void onMatchStart(bool goesFirst) = 0;
  // Take delivery of whatever the opponent sent, into whatever shape the game
  // keeps. Return true if anything changed. This is a game's method rather than
  // the layer's because adopting is not always a straight copy: battleship's
  // shared state holds both fleets, and the copy would wipe its own.
  virtual bool takeOpponentState() = 0;
  // Both sides have said yes to another game.
  virtual void onRematch() = 0;
  // The match is over and the app is solo again: put the saved game back.
  virtual void onLinkEnded() = 0;
  virtual bool matchGameOver() const = 0;
  // The match has just ended, on the pass it ended, on BOTH devices. Two jobs,
  // and they are one moment: record the result, and go to whatever screen shows
  // the finished game. The layer then keeps that screen on the panel for a
  // couple of seconds before it offers another game -- see LinkEndgame.h.
  //
  // Pure rather than defaulted, and that is the whole point of it. Every game
  // recorded its result in an else-if arm of gameLoop() that multiplayer
  // returns before reaching, so no link match was ever counted in any of them.
  // A hook with a default body would have been forgotten in exactly the same
  // silence; this one makes the compiler ask each game the question.
  virtual void onMatchEnded() = 0;
  virtual void gameLoop() = 0;
  virtual void gameRender() = 0;

  // The phase changed. Optional: chess rebuilds the "MARIO'S MOVE" label here.
  virtual void onLinkPhaseChanged() {}
  // Draws into the rect the link screen leaves under the seats. Optional; on a
  // rematch screen it is where the game you have just finished belongs.
  virtual void drawLinkArt(const Rect&) {}

  // --- what the game gets back --------------------------------------------

  // The player asked to play nearby. Nothing happens on the radio until the
  // next pass, so a failure to start can put them back on a game they can play
  // rather than a screen they are stuck on.
  void enterLink(GameId gameId);
  // Tells them, shuts the radio down, and hands back to the game. Idempotent.
  void leaveLink();

  bool linkRequested() const { return requested_; }
  // Always asked of the session, never mirrored. A copy taken at the top of the
  // pass is stale by the time the game reads it: taking delivery of their state
  // and sending your own both move the turn immediately, so a match can pass
  // through two phases inside one loop. Battleship's setup does exactly that on
  // its first exchange, and the mirror had it firing a shot on their turn.
  Phase linkPhase() const { return linkState().phase(); }
  // In a match, as opposed to merely having asked for one.
  bool inMatch() const { return requested_ && linkPhase() != Phase::Off && linkPhase() != Phase::Searching; }
  bool linkYourTurn() const { return linkPhase() == Phase::YourTurn; }
  const char* opponentName() const { return linkState().opponentName(); }

  // Asks the other device for another game, or accepts one they have already
  // asked for. This is also how a game's own NEW GAME reaches them: as a
  // question, not as a board that silently reset under them.
  void proposeRematch();

  // True while the shared screen owns the pass, so a game that needs to know
  // (a status line, a save guard) can ask rather than infer.
  bool linkOwnsScreen() const;

 private:
  bool driveLink();
  void startRematch();
  void routeLinkScreen();
  void drawLinkScreen();
  linkui::LinkModel linkModel() const;

  GameId gameId_ = GameId::Test;
  bool requested_ = false;
  // Only for spotting a change between passes, which is what the game's hook
  // is told about. Never read as "the phase" -- that is linkPhase().
  Phase lastPhase_ = Phase::Off;
  // Which side of the screen handover we were on last pass, so the shared hit
  // table can be invalidated exactly when the screen changes hands.
  bool ownedScreen_ = false;
  // The rematch screen is up. Reached by a finished game once its final board
  // has had its couple of seconds, or by either player asking for a new one --
  // which is the same question either way, so it is the same screen.
  bool rematch_ = false;
  // The seconds between the last move and ANOTHER GAME?, and the one place a
  // link match is counted. See LinkEndgame.h.
  Endgame endgame_;
  linkui::SeatState you_ = linkui::SeatState::Ready;
  linkui::SeatState them_ = linkui::SeatState::Looking;

 protected:
  // The visible screen's hit table, filled by the chrome as it draws and
  // emptied into an action when input arrives. Shared with the game: only one
  // screen is up at a time, so one buffer serves both.
  toybox::Interactions interactions;
  // False between a screen change and its first paint, when the table still
  // describes a screen that is not on the panel.
  bool interactionsReady = false;
};

}  // namespace linkplay

#include "LinkActivity.h"

#include <Logging.h>

#include "../player/PlayerName.h"
#include "../ui/ToyboxTheme.h"

namespace linkplay {

// The one place both constants are visible. A name too long for the wire is
// silently truncated by Session, which does not break the link -- it delivers a
// half name, and since the face is derived from the name, a half name is a
// stranger's face. Adding a longer word is what would cause it, so the check
// belongs where nobody has to remember to look.
static_assert(player::kMaxNameLength <= kMaxNameBytes, "a rollable name must fit the wire; see PlayerName.h");

namespace {

// The two things one device ever tells another outside a move. They ride the
// note channel, which does not touch the turn -- which is the whole reason it
// exists, since both are asked exactly when a game has ended and there is no
// turn left to carry them.
//
// One vocabulary for every game, which is the point of it living here: two
// games that each invented "play again" would eventually disagree about what
// the byte meant, and the failure would be a silent one between two devices
// that had already agreed to pair.
constexpr uint8_t kNotePlayAgain = 1;
constexpr uint8_t kNoteLeaving = 2;

}  // namespace

void LinkActivity::enterLink(const GameId gameId) {
  gameId_ = gameId;
  requested_ = true;
  rematch_ = false;
  endgame_.reset();
  lastEndgameStage_ = Endgame::Stage::Live;
  lastPhase_ = Phase::Off;
  you_ = linkui::SeatState::Ready;
  them_ = linkui::SeatState::Looking;
  requestUpdate();
}

void LinkActivity::leaveLink() {
  if (!requested_) return;
  // Only while a question is open. Saying it otherwise is harmless but
  // meaningless: stop() already sends a Bye, and this note exists so a seat
  // reads LEFT rather than sitting on DECIDING until the timeout notices.
  if (rematch_) linkState().say(kNoteLeaving);
  linkState().stop();
  requested_ = false;
  rematch_ = false;
  endgame_.reset();
  lastEndgameStage_ = Endgame::Stage::Live;
  lastPhase_ = Phase::Off;
  onLinkEnded();
}

bool LinkActivity::linkOwnsScreen() const {
  if (!requested_) return false;
  // The final board outranks everything the link layer would otherwise put on
  // the panel, including the opponent walking away in the same second. A player
  // who has just lost must see the move that beat them; being told THEY LEFT
  // instead is the same bug wearing a different screen.
  if (endgame_.showingFinal()) return false;
  return linkPhase() == Phase::Searching || linkPhase() == Phase::Over || rematch_;
}

bool LinkActivity::driveLink() {
  if (linkPhase() == Phase::Off) {
    // Arriving here is the entire setup. No menu, no host or join, no pairing
    // code: it starts looking immediately.
    if (!linkState().start(gameId_, player::name())) {
      // The one failure worth surfacing, and the kindest answer is a game the
      // player can still play rather than a screen they are stuck on.
      LOG_ERR("LINK", "the radio would not start");
      leaveLink();
      return false;
    }
  }

  const Phase before = lastPhase_;
  linkState().update(millis());

  if (linkState().takeMatchStart()) {
    rematch_ = false;
    you_ = linkui::SeatState::Deciding;
    them_ = linkui::SeatState::Deciding;
    onMatchStart(linkState().goesFirst());
  }

  // Notes first: one of them can end the wait, and acting on it in the same
  // pass is the difference between "instant" and "a screen refresh late".
  uint8_t note = 0;
  if (linkState().heard(note)) {
    if (note == kNoteLeaving) {
      // They answered with no. Say so, rather than leaving the other player
      // looking at a button that is never going to be answered.
      them_ = linkui::SeatState::Left;
      rematch_ = true;
      requestUpdate();
    } else if (note == kNotePlayAgain) {
      them_ = linkui::SeatState::Ready;
      rematch_ = true;
      if (you_ == linkui::SeatState::Ready) {
        startRematch();
      } else {
        // They asked and we have not answered.
        you_ = linkui::SeatState::Deciding;
        requestUpdate();
      }
    }
  }

  if (takeOpponentState()) requestUpdate();

  // A finished game is a question, but not yet. The board that ended it goes up
  // first and stays there long enough to read, and the match is counted on the
  // same pass -- both of those are Endgame's, and this is the only place either
  // happens. See LinkEndgame.h.
  //
  // A local class in a member function has that member's access, which is how
  // the game's protected hooks reach a helper that knows nothing about
  // Activity. No vtable and no allocation: Endgame::update is a template.
  struct EndgameHost {
    LinkActivity& self;
    bool matchGameOver() const { return self.matchGameOver(); }
    void onMatchEnded() const { self.onMatchEnded(); }
    void onEndgameChanged() const { self.requestUpdate(); }
  } endgameHost{*this};
  endgame_.update(endgameHost, inMatch(), millis());
  if (endgame_.offering() && !rematch_) {
    rematch_ = true;
    you_ = linkui::SeatState::Deciding;
    them_ = linkui::SeatState::Deciding;
    requestUpdate();
  }

  if (linkPhase() == Phase::Over) {
    // Gone for good, whichever way. There is nothing left to ask them.
    them_ = linkState().ending() == PlayBase::Ending::OpponentLost ? linkui::SeatState::Lost : linkui::SeatState::Left;
    rematch_ = true;
  }

  // Compared end of pass to end of last pass, so a phase the pass moved through
  // twice is still noticed. The hook may already have run inside
  // takeOpponentState(), which is why it has to be safe to call again.
  if (linkPhase() != before) {
    onLinkPhaseChanged();
    requestUpdate();
  }
  lastPhase_ = linkPhase();

  // Whoever is about to own the screen is not the one whose hit table is on the
  // panel. The buffer belongs to this layer, so this is where the invariant it
  // documents has to be kept: a rematch accepted mid-pass hands the pass
  // straight to the game, which would otherwise route the player's next tap
  // against the link screen's table.
  const bool owns = linkOwnsScreen();
  if (owns != ownedScreen_) {
    interactionsReady = false;
    ownedScreen_ = owns;
  }
  // Same invariant, second handover: onMatchEnded() sends the game to its own
  // final screen mid-pass with nobody having touched the panel, and the table
  // on the panel still describes the board. Without this the first tap after a
  // game ends is routed against the screen it just left.
  if (endgame_.stage() != lastEndgameStage_) {
    interactionsReady = false;
    lastEndgameStage_ = endgame_.stage();
  }

  if (owns) {
    routeLinkScreen();
    return false;
  }
  return true;
}

void LinkActivity::proposeRematch() {
  // Only an answer actually given counts. Reading the other seat outside a live
  // conversation is what made chess's first NEW GAME start a game without
  // asking: the seats had been left on Ready by the match handshake, so the
  // proposal saw agreement that had never been given.
  const bool theyAlreadyAgreed = rematch_ && them_ == linkui::SeatState::Ready;
  // Asked for while the final board was still up: they have seen what the hold
  // exists to show them, so it stops holding rather than swallowing the tap.
  endgame_.skip();
  you_ = linkui::SeatState::Ready;
  linkState().say(kNotePlayAgain);
  if (theyAlreadyAgreed) {
    startRematch();
    return;
  }
  rematch_ = true;
  if (them_ != linkui::SeatState::Left && them_ != linkui::SeatState::Lost) {
    them_ = linkui::SeatState::Deciding;
  }
  requestUpdate();
}

void LinkActivity::startRematch() {
  endgame_.reset();
  lastEndgameStage_ = Endgame::Stage::Live;
  rematch_ = false;
  you_ = linkui::SeatState::Deciding;
  them_ = linkui::SeatState::Deciding;
  onRematch();
  requestUpdate();
}

linkui::LinkModel LinkActivity::linkModel() const {
  linkui::LinkModel model;
  model.gameTitle = linkGameTitle();
  // A game that has not ended, on a screen that is only up because the other
  // player has gone, must not ask ANOTHER GAME? next to a seat reading LEFT and
  // no button to answer with. The game cannot know this -- the seat states live
  // here -- so the layer says it, and says it the same way in every game.
  //
  // Not "they left" for a silence: they did not choose to, the battery did, and
  // blaming them for it reads wrong.
  if (!matchGameOver() && them_ == linkui::SeatState::Lost) {
    model.headline = "LOST CONNECTION";
  } else if (!matchGameOver() && them_ == linkui::SeatState::Left) {
    model.headline = "THEY LEFT";
  } else {
    model.headline = linkHeadline();
  }
  model.yourName = "YOU";
  model.yourFaceName = player::name();
  model.theirName = linkState().opponentName();
  model.you = you_;
  model.them = them_;
  model.linked = linkPhase() != Phase::Searching;
  // No point offering a game to somebody who is not there, and none at all
  // before there is a game to play again.
  model.offerPlayAgain = rematch_ && them_ != linkui::SeatState::Left && them_ != linkui::SeatState::Lost;
  return model;
}

void LinkActivity::routeLinkScreen() {
  namespace fui = freeink::ui;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    leaveLink();
    return;
  }

  fui::InputSnapshot input;
  int tapX = 0;
  int tapY = 0;
  if (mappedInput.wasScreenTapped(tapX, tapY)) {
    input.touchReleased = true;
    input.touchX = static_cast<int16_t>(tapX);
    input.touchY = static_cast<int16_t>(tapY);
  }
  if (!input.touchReleased || !interactionsReady) return;

  const fui::ActionEvent event = interactions.route(input);
  if (event.action == linkui::ActionPlayAgain) {
    proposeRematch();
    return;
  }
  if (event.action == linkui::ActionLeaveLink) leaveLink();
}

void LinkActivity::drawLinkScreen() {
  namespace fui = freeink::ui;

  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions);
  toybox::Screen screen(frame);
  const fui::Rect slot = linkui::buildLink(screen, linkModel());
  // Only once a game has been played. While searching there is nothing to show
  // but a starting position, which is the same picture every time and therefore
  // wallpaper.
  if (rematch_) drawLinkArt(Rect{slot.x, slot.y, slot.width, slot.height});
  interactionsReady = true;
  toybox::reportOverflow(interactions, "Link");
}

void LinkActivity::loop() {
  // Ahead of everything the game does, and there is nowhere else it could go.
  if (requested_ && !driveLink()) return;
  gameLoop();
}

void LinkActivity::render(RenderLock&&) {
  if (linkOwnsScreen()) {
    drawLinkScreen();
    renderer.displayBuffer();
    return;
  }
  // Read BEFORE the frame is built and reported after: a repaint of the old
  // board can already be in flight when the match ends, and a hold started from
  // that frame is a hold the player never saw.
  const Endgame::Stage stageAtBuild = endgame_.stage();
  gameRender();
  // Reported after gameRender() rather than before, because gameRender() ends
  // in displayBuffer(): this is the moment the frame is actually on the panel,
  // and drawn is not seen until it is. Endgame decides whether this was the
  // final board; every frame is reported so that decision has one home.
  endgame_.notePainted(stageAtBuild, millis());
}

}  // namespace linkplay

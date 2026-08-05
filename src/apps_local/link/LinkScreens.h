#pragma once

// The multiplayer screens, written once and shared by every game.
//
// This is where the DS's uniformity actually lived. Its games all connected
// identically not because the radio made them, but because no game was allowed
// to write this part. If chess drew its own searching screen and battleship
// drew a different one, the abstraction underneath would still be correct and
// the feeling would still be gone.
//
// ---------------------------------------------------------------------------
// One screen, not three.
//
// Searching, "they left", and "shall we play again" are the same picture in
// different states: two seats and the link between them. Giving them one
// builder is not tidiness -- it is what makes the transitions read as one
// object changing rather than three screens replacing each other, which is the
// only kind of motion an e-ink panel can afford.
//
// The seats are the ornament, and they obey the rule: made of the system's own
// material (a match is two seats) and carrying its own data (who is in them and
// what they have just decided). The rail down the left is the link itself --
// dithered while there is nobody on the other end, solid once there is. That is
// the one thing on the screen worth watching, so it is the thing that changes.
// ---------------------------------------------------------------------------
//
// Freestanding builders in the ChessScreens mould: a model in, a drawn frame
// out, no renderer and no Activity, so host-tests/ui/ can assert what they drew
// and what they made tappable.

#include "../ui/ToyboxScreen.h"

namespace linkui {

namespace fui = freeink::ui;

// Shared across every app, so these must not collide with any game's own action
// ids (chess uses 1-4). Anything in this file stays in the 200s.
enum : fui::ActionId {
  ActionLeaveLink = 200,
  ActionPlayAgain = 201,
};

// What a seat is doing. The player reads their own row and the other one and
// knows the whole situation, which is the point: a rematch used to be a guess.
enum class SeatState : uint8_t {
  Looking,   // nobody there yet
  Deciding,  // they are being asked and have not answered
  Ready,     // said yes to another game
  Left,      // chose to stop, or closed the app
  Lost,      // went silent: flat battery, out of range
};

struct LinkModel {
  const char* gameTitle = "";
  // Why this screen is up, in the player's words: LOOKING FOR A PLAYER,
  // CHECKMATE, MARIO LEFT. The game owns this sentence because only the game
  // knows how its own match ended.
  const char* headline = "";
  // What your seat is labelled, which is "YOU" rather than your name: on a
  // screen with exactly two rows, naming yourself is a riddle and naming the
  // other person is the information.
  const char* yourName = "";
  // ...but the face still has to come from somewhere, and a face is a pure
  // function of a *name*. Passing the label was the first version and it drew
  // every player a blank head, because "YOU" parses to no words at all. The two
  // fields exist because the seat says one thing and draws another.
  const char* yourFaceName = "";
  // Empty until somebody is found; the seat draws as vacant, label and face
  // both.
  const char* theirName = "";
  SeatState you = SeatState::Ready;
  SeatState them = SeatState::Looking;
  // Draws the rail solid. False while searching, true once there is a match,
  // and it stays true after a game ends because they are still there deciding.
  bool linked = false;
  // Offer another game. False while searching, and false once they have gone,
  // because a button that cannot work is worse than one that is not there.
  bool offerPlayAgain = false;
};

// The multiplayer mark.
//
// One symbol for "this connects to another device", everywhere it happens: the
// PLAY NEARBY row of every game's start menu, and this screen. The DS did the
// same thing and it is the reason nobody ever had to explain local play -- you
// learned the mark once.
//
// It lives here rather than in each game so there can only ever be one of it.
// A game draws it with toybox::iconAtRowRight; this screen draws it beside the
// headline.
const freeink::Icon& nearbyMark();

// What a seat's right-hand column reads. Split out because it is the only text
// on the screen that changes with state, so it is worth asserting directly.
const char* seatValue(SeatState state, bool linked);

// Puts the opponent's face at the left of `band` and returns what is left of it.
//
// For the status band at the bottom of a board, which is where a game says
// whose move it is. Faces used to appear only while pairing, so the person you
// were playing vanished the moment you started playing them -- which is exactly
// backwards, since the whole point of a face is knowing who is on the other
// end. The capsule already names them; this is the same thought, drawn.
//
// Shared rather than copied into each game for the reason every screen in this
// directory is shared: two games that placed it differently would look like two
// devices. A null or empty name returns `band` untouched, which is what solo
// play against the engine gets -- there is nobody to show.
//
// Drawn in ink: the band is page background, and the capsule that follows is
// the black thing. Check what is behind a mark before you pick its colour.
fui::Rect withOpponentFace(toybox::Screen& screen, const fui::Rect& band, const char* theirName);

// Draws the screen and returns the rect left below the seats, for the game to
// draw into. On the rematch screen that is where the position you have just
// finished belongs: it is the one thing worth looking at while deciding whether
// to play again, and it is what turned this zone from slack into content.
//
// The layer cannot draw it -- a board is chess's material, not the link's -- so
// it hands over a slot and stays out of it, the same split the board screen
// uses. A game with nothing to show can ignore the rect.
fui::Rect buildLink(toybox::Screen& screen, const LinkModel& model);

}  // namespace linkui

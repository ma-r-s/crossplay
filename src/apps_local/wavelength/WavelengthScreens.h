#pragma once

// WAVELENGTH on screen. Freestanding builders over plain models.
//
// Portrait throughout, and the strip runs down the long axis rather than across
// it. That is for the keys, not for looks: the two page keys sit on one edge and
// the board profile calls them Up and Down, so a vertical strip is the identity
// mapping and skips the rotation arithmetic that FOREHEAD had to reason its way
// through. Do not claim it also survives people sitting around a table; no
// orientation does, and the person opposite reads it inverted. That is why the
// end-call is labelled with the spectrum's own words rather than with higher and
// lower, which mean different things in different seats.

#include "../ui/ToyboxScreen.h"
#include "WavelengthCore.h"

// The board is a ladder: twenty rungs, no container, the guess a heavy bar.
// Chosen from three arrangements rendered side by side rather than argued
// about; the column read as a barcode and the trough put a large dithered area
// on a surface that repaints on every step. The other two are deleted rather
// than left behind a macro, because a variant macro that survives is a second
// design nobody maintains.

namespace wavelengthui {

namespace fui = freeink::ui;

enum : fui::ActionId {
  // Tapping the strip steps one slot toward the end you tapped. The target is
  // the HALF of the strip above or below the marker, about 230px tall, never
  // the 27px slot itself: a slot is far too small to aim at in a bar and
  // nobody is asked to. Precision comes from stepping, which is also the
  // shuffling that is the best part of the round.
  ActionStepTowardTop = 1,
  ActionStepTowardBottom = 2,
  ActionLock = 3,
  ActionReady = 4,
  ActionPickFirst = 5,
  ActionPickSecond = 6,
  ActionPeekPad = 7,
  ActionClueGiven = 8,
  ActionCallTop = 9,
  ActionCallBottom = 10,
  ActionNextRound = 11,
  ActionStartRound = 12,
  ActionHowTo = 13,
  ActionEndSession = 14,
  ActionKeepPlaying = 15,
  ActionNewSession = 16,
  ActionBackToMenu = 17,
  ActionResume = 18,
  ActionAbandon = 19,
  ActionCarryOn = 20,
  ActionStartFresh = 21,
};

struct Spectrum {
  const char* top = "CHARMING";
  const char* bottom = "ANNOYING";
};

struct DialModel {
  Spectrum spectrum;
  int guess = 10;
  int roundNumber = 1;
  bool practice = false;
};

struct PickModel {
  Spectrum first;
  Spectrum second;
  bool onlyOne = false;  // the deck has a single unseen pair left
  int roundNumber = 1;
  bool practice = false;
};

struct PeekModel {
  Spectrum spectrum;
  int target = 10;
  bool revealed = false;  // true only while a thumb is on the pad
  bool everRevealed = false;
  // Set when a bare tap lands on a control that only answers to a hold. Without
  // it the pad is silent on a tap, which reads as a broken button rather than
  // as the wrong gesture: a cold player tapped twice, gave up, and got stuck.
  bool nudgeHold = false;
};

struct ClueModel {
  Spectrum spectrum;
  int roundNumber = 1;
  bool practice = false;
};

struct CallModel {
  Spectrum spectrum;
  int guess = 10;
  bool practice = false;
};

struct RevealModel {
  // Co-op has no end-call, so the reveal must not report one.
  bool showCall = false;
  Spectrum spectrum;
  int guess = 10;
  int target = 10;
  int points = 0;
  bool callWasRight = false;
  bool practice = false;
  int roundNumber = 1;
  int total = 0;
};

struct MenuModel {
  // Read, never owned: the activity holds the record and the save file.
  const wavelength::Record* record = nullptr;
  bool sessionInProgress = false;
  int sessionRound = 1;
  int sessionTotal = 0;
  // SCORED rounds, which is what the end screen counts. The front door used to
  // count the round about to start instead, so the two screens described one
  // session with two different numbers a single tap apart.
  int sessionScored = 0;
};

// The screen that asks whether the evening on the card is this table's.
//
// It exists because the save had no notion of going stale. A round, a hidden
// number and a score are written on every screen change so that Home, or the
// device sleeping mid-argument, does not cost the table its game -- and days
// later a completely different group opened the app and was dropped into the
// middle of the previous group's round 2, with nothing on the panel saying that
// was what had happened. This is a party game: a different group is the normal
// case.
//
// It is shown ONLY when the answer is genuinely unknown, which is when the
// evening on the card was written by a different run of the chip
// (wavelength::resumeFor). Within one boot the round resumes silently, so Home
// and back still costs nothing.
struct ResumeModel {
  // The round CARRY ON would play, named on the button in the front door's own
  // words so the two screens cannot describe one evening two ways.
  int roundNumber = 1;
  int total = 0;
  int scored = 0;
  // A round was mid-play rather than merely a session being open. Worth saying:
  // carrying on means somebody has already seen a number and heard a clue.
  bool roundInFlight = false;
  // How long ago, or -1 when the device cannot say -- no RTC on the board, or a
  // clock that has never been synced. The line is then simply absent, because
  // the question stands without it.
  int minutesAgo = -1;
};

struct SummaryModel {
  const wavelength::Record* record = nullptr;
  int rounds = 0;
  int total = 0;
  int averageTenths = 0;
  // Shown because an abandon is free and invisible otherwise. The board is
  // public in this game; so is walking away from a target you did not like.
  int abandoned = 0;
  // What the continue button plays. This screen is a look at the score with
  // the evening still running, and saying which round comes next is what makes
  // that unambiguous -- in the same words the front door's own button uses.
  int nextRound = 1;
};

// The pause, reached by Back from any screen inside a round. It exists because
// Back USED to abandon silently, which was three faults at once: no on-screen
// way out of a round, no way to check the scoring without destroying the round
// to reach it, and a clue-giver who could re-deal until they liked their target
// while the game had just told everyone else to look away.
struct PauseModel {
  int roundNumber = 1;
  int total = 0;
  bool practice = false;
  int abandoned = 0;
};

struct PassModel {
  int roundNumber = 1;
  int total = 0;
  bool practice = false;
  // Set when the previous round was backed out of rather than played. Silent,
  // it looks exactly like a normal pass, so a clue-giver who did not like their
  // target could abandon and redraw with nobody at the table any the wiser.
  bool abandoned = false;
  int abandonedCount = 0;
};

// The hold-to-reveal pad, so the activity tests a held finger against the very
// rect that drew it rather than recomputing the geometry a second time. Three
// separate bugs in this fork came from breaking that.
fui::Rect peekPadRect(int16_t screenW, int16_t screenH);

// The LOCK button. An ORDINARY button: it carries ActionLock and fires on the
// release like every other control in the fork, because a hold whose duration is
// invisible is not a safeguard, it is a guessing game -- nothing on the panel
// could tell you it wanted 600ms rather than 200 or 4000.
//
// What the hold was really guarding is that this control sits in the same
// footer band as the strip the table has just been tapping, so a finger sliding
// off the bottom of the board could commit the round. That is answered by
// GEOMETRY instead: the bar no longer spans the panel, it occupies only the
// number column's third of the footer, and everything below the strip is dead
// paper. See lockBarRect() in the .cpp for the numbers.
//
// Still exposed rather than recomputed by the caller because the tests measure
// separation against the very rect that drew it. Three bugs in this fork came
// from a second copy of a control's geometry.
fui::Rect lockBarRect(int16_t screenW, int16_t screenH);

// Which way a finger held at (x,y) on the dial is asking the marker to move:
// +1 toward the top pole, -1 toward the bottom, 0 for neither. Lives here so
// the activity's repeat and the screen's drawing share one geometry rather
// than computing it twice, which is how three separate bugs started.
// Which slot a point on the strip falls in, or 0 if it is off the board. A tap
// PLACES the marker: a cold player tapped near the top expecting to jump there
// and moved one slot, then had to tap nine more times on a panel that repaints
// between each.
int dialSlotAt(int16_t screenW, int16_t screenH, int16_t x, int16_t y);

// A held finger keeps stepping. Design said so from the start and the code
// never did it: three cold testers all reported that crossing the strip is
// nine to nineteen separate taps on a screen that repaints slowly.

void renderHowTo(toybox::Screen& screen);
void renderMenu(toybox::Screen& screen, const MenuModel& model);
void renderResume(toybox::Screen& screen, const ResumeModel& model);
void renderSummary(toybox::Screen& screen, const SummaryModel& model);
void renderPause(toybox::Screen& screen, const PauseModel& model);
void renderPassLeft(toybox::Screen& screen, const PassModel& model);
void renderPick(toybox::Screen& screen, const PickModel& model);
void renderPeek(toybox::Screen& screen, const PeekModel& model);
void renderClue(toybox::Screen& screen, const ClueModel& model);
void renderDial(toybox::Screen& screen, const DialModel& model);
void renderCall(toybox::Screen& screen, const CallModel& model);
void renderReveal(toybox::Screen& screen, const RevealModel& model);

}  // namespace wavelengthui

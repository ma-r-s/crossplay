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
};

struct Spectrum {
  const char* top = "CHARMING";
  const char* bottom = "ANNOYING";
};

struct DialModel {
  Spectrum spectrum;
  int guess = 10;
};

struct PickModel {
  Spectrum first;
  Spectrum second;
  bool onlyOne = false;  // the deck has a single unseen pair left
};

struct PeekModel {
  Spectrum spectrum;
  int target = 10;
  bool revealed = false;  // true only while a thumb is on the pad
};

struct ClueModel {
  Spectrum spectrum;
};

struct CallModel {
  Spectrum spectrum;
  int guess = 10;
};

struct RevealModel {
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
};

struct SummaryModel {
  const wavelength::Record* record = nullptr;
  int rounds = 0;
  int total = 0;
  int averageTenths = 0;
};

struct PassModel {
  int roundNumber = 1;
  int total = 0;
  bool practice = false;
};

// The hold-to-reveal pad, so the activity tests a held finger against the very
// rect that drew it rather than recomputing the geometry a second time. Three
// separate bugs in this fork came from breaking that.
fui::Rect peekPadRect(int16_t screenW, int16_t screenH);

// The LOCK bar, exposed for the same reason: the activity tests a held finger
// against the very rect that drew it. This button says HOLD and must mean it.
// It shipped in v1.12.0 as a plain tap, so a brush of a sleeve ended the round
// while a deliberate four-second press did nothing -- the exact inverse of its
// own label, on a bar under everyone's thumb with the device flat on a table.
fui::Rect lockBarRect(int16_t screenW, int16_t screenH);

// How long the bar must be held. Long enough that a stray touch cannot commit,
// short enough that nobody wonders whether it is broken.
inline constexpr int kLockHoldMs = 600;

void renderHowTo(toybox::Screen& screen);
void renderMenu(toybox::Screen& screen, const MenuModel& model);
void renderSummary(toybox::Screen& screen, const SummaryModel& model);
void renderPassLeft(toybox::Screen& screen, const PassModel& model);
void renderPick(toybox::Screen& screen, const PickModel& model);
void renderPeek(toybox::Screen& screen, const PeekModel& model);
void renderClue(toybox::Screen& screen, const ClueModel& model);
void renderDial(toybox::Screen& screen, const DialModel& model);
void renderCall(toybox::Screen& screen, const CallModel& model);
void renderReveal(toybox::Screen& screen, const RevealModel& model);

}  // namespace wavelengthui

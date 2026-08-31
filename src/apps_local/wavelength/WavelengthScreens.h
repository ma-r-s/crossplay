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

// Three arrangements of the one surface that matters, per the fork's rule that
// a new screen is chosen from renders rather than from prose. Built behind the
// macro, shot side by side, and the losers deleted in the same commit that
// picks a winner.
//
//   1  THE COLUMN  a contained ladder of cells; the guess is a filled cell
//   2  THE LADDER  no container, twenty rungs; the guess is a fader bar
//   3  THE TROUGH  one outlined trough; the guess is the top of a dithered fill
#ifndef WAVELENGTH_VARIANT
#define WAVELENGTH_VARIANT 2
#endif

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
};

struct DialModel {
  const char* topWord = "CHARMING";
  const char* bottomWord = "ANNOYING";
  int guess = 10;
};

void renderDial(toybox::Screen& screen, const DialModel& model);

}  // namespace wavelengthui

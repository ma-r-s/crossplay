#pragma once

// The one screen that is not a game: who you are on this device.
//
// This is the fork's System Settings, and it holds exactly one setting, because
// exactly one thing about this device is yours. Calling it PLAYER rather than
// SETTINGS is deliberate -- a screen named for a category it does not have yet
// is a promise, and the growth path is a real settings list with PLAYER as its
// first row, not a rename.
//
// ---------------------------------------------------------------------------
// Why three words instead of one button.
//
// The name used to be one pill you tapped to reroll: a slot machine with a
// single lever. You could accept what it gave you or pull again, and there was
// nothing in between, so nobody's name was ever *chosen*.
//
// Three words, each its own target, turns the same lists into something you
// steer -- and because the words are the face (PlayerName.h), the feedback is
// not a label changing, it is a person changing. Tap the middle word and watch
// the eyes. That is the entire reason this screen exists.
// ---------------------------------------------------------------------------
//
// Freestanding in the ChessScreens mould: a model in, a drawn frame out, no
// renderer and no Activity, so host-tests/ui/ can assert what it drew and what
// it made tappable.

#include "../ui/ToyboxScreen.h"

namespace playerui {

namespace fui = freeink::ui;

// Shared-screen ids, kept clear of every game's own (chess 1-4, shelf 1-2) and
// of the link screens' 200s.
enum : fui::ActionId {
  // Carries the slot index as its value, so three targets cost one id and the
  // handler cannot get out of step with the layout.
  ActionStepSlot = 300,
  ActionLeavePlayer = 301,
};

struct PlayerModel {
  // The whole name, "HAIR EYES MOUTH". The face is derived from this string and
  // nothing else, which is why the model has no avatar field: there is no
  // second thing to keep in step.
  const char* name = "";
  // The three words, in order, as the buttons should read them. Passed rather
  // than re-split from `name` so the screen stays a pure function of its model
  // and a test can hand it a word list that does not parse.
  const char* words[3] = {"", "", ""};
};

// The face, at the size this screen draws it.
//
// An exact multiple of the 120px asset, and that is load-bearing: the SDK's
// bitmap sampler is nearest-neighbour, so an integer scale doubles every pixel
// evenly while a fractional one leaves some strokes a pixel fatter than their
// neighbours. It also means the big face costs no flash -- 3x from the asset we
// already have, rather than a third generated size at 405KB.
//
// 240 and 360 were both built and rendered on the device path before choosing.
// 240 was the safe number and it was wrong: it left ~430px of nothing between
// the words and BACK, which on a screen that holds its image is the defect the
// design language names outright. At 360 the slack collapses into one zone
// above the footer, the same shape chess leaves for its capture strips, and the
// face becomes the subject of the page instead of a thumbnail of one.
constexpr int16_t kFaceSize = 360;

void buildPlayer(toybox::Screen& screen, const PlayerModel& model);

}  // namespace playerui

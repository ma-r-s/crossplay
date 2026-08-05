#pragma once

// The Insider screens, as freestanding builders over plain models.
//
// Everything shaped like chrome goes through toybox::Screen and inherits the
// theme. What is hand-drawn is this game's own material, and it comes to three
// things: the seat, the role card, and the clock. They are drawn by the helpers
// below rather than per screen, because a seat that looked different on the
// vote screen from the pass screen would stop reading as the same person.
//
// See ../ui/ToyboxScreen.h for why these take a Screen and touch no renderer.

#include "../ui/ToyboxScreen.h"
#include "InsiderCore.h"

namespace insiderui {

namespace fui = freeink::ui;

enum : fui::ActionId {
  // value = -1 or +1
  ActionPlayers = 1,
  ActionDeal = 2,
  ActionRules = 3,
  // The single "carry on" tap that drives the whole pass-around. One action for
  // reveal, hide and pass, because from the player's side it is one gesture
  // repeated, and splitting it into three would be three ways to get the state
  // machine wrong.
  ActionAdvance = 4,
  ActionFoundWord = 5,
  // value = a seat, or insider::kNoInsider
  ActionAccuse = 6,
  ActionConfirmVote = 7,
  ActionPlayAgain = 8,
  ActionDone = 9,
  ActionBack = 10,
};

// ---------------------------------------------------------------------------

struct MenuModel {
  int players = 5;
  const insider::Record* record = nullptr;
};

void buildMenu(toybox::Screen& screen, const MenuModel& model);

// The pass-around. One model for both faces of it, because they are one screen
// with the card turned over: `revealed` is the whole difference.
struct PassModel {
  int seat = 0;  // 0-based
  int players = 5;
  bool revealed = false;
  insider::Role role = insider::Role::Citizen;
  const char* word = "";
  // True on the last player's hand-back, when the next tap starts the clock
  // rather than passing to somebody else.
  bool lastSeat = false;
};

void buildPass(toybox::Screen& screen, const PassModel& model);

struct QuestionsModel {
  int secondsLeft = insider::kQuestionSeconds;
  int masterSeat = 0;
  int players = 5;
};

void buildQuestions(toybox::Screen& screen, const QuestionsModel& model);

struct VoteModel {
  int players = 5;
  int masterSeat = 0;
  // The seat the table has settled on, a real seat or insider::kNoInsider, or
  // kNothingChosen while they are still arguing.
  static constexpr int kNothingChosen = -2;
  int chosen = kNothingChosen;
};

void buildVote(toybox::Screen& screen, const VoteModel& model);

struct RevealModel {
  insider::Outcome outcome = insider::Outcome::Won;
  int insiderSeat = insider::kNoInsider;
  int accused = insider::kNoInsider;
  int players = 5;
  const char* word = "";
};

void buildReveal(toybox::Screen& screen, const RevealModel& model);

// The tutorial, as a deck of pages you tap through: the round in the order it
// happens, one beat a page, each a diagram made of the game's own material.
//
// Chosen by rendering it. Two other complete designs were built and thrown away
// with it -- one page per role, and a three-page storyboard of paired panels --
// because a screen whose shape is open is decided by looking at the options
// side by side, not by describing them. See docs/building-apps.md.
struct TutorialModel {
  int page = 0;
};

int tutorialPages();
void buildTutorial(toybox::Screen& screen, const TutorialModel& model);

// Minutes and seconds, at the granularity the clock actually moves in.
// Written into `out`, which needs 8 bytes.
void formatClock(int secondsLeft, char* out, int cap);

}  // namespace insiderui
